#!/usr/bin/env python3
"""Bisect helper: build variants of the ntdll shim.

usage: experiment_ntdll.py <variant>
variants: forward_all | only_precise | only_wait | impl_wait_only
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
EXE = r"C:\Users\build\AppData\Roaming\npm\node_modules\opencode-ai\bin\opencode.exe"

variant = sys.argv[1] if len(sys.argv) > 1 else "forward_all"

subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "shim_tool.py"), "gen_defs", EXE, BUILD])

ocn_def = os.path.join(BUILD, "ocn.def")
text = open(ocn_def, encoding="ascii").read()
IMPLEMENTED = ("RtlGetSystemTimePrecise", "RtlWaitOnAddress", "RtlWakeAddressAll", "RtlWakeAddressSingle")

if variant in ("forward_all", "only_precise", "only_wait"):
    forward = list(IMPLEMENTED)
    if variant == "only_precise":
        forward = ["RtlWaitOnAddress", "RtlWakeAddressAll", "RtlWakeAddressSingle"]
    if variant == "only_wait":
        forward = ["RtlGetSystemTimePrecise"]
    for fn in forward:
        text = re.sub(rf"^(    {fn})$", rf"\1=ntdll.{fn}", text, flags=re.M)
    open(ocn_def, "w", encoding="ascii", newline="\r\n").write(text)

src = os.path.join(ROOT, "src", "ocn.cpp")
if variant == "impl_wait_only":
    # implement only the wait primitives, forward RtlGetSystemTimePrecise
    text = open(ocn_def, encoding="ascii").read()
    text = re.sub(r"^(    RtlGetSystemTimePrecise)$", r"\1=ntdll.RtlGetSystemTimePrecise", text, flags=re.M)
    open(ocn_def, "w", encoding="ascii", newline="\r\n").write(text)

subprocess.check_call(
    ["cmd", "/c", os.path.join(ROOT, "tools", "vsenv.cmd"), "link", "/nologo", "/DLL",
     f"/OUT:{os.path.join(BUILD, 'ocn.dll')}", f"/DEF:{ocn_def}",
     os.path.join(BUILD, "ocn.obj"), os.path.join(BUILD, "ntdll_stand_in.lib"), "kernel32.lib"],
)

outdir = os.path.join(BUILD, "test_" + variant)
os.makedirs(outdir, exist_ok=True)
subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "shim_tool.py"), "patch",
                       EXE, os.path.join(outdir, "opencode.exe"), "ntdll"])
for f in ("ocshim32.dll", "ocn.dll"):
    subprocess.check_call(["cmd", "/c", "copy", "/Y", os.path.join(BUILD, f), outdir + "\\"])
print("variant", variant, "ready in", outdir)
