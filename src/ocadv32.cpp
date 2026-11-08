// Minimal ADVAPI32 shim for node.exe on Windows 7.
// Everything except EventSetInformation is forwarded to the real ADVAPI32
// (see ocadv32.def); node.exe only uses EventSetInformation (Windows 8+) to
// tune ETW tracing, so a no-op stub is enough.
extern "C" __declspec(dllexport) unsigned long __stdcall
ocadv_EventSetInformation(void * /*RegHandle*/, unsigned /*InformationClass*/,
                          void * /*EventInformation*/, unsigned /*InformationLength*/) {
    return 0; // ERROR_SUCCESS - pretend the trace settings were accepted
}
