/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_VIDEO_SPS_H
#define IPTV_VIDEO_SPS_H

#include "iptv_stream.h"

#include <cstddef>
#include <cstdint>

// H.264 and HEVC sequence parameter sets: what the native decoder must be opened with. Shared
// by the MPEG-TS demuxer and the file (MKV/MP4) player.
namespace iptv::video
{

struct BitReader
{
    const uint8_t *data;
    size_t bits;
    size_t at;
};

bool ReadBits(BitReader *reader, unsigned count, uint32_t *value);
bool SkipBits(BitReader *reader, size_t count);
bool ReadUe(BitReader *reader, uint32_t *value);
bool ReadSe(BitReader *reader, int32_t *value);
// Removes emulation-prevention bytes; returns 0 when the result does not fit.
size_t MakeRbsp(const uint8_t *data, size_t bytes, uint8_t *out, size_t capacity);

struct SpsInfo
{
    uint32_t profile = 0;
    uint32_t level = 0;
    uint32_t coded_width = 0;
    uint32_t coded_height = 0;
    uint32_t visible_width = 0;
    uint32_t visible_height = 0;
    uint32_t bit_depth = 0;
    uint32_t chroma = IPTV_STREAM_CHROMA_UNKNOWN;
    // H.264 only: the pixel shape from the VUI (0:0 when absent or square).
    uint32_t sar_num = 0;
    uint32_t sar_den = 0;
};

// `nal` starts at the NAL header (no start code). Returns IPTV_STREAM_OK, or
// IPTV_STREAM_MALFORMED_TS / IPTV_STREAM_UNSUPPORTED_FORMAT with *error describing why.
int ParseH264Sps(const uint8_t *nal, size_t bytes, SpsInfo *sps, const char **error);
int ParseHevcSps(const uint8_t *nal, size_t bytes, SpsInfo *sps, const char **error);

// Finds the first SPS in an Annex-B access unit (codec is IPTV_STREAM_VIDEO_H264 or _HEVC).
bool FindSps(const uint8_t *access_unit, size_t bytes, uint32_t codec, const uint8_t **nal,
             size_t *nal_bytes);

// The NAL type of an Annex-B access unit's first picture slice (H.264: 1-5, HEVC: 0-31), or
// -1 when it has none. Playback starts at a random-access picture (H.264 IDR 5, HEVC IRAP
// 16-21); after an HEVC CRA (21) it skips the RASL pictures (8, 9) that refer to earlier ones.
int PictureType(const uint8_t *access_unit, size_t bytes, uint32_t codec);

} // namespace iptv::video

#endif
