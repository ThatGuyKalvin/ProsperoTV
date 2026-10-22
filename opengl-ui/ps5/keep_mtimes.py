#!/usr/bin/env python3
# ProsperoTV - Keep the modification times of build-tree files whose content did not change.
# Copyright (C) 2026 ThatGuyKalvin
# SPDX-License-Identifier: GPL-3.0-or-later
"""usage: keep_mtimes.py snapshot|restore <build tree>

assemble.sh deletes and copies the sources again, giving every file a new
modification time, so ninja would rebuild everything. `snapshot` records each
file's sha256 and mtime before the wipe; `restore` puts the old mtime back on
every file whose content is the same, so ninja rebuilds only what changed.
"""
import hashlib
import json
import os
import sys

DIRS = ["src", "include", "assets", "sce_sys", "vendor", "tools", "tooling", "runtime",
        "update_helper", "third_party", "Makefile"]


def files(tree):
    for top in DIRS:
        path = os.path.join(tree, top)
        if os.path.isfile(path):
            yield top
        for root, _dirs, names in os.walk(path):
            for name in names:
                full = os.path.join(root, name)
                if os.path.isfile(full) and not os.path.islink(full):
                    yield os.path.relpath(full, tree)


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


if len(sys.argv) != 3 or sys.argv[1] not in ("snapshot", "restore"):
    sys.exit(__doc__)
mode, tree = sys.argv[1], sys.argv[2]
record = os.path.join(tree, ".keep-mtimes.json")
if mode == "snapshot":
    snap = {}
    for rel in files(tree):
        full = os.path.join(tree, rel)
        snap[rel] = [digest(full), os.stat(full).st_mtime_ns]
    with open(record, "w", encoding="utf-8") as fh:
        json.dump(snap, fh)
else:
    if not os.path.exists(record):
        sys.exit(0)
    with open(record, encoding="utf-8") as fh:
        snap = json.load(fh)
    kept = changed = 0
    for rel in files(tree):
        full = os.path.join(tree, rel)
        old = snap.get(rel)
        if old and old[0] == digest(full):
            os.utime(full, ns=(old[1], old[1]))
            kept += 1
        else:
            changed += 1
    os.remove(record)
    print(f"{kept} files unchanged, {changed} new or changed")
