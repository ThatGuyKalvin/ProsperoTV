/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_video_sps.h"

#include <cstdint>

namespace iptv::video
{

bool ReadBits(BitReader *reader, unsigned count, uint32_t *value)
{
    if (count > 32u || reader->at + count > reader->bits)
        return false;
    uint32_t out = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        out = (out << 1) | ((reader->data[reader->at >> 3] >> (7u - (reader->at & 7u))) & 1u);
        ++reader->at;
    }
    *value = out;
    return true;
}

bool SkipBits(BitReader *reader, size_t count)
{
    if (reader->at + count > reader->bits)
        return false;
    reader->at += count;
    return true;
}

bool ReadUe(BitReader *reader, uint32_t *value)
{
    unsigned zeros = 0;
    uint32_t bit = 0;
    while (zeros < 31u)
    {
        if (!ReadBits(reader, 1, &bit))
            return false;
        if (bit)
            break;
        ++zeros;
    }
    uint32_t suffix = 0;
    if (zeros && !ReadBits(reader, zeros, &suffix))
        return false;
    *value = ((UINT32_C(1) << zeros) - 1u) + suffix;
    return true;
}

bool ReadSe(BitReader *reader, int32_t *value)
{
    uint32_t code = 0;
    if (!ReadUe(reader, &code))
        return false;
    *value =
        (code & 1u) ? static_cast<int32_t>((code + 1u) >> 1) : -static_cast<int32_t>(code >> 1);
    return true;
}

size_t MakeRbsp(const uint8_t *data, size_t bytes, uint8_t *out, size_t capacity)
{
    size_t written = 0;
    unsigned zeros = 0;
    for (size_t i = 0; i < bytes; ++i)
    {
        if (zeros >= 2u && data[i] == 0x03u)
        {
            zeros = 0;
            continue;
        }
        if (written >= capacity)
            return 0;
        out[written++] = data[i];
        zeros = data[i] == 0 ? zeros + 1u : 0u;
    }
    return written;
}

namespace
{

bool H264ExtendedProfile(uint32_t profile)
{
    return profile == 100u || profile == 110u || profile == 122u || profile == 244u ||
           profile == 44u || profile == 83u || profile == 86u || profile == 118u ||
           profile == 128u || profile == 138u || profile == 139u || profile == 134u ||
           profile == 135u;
}

uint32_t ChromaApi(uint32_t chroma)
{
    switch (chroma)
    {
    case 0:
        return IPTV_STREAM_CHROMA_MONO;
    case 1:
        return IPTV_STREAM_CHROMA_420;
    case 2:
        return IPTV_STREAM_CHROMA_422;
    case 3:
        return IPTV_STREAM_CHROMA_444;
    default:
        return IPTV_STREAM_CHROMA_UNKNOWN;
    }
}

int Fail(const char **error, int result, const char *text)
{
    if (error)
        *error = text;
    return result;
}

int Accept(SpsInfo *sps, uint32_t profile, uint32_t level, uint32_t coded_width,
           uint32_t coded_height, uint32_t visible_width, uint32_t visible_height,
           uint32_t bit_depth, uint32_t chroma)
{
    *sps = {profile,       level,          coded_width, coded_height,
            visible_width, visible_height, bit_depth,   chroma};
    return IPTV_STREAM_OK;
}

} // namespace

int ParseH264Sps(const uint8_t *nal, size_t bytes, SpsInfo *sps, const char **error)
{
    uint8_t rbsp[1024];
    if (bytes < 4u || (nal[0] & 0x1fu) != 7u)
        return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid H.264 SPS");
    const size_t rbsp_bytes = MakeRbsp(nal + 1u, bytes - 1u, rbsp, sizeof(rbsp));
    if (rbsp_bytes < 4u)
        return Fail(error, IPTV_STREAM_MALFORMED_TS, "truncated H.264 SPS");

    BitReader bits{rbsp, rbsp_bytes * 8u, 0};
    uint32_t profile = 0, constraints = 0, level = 0, value = 0;
    if (!ReadBits(&bits, 8, &profile) || !ReadBits(&bits, 8, &constraints) ||
        !ReadBits(&bits, 8, &level) || !ReadUe(&bits, &value))
        return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid H.264 SPS header");
    (void)constraints;

    uint32_t chroma = 1;
    uint32_t bit_depth_luma = 8;
    uint32_t bit_depth_chroma = 8;
    bool separate_colour_plane = false;
    if (H264ExtendedProfile(profile))
    {
        uint32_t separate = 0, transform = 0, scaling = 0;
        if (!ReadUe(&bits, &chroma) || chroma > 3u ||
            (chroma == 3u && (!ReadBits(&bits, 1, &separate))))
            return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid H.264 chroma format");
        separate_colour_plane = separate != 0;
        if (!ReadUe(&bits, &value))
            return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid H.264 luma depth");
        bit_depth_luma = value + 8u;
        if (!ReadUe(&bits, &value))
            return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid H.264 chroma depth");
        bit_depth_chroma = value + 8u;
        if (!ReadBits(&bits, 1, &transform) || !ReadBits(&bits, 1, &scaling))
            return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid H.264 SPS flags");
        if (scaling)
        {
            const uint32_t lists = chroma == 3u ? 12u : 8u;
            for (uint32_t i = 0; i < lists; ++i)
            {
                uint32_t present = 0;
                if (!ReadBits(&bits, 1, &present))
                    return Fail(error, IPTV_STREAM_MALFORMED_TS,
                                "truncated H.264 scaling-list flags");
                if (!present)
                    continue;
                int32_t last = 8;
                int32_t next = 8;
                const uint32_t count = i < 6u ? 16u : 64u;
                for (uint32_t j = 0; j < count; ++j)
                {
                    if (next != 0)
                    {
                        int32_t delta = 0;
                        if (!ReadSe(&bits, &delta))
                            return Fail(error, IPTV_STREAM_MALFORMED_TS,
                                        "truncated H.264 scaling list");
                        next = (last + delta + 256) & 255;
                    }
                    if (next != 0)
                        last = next;
                }
            }
        }
    }
    if (bit_depth_luma != 8u || bit_depth_chroma != 8u || chroma != 1u || separate_colour_plane)
        return Fail(error, IPTV_STREAM_UNSUPPORTED_FORMAT, "H.264 must be 8-bit 4:2:0");

    uint32_t pic_order_cnt_type = 0;
    if (!ReadUe(&bits, &value) || !ReadUe(&bits, &pic_order_cnt_type))
        return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid H.264 SPS timing");
    if (pic_order_cnt_type == 0u)
    {
        if (!ReadUe(&bits, &value))
            return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid H.264 POC");
    }
    else if (pic_order_cnt_type == 1u)
    {
        uint32_t flag = 0, cycle = 0;
        int32_t signed_value = 0;
        if (!ReadBits(&bits, 1, &flag) || !ReadSe(&bits, &signed_value) ||
            !ReadSe(&bits, &signed_value) || !ReadUe(&bits, &cycle) || cycle > 255u)
            return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid H.264 POC cycle");
        for (uint32_t i = 0; i < cycle; ++i)
            if (!ReadSe(&bits, &signed_value))
                return Fail(error, IPTV_STREAM_MALFORMED_TS, "truncated H.264 POC cycle");
    }
    else if (pic_order_cnt_type > 2u)
    {
        return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid H.264 POC type");
    }

    uint32_t gaps = 0, width_mbs = 0, height_maps = 0, frame_only = 0;
    if (!ReadUe(&bits, &value) || !ReadBits(&bits, 1, &gaps) || !ReadUe(&bits, &width_mbs) ||
        !ReadUe(&bits, &height_maps) || !ReadBits(&bits, 1, &frame_only))
        return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid H.264 dimensions");
    (void)gaps;
    if (!frame_only && !SkipBits(&bits, 1))
        return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid H.264 field mode");
    uint32_t direct = 0, crop = 0;
    if (!ReadBits(&bits, 1, &direct) || !ReadBits(&bits, 1, &crop))
        return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid H.264 crop flags");
    (void)direct;
    uint32_t crop_left = 0, crop_right = 0, crop_top = 0, crop_bottom = 0;
    if (crop && (!ReadUe(&bits, &crop_left) || !ReadUe(&bits, &crop_right) ||
                 !ReadUe(&bits, &crop_top) || !ReadUe(&bits, &crop_bottom)))
        return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid H.264 crop");

    const uint64_t coded_width = static_cast<uint64_t>(width_mbs + 1u) * 16u;
    const uint64_t coded_height = static_cast<uint64_t>(2u - frame_only) * (height_maps + 1u) * 16u;
    const uint64_t crop_x = static_cast<uint64_t>(crop_left + crop_right) * 2u;
    const uint64_t crop_y = static_cast<uint64_t>(crop_top + crop_bottom) * 2u * (2u - frame_only);
    if (coded_width > UINT32_MAX || coded_height > UINT32_MAX || crop_x >= coded_width ||
        crop_y >= coded_height)
        return Fail(error, IPTV_STREAM_UNSUPPORTED_FORMAT,
                    "H.264 dimensions exceed the stream contract");

    const int accepted =
        Accept(sps, profile, level, static_cast<uint32_t>(coded_width),
               static_cast<uint32_t>(coded_height), static_cast<uint32_t>(coded_width - crop_x),
               static_cast<uint32_t>(coded_height - crop_y), 8, ChromaApi(chroma));
    // The pixel shape, when the VUI gives one; anything unreadable leaves pixels square.
    static const uint8_t kSarTable[17][2] = {
        {0, 0},   {1, 1},   {12, 11}, {10, 11}, {16, 11},  {40, 33}, {24, 11}, {20, 11}, {32, 11},
        {80, 33}, {18, 11}, {15, 11}, {64, 33}, {160, 99}, {4, 3},   {3, 2},   {2, 1}};
    uint32_t vui = 0, present = 0, idc = 0;
    if (accepted == IPTV_STREAM_OK && ReadBits(&bits, 1, &vui) && vui &&
        ReadBits(&bits, 1, &present) && present && ReadBits(&bits, 8, &idc))
    {
        uint32_t num = 0, den = 0;
        if (idc == 255u)
        {
            if (!ReadBits(&bits, 16, &num) || !ReadBits(&bits, 16, &den))
                num = den = 0;
        }
        else if (idc < 17u)
        {
            num = kSarTable[idc][0];
            den = kSarTable[idc][1];
        }
        if (num && den && num != den)
        {
            sps->sar_num = num;
            sps->sar_den = den;
        }
    }
    return accepted;
}

int ParseHevcSps(const uint8_t *nal, size_t bytes, SpsInfo *sps, const char **error)
{
    uint8_t rbsp[1024];
    if (bytes < 5u || ((nal[0] >> 1) & 0x3fu) != 33u)
        return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid HEVC SPS");
    const size_t rbsp_bytes = MakeRbsp(nal + 2u, bytes - 2u, rbsp, sizeof(rbsp));
    if (!rbsp_bytes)
        return Fail(error, IPTV_STREAM_MALFORMED_TS, "truncated HEVC SPS");
    BitReader bits{rbsp, rbsp_bytes * 8u, 0};
    uint32_t value = 0, sublayers = 0, profile = 0, level = 0;
    if (!ReadBits(&bits, 4, &value) || !ReadBits(&bits, 3, &sublayers) || !SkipBits(&bits, 1) ||
        !SkipBits(&bits, 3) || !ReadBits(&bits, 5, &profile) || !SkipBits(&bits, 32u + 48u) ||
        !ReadBits(&bits, 8, &level))
        return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid HEVC profile tier level");
    if (profile != 1u && profile != 2u)
        return Fail(error, IPTV_STREAM_UNSUPPORTED_FORMAT, "HEVC profile must be Main or Main10");

    uint32_t profile_present[7]{};
    uint32_t level_present[7]{};
    for (uint32_t i = 0; i < sublayers; ++i)
        if (!ReadBits(&bits, 1, &profile_present[i]) || !ReadBits(&bits, 1, &level_present[i]))
            return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid HEVC sublayer flags");
    if (sublayers)
        for (uint32_t i = sublayers; i < 8u; ++i)
            if (!SkipBits(&bits, 2))
                return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid HEVC reserved bits");
    for (uint32_t i = 0; i < sublayers; ++i)
    {
        if (profile_present[i] && !SkipBits(&bits, 88u))
            return Fail(error, IPTV_STREAM_MALFORMED_TS, "truncated HEVC sublayer profile");
        if (level_present[i] && !SkipBits(&bits, 8u))
            return Fail(error, IPTV_STREAM_MALFORMED_TS, "truncated HEVC sublayer level");
    }

    uint32_t chroma = 0, separate = 0, width = 0, height = 0, crop = 0;
    if (!ReadUe(&bits, &value) || !ReadUe(&bits, &chroma) || chroma > 3u ||
        (chroma == 3u && !ReadBits(&bits, 1, &separate)) || !ReadUe(&bits, &width) ||
        !ReadUe(&bits, &height) || !ReadBits(&bits, 1, &crop))
        return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid HEVC dimensions");
    uint32_t left = 0, right = 0, top = 0, bottom = 0;
    if (crop && (!ReadUe(&bits, &left) || !ReadUe(&bits, &right) || !ReadUe(&bits, &top) ||
                 !ReadUe(&bits, &bottom)))
        return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid HEVC crop");
    uint32_t luma_depth = 0, chroma_depth = 0;
    if (!ReadUe(&bits, &luma_depth) || !ReadUe(&bits, &chroma_depth))
        return Fail(error, IPTV_STREAM_MALFORMED_TS, "invalid HEVC bit depth");
    if (luma_depth != chroma_depth || (luma_depth != 0u && luma_depth != 2u) ||
        (luma_depth == 2u && profile != 2u) || chroma != 1u || separate)
        return Fail(error, IPTV_STREAM_UNSUPPORTED_FORMAT, "HEVC must be 8-bit or 10-bit 4:2:0");
    const uint32_t sub_width = chroma == 1u || chroma == 2u ? 2u : 1u;
    const uint32_t sub_height = chroma == 1u ? 2u : 1u;
    const uint64_t crop_x = static_cast<uint64_t>(left + right) * sub_width;
    const uint64_t crop_y = static_cast<uint64_t>(top + bottom) * sub_height;
    if (!width || !height || crop_x >= width || crop_y >= height)
        return Fail(error, IPTV_STREAM_UNSUPPORTED_FORMAT,
                    "HEVC dimensions exceed the stream contract");

    return Accept(sps, profile, level, width, height, width - static_cast<uint32_t>(crop_x),
                  height - static_cast<uint32_t>(crop_y), 8u + luma_depth, ChromaApi(chroma));
}

bool FindSps(const uint8_t *access_unit, size_t bytes, uint32_t codec, const uint8_t **nal,
             size_t *nal_bytes)
{
    if (!access_unit || !nal || !nal_bytes)
        return false;
    size_t at = 0;
    while (at + 3u <= bytes)
    {
        // Find the next start code (00 00 01, possibly after a further zero byte).
        size_t start = at;
        while (start + 3u <= bytes && !(access_unit[start] == 0 && access_unit[start + 1u] == 0 &&
                                        access_unit[start + 2u] == 1u))
            ++start;
        if (start + 3u > bytes)
            return false;
        const size_t payload = start + 3u;
        size_t end = payload;
        while (end + 3u <= bytes &&
               !(access_unit[end] == 0 && access_unit[end + 1u] == 0 &&
                 (access_unit[end + 2u] == 1u ||
                  (access_unit[end + 2u] == 0 && end + 3u < bytes && access_unit[end + 3u] == 1u))))
            ++end;
        if (end + 3u > bytes)
            end = bytes;
        if (payload < end)
        {
            const uint8_t header = access_unit[payload];
            const bool sps = codec == IPTV_STREAM_VIDEO_H264 ? (header & 0x1fu) == 7u
                                                             : ((header >> 1) & 0x3fu) == 33u;
            if (sps)
            {
                *nal = access_unit + payload;
                *nal_bytes = end - payload;
                return true;
            }
        }
        at = end;
    }
    return false;
}

int PictureType(const uint8_t *access_unit, size_t bytes, uint32_t codec)
{
    if (!access_unit)
        return -1;
    const bool hevc = codec == IPTV_STREAM_VIDEO_HEVC;
    for (size_t at = 0; at + 3u < bytes; ++at)
    {
        if (access_unit[at] != 0 || access_unit[at + 1u] != 0 || access_unit[at + 2u] != 1u)
            continue;
        const uint8_t header = access_unit[at + 3u];
        const int type = hevc ? (header >> 1) & 0x3f : header & 0x1f;
        if (hevc ? type <= 31 : (type >= 1 && type <= 5))
            return type;
        at += 2u;
    }
    return -1;
}

} // namespace iptv::video
