#!/usr/bin/env bash
# ProsperoTV - Bake the fonts the interface draws with into build/fonts.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# usage: tools/bake-fonts.sh        (prints the folder)
#
# KIT the ps5-homebrew-ui checkout (default: ../../ps5-homebrew-ui, beside the repository)
#
# The faces are the kit's (Inter, Montserrat, DejaVu Sans Mono, from its
# third_party/fonts); the alphabet is wider than the kit's own bake, so
# channel names in Western and Central European languages, Greek and Cyrillic
# read as written. Nothing is downloaded. The result is the same for the same
# inputs, so it is made once and reused.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
kit=$(cd -- "${KIT:-$root/../../ps5-homebrew-ui}" && pwd)
cxx=$(command -v "${HOST_CXX:-clang++}")
out="$root/build/fonts"
tool="$root/build/host/bake_wide"
source="$root/tools/font-baker/bake_wide.cpp"
mkdir -p "$out" "$root/build/host"

stamp=$(cat "$source" "${BASH_SOURCE[0]}" "$kit"/third_party/fonts/{Inter-Regular,Inter-SemiBold,Montserrat-Medium,DejaVuSansMono}.ttf | sha256sum | cut -d' ' -f1)
if [[ -f $out/.stamp && $(<"$out/.stamp") == "$stamp" ]]; then
    printf '%s\n' "$out"
    exit 0
fi

"$cxx" -std=c++20 -O2 -w -I"$kit/third_party" -I"$kit/src" "$source" -o "$tool"
while read -r ttf name size range; do
    [[ -n $ttf ]] || continue
    "$tool" "$kit/third_party/fonts/$ttf" "$out/$name.huifont" "$size" "$range" 2048 >&2
done <<'FONTS'
Inter-Regular.ttf inter-regular 48 6
Inter-SemiBold.ttf inter-semibold 48 6
Montserrat-Medium.ttf montserrat-medium 52 7
DejaVuSansMono.ttf dejavu-sans-mono 44 6
FONTS
cp "$kit/third_party/fonts/Inter-LICENSE.txt" "$kit/third_party/fonts/Montserrat-LICENSE.txt" \
    "$kit/third_party/fonts/DejaVu-LICENSE.txt" "$out/"
printf '%s\n' "$stamp" > "$out/.stamp"
printf '%s\n' "$out"
