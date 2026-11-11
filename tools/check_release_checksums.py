#!/usr/bin/env python3
"""Compare the checksums published in README.md with the latest GitHub release.

Stale checksums are the easiest way to scare a new user away ("certutil says the
file is corrupted"), so CI runs this on every release and once a week.

Usage:
    python tools/check_release_checksums.py [repo-slug]

Exit code 1 means README.md and the latest release asset disagree (or the asset
has no digest yet). Read-only, no token required for public repositories; in
GitHub Actions the GITHUB_TOKEN is used when present to avoid rate limits.
"""

import json
import os
import re
import sys
import urllib.request

DEFAULT_SLUG = "craftinglivegames/opencode_windows7_32bit_port"
ASSET = "OpenCode-Win7-x32-portable.zip"


def api(url):
    req = urllib.request.Request(url, headers={"User-Agent": "opencode-win7-port-ci"})
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        req.add_header("Authorization", "Bearer " + token)
    req.add_header("Accept", "application/vnd.github+json")
    with urllib.request.urlopen(req, timeout=30) as res:
        return json.load(res)


def readme_values(path="README.md"):
    """Return (size, sha256) from the first checksum table row in README.md."""
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    row = re.search(r"\|\s*`?OpenCode-Win7-x32-portable\.zip`?\s*\|([^|]+)\|([^|]+)\|", text)
    if not row:
        raise SystemExit("README.md: checksum row for the archive not found")
    size = re.sub(r"[^0-9]", "", row.group(1))
    sha = re.sub(r"[`\s]", "", row.group(2)).lower()
    return int(size or 0), sha


def main():
    slug = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("GITHUB_REPOSITORY", DEFAULT_SLUG)
    rel = api("https://api.github.com/repos/%s/releases/latest" % slug)
    asset = next((a for a in rel.get("assets", []) if a.get("name") == ASSET), None)
    if asset is None:
        print("asset %s is not attached to release %s" % (ASSET, rel.get("tag_name")))
        return 1

    readme_size, readme_sha = readme_values()
    actual_size = int(asset.get("size") or 0)
    digest = asset.get("digest") or ""
    actual_sha = digest.split(":", 1)[1].lower() if digest.startswith("sha256:") else ""

    ok = True
    if readme_size != actual_size:
        print("size mismatch: README %d, release %s %d" % (readme_size, rel.get("tag_name"), actual_size))
        ok = False
    if not actual_sha:
        print("release asset has no sha256 digest yet (upload may still be in progress)")
        ok = False
    elif readme_sha != actual_sha:
        print("sha256 mismatch: README %s, release %s" % (readme_sha, actual_sha))
        ok = False

    if ok:
        print("README checksums match %s: %d bytes, %s" % (rel.get("tag_name"), actual_size, actual_sha))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
