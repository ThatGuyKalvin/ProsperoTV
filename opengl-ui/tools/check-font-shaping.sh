#!/usr/bin/env bash
# ProsperoTV - Bakes and checks the actual RTL shaping font on the host.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
kit=$(bash "$root/tools/fetch-kit.sh")
text=$(bash "$root/tools/setup-text-dependencies.sh")
build="$root/build/text"
mkdir -p "$build"
cxx=${HOST_CXX:-clang++}
"$cxx" -std=c++20 -O2 -I"$kit/src" -I"$kit/third_party" -I"$text/include/harfbuzz" \
    "$root/tools/font-baker/bake_list.cpp" "$text/lib/libharfbuzz-subset.a" "$text/lib/libharfbuzz.a" -lpthread -o "$build/bake_shaped"
"$build/bake_shaped" "$kit/third_party/fonts/Inter-Regular.ttf" "$build/inter-shaped.huifont" 48 6 4096 unused \
    "$root/.deps/text-host/dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf"
"$cxx" -std=c++20 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$kit/src" -I"$text/include/harfbuzz" -I"$text/include/fribidi" \
    "$root/tests/font_shape_check.cc" "$root/src/tv/font_shape.cpp" "$kit/src/gfx/font.cpp" "$kit/src/gfx/draw_list.cpp" "$kit/src/gfx/triangulate.cpp" \
    "$text/lib/libharfbuzz.a" "$text/lib/libfribidi.a" -lpthread -Wl,--wrap=hb_shape -o "$build/font_shape_check"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/font_shape_check" "$build/inter-shaped.huifont"
