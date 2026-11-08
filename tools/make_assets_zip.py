import os
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(ROOT, "spike", "opencode-src", "packages", "opencode")
BUN = os.path.join(ROOT, "spike", "opencode-src", "node_modules", ".bun")
zp = os.path.join(ROOT, "spike", "bundle_out", "ocweb_assets.zip")

z = zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED, compresslevel=6)


def add_dir_abs(root, arc_prefix, skip_ext=(".map",)):
    for r, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in ("obj",)]
        for f in files:
            if f.endswith(skip_ext):
                continue
            p = os.path.join(r, f)
            rel = os.path.relpath(p, root).replace(os.sep, "/")
            z.write(p, arc_prefix + rel)


def add_pkg_from_bun(store_name, arc):
    src = os.path.join(BUN, store_name, "node_modules")
    # package itself
    add_dir_abs(os.path.join(src, arc.rsplit("/", 1)[-1]), arc + "/")
    # its runtime deps stored as siblings in the same .bun dir
    return src


# webui, impl, assets
add_dir_abs(os.path.join(BASE, "webui"), "webui/")
for f in os.listdir(os.path.join(BASE, "impl")):
    if f != "package.json":
        z.write(os.path.join(BASE, "impl", f), "impl/" + f)
for f in os.listdir(BASE):
    if f.endswith(".wasm") or f.endswith(".mp3"):
        z.write(os.path.join(BASE, f), f)

# @opentui packages (from packages/opencode/node_modules)
for pkg in ("core", "solid", "keymap"):
    add_dir_abs(os.path.join(BASE, "node_modules", "@opentui", pkg), "node_modules/@opentui/" + pkg + "/")

# node-pty x86
add_dir_abs(os.path.join(BASE, "node_modules", "node-pty"), "node_modules/node-pty/",
            skip_ext=(".map", ".pdb", ".ilk", ".exp", ".lib"))

# runtime deps of @opentui/solid + solid-js chain, pulled from the .bun store
for store, arc in [
    ("solid-js@1.9.10", "node_modules/solid-js"),
    ("csstype@3.2.3", "node_modules/csstype"),
    ("seroval@1.3.2", "node_modules/seroval"),
    ("seroval-plugins@1.3.3+7c651f2eb938f734", "node_modules/seroval-plugins"),
    ("s-js@0.4.9", "node_modules/s-js"),
    ("entities@7.0.1", "node_modules/entities"),
]:
    src = os.path.join(BUN, store, "node_modules", arc.rsplit("/", 1)[-1])
    if os.path.isdir(src):
        add_dir_abs(src, arc + "/")
    else:
        # find actual store dir by prefix
        cands = [d for d in os.listdir(BUN) if d.startswith(store.rsplit("@", 1)[0] + "@")]
        print("lookup", store, "->", cands)
        if cands:
            add_dir_abs(os.path.join(BUN, cands[0], "node_modules", arc.rsplit("/", 1)[-1]), arc + "/")

z.writestr("package.json", '{"type":"module"}\n')
z.writestr("impl/package.json", '{"type":"commonjs"}\n')
z.close()
print("zip size:", os.path.getsize(zp))
