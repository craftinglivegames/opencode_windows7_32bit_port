// ---------------------------------------------------------------------------
//  OpenCode.exe - 32-bit (x86) Windows 7 launcher for the portable Node port.
//
//  Layout expected next to the executable:
//      OpenCode.exe
//      node\node.exe  (+ ocshim32.dll, ocadv32.dll)
//      app\index.js   (+ impl\, webui\, node_modules\)
//
//  What it does:
//    1. if port 4096 is already served by our own node.exe, only opens the UI;
//    2. if the port is held by another node.exe (old deployment), stops it;
//    3. starts our server hidden:
//         node\node.exe app\index.js serve --hostname 0.0.0.0 --port 4096
//       stdout/stderr -> logs\server.log, NODE_SKIP_PLATFORM_CHECK=1;
//    4. waits for http://127.0.0.1:4096 to accept connections (up to 90 s);
//    5. opens the UI in Supermium app mode (or the default browser).
//
//  Modes (for diagnostics / automation):
//      OpenCode.exe                normal GUI launch
//      OpenCode.exe --accounts     accounts & limits panel (local web page)
//      OpenCode.exe --serve-only   start the server, do not open a browser
//      OpenCode.exe --stop         stop the server on port 4096
//      OpenCode.exe --selftest     write diagnostics to logs\launcher.log
//  Exit codes: 0 ok, 2 bad layout, 3 port busy by a foreign process,
//              4 server did not start, 5 stop failed.
// ---------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <shellapi.h>

#include <string>
#include <vector>
#include <cstdio>
#include <cstdarg>
#include <cwchar>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

static const wchar_t* kUrl = L"http://127.0.0.1:4096/";
static const wchar_t* kHealth = L"http://127.0.0.1:4096/global/health";
static const int kPort = 4096;
static const int kPanelPort = 4099;
static const wchar_t* kPanelUrl = L"http://127.0.0.1:4099/";

// ---------------------------------------------------------------------------
//  helpers
// ---------------------------------------------------------------------------

static std::wstring ExeDir() {
  wchar_t buf[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(NULL, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
  if (!n) return L".";
  std::wstring p(buf, n);
  size_t pos = p.find_last_of(L"\\/");
  if (pos == std::wstring::npos) return L".";
  return p.substr(0, pos);
}

static std::wstring Join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  std::wstring r = a;
  if (r[r.size() - 1] != L'\\' && r[r.size() - 1] != L'/') r += L'\\';
  r += b;
  return r;
}

static bool FileExists(const std::wstring& p) {
  if (p.empty()) return false;
  DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool DirExists(const std::wstring& p) {
  if (p.empty()) return false;
  DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool EnsureDir(const std::wstring& dir) {
  if (dir.empty()) return false;
  if (DirExists(dir)) return true;
  size_t pos = dir.find_last_of(L"\\/");
  if (pos != std::wstring::npos && pos > 2) EnsureDir(dir.substr(0, pos));
  if (CreateDirectoryW(dir.c_str(), NULL)) return true;
  return GetLastError() == ERROR_ALREADY_EXISTS;
}

static std::wstring GetEnv(const wchar_t* name) {
  DWORD n = GetEnvironmentVariableW(name, NULL, 0);
  if (!n) return L"";
  std::wstring v(n, L'\0');
  DWORD r = GetEnvironmentVariableW(name, &v[0], n);
  if (!r || r >= n) return L"";
  v.resize(r);
  return v;
}

static bool EndsWithNoCase(const std::wstring& s, const std::wstring& tail) {
  if (tail.size() > s.size()) return false;
  return _wcsicmp(s.c_str() + (s.size() - tail.size()), tail.c_str()) == 0;
}

static bool SamePath(const std::wstring& a, const std::wstring& b) {
  return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

static std::wstring SysErr(DWORD code) {
  wchar_t* buf = NULL;
  DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                               FORMAT_MESSAGE_IGNORE_INSERTS,
                           NULL, code, 0, (LPWSTR)&buf, 0, NULL);
  std::wstring out;
  if (n && buf) {
    out.assign(buf, n);
    while (!out.empty() && (out[out.size() - 1] == L'\r' || out[out.size() - 1] == L'\n')) out.erase(out.size() - 1);
  }
  LocalFree(buf);
  wchar_t num[32];
  wsprintfW(num, L" (error %lu)", (unsigned long)code);
  if (out.empty()) out = L"unknown error";
  return out + num;
}

// ---------------------------------------------------------------------------
//  logging (GUI app: no console, everything goes to logs\launcher.log)
// ---------------------------------------------------------------------------

static std::wstring g_logPath;

static void Log(const wchar_t* fmt, ...) {
  if (g_logPath.empty()) return;
  HANDLE h = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t head[64];
  wsprintfW(head, L"%04u-%02u-%02u %02u:%02u:%02u  ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
            st.wSecond);
  DWORD written = 0;
  WriteFile(h, head, (DWORD)(wcslen(head) * sizeof(wchar_t)), &written, NULL);
  wchar_t body[2048];
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf(body, (sizeof(body) / sizeof(body[0])) - 3, fmt, ap);
  va_end(ap);
  body[(sizeof(body) / sizeof(body[0])) - 3] = 0;
  size_t len = wcslen(body);
  body[len++] = L'\r';
  body[len++] = L'\n';
  WriteFile(h, body, (DWORD)(len * sizeof(wchar_t)), &written, NULL);
  CloseHandle(h);
}

// ---------------------------------------------------------------------------
//  TCP helpers
// ---------------------------------------------------------------------------

static bool PortOpen(int port, int timeoutMs) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((u_short)port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  bool ok = false;
  if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
    ok = true;
  } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
    fd_set wf, ef;
    FD_ZERO(&wf);
    FD_ZERO(&ef);
    FD_SET(s, &wf);
    FD_SET(s, &ef);
    timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    int r = select(0, NULL, &wf, &ef, &tv);
    ok = (r > 0 && FD_ISSET(s, &wf));
  }
  closesocket(s);
  return ok;
}

static DWORD PortListenerPid(int port) {
  DWORD size = 0;
  DWORD pid = 0;
  if (GetExtendedTcpTable(NULL, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) !=
          ERROR_INSUFFICIENT_BUFFER &&
      size == 0)
    return 0;
  std::vector<unsigned char> buf(size);
  if (GetExtendedTcpTable(buf.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR)
    return 0;
  MIB_TCPTABLE_OWNER_PID* table = (MIB_TCPTABLE_OWNER_PID*)buf.data();
  for (DWORD i = 0; i < table->dwNumEntries; ++i) {
    const MIB_TCPROW_OWNER_PID& row = table->table[i];
    if (ntohs((u_short)row.dwLocalPort) == port) {
      pid = row.dwOwningPid;
      if (row.dwLocalAddr == 0 || row.dwLocalAddr == inet_addr("127.0.0.1")) return pid;
    }
  }
  return pid;
}

static std::wstring ProcessImage(DWORD pid) {
  if (!pid) return L"";
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
  if (!h) return L"";
  wchar_t buf[MAX_PATH * 2];
  DWORD n = (DWORD)(sizeof(buf) / sizeof(buf[0]));
  std::wstring out;
  if (QueryFullProcessImageNameW(h, 0, buf, &n)) out.assign(buf, n);
  CloseHandle(h);
  return out;
}

// ---------------------------------------------------------------------------
//  server control
// ---------------------------------------------------------------------------

static bool WaitPort(int port, int timeoutMs) {
  DWORD start = GetTickCount();
  while ((int)(GetTickCount() - start) < timeoutMs) {
    if (PortOpen(port, 400)) return true;
    Sleep(400);
  }
  return false;
}

static bool StopPid(DWORD pid) {
  HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
  if (!h) return false;
  BOOL ok = TerminateProcess(h, 1);
  if (ok) WaitForSingleObject(h, 5000);
  CloseHandle(h);
  return ok != FALSE;
}

// Returns: 1 = our server already running, 0 = started now, 3 = foreign listener.
static int EnsureServer(const std::wstring& dir, bool quiet) {
  std::wstring node = Join(Join(dir, L"node"), L"node.exe");
  std::wstring script = Join(Join(dir, L"app"), L"index.js");
  std::wstring logs = Join(dir, L"logs");
  std::wstring serverLog = Join(logs, L"server.log");

  if (!FileExists(node) || !FileExists(script)) {
    Log(L"layout error: node=%s exists=%d, app=%s exists=%d", node.c_str(), (int)FileExists(node),
        script.c_str(), (int)FileExists(script));
    if (!quiet)
      MessageBoxW(NULL,
                  (L"Не найдены файлы программы.\n\nОжидалось:\n" + node + L"\n" + script +
                       L"\n\nРаспакуйте portable-пакет целиком (папки node и app должны лежать рядом с "
                       L"OpenCode.exe).")
                      .c_str(),
                  L"OpenCode", MB_ICONERROR | MB_OK);
    return 2;
  }

  DWORD pid = PortListenerPid(kPort);
  if (pid) {
    std::wstring image = ProcessImage(pid);
    Log(L"port %d is held by pid %lu image=%s", kPort, (unsigned long)pid, image.c_str());
    if (SamePath(image, node)) {
      Log(L"our server is already running");
      return 1;
    }
    if (EndsWithNoCase(image, L"node.exe")) {
      Log(L"stopping foreign node.exe pid %lu", (unsigned long)pid);
      StopPid(pid);
      DWORD start = GetTickCount();
      while (PortListenerPid(kPort) && (GetTickCount() - start) < 10000) Sleep(300);
      if (PortListenerPid(kPort)) {
        Log(L"foreign listener survived");
        if (!quiet)
          MessageBoxW(NULL, L"Не удалось остановить старый сервер на порту 4096.", L"OpenCode",
                      MB_ICONERROR | MB_OK);
        return 3;
      }
    } else {
      Log(L"port %d is busy by a non-node process: %s", kPort, image.c_str());
      if (!quiet)
        MessageBoxW(NULL, (L"Порт 4096 занят другой программой:\n" + image).c_str(), L"OpenCode",
                    MB_ICONERROR | MB_OK);
      return 3;
    }
  }

  EnsureDir(logs);
  SetEnvironmentVariableW(L"NODE_SKIP_PLATFORM_CHECK", L"1");

  SECURITY_ATTRIBUTES sa;
  memset(&sa, 0, sizeof(sa));
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE logHandle = CreateFileW(serverLog.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (logHandle == INVALID_HANDLE_VALUE) logHandle = NULL;

  std::wstring cmd = L"\"" + node + L"\" \"" + script + L"\" serve --hostname 0.0.0.0 --port " +
                     std::to_wstring(kPort);
  Log(L"starting: %s", cmd.c_str());

  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof(si));
  memset(&pi, 0, sizeof(pi));
  si.cb = sizeof(si);
  if (logHandle) {
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = INVALID_HANDLE_VALUE;
    si.hStdOutput = logHandle;
    si.hStdError = logHandle;
  }
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(0);
  BOOL ok = CreateProcessW(node.c_str(), mutableCmd.data(), NULL, NULL, logHandle ? TRUE : FALSE,
                           CREATE_NO_WINDOW, NULL, dir.c_str(), &si, &pi);
  DWORD err = GetLastError();
  if (logHandle) CloseHandle(logHandle);
  if (!ok) {
    Log(L"CreateProcessW failed: %s", SysErr(err).c_str());
    if (!quiet)
      MessageBoxW(NULL, (L"Не удалось запустить сервер:\n" + SysErr(err)).c_str(), L"OpenCode",
                  MB_ICONERROR | MB_OK);
    return 4;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  bool up = WaitPort(kPort, 90000);
  Log(L"server %s", up ? L"is up" : L"did not start");
  if (!up) {
    if (!quiet)
      MessageBoxW(NULL,
                  (L"Сервер не запустился за 90 секунд.\n\nЛог: " + serverLog).c_str(),
                  L"OpenCode", MB_ICONERROR | MB_OK);
    return 4;
  }
  return 0;
}

// ---------------------------------------------------------------------------
//  accounts panel (local web page: OpenCode Console accounts and free limits)
// ---------------------------------------------------------------------------

// Returns 0 on success, 5 when accounts_panel.js is missing, 6 when the panel
// did not start listening.
static int EnsurePanel(const std::wstring& dir, bool quiet) {
  std::wstring node = Join(Join(dir, L"node"), L"node.exe");
  std::wstring script = Join(dir, L"accounts_panel.js");
  if (!FileExists(node) || !FileExists(script)) {
    Log(L"panel: layout error node=%d script=%s exists=%d", (int)FileExists(node), script.c_str(),
        (int)FileExists(script));
    if (!quiet)
      MessageBoxW(NULL,
                  (L"Не найден файл панели аккаунтов:\n\n" + script +
                       L"\n\nРаспакуйте portable-пакет целиком (accounts_panel.js должен лежать рядом с "
                       L"OpenCode.exe).")
                      .c_str(),
                  L"OpenCode", MB_ICONERROR | MB_OK);
    return 5;
  }

  if (PortOpen(kPanelPort, 400)) {
    Log(L"panel already listening on %d", kPanelPort);
    return 0;
  }

  std::wstring logs = Join(dir, L"logs");
  EnsureDir(logs);
  std::wstring panelLog = Join(logs, L"accounts_panel.out");
  SetEnvironmentVariableW(L"NODE_SKIP_PLATFORM_CHECK", L"1");

  SECURITY_ATTRIBUTES sa;
  memset(&sa, 0, sizeof(sa));
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE h = CreateFileW(panelLog.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) h = NULL;

  std::wstring cmd = L"\"" + node + L"\" \"" + script + L"\" --port " + std::to_wstring(kPanelPort);
  Log(L"starting panel: %s", cmd.c_str());

  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  memset(&si, 0, sizeof(si));
  memset(&pi, 0, sizeof(pi));
  si.cb = sizeof(si);
  if (h) {
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = INVALID_HANDLE_VALUE;
    si.hStdOutput = h;
    si.hStdError = h;
  }
  std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
  mutableCmd.push_back(0);
  BOOL ok = CreateProcessW(node.c_str(), mutableCmd.data(), NULL, NULL, h ? TRUE : FALSE, CREATE_NO_WINDOW, NULL,
                           dir.c_str(), &si, &pi);
  DWORD err = GetLastError();
  if (h) CloseHandle(h);
  if (!ok) {
    Log(L"panel CreateProcessW failed: %s", SysErr(err).c_str());
    if (!quiet)
      MessageBoxW(NULL, (L"Не удалось запустить панель аккаунтов:\n" + SysErr(err)).c_str(), L"OpenCode",
                  MB_ICONERROR | MB_OK);
    return 6;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  if (!WaitPort(kPanelPort, 20000)) {
    Log(L"panel did not start, see %s", panelLog.c_str());
    if (!quiet)
      MessageBoxW(NULL, (L"Панель аккаунтов не запустилась.\n\nЛог: " + panelLog).c_str(), L"OpenCode",
                  MB_ICONERROR | MB_OK);
    return 6;
  }
  return 0;
}

static void OpenPanel() {
  HINSTANCE r = ShellExecuteW(NULL, L"open", kPanelUrl, NULL, NULL, SW_SHOWNORMAL);
  if ((INT_PTR)r <= 32) Log(L"ShellExecuteW(panel) failed: %ld", (long)(INT_PTR)r);
}

// ---------------------------------------------------------------------------
//  browser
// ---------------------------------------------------------------------------

static std::wstring ReadBrowserOverride(const std::wstring& dir) {
  std::wstring p = Join(dir, L"browser.txt");
  if (!FileExists(p)) return L"";
  HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return L"";
  char buf[1024];
  DWORD got = 0;
  ReadFile(h, buf, sizeof(buf) - 1, &got, NULL);
  CloseHandle(h);
  buf[got] = 0;
  std::wstring line;
  for (DWORD i = 0; i < got && buf[i] != '\r' && buf[i] != '\n'; ++i) {
    wchar_t w = (wchar_t)(unsigned char)buf[i];
    line += w;
  }
  while (!line.empty() && (line[0] == L' ' || line[0] == L'"')) line.erase(line.begin());
  while (!line.empty() && (line[line.size() - 1] == L' ' || line[line.size() - 1] == L'"')) line.erase(line.size() - 1);
  return FileExists(line) ? line : L"";
}

static std::wstring FindBrowser(const std::wstring& dir) {
  std::wstring override = ReadBrowserOverride(dir);
  if (!override.empty()) return override;

  std::vector<std::wstring> candidates;
  std::wstring local = GetEnv(L"LOCALAPPDATA");
  std::wstring pf = GetEnv(L"ProgramFiles");
  std::wstring pf86 = GetEnv(L"ProgramFiles(x86)");
  if (!local.empty()) candidates.push_back(Join(local, L"Supermium\\Application\\chrome.exe"));
  if (!pf.empty()) {
    candidates.push_back(Join(pf, L"Supermium\\chrome.exe"));
    candidates.push_back(Join(pf, L"Supermium\\Application\\chrome.exe"));
  }
  if (!pf86.empty()) {
    candidates.push_back(Join(pf86, L"Supermium\\chrome.exe"));
    candidates.push_back(Join(pf86, L"Supermium\\Application\\chrome.exe"));
  }
  candidates.push_back(L"C:\\Program Files\\Supermium\\chrome.exe");
  candidates.push_back(L"C:\\Program Files\\Supermium\\chrome.exe");
  for (size_t i = 0; i < candidates.size(); ++i)
    if (FileExists(candidates[i])) return candidates[i];
  return L"";
}

static void OpenUi(const std::wstring& dir) {
  std::wstring browser = FindBrowser(dir);
  Log(L"browser: %s", browser.empty() ? L"<default>" : browser.c_str());
  if (!browser.empty()) {
    HINSTANCE r = ShellExecuteW(NULL, L"open", browser.c_str(), L"--app=http://127.0.0.1:4096/",
                                dir.c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)r > 32) return;
    Log(L"ShellExecuteW(browser) failed: %ld", (long)(INT_PTR)r);
  }
  ShellExecuteW(NULL, L"open", kUrl, NULL, NULL, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------------------
//  entry point
// ---------------------------------------------------------------------------

int wmain(int argc, wchar_t** argv) {
  std::wstring dir = ExeDir();
  EnsureDir(Join(dir, L"logs"));
  g_logPath = Join(Join(dir, L"logs"), L"launcher.log");

  const wchar_t* mode = argc > 1 ? argv[1] : L"";
  bool serveOnly = wcscmp(mode, L"--serve-only") == 0;
  bool stop = wcscmp(mode, L"--stop") == 0;
  bool selftest = wcscmp(mode, L"--selftest") == 0;
  bool accounts = wcscmp(mode, L"--accounts") == 0;

  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);

  Log(L"---- start mode=%s dir=%s", mode, dir.c_str());

  if (stop) {
    DWORD pid = PortListenerPid(kPort);
    if (!pid) {
      Log(L"stop: nothing listens on %d", kPort);
      WSACleanup();
      return 0;
    }
    std::wstring image = ProcessImage(pid);
    Log(L"stop: pid %lu image=%s", (unsigned long)pid, image.c_str());
    bool ok = !image.empty() && EndsWithNoCase(image, L"node.exe") && StopPid(pid);
    WSACleanup();
    return ok ? 0 : 5;
  }

  if (selftest) {
    Log(L"selftest: node=%d app=%d port4096=%d pid=%lu", (int)FileExists(Join(Join(dir, L"node"), L"node.exe")),
        (int)FileExists(Join(Join(dir, L"app"), L"index.js")), (int)PortOpen(kPort, 500),
        (unsigned long)PortListenerPid(kPort));
    std::wstring b = FindBrowser(dir);
    Log(L"selftest: browser=%s", b.empty() ? L"<default>" : b.c_str());
    WSACleanup();
    return FileExists(Join(Join(dir, L"node"), L"node.exe")) ? 0 : 2;
  }

  if (accounts) {
    int rc = EnsureServer(dir, true);
    if (rc != 0 && rc != 1) {
      WSACleanup();
      return rc;
    }
    int prc = EnsurePanel(dir, false);
    if (prc == 0) OpenPanel();
    WSACleanup();
    Log(L"---- exit %d", prc);
    return prc;
  }

  int rc = EnsureServer(dir, serveOnly || selftest);
  if (rc == 0 || rc == 1) {
    if (!serveOnly) OpenUi(dir);
    rc = 0;
  }
  WSACleanup();
  Log(L"---- exit %d", rc);
  return rc;
}
