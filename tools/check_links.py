#!/usr/bin/env python3
"""Check relative links in the repository's markdown files.

Used by CI: catches renamed or deleted files that would leave a dead link in
README/README.ru/docs. External links (http/https/mailto) are not fetched --
the build machines for this project may be offline and DNS is not part of the
build contract.

Usage:
    python tools/check_links.py [root]
"""

import os
import re
import sys

LINK_RE = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")
SKIP_DIRS = {".git", "build", "dist", "spike", "node_modules", "__pycache__"}
EXTERNAL = ("http://", "https://", "mailto:", "#", "ftp://")


def markdown_files(root):
    for r, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for f in files:
            if f.lower().endswith(".md"):
                yield os.path.join(r, f)


def check(root):
    problems = []
    total = 0
    for path in markdown_files(root):
        try:
            with open(path, encoding="utf-8") as fh:
                text = fh.read()
        except (OSError, UnicodeDecodeError) as err:
            problems.append("%s: cannot read (%s)" % (path, err))
            continue
        for match in LINK_RE.finditer(text):
            target = match.group(1).strip()
            if not target or target.startswith(EXTERNAL):
                continue
            if target.startswith("<") and target.endswith(">"):
                target = target[1:-1]
            rel = target.split("#", 1)[0]
            if not rel:
                continue
            total += 1
            rel = rel.replace("%20", " ")
            full = os.path.normpath(os.path.join(os.path.dirname(path), rel))
            if not os.path.exists(full):
                line = text.count("\n", 0, match.start()) + 1
                problems.append("%s:%d: missing %s" % (os.path.relpath(path, root), line, target))
    return total, problems


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
    total, problems = check(root)
    for p in problems:
        print("MISSING " + p)
    print("checked %d relative links in markdown, %d problem(s)" % (total, len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
