#!/usr/bin/env python3
"""Apply the release hot-fixes to an extracted OpenCode x32 package.

The fixes are small, surgical patches to the Node-port bundle that were
verified on a real Windows 7 x32 netbook (see docs/PORT-native-x32.md):

1. server/project.ts  - a non-git project used to resolve to the drive root
   (worktree "/"), which made the web UI request providers for a directory
   that does not exist and kept the model list empty.  Use the real directory.
2. server/project.ts  - store that real directory as the project worktree
   instead of the special "/" marker.
3. app web UI (Solid 1.9.10) - memos created inside a router transition were
   read as `undefined`, so "New session" died with
   "Cannot read properties of undefined (reading 'child' / 'directory' / 'data')".
   Run transitions synchronously for the bundled dev UI.
4. index.html - cache-bust the patched main chunk for browsers that already
   cached the previous build.

Usage:  python tools/patch_release.py <package-dir>
        python tools/patch_release.py spike/bundle_out/portable_x86

Exit code 0 = all four patches applied (or already present), 1 = a target
string was not found (bundle changed - update the patch).
"""

import os
import sys

SERVER_PATCHES = [
    (
        'return { id: ID11.global, directory: AbsolutePath.make(path11.parse(input).root), vcs: undefined };',
        'return { id: ID11.global, directory: AbsolutePath.make(input), vcs: undefined };',
        "project directory (drive root -> real directory)",
    ),
    (
        'const worktree = data.id === ID11.make("global") && !data.vcs ? "/" : data.directory;',
        'const worktree = data.directory;',
        "project worktree ('/' -> real directory)",
    ),
]

UI_OLD = (
    "function ms(e){if(_t&&_t.running)return e(),_t.done;const t=Xn,n=yn;"
    "return Promise.resolve().then(()=>{Xn=t,yn=n;let r;return hd&&(r=_t||"
    "(_t={sources:new Set,effects:[],promises:new Set,disposed:new Set,queue:new Set,running:!0}),"
    "r.done||(r.done=new Promise(i=>r.resolve=i)),r.running=!0),fo(e,!1),Xn=yn=null,r?r.done:void 0})}"
)
UI_NEW = (
    "function ms(e){return Promise.resolve().then(()=>{try{return e()}catch(_e){"
    "setTimeout(()=>{throw _e},0)}})}"
)


def patch_text(path, old, new, label):
    with open(path, "r", encoding="utf-8", errors="surrogateescape", newline="") as f:
        text = f.read()
    if new in text:
        print("  OK   %s (already patched)" % label)
        return True
    if old not in text:
        print("  FAIL %s (target string not found)" % label)
        return False
    text = text.replace(old, new)
    with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
        f.write(text)
    print("  PATCH %s" % label)
    return True


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = os.path.abspath(sys.argv[1])
    server = os.path.join(root, "app", "index.js")
    ui = os.path.join(root, "app", "webui", "assets", "index-DdvDNd3q.js")
    html = os.path.join(root, "app", "webui", "index.html")

    for p in (server, ui, html):
        if not os.path.isfile(p):
            print("ERROR: missing %s" % p)
            return 1

    print("Patching %s" % root)
    ok = True
    for old, new, label in SERVER_PATCHES:
        ok = patch_text(server, old, new, label) and ok
    ok = patch_text(ui, UI_OLD, UI_NEW, "web UI transition fix") and ok

    with open(html, "r", encoding="utf-8", errors="surrogateescape", newline="") as f:
        html_text = f.read()
    if "index-DdvDNd3q.js?v=2" in html_text:
        print("  OK   index.html cache-bust (already patched)")
    elif "/assets/index-DdvDNd3q.js" in html_text:
        html_text = html_text.replace("/assets/index-DdvDNd3q.js", "/assets/index-DdvDNd3q.js?v=2")
        with open(html, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
            f.write(html_text)
        print("  PATCH index.html cache-bust")
    else:
        print("  FAIL index.html (script tag not found)")
        ok = False

    print("RESULT: %s" % ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
