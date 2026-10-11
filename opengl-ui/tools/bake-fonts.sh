#!/usr/bin/env bash
# ProsperoTV - Bake the fonts the interface draws with into build/fonts.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/bake-fonts.sh        (prints the folder)
#
# KIT the ps5-homebrew-ui checkout (default: fetched at its pinned commit by tools/fetch-kit.sh)
#
# The primary faces are the kit's Inter, Montserrat and DejaVu Sans Mono.
# A pinned DejaVu Sans fallback provides Arabic/Hebrew glyphs and OpenType
# tables in shaped v2 bundles; provider text stays in logical UTF-8 order.
# Channel names in Western and Central European languages, in Greek and in Cyrillic
# read as written. Chinese, Japanese and Korean names get two faces of their
# own, baked from Noto Sans (tools/fetch-cjk-fonts.sh) with the code points of
# tools/cjk-ranges.py by this folder's font-baker/bake_list.cpp. The result is
# the same for the same inputs, so it is made once and reused.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
kit=$(bash "$root/tools/fetch-kit.sh")
cxx=$(command -v "${HOST_CXX:-clang++}")
out="$root/build/fonts"
text=$(bash "$root/tools/setup-text-dependencies.sh")
fallback="$root/.deps/text-host/dejavu-fonts-ttf-2.37"
tool="$root/build/host/bake_list"
source="$kit/tools/font-baker/bake_font.cpp"
mkdir -p "$out" "$root/build/host"

cjk=$(bash "$root/tools/fetch-cjk-fonts.sh")
list_source="$root/tools/font-baker/bake_list.cpp"
cp "$root/.deps/text-host/harfbuzz-12.3.2/COPYING" "$out/HarfBuzz-LICENSE.txt"
cp "$root/.deps/text-host/fribidi-1.0.15/COPYING" "$out/FriBidi-LICENSE.txt"
stamp=$(cat "$source" "$list_source" "$root/tools/cjk-ranges.py" "${BASH_SOURCE[0]}" "$kit"/third_party/fonts/{Inter-Regular,Inter-SemiBold,Montserrat-Medium,DejaVuSansMono}.ttf "$cjk"/NotoSans{SC,KR}-Regular.otf "$fallback/ttf/DejaVuSans.ttf" | sha256sum | cut -d' ' -f1)
if [[ -f $out/.stamp && $(<"$out/.stamp") == "$stamp" ]]; then
    printf '%s\n' "$out"
    exit 0
fi

"$cxx" -std=c++20 -O2 -w -I"$kit/third_party" -I"$kit/src" -I"$text/include/harfbuzz"     "$list_source" "$text/lib/libharfbuzz-subset.a" "$text/lib/libharfbuzz.a" -lpthread -o "$tool"
while read -r ttf name size range; do
    [[ -n $ttf ]] || continue
    "$tool" "$kit/third_party/fonts/$ttf" "$out/$name.huifont" "$size" "$range" 4096 unused "$fallback/ttf/DejaVuSans.ttf" >&2
done <<'FONTS'
Inter-Regular.ttf inter-regular 48 6
Inter-SemiBold.ttf inter-semibold 48 6
Montserrat-Medium.ttf montserrat-medium 52 7
DejaVuSansMono.ttf dejavu-sans-mono 44 6
FONTS

# The faces for Chinese and Japanese (Noto Sans SC) and for Korean (Noto Sans KR).

python3 "$root/tools/cjk-ranges.py" "$root/build/host/cjk-ranges" >&2
"$root/build/host/bake_list" "$cjk/NotoSansSC-Regular.otf" "$out/noto-sans-east-asian.huifont" 32 4 4096 \
    "$root/build/host/cjk-ranges/east-asian.txt" >&2
"$root/build/host/bake_list" "$cjk/NotoSansKR-Regular.otf" "$out/noto-sans-korean.huifont" 32 4 2048 \
    "$root/build/host/cjk-ranges/korean.txt" >&2
cp "$fallback/LICENSE" "$out/DejaVuSans-RTL-LICENSE.txt"
cp "$cjk/LICENSE" "$out/NotoSansCJK-LICENSE.txt"
cp "$kit/third_party/fonts/Inter-LICENSE.txt" "$kit/third_party/fonts/Montserrat-LICENSE.txt" \
    "$kit/third_party/fonts/DejaVu-LICENSE.txt" "$out/"
printf '%s\n' "$stamp" > "$out/.stamp"
printf '%s\n' "$out"
