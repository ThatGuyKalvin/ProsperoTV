#!/usr/bin/env bash
# ps5-native-app-boilerplate - Pinned FFmpeg: audio decoders for the native fallback, and the
# MKV/MP4 demuxers for films and episodes (fed by the app's own HTTP reader, so no protocols).
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=8.0.1
hash=05ee0b03119b45c0bdb4df654b96802e909e0a752f72e4fe3794f487229e5a41
archive="$root/.deps/ffmpeg-$version.tar.xz"
source="$root/.deps/ffmpeg-$version"
build="$root/.deps/ffmpeg-audio/build"
prefix="$root/.deps/ffmpeg-audio/root"
stamp=$(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)
if [[ -f $prefix/.complete && $(<"$prefix/.complete") == "$stamp" && -f $prefix/lib/libavcodec.a ]]; then
    exit 0
fi
mkdir -p "$root/.deps" "$build" "$prefix"
if [[ ! -f $archive ]]; then
    curl --fail --location --max-time 120 "https://ffmpeg.org/releases/ffmpeg-$version.tar.xz" -o "$archive"
fi
printf '%s  %s\n' "$hash" "$archive" | sha256sum --check --strict
[[ -f $source/configure ]] || tar -xJf "$archive" -C "$root/.deps"
sdk="$root/.deps/native/ps5-payload-sdk"
source "$sdk/toolchain/prospero.sh"
unset DESTDIR PREFIX
cd "$build"
"$source/configure" --prefix="$prefix" --target-os=freebsd --arch=x86_64 \
    --enable-cross-compile --cc="$sdk/bin/prospero-clang" --ar="$sdk/bin/prospero-ar" \
    --ranlib="$sdk/bin/prospero-ranlib" --disable-autodetect --disable-everything \
    --disable-programs --disable-doc --disable-network --disable-avdevice \
    --disable-avfilter --disable-swscale --disable-shared --enable-static \
    --disable-pthreads --disable-w32threads --disable-os2threads --disable-x86asm \
    --enable-avcodec --enable-avformat --enable-avutil --enable-swresample \
    --enable-decoder=aac,aac_latm,ac3,eac3,mp2,mp3 \
    --enable-decoder=subrip,srt,ass,ssa,movtext,webvtt,text,pgssub,dvdsub,dvbsub \
    --enable-demuxer=matroska,mov \
    --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb,eac3_core \
    --enable-parser=ac3
# The SDK probe linker permits unresolved imports. PS5 exports gmtime and localtime, not
# gmtime_r or localtime_r; select FFmpeg's own portability fallbacks instead of bogus imports.
sed -i 's/^#define HAVE_GMTIME_R 1$/#define HAVE_GMTIME_R 0/' config.h
sed -i 's/^#define HAVE_LOCALTIME_R 1$/#define HAVE_LOCALTIME_R 0/' config.h
# configure drops a component silently when a dependency is missing; the player needs these.
# Component flags live in config_components.h since FFmpeg 5.1.
for wanted in CONFIG_MATROSKA_DEMUXER CONFIG_MOV_DEMUXER CONFIG_H264_MP4TOANNEXB_BSF \
    CONFIG_HEVC_MP4TOANNEXB_BSF CONFIG_EAC3_CORE_BSF CONFIG_AC3_PARSER \
    CONFIG_SUBRIP_DECODER CONFIG_ASS_DECODER CONFIG_MOVTEXT_DECODER CONFIG_WEBVTT_DECODER \
    CONFIG_PGSSUB_DECODER CONFIG_DVDSUB_DECODER CONFIG_DVBSUB_DECODER; do
    grep -q "^#define $wanted 1$" config_components.h ||
        { echo "FFmpeg configure dropped $wanted" >&2; exit 1; }
done
make -j4
make install
bash "$root/tools/check-ffmpeg-imports.sh" "$prefix/lib" "$sdk"
printf '%s\n' "$stamp" > "$prefix/.complete"
