// ---------------------------------------------------------------------------
//  ocn - Windows 7 compatibility shim for the ntdll.dll import descriptor.
//
//  The Bun runtime imports a few ntdll entry points that only exist on
//  Windows 8 and newer:
//
//      RtlGetSystemTimePrecise   -> ULONGLONG (void)
//      RtlWaitOnAddress          -> NTSTATUS (addr, cmp, size, PLARGE_INTEGER timeout)
//      RtlWakeAddressAll         -> VOID (addr)
//      RtlWakeAddressSingle      -> VOID (addr)
//
//  Everything else is re-exported straight from ntdll.dll (see ocn.def), the
//  four functions above are implemented here.
// ---------------------------------------------------------------------------

#include "ocshim_common.h"

extern "C" ULONGLONG WINAPI RtlGetSystemTimePrecise(void) { return ocshim_precise_time_value(); }

extern "C" NTSTATUS WINAPI RtlWaitOnAddress(volatile VOID* address, PVOID compare, SIZE_T size,
                                             const LARGE_INTEGER* timeout) {
  if (ocshim_wait_address(address, compare, size, ocshim_timeout_to_ms(timeout)))
    return OCSHIM_STATUS_SUCCESS;
  return OCSHIM_STATUS_TIMEOUT;
}

extern "C" VOID WINAPI RtlWakeAddressAll(volatile VOID* address) { ocshim_wake_address(address, TRUE); }

extern "C" VOID WINAPI RtlWakeAddressSingle(volatile VOID* address) { ocshim_wake_address(address, FALSE); }

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
