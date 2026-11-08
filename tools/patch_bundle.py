"""Post-build patch for the Node-port bundle.

uv_os_gethostname returns ENOSYS in the Node 22 x86 build on Win7, so any
os.hostname() call crashes the process. Replace all .hostname() calls with the
COMPUTERNAME env fallback (set on every Windows install).

Run after every `bun build`:  python patch_bundle.py
"""
import os

p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "opencode-src", "packages", "opencode", "index-bunnode.js")
s = open(p, encoding="utf-8", errors="surrogatepass").read()

variants = [
    "os3.hostname()",
    "(0, os_1.hostname)()",
    "os6.hostname()",
    "(0, import_os.hostname)()",
    "os.hostname()",
    "os_1.default.hostname()",
]
total = 0
for v in variants:
    n = s.count(v)
    if n:
        s = s.replace(v, '(process.env.COMPUTERNAME || "localhost")')
        total += n
        print("patched", n, "x", v)

open(p, "w", encoding="utf-8", errors="surrogatepass", newline="").write(s)
print("total patched:", total)
