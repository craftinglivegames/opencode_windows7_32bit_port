import os
import shutil

BASE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(BASE, "..", "spike", "opencode-src")
DIST = os.path.join(SRC, "packages", "app", "dist")
WEBUI = os.path.join(SRC, "packages", "opencode", "webui")

# Refresh packages/opencode/webui with the vite build output.
if os.path.isdir(WEBUI):
    shutil.rmtree(WEBUI)
shutil.copytree(DIST, WEBUI, ignore=shutil.ignore_patterns("*.map"))

files = []
for root, dirs, names in os.walk(WEBUI):
    for n in names:
        full = os.path.join(root, n)
        rel = os.path.relpath(full, WEBUI).replace("\\", "/")
        files.append(rel)
files.sort()

lines = [
    "// Generated: maps web UI paths to files shipped next to the bundle",
    '// (relative to packages/opencode, resolved against import.meta.url at runtime).',
    "export default {",
]
for rel in files:
    lines.append("  %r: %r," % (rel, "webui/" + rel))
lines.append("}")
out = os.path.join(SRC, "packages", "opencode", "src", "server", "shared", "opencode-web-ui.gen.ts")
with open(out, "w", encoding="utf-8", newline="") as f:
    f.write("\n".join(lines) + "\n")
print("files:", len(files))
