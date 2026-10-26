// ---------------------------------------------------------------------------
//  ansi_console.h - runs a child process and, optionally, proxies its ANSI
//  terminal output through a Windows 7 console.
//
//  Windows 7 does not understand ANSI escape sequences (VT100 support only
//  arrived with Windows 10), so the terminal user interface of opencode would
//  be unreadable.  The proxy therefore interprets the escape sequences itself
//  and paints the result into the console buffer.
// ---------------------------------------------------------------------------
#ifndef OCSHELL_ANSI_CONSOLE_H
#define OCSHELL_ANSI_CONSOLE_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Runs `exe` with the given arguments.
//   workdir      working directory (may be NULL)
//   use_console  1 = run behind the ANSI console proxy, 0 = inherit stdio
// Returns the exit code of the child.
int oc_run(const wchar_t* workdir, const wchar_t* exe, int argc, wchar_t** argv, int use_console, void* unused);

// Starts `exe` without waiting for it.  Returns the process handle or NULL.
HANDLE oc_spawn(const wchar_t* workdir, const wchar_t* exe, int argc, wchar_t** argv);

#ifdef __cplusplus
}
#endif

#endif
