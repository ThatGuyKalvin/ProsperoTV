#!/usr/bin/env bash
# ps5-native-app-boilerplate - Checks that FFmpeg only imports functions the console provides.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The SDK linker accepts unresolved imports, so a libc function the console does not export
# would only fail when the app starts. Every symbol the FFmpeg archives need from outside
# themselves must be exported by one of the console's modules, as the SDK's import libraries
# (*.so) list them. Its static libc.a is not a guide: it has gmtime_r and localtime_r, which
# the console does not export.
set -euo pipefail
lib=${1:?usage: check-ffmpeg-imports.sh <ffmpeg lib dir> <sdk root>}
sdk=${2:?usage: check-ffmpeg-imports.sh <ffmpeg lib dir> <sdk root>}
nm=$(command -v llvm-nm-18 || command -v llvm-nm || echo "$sdk/bin/llvm-nm")
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

archives=("$lib"/libavformat.a "$lib"/libavcodec.a "$lib"/libswresample.a "$lib"/libavutil.a)
"$nm" --defined-only -g --format=just-symbols "${archives[@]}" 2>/dev/null |
    grep -v ':$' | sort -u > "$work/defined"
"$nm" --undefined-only --format=just-symbols "${archives[@]}" 2>/dev/null |
    grep -v ':$' | sort -u > "$work/undefined"
comm -23 "$work/undefined" "$work/defined" > "$work/external"

: > "$work/provided"
for provider in "$sdk"/target/lib/*.so; do
    [[ -e $provider ]] || continue
    "$nm" --defined-only -g -D --format=just-symbols "$provider" 2>/dev/null >> "$work/provided" ||
        true
done
grep -v ':$' "$work/provided" | sort -u > "$work/provided.sorted"
comm -23 "$work/external" "$work/provided.sorted" > "$work/missing"
if [[ -s $work/missing ]]; then
    echo "FFmpeg needs functions the console does not provide:" >&2
    sed 's/^/  /' "$work/missing" >&2
    exit 1
fi
printf 'FFmpeg imports %u external symbols; all are provided.\n' "$(wc -l < "$work/external")"
