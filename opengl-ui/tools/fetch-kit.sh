#!/usr/bin/env bash
# ProsperoTV - Fetch the UI kit the interface is built on, at its pinned commit.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/fetch-kit.sh        (prints the kit's folder)
#
# The kit (https://github.com/blackbearreloaded/ps5-homebrew-ui) is a build
# dependency, not part of this repository: nothing of it is copied here. KIT
# names a checkout to use as it is (for work on both at once); otherwise the
# pinned commit is fetched once into .deps/ps5-homebrew-ui.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ -n ${KIT:-} ]]; then
    kit=$(cd -- "$KIT" && pwd)
    if ! git -C "$kit" apply --reverse --check "$root/tools/kit-text.patch" 2>/dev/null; then
        echo 'KIT must include the app-owned tools/kit-text.patch; custom checkouts are not modified automatically.' >&2
        exit 1
    fi
    printf '%s\n' "$kit"
    exit 0
fi
pin=4bd942579dd981b3df9c740438489ca6a614ddc0
kit="$root/.deps/ps5-homebrew-ui"
if [[ $(git -C "$kit" rev-parse HEAD 2>/dev/null || true) != "$pin" ]]; then
    rm -rf -- "$kit"
    mkdir -p "$kit"
    git -C "$kit" init -q
    git -C "$kit" remote add origin https://github.com/blackbearreloaded/ps5-homebrew-ui.git
    git -C "$kit" fetch -q --depth 1 origin "$pin"
    git -C "$kit" checkout -q --detach FETCH_HEAD
fi
# The checked-in patch is part of the application, not an untracked dependency edit.
patch="$root/tools/kit-text.patch"
if [[ -f $patch ]] && ! git -C "$kit" apply --reverse --check "$patch" 2>/dev/null; then
    git -C "$kit" apply --check "$patch"
    git -C "$kit" apply "$patch"
fi
printf '%s\n' "$kit"
