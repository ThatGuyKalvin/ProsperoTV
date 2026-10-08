/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_MEDIA_PACK_H
#define IPTV_MEDIA_PACK_H

#include <cstddef>
#include <cstdint>

// Small pieces of the file (MKV/MP4) player that FFmpeg has no public API for: recognising
// the container, and repackaging AAC for the console's ADTS-only audio path.
namespace iptv::media
{

enum class Container : std::uint8_t
{
    none,
    webm,     // Matroska with DocType "webm" (VP9; played by the WebM reader)
    matroska, // any other Matroska file (MKV)
    mp4,      // ISO base media / QuickTime (MP4, M4V, MOV)
};

// From the first bytes of a response; `none` when they are neither Matroska nor MP4.
Container SniffContainer(const std::uint8_t *data, std::size_t bytes);
// From the extension of the URL's path.
Container ContainerFromUrl(const char *url);

// The parts of an MPEG-4 AudioSpecificConfig an ADTS header carries.
struct AacConfig
{
    unsigned object_type = 0;     // 1 Main, 2 LC, 3 SSR, 4 LTP
    unsigned frequency_index = 0; // of the core (pre-SBR) rate
    unsigned channel_config = 0;  // 1..7
};

// Reads an AudioSpecificConfig (MKV CodecPrivate / MP4 esds). False for configurations an
// ADTS header cannot describe (channel layout given by a PCE, or an object type above 4 once
// explicit SBR/PS signalling is removed; HE-AAC is then sent as its LC core).
bool ParseAudioSpecificConfig(const std::uint8_t *data, std::size_t bytes, AacConfig *config);

inline constexpr std::size_t kAdtsHeaderBytes = 7u;
inline constexpr std::size_t kMaxAdtsFrameBytes = 8191u;

// Writes the 7-byte ADTS header for one raw AAC frame of `payload_bytes`. False when the
// whole frame would exceed kMaxAdtsFrameBytes.
bool WriteAdtsHeader(const AacConfig &config, std::size_t payload_bytes,
                     std::uint8_t out[kAdtsHeaderBytes]);

// A timestamp in microseconds relative to the file's start, clamped at zero, so an MP4 edit
// list that starts before zero cannot wrap the player's unsigned clock.
std::uint64_t NormalizePtsUs(std::int64_t pts_us, std::int64_t base_us);

} // namespace iptv::media

#endif
