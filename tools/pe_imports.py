#!/usr/bin/env python3
"""Minimal PE import table dumper (no external deps).

Usage: python pe_imports.py <file.exe> [--all]
Prints imported DLLs and functions, and flags APIs that do not exist on
Windows 7 SP1 (so we know what a shim would have to provide).
"""

import struct
import sys

# APIs introduced after Windows 7 (SP1).  Value = first Windows version.
WIN8_PLUS = {
    # Windows 8 / 8.1
    "GetSystemTimePreciseAsFileTime": "Win8",
    "CreateFile2": "Win8",
    "SetProcessInformation": "Win8",
    "GetProcessInformation": "Win8",
    "SetThreadDpiAwarenessContext": "Win10",
    "GetDpiForWindow": "Win10",
    "GetDpiForSystem": "Win10",
    "GetSystemCpuSetInformation": "Win10",
    "SetThreadDescription": "Win10",
    "GetThreadDescription": "Win10",
    "SetProcessDpiAwarenessContext": "Win10",
    "GetSystemDefaultLocaleName": "Win10",
    "CompareStringEx": "Win7",
    "GetNLSVersionEx": "Win7",
    "FindNLSStringEx": "Win7",
    "GetNLSVersion": "Win7",
    "IdnToAscii": "Win7",
    "SetThreadStackGuarantee": "Win8",
    "GetThreadPreferredUILanguages": "Win8",
    "GetUserPreferredUILanguages": "Win8",
    "GetSystemPreferredUILanguages": "Win8",
    "SetProcessPreferredUILanguages": "Win8",
    "GetLocaleInfoEx": "Win7",
    "LCIDToLocaleName": "Win7",
    "LocaleNameToLCID": "Win7",
    "IsValidLocaleName": "Win7",
    "GetUserDefaultLocaleName": "Win7",
    "ResolveLocaleName": "Win7",
    "EnumSystemLocalesEx": "Win7",
    "GetDateFormatEx": "Win7",
    "GetTimeFormatEx": "Win7",
    "SetFileInformationByHandle": "Vista",
    "GetFinalPathNameByHandleW": "Vista",
    "ReOpenFile": "Win7",
    "CreateSymbolicLinkW": "Vista",
    "GetFinalPathNameByHandleA": "Vista",
    "GetFileInformationByHandleEx": "Vista",
    "SetFilePointerEx": "Vista",
    "SetFileTime": "Win7",
    "GetSystemTimeAsFileTime": "WinXP",
    "InitOnceExecuteOnce": "Vista",
    "InitOnceBeginInitialize": "Vista",
    "CreateEventExW": "Vista",
    "CreateSemaphoreExW": "Vista",
    "CreateTimerQueue": "Vista",
    "CreateThreadpoolWait": "Vista",
    "CreateThreadpoolWork": "Vista",
    "CloseThreadpool": "Vista",
    "FlushThreadpoolWorkItems": "Vista",
    "WaitForThreadpoolWaitCallbacks": "Vista",
    "GetTickCount64": "Vista",
    "GetSystemTimePreciseAsFileTime": "Win8",
    "CreateSymbolicLinkW": "Vista",
    "GetProductInfo": "Vista",
    "VerifyVersionInfoW": "WinXP",
    "Wow64DisableWow64FsRedirection": "Vista",
    "Wow64RevertWow64FsRedirection": "Vista",
    "IsWow64Process": "Vista",
    "SetDefaultDllDirectories": "Win8",
    "AddDllDirectory": "Win8",
    "SetSearchPathMode": "Win8",
    "NeedCurrentDirectoryForExePathW": "Win8",
    "GetCurrentDirectory": "WinXP",
    "CreateDirectoryExW": "WinXP",
    "RemoveDirectoryW": "WinXP",
    "GetDllDirectoryW": "WinXP",
    "SetDllDirectoryW": "WinXP",
    "K32EnumProcessModules": "Win7",
    "K32GetModuleFileNameExW": "Win7",
    "K32GetModuleInformation": "Win7",
    "GetProcessImageFileNameW": "Vista",
    "QueryFullProcessImageNameW": "Vista",
    "GetConsoleFontSize": "WinXP",
    "GetCurrentConsoleFontEx": "Win10",
    "SetConsoleScreenBufferSizeEx": "Win8",
    "GetConsoleScreenBufferInfoEx": "Win8",
    "SetConsoleTextAttribute": "Vista",
    "GetConsoleSelectionInfo": "Win7",
    "SetConsoleSelectionInfo": "Win7",
    "GetConsoleHistoryInfo": "Win7",
    "SetConsoleHistoryInfo": "Win7",
    "GetConsoleAliasW": "WinXP",
    "GetConsoleAliasesW": "WinXP",
    "GetConsoleAliasExesW": "Win7",
    "GetConsoleAliasExesLengthW": "Win7",
    "AddConsoleAliasW": "WinXP",
    "GetConsoleProcessList": "Vista",
    "AttachConsole": "Vista",
    "AllocConsole": "Vista",
    "FreeConsole": "Vista",
    "SetConsoleTitleW": "WinXP",
    "GetConsoleTitleW": "WinXP",
    "SetConsoleWindowInfo": "WinXP",
    "GetConsoleWindow": "WinXP",
    "GetConsoleCursorInfo": "WinXP",
    "SetConsoleCursorInfo": "WinXP",
    "SetConsoleCtrlHandler": "WinXP",
    "GenerateConsoleCtrlEvent": "WinXP",
    "GetConsoleMode": "WinXP",
    "SetConsoleMode": "WinXP",
    "GetConsoleCP": "WinXP",
    "SetConsoleCP": "WinXP",
    "GetConsoleOutputCP": "WinXP",
    "SetConsoleOutputCP": "WinXP",
    "WriteConsoleW": "WinXP",
    "ReadConsoleW": "WinXP",
    "WriteConsoleOutputW": "WinXP",
    "CreateConsoleScreenBuffer": "WinXP",
    "SetConsoleScreenBufferSizeEx": "Win8",
    "GetLargestConsoleWindowSize": "WinXP",
    "PeekConsoleInputW": "WinXP",
    "ReadConsoleInputW": "WinXP",
    "WriteConsoleInputW": "WinXP",
    "GetNumberOfConsoleInputEvents": "WinXP",
    "GetNumberOfConsoleMouseButtons": "WinXP",
    "GetConsoleOriginalTitleW": "Vista",
    "SetConsoleTitle": "WinXP",
    "GetProcessTimes": "WinXP",
    "GetProcessWorkingSetSize": "WinXP",
    "SetProcessWorkingSetSize": "WinXP",
    "GetProcessAffinityMask": "WinXP",
    "SetProcessAffinityMask": "WinXP",
    "GetThreadPriority": "WinXP",
    "SetThreadPriority": "WinXP",
    "GetPriorityClass": "WinXP",
    "SetPriorityClass": "WinXP",
    "CreateToolhelp32Snapshot": "WinXP",
    "Process32FirstW": "WinXP",
    "Process32NextW": "WinXP",
    "Thread32First": "WinXP",
    "Thread32Next": "WinXP",
    "Module32FirstW": "WinXP",
    "Module32NextW": "WinXP",
    "Heap32First": "WinXP",
    "Heap32Next": "WinXP",
    "Heap32ListFirst": "WinXP",
    "Toolhelp32ReadProcessMemory": "WinXP",
    "QueueUserAPC": "WinXP",
    "VirtualAllocEx": "WinXP",
    "WriteProcessMemory": "WinXP",
    "ReadProcessMemory": "WinXP",
    "FlushInstructionCache": "WinXP",
    "SuspendThread": "WinXP",
    "ResumeThread": "WinXP",
    "GetThreadContext": "WinXP",
    "SetThreadContext": "WinXP",
    "CreateRemoteThread": "WinXP",
    "CreateRemoteThreadEx": "Win7",
    "NtQueryInformationProcess": "WinXP",
    "SetThreadContext": "WinXP",
    "GetProcessDEPPolicy": "Win8",
    "SetProcessDEPPolicy": "Win8",
    "GetSystemRegistryQuota": "WinXP",
}


def rva_to_offset(pe, rva, size_of_headers):
    for section in pe["sections"]:
        if section["va"] <= rva < section["va"] + section["vsize"]:
            return rva - section["va"] + section["raw"]
    if rva < size_of_headers:
        return rva
    raise ValueError(f"cannot map rva {rva:#x}")


def read_cstr(data, off):
    end = data.index(b"\x00", off)
    return data[off:end].decode("ascii", "replace")


def parse(path):
    data = open(path, "rb").read()
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[e_lfanew : e_lfanew + 4] == b"PE\0\0", "not a PE file"
    coff = e_lfanew + 4
    machine, nsec, _, _, _, opt_size, _ = struct.unpack_from("<HHIIIHH", data, coff)
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    pe32plus = magic == 0x20B
    if pe32plus:
        dd_offset = opt + 112
    else:
        dd_offset = opt + 96
    image_base = struct.unpack_from("<Q" if pe32plus else "<I", data, opt + 24)[0]
    size_of_headers = struct.unpack_from("<I", data, opt + 60)[0]
    num_rva = struct.unpack_from("<I", data, opt + (108 if pe32plus else 92))[0]
    dd_offset += (num_rva - 16) * 8

    sections = []
    sec_off = opt + opt_size
    for i in range(nsec):
        base = sec_off + i * 40
        name = data[base : base + 8].rstrip(b"\x00").decode("ascii", "replace")
        vsize, va, raw_size, raw = struct.unpack_from("<IIII", data, base + 8)
        sections.append({"name": name, "vsize": vsize, "va": va, "raw": raw})

    pe = {"sections": sections}
    imports = []
    import_rva, import_size = struct.unpack_from("<II", data, dd_offset + 8)
    if not import_rva:
        return imports, data
    off = rva_to_offset(pe, import_rva, size_of_headers)
    while True:
        ilt_rva, ts, fchain, name_rva, iat_rva = struct.unpack_from("<IIIII", data, off)
        if not any((ilt_rva, name_rva, iat_rva)):
            break
        dll = read_cstr(data, rva_to_offset(pe, name_rva, size_of_headers))
        thunk_rva = ilt_rva or iat_rva
        toff = rva_to_offset(pe, thunk_rva, size_of_headers)
        funcs = []
        idx = 0
        while True:
            if pe32plus:
                val = struct.unpack_from("<Q", data, toff + idx * 8)[0]
                ordinal_flag = 1 << 63
            else:
                val = struct.unpack_from("<I", data, toff + idx * 4)[0]
                ordinal_flag = 1 << 31
            if val == 0:
                break
            if val & ordinal_flag:
                funcs.append(f"#{val & 0xFFFF}")
            else:
                hint_off = rva_to_offset(pe, val & 0x7FFFFFFF, size_of_headers)
                funcs.append(read_cstr(data, hint_off + 2))
            idx += 1
        imports.append((dll, funcs))
        off += 20
    return imports, data


if __name__ == "__main__":
    path = sys.argv[1]
    imports, _ = parse(path)
    flagged = []
    for dll, funcs in imports:
        print(f"{dll}: {len(funcs)} imports")
        for f in funcs:
            if f in WIN8_PLUS:
                flagged.append((dll, f, WIN8_PLUS[f]))
    print()
    print("=== potentially missing on Windows 7 SP1 ===")
    for dll, f, ver in flagged:
        print(f"  {dll:24s} {f:44s} ({ver})")
    print(f"total flagged: {len(flagged)}")
