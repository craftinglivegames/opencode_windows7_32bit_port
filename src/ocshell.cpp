// ---------------------------------------------------------------------------
//  ocshell - launcher / shell for the portable OpenCode build on Windows 7.
//
//  The executable can be shipped in two shapes:
//
//    * a plain launcher that lives next to an already extracted runtime, or
//    * a self extracting package: the runtime is appended to the executable
//      and unpacked into a portable directory on the first run.
//
//  Modes:
//    ocshell                 start the terminal user interface (TUI)
//    ocshell web             start the HTTP server and open the web UI
//    ocshell serve           start the HTTP server only
//    ocshell run <prompt>    run a single prompt
//    ocshell --help/--version
//  Every other argument is forwarded to opencode.
// ---------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "payload.h"
#include "ansi_console.h"

static void log_error(const wchar_t* what) {
  fwprintf(stderr, L"[OC Shell] %s\n", what);
  fwprintf(stderr, L"Windows error code: %lu\n", (unsigned long)GetLastError());
}

static wchar_t* join(const wchar_t* a, const wchar_t* b) {
  size_t la = wcslen(a), lb = b ? wcslen(b) : 0;
  wchar_t* out = (wchar_t*)malloc((la + lb + 2) * sizeof(wchar_t));
  if (!out) return NULL;
  memcpy(out, a, la * sizeof(wchar_t));
  if (!b) {
    out[la] = 0;
    return out;
  }
  out[la] = L'\\';
  memcpy(out + la + 1, b, (lb + 1) * sizeof(wchar_t));
  return out;
}

static wchar_t* exe_dir(void) {
  wchar_t path[MAX_PATH * 2];
  DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH * 2);
  if (!n) return NULL;
  wchar_t* slash = wcsrchr(path, L'\\');
  if (!slash) return NULL;
  *slash = 0;
  return _wcsdup(path);
}

// ---------------------------------------------------------------------------
//  payload handling
// ---------------------------------------------------------------------------

static int directory_writable(const wchar_t* dir) {
  wchar_t* probe = join(dir, L".oc-write-test");
  if (!probe) return 0;
  HANDLE h = CreateFileW(probe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  int ok = h != INVALID_HANDLE_VALUE;
  if (ok) {
    CloseHandle(h);
    DeleteFileW(probe);
  }
  free(probe);
  return ok;
}

// Extract the appended payload into <base>/portable (or %LOCALAPPDATA%\OCShellWin7)
// and return the directory that holds the runtime.
static wchar_t* extract_payload(const wchar_t* base) {
  wchar_t* target = join(base, L"portable");
  if (!target) return NULL;
  if (!directory_writable(base)) {
    free(target);
    wchar_t local[MAX_PATH];
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) return NULL;
    wchar_t* sub = join(local, L"OCShellWin7");
    if (!sub) return NULL;
    target = sub;
  }
  // already extracted?
  wchar_t* marker = join(target, L".extracted");
  if (marker) {
    if (GetFileAttributesW(marker) != INVALID_FILE_ATTRIBUTES) {
      free(marker);
      return target;
    }
    free(marker);
  }
  fwprintf(stderr, L"[OC Shell] unpacking portable runtime into %s ...\n", target);
  if (oc_payload_extract(target) != 0) {
    log_error(L"failed to unpack the portable runtime");
    free(target);
    return NULL;
  }
  wchar_t* done = join(target, L".extracted");
  if (done) {
    HANDLE h = CreateFileW(done, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    free(done);
  }
  return target;
}

// ---------------------------------------------------------------------------
//  helpers
// ---------------------------------------------------------------------------

static void open_browser(const wchar_t* url) {
  wchar_t* command = join(url, NULL);
  (void)command;
  ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
}

static wchar_t* runtime_exe(wchar_t* dir) { return join(dir, L"opencode.exe"); }

static void usage(void) {
  wprintf(L"OC Shell for Windows 7 - portable OpenCode\n\n");
  wprintf(L"  ocshell                 start the terminal user interface\n");
  wprintf(L"  ocshell web             start the server and open the web interface\n");
  wprintf(L"  ocshell serve           start the server only (http://127.0.0.1:4096)\n");
  wprintf(L"  ocshell run <prompt>    run a single prompt and exit\n");
  wprintf(L"  ocshell --help          this text\n");
  wprintf(L"  ocshell --version       print the bundled OpenCode version\n");
  wprintf(L"\nAny other argument is passed straight to opencode.\n");
}

int wmain(int argc, wchar_t** argv) {
  wchar_t* base = exe_dir();
  if (!base) return 1;

  wchar_t* runtime = extract_payload(base);
  if (!runtime) return 2;

  wchar_t* opencode = runtime_exe(runtime);
  if (!opencode) return 3;
  if (GetFileAttributesW(opencode) == INVALID_FILE_ATTRIBUTES) {
    log_error(L"opencode.exe is missing from the portable runtime");
    return 4;
  }

  // ------------------------------------------------------------------ modes
  int mode = 0;  // 0 = tui, 1 = web, 2 = serve, 3 = run
  int first = 1;
  if (argc > 1) {
    if (wcscmp(argv[1], L"--help") == 0 || wcscmp(argv[1], L"-h") == 0 || wcscmp(argv[1], L"/?") == 0) {
      usage();
      return 0;
    }
    if (wcscmp(argv[1], L"--version") == 0 || wcscmp(argv[1], L"-v") == 0) {
      wchar_t* args[] = {L"--version"};
      return (int)oc_run(runtime, opencode, 1, args, 0, NULL);
    }
    if (wcscmp(argv[1], L"web") == 0) {
      mode = 1;
      first = 2;
    } else if (wcscmp(argv[1], L"serve") == 0) {
      mode = 2;
      first = 2;
    } else if (wcscmp(argv[1], L"run") == 0) {
      mode = 3;
      first = 2;
    }
  }

  if (mode == 1 || mode == 2) {
    // opencode serve, optionally followed by extra arguments
    int extra = 0;
    wchar_t** args = (wchar_t**)calloc(argc + 4, sizeof(wchar_t*));
    if (!args) return 5;
    args[extra++] = L"serve";
    for (int i = first; i < argc; i++) args[extra++] = argv[i];
    args[extra] = NULL;
    if (mode == 1) {
      // start the server in the background and show the web interface
      HANDLE process = oc_spawn(runtime, opencode, extra, args);
      if (!process) {
        log_error(L"cannot start opencode serve");
        free(args);
        return 6;
      }
      Sleep(2500);
      open_browser(L"http://127.0.0.1:4096/");
      WaitForSingleObject(process, INFINITE);
      DWORD code = 0;
      GetExitCodeProcess(process, &code);
      CloseHandle(process);
      free(args);
      return (int)code;
    }
    int rc = (int)oc_run(runtime, opencode, extra, args, 0, NULL);
    free(args);
    return rc;
  }

  if (mode == 3) {
    int extra = 0;
    wchar_t** args = (wchar_t**)calloc(argc + 4, sizeof(wchar_t*));
    if (!args) return 5;
    args[extra++] = L"run";
    for (int i = first; i < argc; i++) args[extra++] = argv[i];
    args[extra] = NULL;
    int rc = (int)oc_run(runtime, opencode, extra, args, 0, NULL);
    free(args);
    return rc;
  }

  // default: terminal user interface
  int extra = 0;
  wchar_t** args = (wchar_t**)calloc(argc + 2, sizeof(wchar_t*));
  if (!args) return 5;
  for (int i = first; i < argc; i++) args[extra++] = argv[i];
  args[extra] = NULL;
  int rc = (int)oc_run(runtime, opencode, extra, args, 1, NULL);
  free(args);
  return rc;
}
