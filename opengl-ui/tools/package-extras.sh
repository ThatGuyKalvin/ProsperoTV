#!/usr/bin/env bash
# ProsperoTV - Build the two helper programs the app sends to the payload loader.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/package-extras.sh <title id> <app folder>
#
# Run by the build tree's tools/build.sh once the app folder is assembled:
#
#   lapy.elf           upstream Lapy's one-shot helper for exactly this title,
#                      built from its pinned commit by tools/build-lapy-helper.py
#                      (filesystem access; see ps5/src/elevation/README.md)
#   self-updater.elf   the self-update helper (update_helper/), which unpacks a
#                      release beside the app and replaces its files once the
#                      app has closed
#
# Both are checked before they are packaged, and their licenses go with them.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
title=$1
app=$2
build="$root/build"

helper="$build/lapy-helper/$title"
python3 -B "$root/tools/build-lapy-helper.py" "$title" "$helper"
cp "$helper/lapy.elf" "$app/lapy.elf"
cp "$helper/lapy-manifest.json" "$app/lapy-manifest.json"
mkdir -p "$app/licenses"
cp "$helper/Lapy-MIT.txt" "$app/licenses/Lapy-MIT.txt"
python3 - "$app" "$title" <<'PY'
import hashlib, json, sys
from pathlib import Path
app = Path(sys.argv[1])
manifest = json.loads((app / "lapy-manifest.json").read_text())
actual = hashlib.sha256((app / "lapy.elf").read_bytes()).hexdigest()
if manifest.get("elf_sha256") != actual or manifest.get("target_title") != sys.argv[2]:
    raise SystemExit("the packaged Lapy helper does not match its manifest")
PY

make -s -C "$root/update_helper" PS5_PAYLOAD_SDK="$root/.deps/native/ps5-payload-sdk" \
    OUTPUT="$build/self-update/self-updater.elf"
python3 -B "$root/tools/validate-loader-elf.py" "$build/self-update/self-updater.elf"
cp "$build/self-update/self-updater.elf" "$app/self-updater.elf"
cp "$root/third_party/miniz/LICENSE" "$app/licenses/miniz-MIT.txt"

# Nothing of this machine goes into a package.
for file in "$app/lapy.elf" "$app/self-updater.elf"; do
    if grep -aqE "/home/[A-Za-z0-9_.-]+/|/mnt/[a-z]/Users/" "$file"; then
        echo "a local path is recorded in ${file##*/}" >&2
        exit 1
    fi
done
printf 'Helpers packaged: lapy.elf (%s), self-updater.elf\n' "$title"
