#!/usr/bin/env bash
# ProsperoTV - Pinned host text shaping libraries and licensed RTL fallback font.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cache="$root/.deps/text-download"
out="$root/.deps/text-host"
mkdir -p "$cache" "$out"
fetch() {
    local file=$1 hash=$2 url=$3
    if [[ ! -f $cache/$file ]] || ! printf '%s  %s\n' "$hash" "$cache/$file" | sha256sum -c --status; then
        curl -fLsS --retry 3 "$url" -o "$cache/$file.part"
        printf '%s  %s\n' "$hash" "$cache/$file.part" | sha256sum -c --status
        mv "$cache/$file.part" "$cache/$file"
    fi
}
fetch fribidi-1.0.15.tar.xz 0bbc7ff633bfa208ae32d7e369cf5a7d20d5d2557a0b067c9aa98bcbf9967587 https://github.com/fribidi/fribidi/releases/download/v1.0.15/fribidi-1.0.15.tar.xz
fetch harfbuzz-12.3.2.tar.xz 6f6db164359a2da5a84ef826615b448b33e6306067ad829d85d5b0bf936f1bb8 https://github.com/harfbuzz/harfbuzz/releases/download/12.3.2/harfbuzz-12.3.2.tar.xz
fetch dejavu-fonts-ttf-2.37.tar.bz2 fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7 https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2
for archive in fribidi-1.0.15.tar.xz harfbuzz-12.3.2.tar.xz dejavu-fonts-ttf-2.37.tar.bz2; do
    name=${archive%.tar.*}
    [[ -d $out/$name ]] || tar -xf "$cache/$archive" -C "$out"
done
prefix="$out/root"
if [[ ! -f $prefix/lib/libfribidi.a ]]; then
    meson setup "$out/fribidi-build" "$out/fribidi-1.0.15" --prefix="$prefix" --libdir=lib --default-library=static -Ddocs=false -Dbin=false -Dtests=false >&2
    ninja -C "$out/fribidi-build" -j "${BUILD_JOBS:-4}" install >&2
fi
if [[ ! -f $prefix/lib/libharfbuzz-subset.a ]]; then
    meson setup "$out/harfbuzz-build" "$out/harfbuzz-12.3.2" --prefix="$prefix" --libdir=lib --default-library=static -Dglib=disabled -Dgobject=disabled -Dcairo=disabled -Dicu=disabled -Dfreetype=disabled -Dtests=disabled -Ddocs=disabled -Dutilities=disabled >&2
    ninja -C "$out/harfbuzz-build" -j "${BUILD_JOBS:-4}" install >&2
fi
printf '%s\n' "$prefix"
