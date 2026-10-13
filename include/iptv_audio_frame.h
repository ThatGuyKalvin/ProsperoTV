/* ps5-native-app-boilerplate - Bounded transport audio framing.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IPTV_AUDIO_FRAME_H
#define IPTV_AUDIO_FRAME_H
#include <stddef.h>
#include <stdint.h>

/* Canonical TS types; private-stream descriptors are resolved by the demuxer. 0x82 is DTS. */
static inline int iptv_audio_software_type(uint32_t type)
{
    return type == 0x81u || type == 0x87u || type == 0x11u || type == 0x82u;
}

/* The bytes a caller waits for before probing a frame: a DTS header reaches further. */
static inline size_t iptv_audio_frame_header_bytes(uint32_t type)
{
    return type == 0x82u ? 12u : 7u;
}

static inline int iptv_audio_dts_core(const uint8_t *p, size_t bytes)
{
    return p && bytes >= 12 && p[0] == 0x7fu && p[1] == 0xfeu && p[2] == 0x80u && p[3] == 0x01u;
}

/* A DTS-HD extension substream's size (the lossless or high-resolution part that follows each
 * core frame), so that it is passed over and only the core is decoded. Zero for anything else. */
static inline size_t iptv_audio_frame_skip(uint32_t type, const uint8_t *p, size_t bytes)
{
    if (type != 0x82u || !p || bytes < 12 || p[0] != 0x64u || p[1] != 0x58u || p[2] != 0x20u ||
        p[3] != 0x25u)
        return 0;
    uint64_t bits = 0;
    for (unsigned i = 4; i < 12; ++i)
        bits = bits << 8 | p[i];
    /* After the user byte: the substream index (2 bits), then a flag choosing 8 + 16 or
     * 12 + 20 bits for the header and substream sizes, each stored less one. */
    const int wide = (bits >> 53) & 1u;
    const size_t header = wide ? ((bits >> 41) & 0xfffu) + 1u : ((bits >> 45) & 0xffu) + 1u;
    const size_t size = wide ? ((bits >> 21) & 0xfffffu) + 1u : ((bits >> 29) & 0xffffu) + 1u;
    return size >= header && header >= 8u ? size : 0;
}

/* Zero means invalid sync/header. Callers wait for iptv_audio_frame_header_bytes first.
 * Frame size may exceed available bytes; callers must wait for the full frame. */
static inline size_t iptv_audio_frame_info(uint32_t type, const uint8_t *p, size_t bytes,
                                           uint32_t *rate, uint32_t *samples)
{
    static const uint32_t rates[] = {48000, 44100, 32000};
    static const uint16_t kbps[] = {32,  40,  48,  56,  64,  80,  96,  112, 128, 160,
                                    192, 224, 256, 320, 384, 448, 512, 576, 640};
    *rate = *samples = 0;
    if (!p || bytes < 7)
        return 0;
    if (type == 0x82u)
    {
        static const uint32_t dts_rates[16] = {0, 8000, 16000, 32000, 0,     0, 11025, 22050, 44100,
                                               0, 0,    12000, 24000, 48000, 0, 0};
        if (!iptv_audio_dts_core(p, bytes))
            return 0;
        const unsigned blocks = ((p[4] & 1u) << 6 | p[5] >> 2) + 1u;
        const size_t frame_bytes = ((size_t)(p[5] & 3u) << 12 | (size_t)p[6] << 4 | p[7] >> 4) + 1u;
        if (blocks < 6u || frame_bytes < 96u || !dts_rates[(p[8] >> 2) & 15u])
            return 0;
        *rate = dts_rates[(p[8] >> 2) & 15u];
        *samples = blocks * 32u;
        return frame_bytes;
    }
    if (type == 0x11u)
        return p[0] == 0x56u && (p[1] & 0xe0u) == 0xe0u ? 3u + ((size_t)(p[1] & 31u) << 8) + p[2]
                                                        : 0;
    if (p[0] != 0x0bu || p[1] != 0x77u)
        return 0;
    const unsigned bsid = p[5] >> 3;
    if (bsid <= 10u)
    {
        const unsigned code = p[4] & 63u, frequency = p[4] >> 6;
        if (frequency == 3u || code > 37u)
            return 0;
        const unsigned bitrate = kbps[code >> 1];
        const unsigned words = frequency == 0   ? bitrate * 2u
                               : frequency == 2 ? bitrate * 3u
                                                : bitrate * 320u / 147u + (code & 1u);
        *rate = rates[frequency] >> (bsid > 8u ? bsid - 8u : 0u);
        *samples = 1536;
        return words * 2u;
    }
    if (bsid > 16u || (p[2] >> 6) == 3u)
        return 0;
    const unsigned frequency = p[4] >> 6;
    const unsigned blocks[] = {1, 2, 3, 6};
    if (frequency == 3u)
    {
        const unsigned frequency2 = (p[4] >> 4) & 3u;
        if (frequency2 == 3u)
            return 0;
        *rate = rates[frequency2] / 2u;
        *samples = 1536;
    }
    else
    {
        *rate = rates[frequency];
        *samples = 256u * blocks[(p[4] >> 4) & 3u];
    }
    /* A dependent substream (7.1's extra channels) is the same moment as the frame before
     * it: no time of its own. */
    if ((p[2] >> 6) == 1u)
        *samples = 0;
    const size_t frame_bytes = 2u * (1u + ((size_t)(p[2] & 7u) << 8) + p[3]);
    return frame_bytes >= 7u ? frame_bytes : 0;
}

/* Channels an AC-3, E-AC-3 or DTS header announces, the low-frequency one included. Zero when
 * the header does not say (AAC-LATM) or is not one. */
static inline uint32_t iptv_audio_frame_channels(uint32_t type, const uint8_t *p, size_t bytes)
{
    static const uint8_t full_range[] = {2, 1, 2, 3, 3, 4, 4, 5};
    if (type == 0x82u)
    {
        static const uint8_t dts_range[16] = {1, 2, 2, 2, 2, 3, 3, 4, 4, 5, 6, 6, 6, 7, 8, 8};
        if (!iptv_audio_dts_core(p, bytes))
            return 0;
        const unsigned mode = (p[7] & 15u) << 2 | p[8] >> 6;
        return mode < 16u ? dts_range[mode] + (((p[10] >> 1) & 3u) != 0) : 0;
    }
    if (type == 0x11u || !p || bytes < 7 || p[0] != 0x0bu || p[1] != 0x77u)
        return 0;
    if ((p[5] >> 3) > 10u)
        return (p[2] >> 6) == 1u ? 0 : full_range[(p[4] >> 1) & 7u] + (p[4] & 1u);
    const unsigned mode = p[6] >> 5;
    /* The mix levels that follow the mode are two bits each, and present only for some modes. */
    const unsigned skipped =
        ((mode & 1u) && mode != 1u ? 2u : 0u) + ((mode & 4u) ? 2u : 0u) + (mode == 2u ? 2u : 0u);
    return full_range[mode] + ((p[6] >> (4u - skipped)) & 1u);
}
#endif
