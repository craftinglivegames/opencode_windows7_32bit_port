// ---------------------------------------------------------------------------
//  ansi_console.cpp - ANSI terminal proxy for Windows 7.
//
//  The child process (opencode) writes a normal ANSI byte stream into a pipe.
//  This module parses that stream, keeps a virtual screen and paints it into
//  the Windows console.  Key presses are translated back into ANSI sequences
//  and written to the child's stdin.
// ---------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <wchar.h>

#include "ansi_console.h"

// ---------------------------------------------------------------------------
//  colour handling
// ---------------------------------------------------------------------------

struct Rgb {
  BYTE r, g, b;
};

static const Rgb PALETTE[16] = {
    {0, 0, 0},       {0, 0, 128},     {0, 128, 0},     {0, 128, 128}, {128, 0, 0},   {128, 0, 128},
    {128, 128, 0},   {192, 192, 192},  {128, 128, 128}, {0, 0, 255},   {0, 255, 0},   {0, 255, 255},
    {255, 0, 0},     {255, 0, 255},   {255, 255, 0},   {255, 255, 255},
};

static int nearest_color(Rgb c) {
  int best = 0;
  long bestDist = -1;
  for (int i = 0; i < 16; i++) {
    long dr = (long)c.r - PALETTE[i].r;
    long dg = (long)c.g - PALETTE[i].g;
    long db = (long)c.b - PALETTE[i].b;
    long dist = dr * dr + dg * dg + db * db;
    if (bestDist < 0 || dist < bestDist) {
      bestDist = dist;
      best = i;
    }
  }
  return best;
}

// ---------------------------------------------------------------------------
//  virtual screen
// ---------------------------------------------------------------------------

struct Cell {
  wchar_t ch;
  BYTE attr;  // console attribute
};

struct Host {
  HANDLE hIn;
  HANDLE hOut;
  int cols;
  int rows;
  Cell* cells;
  Cell* shadow;
  int cx;
  int cy;
  BYTE attr;
  Rgb fg;
  Rgb bg;
  bool altScreen;
  Cell* mainCells;
  bool mouseEnabled;
  bool mouseSgr;
  bool cursorVisible;
  CRITICAL_SECTION lock;
};

static void host_lock(Host* h) { EnterCriticalSection(&h->lock); }
static void host_unlock(Host* h) { LeaveCriticalSection(&h->lock); }

static void ensure_size(Host* h) {
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (!GetConsoleScreenBufferInfo(h->hOut, &info)) return;
  int cols = info.srWindow.Right - info.srWindow.Left + 1;
  int rows = info.srWindow.Bottom - info.srWindow.Top + 1;
  if (cols == h->cols && rows == h->rows) return;
  if (cols < 20) cols = 20;
  if (rows < 5) rows = 5;
  h->cols = cols;
  h->rows = rows;
  COORD size = {(SHORT)cols, (SHORT)rows};
  SetConsoleScreenBufferSize(h->hOut, size);
  // scroll the window to the top left
  SMALL_RECT rect = {0, 0, (SHORT)(cols - 1), (SHORT)(rows - 1)};
  SetConsoleWindowInfo(h->hOut, TRUE, &rect);
  for (int i = 0; i < cols * rows; i++) {
    h->cells[i].ch = L' ';
    h->cells[i].attr = 0x07;
    h->shadow[i].ch = 0;
    h->shadow[i].attr = 0xFF;
  }
  h->cx = 0;
  h->cy = 0;
}

static BYTE current_attr(Host* h) {
  BYTE attr = 0;
  int fg = nearest_color(h->fg);
  int bg = nearest_color(h->bg);
  if (h->attr & 0x04) fg = 8 + (fg & 7);
  attr |= (BYTE)(fg & 0x0F);
  attr |= (BYTE)((bg & 0x0F) << 4);
  return attr;
}

static void put_char(Host* h, wchar_t ch) {
  if (h->cx >= h->cols) {
    h->cx = 0;
    h->cy++;
  }
  if (h->cy >= h->rows) {
    // scroll up
    memmove(h->cells, h->cells + h->cols, sizeof(Cell) * (size_t)(h->cols * (h->rows - 1)));
    for (int i = 0; i < h->cols; i++) {
      h->cells[(h->rows - 1) * h->cols + i].ch = L' ';
      h->cells[(h->rows - 1) * h->cols + i].attr = current_attr(h);
    }
    h->cy = h->rows - 1;
  }
  Cell* cell = &h->cells[h->cy * h->cols + h->cx];
  cell->ch = ch;
  cell->attr = current_attr(h);
  h->cx++;
}

static void erase_line(Host* h, int from, int to) {
  for (int x = from; x <= to && x < h->cols; x++) {
    Cell* cell = &h->cells[h->cy * h->cols + x];
    cell->ch = L' ';
    cell->attr = current_attr(h);
  }
}

static void erase_display(Host* h, int mode) {
  if (mode == 2 || mode == 3) {
    for (int i = 0; i < h->cols * h->rows; i++) {
      h->cells[i].ch = L' ';
      h->cells[i].attr = current_attr(h);
    }
    return;
  }
  if (mode == 0) {
    erase_line(h, h->cx, h->cols - 1);
    for (int y = h->cy + 1; y < h->rows; y++)
      for (int x = 0; x < h->cols; x++) {
        h->cells[y * h->cols + x].ch = L' ';
        h->cells[y * h->cols + x].attr = current_attr(h);
      }
  } else if (mode == 1) {
    erase_line(h, 0, h->cx);
    for (int y = 0; y < h->cy; y++)
      for (int x = 0; x < h->cols; x++) {
        h->cells[y * h->cols + x].ch = L' ';
        h->cells[y * h->cols + x].attr = current_attr(h);
      }
  }
}

static void scroll_region(Host* h, int top, int bottom, int count) {
  if (bottom <= top) return;
  int height = bottom - top;
  if (count < 0) count = -count;
  for (int i = 0; i < count; i++) {
    memmove(&h->cells[top * h->cols], &h->cells[(top + 1) * h->cols], sizeof(Cell) * (size_t)(height * h->cols));
    for (int x = 0; x < h->cols; x++) {
      h->cells[bottom * h->cols + x].ch = L' ';
      h->cells[bottom * h->cols + x].attr = current_attr(h);
    }
  }
}

// ---------------------------------------------------------------------------
//  ANSI parser
// ---------------------------------------------------------------------------

struct Parser {
  Host* host;
  std::string* pending;  // response queue for the child
  int state;
  char params[64];
  int paramLen;
  bool privateMode;
};

static void write_to_child(HANDLE childIn, const char* data, size_t len) {
  DWORD written = 0;
  WriteFile(childIn, data, (DWORD)len, &written, NULL);
}

static void apply_sgr(Host* h, const char* params, int len) {
  int values[16];
  int count = 0;
  int i = 0;
  while (i < len && count < 16) {
    if (params[i] >= '0' && params[i] <= '9') {
      int v = 0;
      while (i < len && params[i] >= '0' && params[i] <= '9') v = v * 10 + (params[i++] - '0');
      values[count++] = v;
    } else if (params[i] == ';') {
      values[count++] = 0;
      i++;
    } else {
      i++;
    }
  }
  if (count == 0) values[count++] = 0;
  for (int p = 0; p < count; p++) {
    int v = values[p];
    if (v == 0) {
      h->fg = PALETTE[7];
      h->bg = PALETTE[0];
      h->attr &= (BYTE)~0x04;
    } else if (v == 1) {
      h->attr |= 0x04;  // bright
      h->fg = Rgb{255, 255, 255};
    } else if (v == 7) {      Rgb tmp = h->fg;
      h->fg = h->bg;
      h->bg = tmp;
    } else if (v >= 30 && v <= 37) {
      h->fg = PALETTE[v - 30];
    } else if (v == 39) {
      h->fg = PALETTE[7];
    } else if (v >= 40 && v <= 47) {
      h->bg = PALETTE[v - 40];
    } else if (v == 49) {
      h->bg = PALETTE[0];
    } else if (v >= 90 && v <= 97) {
      h->fg = Rgb{255, 255, 255};
    } else if (v >= 100 && v <= 107) {
      h->bg = PALETTE[0];
    } else if (v == 38 || v == 48) {
      // 256 colour / true colour
      Rgb color = PALETTE[7];
      if (p + 1 < count && values[p + 1] == 5 && p + 2 < count) {
        int idx = values[p + 2];
        if (idx < 8)
          color = PALETTE[idx];
        else if (idx < 16)
          color = PALETTE[idx];
        else if (idx >= 232) {
          BYTE g = (BYTE)(((idx - 232) * 10) + 8);
          color = Rgb{g, g, g};
        } else {
          int n = idx - 16;
          int levels[6] = {0, 95, 135, 175, 215, 255};
          color = Rgb{(BYTE)levels[(n / 36) % 6], (BYTE)levels[(n / 6) % 6], (BYTE)levels[n % 6]};
        }
        p += 2;
      } else if (p + 1 < count && values[p + 1] == 2 && p + 4 < count) {
        color = Rgb{(BYTE)values[p + 2], (BYTE)values[p + 3], (BYTE)values[p + 4]};
        p += 4;
      }
      if (v == 38)
        h->fg = color;
      else
        h->bg = color;
    }
  }
}

static void feed(Host* h, HANDLE childIn, const char* data, size_t len, std::string* responses) {
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)data[i];
    switch (0) {
      default:
        break;
    }
    // simple state machine kept in static-like fields
    static int state = 0;
    static char params[64];
    static int paramLen = 0;
    static bool priv = false;
    static std::string oscBuf;

    if (state == 0) {
      if (c == 0x1b) {
        state = 1;
        paramLen = 0;
        priv = false;
        continue;
      }
      if (c == '\r') {
        h->cx = 0;
        continue;
      }
      if (c == '\n') {
        h->cy++;
        if (h->cy >= h->rows) {
          scroll_region(h, 0, h->rows - 1, -1);
          h->cy = h->rows - 1;
        }
        continue;
      }
      if (c == '\b') {
        if (h->cx > 0) h->cx--;
        continue;
      }
      if (c == '\t') {
        h->cx = (h->cx + 8) & ~7;
        continue;
      }
      if (c < 0x20) continue;
      put_char(h, (wchar_t)c);
      continue;
    }
    if (state == 1) {  // after ESC
      if (c == '[') {
        state = 2;
        paramLen = 0;
        priv = false;
        continue;
      }
      if (c == ']') {
        state = 3;
        oscBuf.clear();
        continue;
      }
      if (c == '(' || c == ')' || c == '#' || c == '%') {
        state = 4;
        continue;
      }
      if (c == '7') {
        state = 0;
        continue;
      }
      if (c == '8') {
        state = 0;
        continue;
      }
      if (c == 'c') {
        erase_display(h, 2);
        h->cx = 0;
        h->cy = 0;
        state = 0;
        continue;
      }
      state = 0;
      continue;
    }
    if (state == 2) {  // CSI
      if (c >= 0x30 && c <= 0x3F) {
        if (c == '?') priv = true;
        if (paramLen < 63) params[paramLen++] = (char)c;
        continue;
      }
      if (c >= 0x20 && c <= 0x2F) {
        if (paramLen < 63) params[paramLen++] = (char)c;
        continue;
      }
      if (c >= 0x40 && c <= 0x7E) {
        // final byte
        char final = (char)c;
        char num[8] = {0};
        int n = 0;
        int v = 0;
        bool hasValue = false;
        for (int k = 0; k < paramLen && n < 7; k++) {
          if (params[k] >= '0' && params[k] <= '9') {
            v = v * 10 + (params[k] - '0');
            hasValue = true;
          } else if (params[k] == ';') {
            num[n++] = (char)(hasValue ? v : 0);
            v = 0;
            hasValue = false;
          }
        }
        num[n++] = (char)(hasValue ? v : 0);

        if (priv) {
          if (final == 'h' || final == 'l') {
            bool enable = final == 'h';
            if (v == 1049 || v == 47 || v == 1047) {
              if (enable && !h->altScreen) {
                memcpy(h->mainCells, h->cells, sizeof(Cell) * (size_t)(h->cols * h->rows));
                h->altScreen = true;
                erase_display(h, 2);
                h->cx = h->cy = 0;
              } else if (!enable && h->altScreen) {
                h->altScreen = false;
                memcpy(h->cells, h->mainCells, sizeof(Cell) * (size_t)(h->cols * h->rows));
                h->cx = h->cy = 0;
              }
            } else if (v == 1000 || v == 1002 || v == 1003) {
              h->mouseEnabled = enable;
            } else if (v == 1006) {
              h->mouseSgr = enable;
            }
          }
          state = 0;
          continue;
        }

        switch (final) {
          case 'A':
            h->cy -= (v ? v : 1);
            if (h->cy < 0) h->cy = 0;
            break;
          case 'B':
            h->cy += (v ? v : 1);
            if (h->cy >= h->rows) h->cy = h->rows - 1;
            break;
          case 'C':
            h->cx += (v ? v : 1);
            if (h->cx >= h->cols) h->cx = h->cols - 1;
            break;
          case 'D':
            h->cx -= (v ? v : 1);
            if (h->cx < 0) h->cx = 0;
            break;
          case 'E':
            h->cy += (v ? v : 1);
            h->cx = 0;
            break;
          case 'F':
            h->cy -= (v ? v : 1);
            h->cx = 0;
            break;
          case 'G':
          case '`':
            h->cx = (v ? v : 1) - 1;
            if (h->cx < 0) h->cx = 0;
            if (h->cx >= h->cols) h->cx = h->cols - 1;
            break;
          case 'd':
            h->cy = (v ? v : 1) - 1;
            if (h->cy < 0) h->cy = 0;
            if (h->cy >= h->rows) h->cy = h->rows - 1;
            break;
          case 'H':
          case 'f': {
            int row = num[0] ? num[0] : 1;
            int col = (paramLen > 1 && num[1]) ? num[1] : 1;
            h->cy = row - 1;
            h->cx = col - 1;
            if (h->cy < 0) h->cy = 0;
            if (h->cy >= h->rows) h->cy = h->rows - 1;
            if (h->cx < 0) h->cx = 0;
            if (h->cx >= h->cols) h->cx = h->cols - 1;
            break;
          }
          case 'J':
            erase_display(h, v);
            break;
          case 'K':
            if (v == 0)
              erase_line(h, h->cx, h->cols - 1);
            else if (v == 1)
              erase_line(h, 0, h->cx);
            else
              erase_line(h, 0, h->cols - 1);
            break;
          case 'L': {
            int count = v ? v : 1;
            for (int k = 0; k < count; k++) {
              memmove(&h->cells[(h->cy + 1) * h->cols], &h->cells[h->cy * h->cols],
                      sizeof(Cell) * (size_t)((h->rows - h->cy - 1) * h->cols));
              erase_line(h, 0, h->cols - 1);
            }
            break;
          }
          case 'M': {
            int count = v ? v : 1;
            for (int k = 0; k < count; k++) {
              memmove(&h->cells[h->cy * h->cols], &h->cells[(h->cy + 1) * h->cols],
                      sizeof(Cell) * (size_t)((h->rows - h->cy - 1) * h->cols));
              erase_line(h, 0, h->cols - 1);
            }
            break;
          }
          case 'P': {
            int count = v ? v : 1;
            if (count > h->cols - h->cx) count = h->cols - h->cx;
            memmove(&h->cells[h->cy * h->cols + h->cx], &h->cells[h->cy * h->cols + h->cx + count],
                    sizeof(Cell) * (size_t)(h->cols - h->cx - count));
            erase_line(h, h->cx, h->cx + count - 1);
            break;
          }
          case '@': {
            int count = v ? v : 1;
            if (count > h->cols - h->cx) count = h->cols - h->cx;
            memmove(&h->cells[h->cy * h->cols + h->cx + count], &h->cells[h->cy * h->cols + h->cx],
                    sizeof(Cell) * (size_t)(h->cols - h->cx - count));
            erase_line(h, h->cx, h->cx + count - 1);
            break;
          }
          case 'X': {
            int count = v ? v : 1;
            erase_line(h, h->cx, h->cx + count - 1);
            break;
          }
          case 'S':
            scroll_region(h, 0, h->rows - 1, v ? v : 1);
            break;
          case 'T':
            scroll_region(h, 0, h->rows - 1, -(v ? v : 1));
            break;
          case 'm':
            apply_sgr(h, params, paramLen);
            break;
          case 'n': {
            if (v == 6) {
              char buf[32];
              int len = sprintf(buf, "\x1b[%d;%dR", h->cy + 1, h->cx + 1);
              responses->append(buf, len);
            }
            break;
          }
          case 't': {
            if (v == 14 || v == 18) {
              char buf[64];
              int len = sprintf(buf, "\x1b[4;%d;%dt", h->rows * 16, h->cols * 8);
              responses->append(buf, len);
            }
            break;
          }
          case 'c': {
            char buf[32];
            int len = sprintf(buf, "\x1b[?62;1;6c");
            responses->append(buf, len);
            break;
          }
          default:
            break;
        }
        state = 0;
        continue;
      }
      state = 0;
      continue;
    }
    if (state == 3) {  // OSC
      if (c == 0x07) {
        state = 0;
        continue;
      }
      if (c == 0x1b) {
        state = 5;
        continue;
      }
      if (oscBuf.size() < 128) oscBuf.push_back((char)c);
      continue;
    }
    if (state == 5) {  // ESC \ (end of OSC)
      state = 0;
      continue;
    }
    if (state == 4) {  // charset designator
      state = 0;
      continue;
    }
  }
}

// ---------------------------------------------------------------------------
//  painting
// ---------------------------------------------------------------------------

static void paint(Host* h) {
  host_lock(h);
  ensure_size(h);
  CHAR_INFO* buffer = (CHAR_INFO*)malloc(sizeof(CHAR_INFO) * (size_t)(h->cols * h->rows));
  if (buffer) {
    int changed = 0;
    for (int i = 0; i < h->cols * h->rows; i++) {
      Cell& c = h->cells[i];
      Cell& s = h->shadow[i];
      if (c.ch != s.ch || c.attr != s.attr) {
        buffer[i].Char.AsciiChar = (char)(c.ch < 256 ? c.ch : '?');
        buffer[i].Attributes = c.attr;
        s = c;
        changed++;
      }
    }
    if (changed) {
      COORD size = {(SHORT)h->cols, (SHORT)h->rows};
      COORD start = {0, 0};
      WriteConsoleOutputA(h->hOut, buffer, size, start, NULL);
    }
    free(buffer);
  }
  host_unlock(h);
}

// ---------------------------------------------------------------------------
//  child process plumbing
// ---------------------------------------------------------------------------

struct PipeState {
  Host* host;
  HANDLE childIn;
};

static DWORD WINAPI reader_thread(LPVOID param) {
  PipeState* state = (PipeState*)param;
  Host* h = state->host;
  char buffer[8192];
  std::string responses;
  for (;;) {
    DWORD read = 0;
    if (!ReadFile(h->hOut, buffer, sizeof(buffer), &read, NULL) || read == 0) break;
    host_lock(h);
    feed(h, state->childIn, buffer, read, &responses);
    host_unlock(h);
    if (!responses.empty()) {
      write_to_child(state->childIn, responses.c_str(), responses.size());
      responses.clear();
    }
  }
  return 0;
}

static void translate_key(Host* h, const KEY_EVENT_RECORD& ev, std::string* out) {
  WORD vk = ev.wVirtualKeyCode;
  WORD scan = ev.wVirtualKeyCode;
  bool ctrl = (ev.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
  bool alt = (ev.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
  (void)scan;
  switch (vk) {
    case VK_UP:
      out->append("\x1b[A");
      return;
    case VK_DOWN:
      out->append("\x1b[B");
      return;
    case VK_RIGHT:
      out->append("\x1b[C");
      return;
    case VK_LEFT:
      out->append("\x1b[D");
      return;
    case VK_HOME:
      out->append("\x1b[H");
      return;
    case VK_END:
      out->append("\x1b[F");
      return;
    case VK_PRIOR:
      out->append("\x1b[5~");
      return;
    case VK_NEXT:
      out->append("\x1b[6~");
      return;
    case VK_INSERT:
      out->append("\x1b[2~");
      return;
    case VK_DELETE:
      out->append("\x1b[3~");
      return;
    case VK_F1:
      out->append("\x1bOP");
      return;
    case VK_F2:
      out->append("\x1bOQ");
      return;
    case VK_F3:
      out->append("\x1bOR");
      return;
    case VK_F4:
      out->append("\x1bOS");
      return;
    case VK_F5:
      out->append("\x1b[15~");
      return;
    case VK_F6:
      out->append("\x1b[17~");
      return;
    case VK_F7:
      out->append("\x1b[18~");
      return;
    case VK_F8:
      out->append("\x1b[19~");
      return;
    case VK_F9:
      out->append("\x1b[20~");
      return;
    case VK_F10:
      out->append("\x1b[21~");
      return;
    case VK_F11:
      out->append("\x1b[23~");
      return;
    case VK_F12:
      out->append("\x1b[24~");
      return;
    default:
      break;
  }
  wchar_t ch = ev.uChar.UnicodeChar;
  if (ch == 0) return;
  if (ch == VK_RETURN) {
    out->append("\r");
    return;
  }
  if (ch == VK_BACK) {
    out->append("\x7f");
    return;
  }
  if (ch == VK_TAB) {
    out->append("\t");
    return;
  }
  if (ch == VK_ESCAPE) {
    out->append("\x1b");
    return;
  }
  if (ch < 0x20) return;
  if (ctrl) {
    wchar_t c = (wchar_t)(ch & 0x1f);
    out->push_back((char)c);
    return;
  }
  if (alt) {
    out->append("\x1b");
  }
  // encode as utf-8
  if (ch < 0x80) {
    out->push_back((char)ch);
  } else if (ch < 0x800) {
    out->push_back((char)(0xC0 | (ch >> 6)));
    out->push_back((char)(0x80 | (ch & 0x3F)));
  } else {
    out->push_back((char)(0xE0 | (ch >> 12)));
    out->push_back((char)(0x80 | ((ch >> 6) & 0x3F)));
    out->push_back((char)(0x80 | (ch & 0x3F)));
  }
  (void)h;
}

// ---------------------------------------------------------------------------
//  entry point
// ---------------------------------------------------------------------------

HANDLE oc_spawn(const wchar_t* workdir, const wchar_t* exe, int argc, wchar_t** argv) {
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  size_t need = wcslen(exe) + 3;
  for (int i = 0; i < argc; i++) need += wcslen(argv[i]) + 1;
  wchar_t* command = (wchar_t*)calloc(need + 1, sizeof(wchar_t));
  if (!command) return NULL;
  wcscpy(command, L"\"");
  wcscat(command, exe);
  wcscat(command, L"\"");
  for (int i = 0; i < argc; i++) {
    wcscat(command, L" ");
    wcscat(command, argv[i]);
  }
  if (!CreateProcessW(NULL, command, NULL, NULL, TRUE, 0, NULL, workdir, &si, &pi)) {
    free(command);
    return NULL;
  }
  free(command);
  CloseHandle(pi.hThread);
  return pi.hProcess;
}

static int run_plain(const wchar_t* workdir, const wchar_t* exe, int argc, wchar_t** argv) {  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  size_t need = wcslen(exe) + 3;
  for (int i = 0; i < argc; i++) need += wcslen(argv[i]) + 1;
  wchar_t* command = (wchar_t*)calloc(need + 1, sizeof(wchar_t));
  if (!command) return -1;
  wcscpy(command, L"\"");
  wcscat(command, exe);
  wcscat(command, L"\"");
  for (int i = 0; i < argc; i++) {
    wcscat(command, L" ");
    wcscat(command, argv[i]);
  }
  if (!CreateProcessW(NULL, command, NULL, NULL, TRUE, 0, NULL, workdir, &si, &pi)) {
    free(command);
    return -1;
  }
  free(command);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return (int)code;
}

int oc_run(const wchar_t* workdir, const wchar_t* exe, int argc, wchar_t** argv, int use_console, void* unused) {
  (void)unused;
  if (!use_console) return run_plain(workdir, exe, argc, argv);

  // make sure we own a console of our own
  if (GetConsoleWindow() != NULL) {
    FreeConsole();
  }
  if (!AllocConsole()) return run_plain(workdir, exe, argc, argv);

  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
  if (hIn == INVALID_HANDLE_VALUE || hOut == INVALID_HANDLE_VALUE) return run_plain(workdir, exe, argc, argv);

  DWORD mode = 0;
  GetConsoleMode(hIn, &mode);
  SetConsoleMode(hIn, (mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT)) | ENABLE_WINDOW_INPUT |
                           ENABLE_EXTENDED_FLAGS);

  Host host;
  ZeroMemory(&host, sizeof(host));
  host.hIn = hIn;
  host.hOut = hOut;
  host.cols = 80;
  host.rows = 25;
  host.fg = PALETTE[7];
  host.bg = PALETTE[0];
  host.mouseEnabled = false;
  host.cursorVisible = true;
  InitializeCriticalSection(&host.lock);

  ensure_size(&host);
  host.cells = (Cell*)malloc(sizeof(Cell) * (size_t)(host.cols * host.rows));
  host.shadow = (Cell*)malloc(sizeof(Cell) * (size_t)(host.cols * host.rows));
  host.mainCells = (Cell*)malloc(sizeof(Cell) * (size_t)(host.cols * host.rows));
  if (!host.cells || !host.shadow || !host.mainCells) return run_plain(workdir, exe, argc, argv);
  for (int i = 0; i < host.cols * host.rows; i++) {
    host.cells[i].ch = L' ';
    host.cells[i].attr = 0x07;
    host.shadow[i].ch = 0;
    host.shadow[i].attr = 0xFF;
    host.mainCells[i].ch = L' ';
    host.mainCells[i].attr = 0x07;
  }

  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = NULL;
  sa.bInheritHandle = TRUE;

  HANDLE childOut = NULL, childIn = NULL;
  if (!CreatePipe(&childOut, &childIn, &sa, 65536)) return run_plain(workdir, exe, argc, argv);
  SetHandleInformation(childIn, HANDLE_FLAG_INHERIT, 0);
  HANDLE parentIn = NULL, childStdin = NULL;
  if (!CreatePipe(&parentIn, &childStdin, &sa, 65536)) return run_plain(workdir, exe, argc, argv);
  SetHandleInformation(parentIn, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdInput = childStdin;
  si.hStdOutput = childOut;
  si.hStdError = childOut;
  si.hStdInput = childStdin;

  size_t cmd_need = wcslen(exe) + 3;
  for (int i = 0; i < argc; i++) cmd_need += wcslen(argv[i]) + 1;
  wchar_t* command = (wchar_t*)calloc(cmd_need + 1, sizeof(wchar_t));
  if (!command) return run_plain(workdir, exe, argc, argv);
  wcscpy(command, L"\"");
  wcscat(command, exe);
  wcscat(command, L"\"");
  for (int i = 0; i < argc; i++) {
    wcscat(command, L" ");
    wcscat(command, argv[i]);
  }

  // environment: the child cannot query the terminal size through a pipe
  wchar_t env[4096];
  wchar_t* dst = env;
  wchar_t* block = GetEnvironmentStringsW();
  if (block) {
    for (wchar_t* p = block; *p; p += wcslen(p) + 1) {
      size_t len = wcslen(p);
      if (wcsncmp(p, L"COLUMNS=", 8) == 0 || wcsncmp(p, L"LINES=", 6) == 0 || wcsncmp(p, L"TERM=", 5) == 0) continue;
      if ((dst - env) + len + 1 >= 3800) break;
      wcscpy(dst, p);
      dst += len + 1;
    }
  }
  wcscpy(env + (dst - env), L"TERM=xterm-256color");
  wcscat(env + (dst - env), L"COLORTERM=truecolor");
  {
    wchar_t buf[64];
    _snwprintf(buf, 64, L"COLUMNS=%d", host.cols);
    wcscat(env + (dst - env), buf);
    _snwprintf(buf, 64, L"LINES=%d", host.rows);
    wcscat(env + (dst - env), buf);
  }
  *dst = 0;
  if (block) FreeEnvironmentStringsW(block);

  if (!CreateProcessW(NULL, command, NULL, NULL, TRUE, CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, env, workdir, &si, &pi)) {
    free(command);
    return run_plain(workdir, exe, argc, argv);
  }
  free(command);
  CloseHandle(childStdin);
  CloseHandle(childOut);

  PipeState state;
  state.host = &host;
  state.childIn = parentIn;
  HANDLE reader = CreateThread(NULL, 0, reader_thread, &state, 0, NULL);

  // main loop
  std::string pending;
  for (;;) {
    DWORD events = 0;
    if (!GetNumberOfConsoleInputEvents(hIn, &events)) break;
    if (events == 0) {
      if (WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0) break;
      paint(&host);
      continue;
    }
    INPUT_RECORD records[64];
    DWORD read = 0;
    if (!ReadConsoleInputW(hIn, records, 64, &read)) break;
    for (DWORD i = 0; i < read; i++) {
      switch (records[i].EventType) {
        case KEY_EVENT:
          if (records[i].Event.KeyEvent.bKeyDown) {
            pending.clear();
            translate_key(&host, records[i].Event.KeyEvent, &pending);
            if (!pending.empty()) write_to_child(parentIn, pending.c_str(), pending.size());
          }
          break;
        case MOUSE_EVENT: {
          if (!host.mouseEnabled) break;
          MOUSE_EVENT_RECORD& m = records[i].Event.MouseEvent;
          short col = (short)(m.dwMousePosition.X - host.cx * 0);
          short row = (short)m.dwMousePosition.Y;
          (void)col;
          char buf[64];
          int button = 0;
          if (m.dwButtonState & 1) button = 0;
          else if (m.dwButtonState & 2) button = 2;
          else if (m.dwButtonState & 4) button = 1;
          int len = sprintf(buf, "\x1b[<%d;%d;%d%c", button, (int)m.dwMousePosition.X + 1, (int)m.dwMousePosition.Y + 1,
                            (m.dwEventFlags & 0x8000) ? 'm' : 'M');
          write_to_child(parentIn, buf, len);
          break;
        }
        case WINDOW_BUFFER_SIZE_EVENT:
          paint(&host);
          {
            char buf[64];
            int len = sprintf(buf, "\x1b[8;%d;%dt", host.rows, host.cols);
            write_to_child(parentIn, buf, len);
          }
          break;
        default:
          break;
      }
    }
    paint(&host);
    if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) break;
  }

  // let the child finish
  WaitForSingleObject(pi.hProcess, 5000);
  if (reader) {
    // closing the pipe ends the reader thread
    CloseHandle(parentIn);
    WaitForSingleObject(reader, 2000);
    CloseHandle(reader);
  }
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return (int)code;
}
