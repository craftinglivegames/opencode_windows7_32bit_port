// ---------------------------------------------------------------------------
//  ocshim32 - Windows 7 compatibility shim for Bun based binaries
//
//  The Bun runtime (and therefore opencode.exe) statically imports a handful of
//  Win8+/Win10+ APIs that do not exist on Windows 7 SP1.  This DLL replaces
//  the kernel32 import descriptor of the host executable and re-exports every
//  kernel32 function the host needs:
//
//    * functions that exist on Windows 7  -> PE forwarders ("KERNEL32.Foo")
//    * functions missing on Windows 7     -> real implementations below
//
//  The same DLL also provides the Windows 8 "synchronization" primitives
//  (WaitOnAddress / WakeByAddress*) that the host imports through the
//  api-ms-win-core-synch-l1-2-0 API set.
// ---------------------------------------------------------------------------

#include "ocshim_common.h"
#include <winternl.h>

// --- GetSystemTimePreciseAsFileTime -----------------------------------------
extern "C" VOID WINAPI ocshim_GetSystemTimePreciseAsFileTime(LPFILETIME out) {
  ULONGLONG value = ocshim_precise_time_value();
  if (out) {
    out->dwLowDateTime = (DWORD)value;
    out->dwHighDateTime = (DWORD)(value >> 32);
  }
}

// --- GetThreadDescription ---------------------------------------------------
extern "C" HRESULT WINAPI ocshim_GetThreadDescription(HANDLE thread, PWSTR buffer) {
  static PFN_GetThreadDescription real = (PFN_GetThreadDescription)ocshim_real_proc("GetThreadDescription");
  if (real) return real(thread, buffer);
  if (buffer) buffer[0] = L'\0';
  return S_OK;
}

// --- CreateFile2 ------------------------------------------------------------
extern "C" HANDLE WINAPI ocshim_CreateFile2(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                            DWORD disposition, DWORD flags, HANDLE templateFile) {
  static PFN_CreateFile2 real = (PFN_CreateFile2)ocshim_real_proc("CreateFile2");
  if (real) return real(name, access, share, sa, disposition, flags, templateFile);
  return CreateFileW(name, access, share, sa, disposition, flags, templateFile);
}

// --- NeedCurrentDirectoryForExePathW ---------------------------------------
extern "C" BOOL WINAPI ocshim_NeedCurrentDirectoryForExePathW(LPCWSTR exeName) {
  static PFN_NeedCurrentDirectoryForExePathW real =
      (PFN_NeedCurrentDirectoryForExePathW)ocshim_real_proc("NeedCurrentDirectoryForExePathW");
  if (real) return real(exeName);
  return TRUE;
}

// --- GetCurrentThreadStackLimits -------------------------------------------
extern "C" VOID WINAPI ocshim_GetCurrentThreadStackLimits(PULONG_PTR low, PULONG_PTR high) {
  static PFN_GetCurrentThreadStackLimits real =
      (PFN_GetCurrentThreadStackLimits)ocshim_real_proc("GetCurrentThreadStackLimits");
  if (real) {
    real(low, high);
    return;
  }
  // Fallback: report the real bounds of the current thread's stack from the
  // TEB.  Bun/JSC use these values to place the stack guard; returning fake
  // numbers makes it think the stack has already overflowed.
  NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
  if (low) *low = (ULONG_PTR)tib->StackLimit;
  if (high) *high = (ULONG_PTR)tib->StackBase;
}
// --- DiscardVirtualMemory ---------------------------------------------------
extern "C" BOOL WINAPI ocshim_DiscardVirtualMemory(HANDLE mem, SIZE_T size, OCSHIM_MEMORY_USAGE_INFORMATION* usage) {
  static PFN_DiscardVirtualMemory real = (PFN_DiscardVirtualMemory)ocshim_real_proc("DiscardVirtualMemory");
  if (real) return real(mem, size, usage);
  if (usage) memset(usage, 0, sizeof(*usage));
  return VirtualFree(mem, 0, MEM_RESET);
}

// --- GetTimeZoneInformationForYear ------------------------------------------
extern "C" DWORD WINAPI ocshim_GetTimeZoneInformationForYear(USHORT year, LPTIME_ZONE_INFORMATION tz) {
  static PFN_GetTimeZoneInformationForYear real =
      (PFN_GetTimeZoneInformationForYear)ocshim_real_proc("GetTimeZoneInformationForYear");
  if (real) return real(year, tz);
  if (!tz) return TIME_ZONE_ID_INVALID;
  memset(tz, 0, sizeof(*tz));
  return GetTimeZoneInformation(tz);
}

// --- ConPTY -----------------------------------------------------------------
extern "C" BOOL WINAPI ocshim_CreatePseudoConsole(COORD size, HANDLE in, HANDLE out, DWORD flags, HPCON* con) {
  static PFN_CreatePseudoConsole real = (PFN_CreatePseudoConsole)ocshim_real_proc("CreatePseudoConsole");
  if (real) return real(size, in, out, flags, con);
  SetLastError(ERROR_NOT_SUPPORTED);
  return FALSE;
}

extern "C" VOID WINAPI ocshim_ClosePseudoConsole(HPCON con) {
  static PFN_ClosePseudoConsole real = (PFN_ClosePseudoConsole)ocshim_real_proc("ClosePseudoConsole");
  if (real) {
    real(con);
    return;
  }
}

extern "C" BOOL WINAPI ocshim_ResizePseudoConsole(HPCON con, COORD size) {
  static PFN_ResizePseudoConsole real = (PFN_ResizePseudoConsole)ocshim_real_proc("ResizePseudoConsole");
  if (real) return real(con, size);
  SetLastError(ERROR_NOT_SUPPORTED);
  return FALSE;
}

// --- Windows 8 synchronisation primitives -----------------------------------
//  kernel32 flavour: the timeout is a relative number of milliseconds.
extern "C" BOOL WINAPI ocshim_WaitOnAddress(volatile VOID* address, PVOID compare, SIZE_T size, DWORD timeoutMs) {
  return ocshim_wait_address(address, compare, size, timeoutMs) ? TRUE : FALSE;
}

extern "C" VOID WINAPI ocshim_WakeByAddressAll(volatile VOID* address) { ocshim_wake_address(address, TRUE); }

extern "C" VOID WINAPI ocshim_WakeByAddressSingle(volatile VOID* address) { ocshim_wake_address(address, FALSE); }

// ---------------------------------------------------------------------------
//  DllMain
// ---------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
