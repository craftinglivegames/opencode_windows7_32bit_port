// ---------------------------------------------------------------------------
//  Shared implementation of the Windows 8 primitives that are missing on
//  Windows 7.  Included by both ocshim32.cpp (kernel32 flavour) and ocn.cpp
//  (ntdll flavour).
//
//  The two flavours have different prototypes:
//
//    kernel32:  BOOL  WaitOnAddress(addr, cmp, size, DWORD milliseconds)
//    ntdll:     NTSTATUS RtlWaitOnAddress(addr, cmp, size, PLARGE_INTEGER timeout)
//
//  The ntdll timeout follows the Nt* convention: a pointer to a 100ns
//  timestamp, negative values are relative, NULL means "wait forever".
// ---------------------------------------------------------------------------
#ifndef OCSHIM_COMMON_H
#define OCSHIM_COMMON_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

#ifndef NTSTATUS
typedef LONG NTSTATUS;
#endif

#define OCSHIM_STATUS_SUCCESS ((NTSTATUS)0)
#define OCSHIM_STATUS_TIMEOUT ((NTSTATUS)0x00000102L)

typedef struct _OCSHIM_MEMORY_USAGE_INFORMATION {
  UINT64 TotalCommit;
  UINT64 TotalPrivate;
  UINT64 TotalPrivateWorkingSet;
  UINT64 TotalShared;
  UINT64 TotalSharedWorkingSet;
  UINT64 TotalPagefile;
  UINT64 TotalPagefileWorkingSet;
  UINT64 TotalVirtual;
  UINT64 TotalVirtualWorkingSet;
} OCSHIM_MEMORY_USAGE_INFORMATION;

typedef VOID(WINAPI* PFN_GetSystemTimePreciseAsFileTime)(LPFILETIME);
typedef HANDLE(WINAPI* PFN_CreateFile2)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HRESULT(WINAPI* PFN_GetThreadDescription)(HANDLE, PWSTR);
typedef BOOL(WINAPI* PFN_NeedCurrentDirectoryForExePathW)(LPCWSTR);
typedef VOID(WINAPI* PFN_GetCurrentThreadStackLimits)(PULONG_PTR, PULONG_PTR);
typedef BOOL(WINAPI* PFN_DiscardVirtualMemory)(HANDLE, SIZE_T, OCSHIM_MEMORY_USAGE_INFORMATION*);
typedef DWORD(WINAPI* PFN_GetTimeZoneInformationForYear)(USHORT, LPTIME_ZONE_INFORMATION);
typedef BOOL(WINAPI* PFN_CreatePseudoConsole)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
typedef VOID(WINAPI* PFN_ClosePseudoConsole)(HPCON);
typedef BOOL(WINAPI* PFN_ResizePseudoConsole)(HPCON, COORD);

static HMODULE ocshim_real_kernel32() {
  static HMODULE cached = NULL;
  if (!cached) cached = GetModuleHandleW(L"kernel32.dll");
  return cached;
}

static FARPROC ocshim_real_proc(const char* name) { return GetProcAddress(ocshim_real_kernel32(), name); }

// ---------------------------------------------------------------------------
//  Optional call tracing (enabled by defining OCSHIM_TRACE)
// ---------------------------------------------------------------------------
#ifdef OCSHIM_TRACE
static void ocshim_trace(const char* what) {
  HANDLE file = CreateFileA("C:\\Temp\\ocshim.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(file, what, (DWORD)lstrlenA(what), &written, NULL);
  WriteFile(file, "\r\n", 2, &written, NULL);
  CloseHandle(file);
}
#else
static void ocshim_trace(const char* what) { (void)what; }
#endif

// ---------------------------------------------------------------------------
//  WaitOnAddress / WakeByAddress
//
//  A waiter is only woken when *its* address is signalled, so the callers
//  never observe a spurious wakeup (the Windows contract allows them, but
//  the JSC atomics rely on the value really having changed).
// ---------------------------------------------------------------------------

struct OCSHIM_WAITER {
  volatile VOID* address;
  HANDLE event;
  struct OCSHIM_WAITER* next;
};

static CONDITION_VARIABLE g_cvar;
static SRWLOCK g_srw;
static SRWLOCK g_wait_lock;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static struct OCSHIM_WAITER* g_waiters = NULL;

static BOOL CALLBACK g_init(PINIT_ONCE, PVOID, PVOID*) {
  InitializeConditionVariable(&g_cvar);
  InitializeSRWLock(&g_srw);
  InitializeSRWLock(&g_wait_lock);
  return TRUE;
}

static void ocshim_ensure_init() { InitOnceExecuteOnce(&g_once, g_init, NULL, NULL); }

// returns TRUE when the value at `address` differs from `compare`
static BOOL ocshim_wait_address(volatile VOID* address, PVOID compare, SIZE_T size, DWORD timeoutMs) {
  ocshim_ensure_init();
  HANDLE event = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (!event) return TRUE;

  struct OCSHIM_WAITER waiter;
  waiter.address = address;
  waiter.event = event;
  waiter.next = NULL;

  AcquireSRWLockExclusive(&g_wait_lock);
  if (memcmp((const void*)address, (const void*)compare, size) != 0) {
    ReleaseSRWLockExclusive(&g_wait_lock);
    CloseHandle(event);
    return TRUE;
  }
  waiter.next = g_waiters;
  g_waiters = &waiter;
  ReleaseSRWLockExclusive(&g_wait_lock);

  WaitForSingleObject(event, timeoutMs);

  AcquireSRWLockExclusive(&g_wait_lock);
  struct OCSHIM_WAITER** slot = &g_waiters;
  while (*slot) {
    if (*slot == &waiter) {
      *slot = waiter.next;
      break;
    }
    slot = &(*slot)->next;
  }
  ReleaseSRWLockExclusive(&g_wait_lock);
  CloseHandle(event);

  return memcmp((const void*)address, (const void*)compare, size) != 0;
}

static void ocshim_wake_address(volatile VOID* address, BOOL all) {
  ocshim_ensure_init();
  AcquireSRWLockExclusive(&g_wait_lock);
  for (struct OCSHIM_WAITER* w = g_waiters; w; w = w->next) {
    if (w->address == address) {
      SetEvent(w->event);
      if (!all) break;
    }
  }
  ReleaseSRWLockExclusive(&g_wait_lock);
}

// convert an ntdll style timeout (100ns timestamp, negative = relative,
// NULL = infinite) into a millisecond timeout for WaitForSingleObject
static DWORD ocshim_timeout_to_ms(const LARGE_INTEGER* timeout) {
  if (!timeout) return INFINITE;
  LONGLONG value = timeout->QuadPart;
  if (value < 0) {
    LONGLONG relative = -value;               // 100ns units
    LONGLONG ms = (relative + 9999) / 10000;  // round up to milliseconds
    if (ms <= 0) ms = 1;
    if (ms > (LONGLONG)INFINITE - 1) ms = INFINITE - 1;
    return (DWORD)ms;
  }
  if (value == 0) return 0;
  // absolute timestamp in 100ns units since 1601-01-01
  FILETIME now;
  GetSystemTimeAsFileTime(&now);
  ULARGE_INTEGER nowValue;
  nowValue.LowPart = now.dwLowDateTime;
  nowValue.HighPart = now.dwHighDateTime;
  if ((ULONGLONG)value <= nowValue.QuadPart) return 0;
  ULONGLONG delta = (ULONGLONG)value - nowValue.QuadPart;  // 100ns units
  ULONGLONG ms = (delta + 9999) / 10000;
  if (ms > (ULONGLONG)INFINITE - 1) ms = INFINITE - 1;
  return (DWORD)ms;
}

static ULONGLONG ocshim_precise_time_value() {
  FILETIME ft;
  static PFN_GetSystemTimePreciseAsFileTime real =
      (PFN_GetSystemTimePreciseAsFileTime)ocshim_real_proc("GetSystemTimePreciseAsFileTime");
  if (real)
    real(&ft);
  else
    GetSystemTimeAsFileTime(&ft);
  return ((ULONGLONG)ft.dwHighDateTime << 32) | (ULONGLONG)ft.dwLowDateTime;
}

#endif  // OCSHIM_COMMON_H
