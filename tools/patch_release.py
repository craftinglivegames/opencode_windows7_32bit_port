#!/usr/bin/env python3
"""Apply the release hot-fixes to an extracted OpenCode x32 package.

The fixes are small, surgical patches to the Node-port bundle that were
verified on a real Windows 7 x32 netbook (see docs/PORT-native-x32.md):

1. server/project.ts  - a non-git project used to resolve to the drive root
   (worktree "/"), which made the web UI request providers for a directory
   that does not exist and kept the model list empty.  Use the real directory.
2. server/project.ts  - store that real directory as the project worktree
   instead of the special "/" marker.
3. app web UI (Solid 1.9.10) - client-side route transitions lost the Solid
   owner across the startTransition microtask, so lazily loaded pages ("New
   session") and dialogs rendered outside the provider chain and died with
   "<X> context must be used within a context provider" / "Cannot read
   properties of undefined (reading 'directory')".  The final transition
   function restores the owner (yn) and listener (Xn) before running the
   transition, keeps memo computation eager (no deferred _t state), and falls
   back to the last component owner when none is captured.
4. app web UI (Solid 1.9.10) - safety net for the remaining deferred-render
   paths (upstream opencode bug, see issues anomalyco/opencode#28857 and
   #28771): every context provider also registers its value in a global
   registry (window.__ctxReg) and context reads that miss the local owner
   chain fall back to the last mounted provider value instead of throwing.
   useDialog no longer hard-requires a reactive owner, and Kobalte's
   useDialogContext degrades to a stub instead of crashing the whole pane.
5. index.html - cache-bust the patched main chunk for browsers that already
   cached the previous build.
6. server retry message - the free-tier 429 carries a retry-after header
   (seconds till midnight UTC, quota is per external IP). The patched
   message surfaces the wait time instead of a bare "Subscribe to Go".

Works on both layouts:
  package root:  <root>/app/index.js + <root>/app/webui/   (portable package)
  dev tree root: <root>/webui/                             (packages/opencode)

Usage:  python tools/patch_release.py <package-dir>
        python tools/patch_release.py spike/bundle_out/portable_x86
        python tools/patch_release.py spike/opencode-src/packages/opencode

Exit code 0 = all patches applied (or already present), 1 = a target
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

# ---- 2b. free-tier message: surface retry-after (shared IP quota) --------------
# Upstream shows a generic "Subscribe to Go" text. The 429 carries a
# retry-after header (seconds till midnight UTC); the quota is per external
# IP, so an office NAT shares it. Surface the wait time in both the status
# message and the dialog text so the limit looks monitorable.
FREE_OLD = (
    'if (error.data.responseBody?.includes("FreeUsageLimitError")) {\n'
    '      return {\n'
    '        message: GO_UPSELL_MESSAGE,\n'
    '        action: {\n'
    '          reason: "free_tier_limit",\n'
    '          provider,\n'
    '          title: "Free limit reached",\n'
    '          message: "Subscribe to OpenCode Go for reliable access to the best open-source models for $10/month.",\n'
    '          label: "subscribe",\n'
    '          link: GO_UPSELL_URL\n'
    '        }\n'
    '      };\n'
    '    }'
)
FREE_NEW = (
    'if (error.data.responseBody?.includes("FreeUsageLimitError")) {\n'
    '      const raH = error.data.responseHeaders || {};\n'
    '      let raSec = Number(raH["retry-after"]);\n'
    '      if (Number.isNaN(raSec)) {\n'
    '        const raMs = Number(raH["retry-after-ms"]);\n'
    '        raSec = Number.isNaN(raMs) ? void 0 : Math.ceil(raMs / 1e3);\n'
    '      } else {\n'
    '        raSec = Math.max(0, Math.ceil(raSec));\n'
    '      }\n'
    '      const raTxt = raSec === void 0 ? "" : (() => {\n'
    '        const s = raSec, d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.ceil(s % 3600 / 60);\n'
    '        const u = (v, n) => `${v} ${n}${v === 1 ? "" : "s"}`;\n'
    '        if (d > 0)\n'
    '          return h > 0 ? `${u(d, "day")} ${u(h, "hour")}` : u(d, "day");\n'
    '        if (h > 0)\n'
    '          return m > 0 ? `${u(h, "hour")} ${u(m, "minute")}` : u(h, "hour");\n'
    '        return m > 0 ? u(m, "minute") : "less than a minute";\n'
    '      })();\n'
    '      return {\n'
    '        message: raTxt ? `${GO_UPSELL_MESSAGE} Retry after ${raTxt} (resets at midnight UTC, quota is shared per external IP).` : GO_UPSELL_MESSAGE,\n'
    '        action: {\n'
    '          reason: "free_tier_limit",\n'
    '          provider,\n'
    '          title: "Free limit reached",\n'
    '          message: raTxt ? `Free IP quota exhausted (shared per external IP, an office NAT shares it). Retry after ${raTxt} (midnight UTC). ` + "Subscribe to OpenCode Go for reliable access to the best open-source models for $10/month." : "Subscribe to OpenCode Go for reliable access to the best open-source models for $10/month.",\n'
    '          label: "subscribe",\n'
    '          link: GO_UPSELL_URL\n'
    '        }\n'
    '      };\n'
    '    }'
)
SERVER_PATCHES.append(
    (
        FREE_OLD,
        FREE_NEW,
        "free-tier message (retry-after wait time)",
    )
)

# ---- 3. transition fix -------------------------------------------------------
# Variants seen over time:
#   MS_ORIGINAL    - stock Solid: defers memo computation through the transition
#                    state (_t), memos read as undefined on the port.
#   MS_NOPRESERVE  - an earlier patch dropped the owner/listener restoration.
#   MS_FINAL       - eager memo computation + owner/listener restoration +
#                    last-owner fallback for callbacks made with no owner.
MS_ORIGINAL = (
    "function ms(e){if(_t&&_t.running)return e(),_t.done;const t=Xn,n=yn;"
    "return Promise.resolve().then(()=>{Xn=t,yn=n;let r;return hd&&(r=_t||"
    "(_t={sources:new Set,effects:[],promises:new Set,disposed:new Set,queue:new Set,running:!0}),"
    "r.done||(r.done=new Promise(i=>r.resolve=i)),r.running=!0),fo(e,!1),Xn=yn=null,r?r.done:void 0})}"
)
MS_NOPRESERVE = (
    "function ms(e){return Promise.resolve().then(()=>{try{return e()}catch(_e){"
    "setTimeout(()=>{throw _e},0)}})}"
)
MS_LASTOWNER = (
    "function ms(e){const t=Xn,n=yn;return Promise.resolve().then(()=>{Xn=t,yn=n;"
    "try{return e()}catch(_e){setTimeout(()=>{throw _e},0)}})}"
)
MS_FINAL = (
    "function ms(e){const t=Xn,n=yn||window.__lastOwner;return Promise.resolve().then(()=>{Xn=t,yn=n;"
    "try{return e()}catch(_e){setTimeout(()=>{throw _e},0)}})}"
)

# createComponent remembers the last created owner (window.__lastOwner) so
# deferred callbacks (ms) can fall back to a full context chain.
XR_OLD = "yn=o,Xn=null;try{return fo(a,!0)}finally{Xn=n,yn=r}"
XR_NEW = "yn=o,(window.__lastOwner=window.__lastOwner||o)&&0,Xn=null;try{return fo(a,!0)}finally{Xn=n,yn=r}"

# ---- 4. context registry safety net ------------------------------------------
IEE_OLD = (
    'function iee(e,t){return function(r){let i;return ce(()=>i=Pt(()=>'
    '(yn.context={...yn.context,[e]:r.value},Yl(()=>r.children))),void 0),i}}'
)
IEE_NEW = (
    'function iee(e,t){return function(r){let i;return ce(()=>i=Pt(()=>'
    '(yn.context={...yn.context,[e]:r.value},(window.__ctxReg=window.__ctxReg||new Map()).set(e,r.value),'
    'Yl(()=>r.children))),void 0),i}}'
)
QT_OLD = (
    'function Qt(e){let t;return yn&&yn.context&&(t=yn.context[e.id])!==void 0?t:e.defaultValue}'
)
QT_NEW = (
    'function Qt(e){var t;if(yn&&yn.context){t=yn.context[e.id];if(t!==void 0)return t}'
    'if(window.__ctxReg){t=window.__ctxReg.get(e.id);if(t!==void 0)return t}'
    'return e.defaultValue}'
)
FR_OLD = (
    'function fr(){const e=Qt(x9),t=as();'
    'if(!t)throw new Error("useDialog must be used within a DialogProvider");'
    'if(!e)throw new Error("useDialog must be used within a DialogProvider");'
)
FR_NEW = (
    'function fr(){const e=Qt(x9),t=as();'
    'if(!e)throw new Error("useDialog must be used within a DialogProvider");'
)
UU_OLD = (
    'function uu(){const e=Qt(f9);if(e===void 0)throw new Error('
    '"[kobalte]: `useDialogContext` must be used within a `Dialog` component");return e}'
)
UU_NEW = (
    'function uu(){var e=Qt(f9);if(e===void 0){e=new Proxy({},{get:function(t,k){'
    'if(k==="translations")return function(){return{dismiss:"Close"}};'
    'if(k==="modal")return function(){return false};'
    'if(k==="generateId")return function(p){return "dlg-"+String(p)+"-"+Math.random().toString(36).slice(2,8)};'
    'if(k==="close")return function(){};'
    'return function(){}}})}return e}'
)

UI_PATCHES = [
    {"label": "web UI transition fix", "target": MS_FINAL, "olds": [MS_ORIGINAL, MS_NOPRESERVE, MS_LASTOWNER]},
    {"label": "web UI last-owner fallback", "target": XR_NEW, "olds": [XR_OLD]},
    {"label": "web UI context registry (provider)", "target": IEE_NEW, "olds": [IEE_OLD]},
    {"label": "web UI context registry (lookup)", "target": QT_NEW, "olds": [QT_OLD]},
    {"label": "web UI useDialog tolerant check", "target": FR_NEW, "olds": [FR_OLD]},
    {"label": "web UI kobalte dialog stub", "target": UU_NEW, "olds": [UU_OLD]},
]

UI_CHUNK = "index-DdvDNd3q.js"
CACHE_VERSION = 3


def patch_text(path, old, new, label):
    """Replace the first matching `old` with `new`; no-op if `new` is present."""
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


def patch_target(path, spec):
    """Apply one patch spec: {label, target, olds} - try each `old` until one matches."""
    with open(path, "r", encoding="utf-8", errors="surrogateescape", newline="") as f:
        text = f.read()
    if spec["target"] in text:
        print("  OK   %s (already patched)" % spec["label"])
        return True
    for old in spec["olds"]:
        if old in text:
            text = text.replace(old, spec["target"])
            with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
                f.write(text)
            print("  PATCH %s" % spec["label"])
            return True
    print("  FAIL %s (target string not found)" % spec["label"])
    return False


def patch_webui(webui):
    ok = True
    ui = os.path.join(webui, "assets", UI_CHUNK)
    html = os.path.join(webui, "index.html")
    if not os.path.isfile(ui) or not os.path.isfile(html):
        print("ERROR: missing %s" % ui if not os.path.isfile(ui) else "ERROR: missing %s" % html)
        return False

    for spec in UI_PATCHES:
        ok = patch_target(ui, spec) and ok

    tag_new = "%s?v=%d" % (UI_CHUNK, CACHE_VERSION)
    tag_prev = "%s?v=%d" % (UI_CHUNK, CACHE_VERSION - 1)
    with open(html, "r", encoding="utf-8", errors="surrogateescape", newline="") as f:
        html_text = f.read()
    if tag_new in html_text:
        print("  OK   index.html cache-bust (already at v=%d)" % CACHE_VERSION)
    elif tag_prev in html_text:
        html_text = html_text.replace(tag_prev, tag_new)
        with open(html, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
            f.write(html_text)
        print("  PATCH index.html cache-bust v%d" % CACHE_VERSION)
    elif "/assets/" + UI_CHUNK in html_text:
        html_text = html_text.replace("/assets/" + UI_CHUNK, "/assets/" + tag_new)
        with open(html, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
            f.write(html_text)
        print("  PATCH index.html cache-bust v%d" % CACHE_VERSION)
    else:
        print("  FAIL index.html (script tag not found)")
        ok = False
    return ok


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = os.path.abspath(sys.argv[1])

    server = os.path.join(root, "app", "index.js")
    if os.path.isfile(server):
        # portable package layout: app/index.js + app/webui
        webui = os.path.join(root, "app", "webui")
        print("Patching package %s" % root)
        ok = True
        for old, new, label in SERVER_PATCHES:
            ok = patch_text(server, old, new, label) and ok
    else:
        # dev tree layout: <root>/webui
        webui = os.path.join(root, "webui")
        print("Patching web UI %s" % root)
        ok = True

    ok = patch_webui(webui) and ok
    print("RESULT: %s" % ("OK" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
