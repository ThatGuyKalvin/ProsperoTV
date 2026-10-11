#!/usr/bin/env bash
# ProsperoTV - Native HarfBuzz without unavailable POSIX locale objects.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/.deps/text-native"
version=12.3.2
hash=6f6db164359a2da5a84ef826615b448b33e6306067ad829d85d5b0bf936f1bb8
mkdir -p "$out"
archive="$out/harfbuzz-$version.tar.xz"
if [[ ! -f $archive ]] || ! printf '%s  %s\n' "$hash" "$archive" | sha256sum -c --status; then
    curl -fLsS --retry 3 "https://github.com/harfbuzz/harfbuzz/releases/download/$version/harfbuzz-$version.tar.xz" -o "$archive.part"
    printf '%s  %s\n' "$hash" "$archive.part" | sha256sum -c --status
    mv "$archive.part" "$archive"
fi
[[ -d $out/harfbuzz-$version ]] || tar -xf "$archive" -C "$out"
sdk=$(bash "$root/tools/setup-native-dependencies.sh" | sed -n 's/^SDK_ROOT=//p')
[[ -d $sdk/target/include ]] || exit 2
stamp="$version $hash HB_NO_SETLOCALE $(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)"
if [[ ! -f $out/.stamp || $(<"$out/.stamp") != "$stamp" || ! -f $out/lib/libharfbuzz.a ]]; then
    mkdir -p "$out/lib" "$out/include/harfbuzz"
    # The upstream amalgamation keeps atomics/thread safety and OpenType support.
    # Locale switching is unnecessary: the app never changes the C locale.
    PS5_PAYLOAD_SDK="$sdk" USE_CCACHE=1 sh "$root/tooling/prospero-clang18" \
        -std=c++17 -O2 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti \
        -DHB_NO_SETLOCALE -c "$out/harfbuzz-$version/src/harfbuzz.cc" -o "$out/harfbuzz.o"
    "${AR:-llvm-ar-18}" rcs "$out/lib/libharfbuzz.a" "$out/harfbuzz.o"
    cp "$out/harfbuzz-$version/src/"*.h "$out/include/harfbuzz/"
    printf '%s\n' "$stamp" > "$out/.stamp"
fi
mkdir -p "$root/assets/licenses"
cp "$out/harfbuzz-$version/COPYING" "$root/assets/licenses/HarfBuzz-LICENSE.txt"
