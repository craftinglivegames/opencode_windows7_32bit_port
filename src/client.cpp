// ---------------------------------------------------------------------------
//  OCShellClient - 32-bit (x86) Windows 7 launcher for the opencode web UI.
//
//  What it does:
//    1. reads server.ini next to the executable ([server] host/port/token/
//       supermium); creates the file with defaults when it is missing;
//    2. probes host:port with a 2 s TCP connect timeout;
//    3. if the server is down, asks the autostart bridge
//       (POST http://host:8011/start?token=...) and polls host:port for 25 s;
//    4. locates supermium.exe (ini, registry, Program Files) and starts it in
//       app mode with a private profile under %LOCALAPPDATA%\OCShellClient;
//    5. shows every failure as a MessageBoxW (GUI subsystem, no console).
//
//  Diagnostic mode (no UI, writes %LOCALAPPDATA%\OCShellClient\client.log):
//      OCShellClient.exe --selftest
//    exit 0 = server reachable and supermium found, 1 = bad config,
//    2 = server unreachable, 3 = supermium not found.
// ---------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <cstdarg>
#include <cstdio>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ws2_32.lib")

// ---------------------------------------------------------------------------
//  small helpers
// ---------------------------------------------------------------------------

static std::wstring Trim(const std::wstring& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n' ||
                   s[b] == L'"'))
    ++b;
  while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' ||
                   s[e - 1] == L'\n' || s[e - 1] == L'"'))
    --e;
  return s.substr(b, e - b);
}

static std::wstring ExeDir() {
  wchar_t buf[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(NULL, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
  if (!n) return L"";
  std::wstring p(buf, n);
  size_t pos = p.find_last_of(L"\\/");
  if (pos == std::wstring::npos) return L".";
  return p.substr(0, pos);
}

static std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (b.size() > 1 && b[1] == L':') return b;  // b is already absolute
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
  DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool FileBiggerThan(const std::wstring& p, DWORD limit) {
  WIN32_FILE_ATTRIBUTE_DATA fad;
  if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) return false;
  return fad.nFileSizeHigh > 0 || fad.nFileSizeLow > limit;
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

static std::wstring Expand(const std::wstring& s) {
  if (s.find(L'%') == std::wstring::npos) return s;
  DWORD n = ExpandEnvironmentStringsW(s.c_str(), NULL, 0);
  if (!n) return s;
  std::wstring out(n, L'\0');
  DWORD r = ExpandEnvironmentStringsW(s.c_str(), &out[0], n);
  if (!r || r > n) return s;
  out.resize(r - 1);
  return out;
}

static std::wstring SysErr(DWORD code) {
  wchar_t* buf = NULL;
  DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                               FORMAT_MESSAGE_IGNORE_INSERTS,
                           NULL, code, 0, (LPWSTR)&buf, 0, NULL);
  std::wstring s;
  if (n && buf) s = Trim(std::wstring(buf, n));
  if (buf) LocalFree(buf);
  if (s.empty()) {
    wchar_t t[64];
    _snwprintf_s(t, _TRUNCATE, L"код ошибки %lu", (unsigned long)code);
    s = t;
  }
  return s;
}

// ---------------------------------------------------------------------------
//  log file (%LOCALAPPDATA%\OCShellClient\client.log)
// ---------------------------------------------------------------------------

static std::wstring DataDir() {
  std::wstring base = GetEnv(L"LOCALAPPDATA");
  if (base.empty()) base = ExeDir();
  return JoinPath(base, L"OCShellClient");
}

static void Log(const wchar_t* fmt, ...) {
  wchar_t msg[2048];
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf_s(msg, _TRUNCATE, fmt, ap);
  va_end(ap);

  SYSTEMTIME st;
  GetLocalTime(&st);
  wchar_t line[2300];
  _snwprintf_s(line, _TRUNCATE, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu] %s\r\n",
               (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
               (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
               (unsigned)st.wMilliseconds, (unsigned long)GetCurrentProcessId(), msg);

  int need = WideCharToMultiByte(CP_UTF8, 0, line, -1, NULL, 0, NULL, NULL);
  if (need <= 1) return;
  std::string utf8((size_t)need - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, line, -1, &utf8[0], need, NULL, NULL);

  std::wstring dir = DataDir();
  EnsureDir(dir);
  std::wstring path = JoinPath(dir, L"client.log");
  if (FileBiggerThan(path, 512 * 1024)) DeleteFileW(path.c_str());

  HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, NULL);
  CloseHandle(h);
}

static void Fail(const wchar_t* fmt, ...) {
  wchar_t msg[3072];
  va_list ap;
  va_start(ap, fmt);
  _vsnwprintf_s(msg, _TRUNCATE, fmt, ap);
  va_end(ap);
  Log(L"FATAL: %s", msg);
  MessageBoxW(NULL, msg, L"OCShellClient", MB_ICONERROR | MB_OK | MB_SETFOREGROUND);
}

// ---------------------------------------------------------------------------
//  configuration (server.ini next to the executable)
// ---------------------------------------------------------------------------

struct Config {
  std::wstring host;
  unsigned short port;
  std::wstring token;
  std::wstring supermium;
  std::wstring iniPath;
};

static const char kDefaultIni[] =
    "; OCShellClient configuration.\r\n"
    "; host/port  - opencode web server on the host machine;\r\n"
    "; token     - shared secret of the autostart bridge (port 8011);\r\n"
    "; supermium - optional explicit path to supermium.exe.\r\n"
    "\r\n"
    "[server]\r\n"
    "host=127.0.0.1\r\n"
    "port=8090\r\n"
    "token=ocshell\r\n"
    "; supermium=C:\\Program Files\\Supermium\\supermium.exe\r\n";

static bool WriteDefaultIni(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  DWORD size = (DWORD)(sizeof(kDefaultIni) - 1);
  BOOL ok = WriteFile(h, kDefaultIni, size, &written, NULL) && written == size;
  CloseHandle(h);
  return ok != FALSE;
}

static bool LoadConfig(Config& cfg, std::wstring& err) {
  cfg.host = L"127.0.0.1";
  cfg.port = 8090;
  cfg.token = L"ocshell";
  cfg.supermium.clear();
  cfg.iniPath = JoinPath(ExeDir(), L"server.ini");

  if (GetFileAttributesW(cfg.iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
    if (WriteDefaultIni(cfg.iniPath))
      Log(L"created default %s", cfg.iniPath.c_str());
    else
      Log(L"cannot create %s: %s", cfg.iniPath.c_str(),
          SysErr(GetLastError()).c_str());
  }

  auto get = [&](const wchar_t* key, const wchar_t* def) -> std::wstring {
    wchar_t buf[1024];
    DWORD n = GetPrivateProfileStringW(L"server", key, def, buf,
                                       (DWORD)(sizeof(buf) / sizeof(buf[0])),
                                       cfg.iniPath.c_str());
    if (!n) return std::wstring(def ? def : L"");
    return Trim(std::wstring(buf, n));
  };

  std::wstring host = get(L"host", L"127.0.0.1");
  std::wstring portStr = get(L"port", L"8090");
  std::wstring token = get(L"token", L"ocshell");
  std::wstring sup = get(L"supermium", L"");

  if (host.empty()) {
    err = L"В файле server.ini не задан параметр host.";
    return false;
  }

  if (portStr.empty()) portStr = L"8090";
  wchar_t* end = NULL;
  unsigned long port = wcstoul(portStr.c_str(), &end, 10);
  if (end == portStr.c_str() || port == 0 || port > 65535) {
    err = L"В файле server.ini параметр port должен быть числом от 1 до 65535 (сейчас: \"" +
          portStr + L"\").";
    return false;
  }

  cfg.host = host;
  cfg.port = (unsigned short)port;
  cfg.token = token;
  cfg.supermium = sup;
  return true;
}

// ---------------------------------------------------------------------------
//  TCP probe with timeout
// ---------------------------------------------------------------------------

static bool TcpProbe(const std::wstring& host, unsigned short port, int timeoutMs) {
  static bool wsaReady = false;
  if (!wsaReady) {
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) return false;
    wsaReady = true;
  }

  wchar_t portStr[16];
  _snwprintf_s(portStr, _TRUNCATE, L"%u", (unsigned)port);

  ADDRINFOW hints;
  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  ADDRINFOW* res = NULL;
  if (GetAddrInfoW(host.c_str(), portStr, &hints, &res) != 0 || !res) return false;

  const DWORD deadline = GetTickCount() + (DWORD)(timeoutMs > 0 ? timeoutMs : 1);
  bool ok = false;

  for (ADDRINFOW* ai = res; ai && !ok; ai = ai->ai_next) {
    DWORD left = deadline - GetTickCount();
    if ((int)left <= 0) break;

    SOCKET s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == INVALID_SOCKET) continue;

    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);

    int rc = connect(s, ai->ai_addr, (int)ai->ai_addrlen);
    if (rc == 0) {
      ok = true;
    } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
      left = deadline - GetTickCount();
      if ((int)left <= 0) left = 0;
      fd_set wr, ex;
      FD_ZERO(&wr);
      FD_SET(s, &wr);
      FD_ZERO(&ex);
      FD_SET(s, &ex);
      timeval tv;
      tv.tv_sec = (long)(left / 1000);
      tv.tv_usec = (long)((left % 1000) * 1000);
      int sel = select(0, NULL, &wr, &ex, &tv);
      if (sel > 0 && FD_ISSET(s, &wr)) {
        int soError = 0;
        int len = (int)sizeof(soError);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soError, &len) == 0 &&
            soError == 0)
          ok = true;
      }
    }
    closesocket(s);
  }

  FreeAddrInfoW(res);
  return ok;
}

static bool WaitServer(const std::wstring& host, unsigned short port, int totalMs) {
  const DWORD deadline = GetTickCount() + (DWORD)totalMs;
  int attempt = 0;
  for (;;) {
    ++attempt;
    if (TcpProbe(host, port, 1000)) {
      Log(L"server %s:%u is up (attempt %d)", host.c_str(), (unsigned)port, attempt);
      return true;
    }
    if ((int)(deadline - GetTickCount()) <= 500) break;
    Sleep(500);
  }
  Log(L"server %s:%u did not come up in %d ms", host.c_str(), (unsigned)port, totalMs);
  return false;
}

// ---------------------------------------------------------------------------
//  autostart bridge request (WinHTTP)
// ---------------------------------------------------------------------------

static std::wstring UrlEncode(const std::wstring& s) {
  static const char* hex = "0123456789ABCDEF";
  std::wstring out;
  for (wchar_t ch : s) {
    unsigned c = (unsigned)ch;
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
        (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '.' || c == '~') {
      out += ch;
    } else if (c < 0x80) {
      out += L'%';
      out += (wchar_t)hex[(c >> 4) & 0xF];
      out += (wchar_t)hex[c & 0xF];
    } else {
      char utf8[8];
      int n = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, utf8, (int)sizeof(utf8), NULL, NULL);
      for (int i = 0; i < n; ++i) {
        unsigned b = (unsigned char)utf8[i];
        out += L'%';
        out += (wchar_t)hex[(b >> 4) & 0xF];
        out += (wchar_t)hex[b & 0xF];
      }
    }
  }
  return out;
}

// Returns 0 on transport failure (err describes it), otherwise HTTP status code.
static DWORD BridgeStart(const std::wstring& host, const std::wstring& token,
                         std::wstring& err) {
  err.clear();

  HINTERNET ses = WinHttpOpen(L"OCShellClient/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) {
    err = SysErr(GetLastError());
    return 0;
  }
  WinHttpSetTimeouts(ses, 3000, 3000, 4000, 4000);

  HINTERNET con = WinHttpConnect(ses, host.c_str(), 8011, 0);
  if (!con) {
    err = SysErr(GetLastError());
    WinHttpCloseHandle(ses);
    return 0;
  }

  std::wstring path = L"/start";
  if (!token.empty()) path += L"?token=" + UrlEncode(token);

  HINTERNET req = WinHttpOpenRequest(con, L"POST", path.c_str(), NULL,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
  if (!req) {
    err = SysErr(GetLastError());
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return 0;
  }

  std::wstring headers = L"Content-Length: 0\r\nContent-Type: text/plain\r\n";
  if (!token.empty()) headers += L"X-Token: " + token + L"\r\n";

  DWORD status = 0;
  if (!WinHttpSendRequest(req, headers.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA,
                          0, 0, 0) ||
      !WinHttpReceiveResponse(req, NULL)) {
    err = SysErr(GetLastError());
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return 0;
  }

  DWORD len = sizeof(status);
  if (!WinHttpQueryHeaders(req,
                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &len,
                           WINHTTP_NO_HEADER_INDEX)) {
    err = SysErr(GetLastError());
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return 0;
  }

  // Drain a short body so the bridge can finish its answer.
  char buf[512];
  DWORD read = 0;
  std::string body;
  while (WinHttpReadData(req, buf, (DWORD)sizeof(buf), &read) && read > 0 &&
         body.size() < 4096) {
    body.append(buf, read);
  }

  Log(L"POST http://%s:8011/start -> %lu %s", host.c_str(), (unsigned long)status,
      body.empty() ? L"(no body)" : L"(body received)");

  WinHttpCloseHandle(req);
  WinHttpCloseHandle(con);
  WinHttpCloseHandle(ses);
  return status;
}

// ---------------------------------------------------------------------------
//  supermium discovery
// ---------------------------------------------------------------------------

static std::wstring NormalizeExePath(const std::wstring& raw) {
  std::wstring p = Trim(Expand(raw));
  if (p.empty()) return p;
  size_t len = p.size();
  if (len < 4 || _wcsicmp(p.c_str() + len - 4, L".exe") != 0)
    p = JoinPath(p, L"supermium.exe");
  return p;
}

static bool RegReadInstallLocation(const wchar_t* subKey, REGSAM extra,
                                   std::wstring& out) {
  HKEY key = NULL;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ | extra, &key) !=
      ERROR_SUCCESS)
    return false;

  static const wchar_t* kNames[] = {L"InstallLocation", L"InstallDir", L"Path"};
  bool found = false;
  for (int i = 0; i < 3 && !found; ++i) {
    wchar_t buf[MAX_PATH * 2];
    DWORD size = (DWORD)sizeof(buf);
    DWORD type = 0;
    if (RegQueryValueExW(key, kNames[i], NULL, &type, (LPBYTE)buf, &size) ==
            ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && size > sizeof(wchar_t)) {
      std::wstring v = Trim(std::wstring(buf));
      if (!v.empty()) {
        out = v;
        found = true;
      }
    }
  }
  RegCloseKey(key);
  return found;
}

static std::wstring FindSupermium(const Config& cfg) {
  struct Candidate {
    std::wstring label;
    std::wstring path;
  };
  std::vector<Candidate> list;

  auto add = [&](const std::wstring& label, const std::wstring& rawPath) {
    if (rawPath.empty()) return;
    Candidate c;
    c.label = label;
    c.path = NormalizeExePath(rawPath);
    if (!c.path.empty()) list.push_back(c);
  };

  // 1. explicit value from server.ini
  add(L"server.ini", cfg.supermium);

  // 2/3. registry: default view (Wow6432Node for this 32-bit process) and
  //      native 64-bit view, then the literal redirected path.
  std::wstring reg;
  if (RegReadInstallLocation(L"SOFTWARE\\Supermium", 0, reg))
    add(L"HKLM\\SOFTWARE\\Supermium", reg);
  if (RegReadInstallLocation(L"SOFTWARE\\Supermium", KEY_WOW64_64KEY, reg))
    add(L"HKLM\\SOFTWARE\\Supermium (64-bit view)", reg);
  if (RegReadInstallLocation(L"SOFTWARE\\Wow6432Node\\Supermium", 0, reg))
    add(L"HKLM\\SOFTWARE\\Wow6432Node\\Supermium", reg);

  // 4. well known installation folders
  add(L"C:\\Program Files\\Supermium", L"C:\\Program Files\\Supermium");
  add(L"C:\\Program Files\\Supermium", L"C:\\Program Files\\Supermium");
  std::wstring programFiles = GetEnv(L"ProgramFiles");
  if (!programFiles.empty()) add(L"%ProgramFiles%\\Supermium", programFiles + L"\\Supermium");
  std::wstring programFilesX86 = GetEnv(L"ProgramFiles(x86)");
  if (!programFilesX86.empty())
    add(L"%ProgramFiles(x86)%\\Supermium", programFilesX86 + L"\\Supermium");

  for (const Candidate& c : list) {
    if (FileExists(c.path)) {
      Log(L"supermium found via %s: %s", c.label.c_str(), c.path.c_str());
      return c.path;
    }
    // Supermium 122 ships its binary as chrome.exe, not supermium.exe.
    size_t slash = c.path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
      std::wstring alt = c.path.substr(0, slash + 1) + L"chrome.exe";
      if (FileExists(alt)) {
        Log(L"supermium found via %s (chrome.exe): %s", c.label.c_str(),
            alt.c_str());
        return alt;
      }
    }
    Log(L"supermium candidate (%s) missing: %s", c.label.c_str(), c.path.c_str());
  }
  Log(L"supermium not found");
  return L"";
}

// ---------------------------------------------------------------------------
//  browser launch
// ---------------------------------------------------------------------------

static bool LaunchBrowser(const std::wstring& exe, const std::wstring& url,
                          const std::wstring& profileDir, DWORD& exitCode,
                          std::wstring& err) {
  if (!EnsureDir(profileDir))
    Log(L"warning: cannot create profile dir %s: %s", profileDir.c_str(),
        SysErr(GetLastError()).c_str());

  std::wstring cmd = L"\"" + exe + L"\"";
  cmd += L" --app=" + url;
  cmd += L" --user-data-dir=\"" + profileDir + L"\"";
  cmd += L" --no-first-run";
  cmd += L" --disable-session-crashed-bubble";
  cmd += L" --window-size=1280,800";

  Log(L"launch: %s", cmd.c_str());

  std::vector<wchar_t> cmdLine(cmd.begin(), cmd.end());
  cmdLine.push_back(L'\0');

  STARTUPINFOW si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));

  if (!CreateProcessW(exe.c_str(), cmdLine.data(), NULL, NULL, FALSE, 0, NULL, NULL,
                      &si, &pi)) {
    err = SysErr(GetLastError());
    Log(L"CreateProcessW failed: %s", err.c_str());
    return false;
  }

  Log(L"supermium started, pid=%lu", (unsigned long)pi.dwProcessId);
  WaitForSingleObject(pi.hProcess, INFINITE);
  exitCode = 0;
  GetExitCodeProcess(pi.hProcess, &exitCode);
  Log(L"supermium exited with code %lu", (unsigned long)exitCode);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

// ---------------------------------------------------------------------------
//  self-test (no UI)
// ---------------------------------------------------------------------------

static int RunSelfTest() {
  Log(L"=== selftest ===");
  OSVERSIONINFOW vi;
  ZeroMemory(&vi, sizeof(vi));
  vi.dwOSVersionInfoSize = sizeof(vi);
#pragma warning(push)
#pragma warning(disable : 4996)
  GetVersionExW(&vi);
#pragma warning(pop)
  Log(L"windows %lu.%lu build %lu, %s process",
      (unsigned long)vi.dwMajorVersion, (unsigned long)vi.dwMinorVersion,
      (unsigned long)vi.dwBuildNumber,
#ifdef _WIN64
      L"64-bit"
#else
      L"32-bit"
#endif
  );

  Config cfg;
  std::wstring err;
  if (!LoadConfig(cfg, err)) {
    Log(L"config: ERROR %s", err.c_str());
    return 1;
  }
  Log(L"config: ini=%s host=%s port=%u token=%s", cfg.iniPath.c_str(),
      cfg.host.c_str(), (unsigned)cfg.port,
      cfg.token.empty() ? L"<empty>" : L"<set>");

  bool up = TcpProbe(cfg.host, cfg.port, 2000);
  Log(L"tcp %s:%u reachable=%s", cfg.host.c_str(), (unsigned)cfg.port,
      up ? L"yes" : L"no");

  std::wstring sup = FindSupermium(cfg);
  Log(L"supermium: %s", sup.empty() ? L"<not found>" : sup.c_str());

  if (!up) return 2;
  if (sup.empty()) return 3;
  Log(L"selftest: OK");
  return 0;
}

static bool HasFlag(const wchar_t* cmdLine, const wchar_t* flag) {
  if (!cmdLine || !*cmdLine) return false;
  return wcsstr(cmdLine, flag) != NULL;
}

// ---------------------------------------------------------------------------
//  entry point
// ---------------------------------------------------------------------------

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR lpCmdLine, int) {
  if (HasFlag(lpCmdLine, L"--selftest") || HasFlag(lpCmdLine, L"/selftest"))
    return RunSelfTest();

  Config cfg;
  std::wstring err;
  if (!LoadConfig(cfg, err)) {
    Fail(L"Не удалось прочитать настройки OCSHellClient.\r\n\r\n%s\r\n\r\nФайл: %s",
         err.c_str(), cfg.iniPath.c_str());
    return 1;
  }

  std::wstring url = L"http://" + cfg.host + L":" +
                     std::to_wstring((unsigned)cfg.port);
  Log(L"=== start: %s (ini=%s) ===", url.c_str(), cfg.iniPath.c_str());
  Log(L"config: host=%s port=%u token=%s supermium=%s", cfg.host.c_str(),
      (unsigned)cfg.port, cfg.token.empty() ? L"<empty>" : L"<set>",
      cfg.supermium.empty() ? L"<auto>" : cfg.supermium.c_str());

  if (!TcpProbe(cfg.host, cfg.port, 2000)) {
    Log(L"server is down, asking the bridge on %s:8011", cfg.host.c_str());
    std::wstring httpErr;
    DWORD status = BridgeStart(cfg.host, cfg.token, httpErr);
    if (status == 0) {
      Fail(L"Сервер opencode недоступен (%s), и мост автозапуска не отвечает.\r\n\r\n"
           L"Проверьте, что на машине %s запущен OCShellBridge (порт 8011).\r\n\r\n"
           L"Ошибка: %s",
           url.c_str(), cfg.host.c_str(), httpErr.c_str());
      return 2;
    }
    if (status == 401 || status == 403) {
      Fail(L"Мост автозапуска отклонил запрос: неверный token.\r\n\r\n"
           L"Проверьте ключ token в файле:\r\n%s",
           cfg.iniPath.c_str());
      return 3;
    }
    if (status < 200 || status >= 300) {
      Fail(L"Мост автозапуска ответил кодом %lu.\r\n\r\nПроверьте журнал "
           L"OCShellBridge на машине %s.",
           (unsigned long)status, cfg.host.c_str());
      return 4;
    }
    if (!WaitServer(cfg.host, cfg.port, 25000)) {
      Fail(L"Сервер opencode не поднялся за 25 секунд.\r\n\r\n"
           L"Адрес: %s\r\nПроверьте журнал моста автозапуска на машине %s.",
           url.c_str(), cfg.host.c_str());
      return 5;
    }
  } else {
    Log(L"server is already up: %s", url.c_str());
  }

  std::wstring supermium = FindSupermium(cfg);
  if (supermium.empty()) {
    Fail(L"Не найден supermium.exe.\r\n\r\n"
         L"Укажите полный путь в файле:\r\n%s\r\n\r\n"
         L"Пример:\r\n[server]\r\nsupermium=C:\\Program Files\\Supermium\\supermium.exe\r\n\r\n"
         L"Либо установите Supermium в стандартную папку Program Files.",
         cfg.iniPath.c_str());
    return 6;
  }

  DWORD exitCode = 0;
  if (!LaunchBrowser(supermium, url, JoinPath(DataDir(), L"profile"), exitCode,
                     err)) {
    Fail(L"Не удалось запустить браузер.\r\n\r\n%s\r\n\r\n%s",
         supermium.c_str(), err.c_str());
    return 7;
  }
  return (int)exitCode;
}
