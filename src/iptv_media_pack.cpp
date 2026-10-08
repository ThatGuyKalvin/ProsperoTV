/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_media_pack.h"

#include <cctype>
#include <cstring>

namespace iptv::media
{
namespace
{

constexpr unsigned kAacRates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                  22050, 16000, 12000, 11025, 8000,  7350};

struct Bits
{
    const std::uint8_t *data;
    std::size_t bytes;
    std::size_t at = 0;

    bool Read(unsigned count, unsigned *value)
    {
        if (at + count > bytes * 8u)
            return false;
        unsigned out = 0;
        for (unsigned index = 0; index < count; ++index, ++at)
            out = (out << 1) | ((data[at >> 3] >> (7u - (at & 7u))) & 1u);
        *value = out;
        return true;
    }
};

bool ReadObjectType(Bits *bits, unsigned *type)
{
    if (!bits->Read(5, type))
        return false;
    unsigned extended = 0;
    if (*type == 31u)
    {
        if (!bits->Read(6, &extended))
            return false;
        *type = 32u + extended;
    }
    return true;
}

// An explicit 24-bit rate is mapped back to its index; ADTS has no other way to carry it.
bool ReadFrequencyIndex(Bits *bits, unsigned *index)
{
    if (!bits->Read(4, index))
        return false;
    if (*index != 15u)
        return *index < sizeof(kAacRates) / sizeof(kAacRates[0]);
    unsigned rate = 0;
    if (!bits->Read(24, &rate))
        return false;
    for (unsigned candidate = 0; candidate < sizeof(kAacRates) / sizeof(kAacRates[0]); ++candidate)
        if (kAacRates[candidate] == rate)
        {
            *index = candidate;
            return true;
        }
    return false;
}

// Reads one EBML variable-length integer; `keep_marker` keeps the length bits (element IDs).
bool ReadVint(const std::uint8_t *data, std::size_t bytes, std::size_t *at, bool keep_marker,
              std::uint64_t *value)
{
    if (*at >= bytes || data[*at] == 0)
        return false;
    const std::uint8_t first = data[*at];
    unsigned length = 1;
    while (!(first & (0x80u >> (length - 1u))))
        ++length;
    if (*at + length > bytes)
        return false;
    std::uint64_t out = keep_marker ? first : (first & (0xffu >> length));
    for (unsigned index = 1; index < length; ++index)
        out = (out << 8) | data[*at + index];
    *at += length;
    *value = out;
    return true;
}

} // namespace

Container SniffContainer(const std::uint8_t *data, std::size_t bytes)
{
    if (!data || bytes < 8u)
        return Container::none;
    if (data[0] == 0x1a && data[1] == 0x45 && data[2] == 0xdf && data[3] == 0xa3)
    {
        // Look through the EBML header for DocType; without one it is plain Matroska.
        std::size_t at = 4;
        std::uint64_t header_size = 0;
        if (!ReadVint(data, bytes, &at, false, &header_size))
            return Container::matroska;
        const std::size_t end =
            header_size < bytes - at ? at + static_cast<std::size_t>(header_size) : bytes;
        while (at < end)
        {
            std::uint64_t id = 0;
            std::uint64_t size = 0;
            if (!ReadVint(data, end, &at, true, &id) || !ReadVint(data, end, &at, false, &size) ||
                size > end - at)
                break;
            if (id == 0x4282u)
                return size == 4u && std::memcmp(data + at, "webm", 4) == 0 ? Container::webm
                                                                            : Container::matroska;
            at += static_cast<std::size_t>(size);
        }
        return Container::matroska;
    }
    // An ISO base media file starts with a box; fragmented-MP4 segments ("styp") are not
    // files this player reads.
    static constexpr const char *kBoxes[] = {"ftyp", "moov", "mdat", "free",
                                             "wide", "skip", "pdin"};
    const std::uint32_t size = (static_cast<std::uint32_t>(data[0]) << 24) |
                               (static_cast<std::uint32_t>(data[1]) << 16) |
                               (static_cast<std::uint32_t>(data[2]) << 8) | data[3];
    if (size != 0u && size != 1u && size < 8u)
        return Container::none;
    for (const char *box : kBoxes)
        if (std::memcmp(data + 4, box, 4) == 0)
            return Container::mp4;
    return Container::none;
}

Container ContainerFromUrl(const char *url)
{
    if (!url)
        return Container::none;
    const std::size_t path_end = std::strcspn(url, "?#");
    std::size_t dot = path_end;
    while (dot > 0 && url[dot - 1u] != '.' && url[dot - 1u] != '/')
        --dot;
    if (dot == 0 || url[dot - 1u] != '.')
        return Container::none;
    char extension[8] = {};
    const std::size_t length = path_end - dot;
    if (length == 0 || length >= sizeof(extension))
        return Container::none;
    for (std::size_t index = 0; index < length; ++index)
        extension[index] =
            static_cast<char>(std::tolower(static_cast<unsigned char>(url[dot + index])));
    if (!std::strcmp(extension, "mkv") || !std::strcmp(extension, "mk3d"))
        return Container::matroska;
    if (!std::strcmp(extension, "webm"))
        return Container::webm;
    if (!std::strcmp(extension, "mp4") || !std::strcmp(extension, "m4v") ||
        !std::strcmp(extension, "mov"))
        return Container::mp4;
    return Container::none;
}

bool ParseAudioSpecificConfig(const std::uint8_t *data, std::size_t bytes, AacConfig *config)
{
    if (!data || !config || bytes < 2u)
        return false;
    Bits bits{data, bytes};
    unsigned type = 0;
    unsigned frequency = 0;
    unsigned channels = 0;
    if (!ReadObjectType(&bits, &type) || !ReadFrequencyIndex(&bits, &frequency) ||
        !bits.Read(4, &channels))
        return false;
    if (type == 5u || type == 29u)
    {
        // Explicit SBR/PS: the extension rate follows, then the core object type. The
        // frequency read above is the core rate, which is what ADTS describes.
        unsigned extension_frequency = 0;
        if (!ReadFrequencyIndex(&bits, &extension_frequency) || !ReadObjectType(&bits, &type))
            return false;
    }
    if (type < 1u || type > 4u || channels < 1u || channels > 7u)
        return false;
    *config = {type, frequency, channels};
    return true;
}

bool WriteAdtsHeader(const AacConfig &config, std::size_t payload_bytes,
                     std::uint8_t out[kAdtsHeaderBytes])
{
    const std::size_t frame = kAdtsHeaderBytes + payload_bytes;
    if (!out || frame > kMaxAdtsFrameBytes || config.object_type < 1u || config.object_type > 4u ||
        config.frequency_index > 12u || config.channel_config < 1u || config.channel_config > 7u)
        return false;
    const unsigned profile = config.object_type - 1u;
    out[0] = 0xff;
    out[1] = 0xf1; // MPEG-4, layer 0, no CRC
    out[2] = static_cast<std::uint8_t>((profile << 6) | (config.frequency_index << 2) |
                                       (config.channel_config >> 2));
    out[3] = static_cast<std::uint8_t>(((config.channel_config & 3u) << 6) | (frame >> 11));
    out[4] = static_cast<std::uint8_t>((frame >> 3) & 0xffu);
    out[5] = static_cast<std::uint8_t>(((frame & 7u) << 5) | 0x1fu); // buffer fullness 0x7ff
    out[6] = 0xfc;                                                   // one raw data block
    return true;
}

std::uint64_t NormalizePtsUs(std::int64_t pts_us, std::int64_t base_us)
{
    return pts_us > base_us ? static_cast<std::uint64_t>(pts_us - base_us) : 0u;
}

} // namespace iptv::media
