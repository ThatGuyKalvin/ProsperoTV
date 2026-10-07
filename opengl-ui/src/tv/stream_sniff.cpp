// ProsperoTV - What an address and the first bytes of its answer say, for the debug trace.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/stream_sniff.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace ptv
{

namespace
{

bool starts_with(const unsigned char *data, std::size_t size, std::string_view text,
                 std::size_t at = 0)
{
    return size >= at + text.size() && std::memcmp(data + at, text.data(), text.size()) == 0;
}

// Text answers are compared without their case and past leading blanks.
bool text_starts_with(const unsigned char *data, std::size_t size, std::string_view text)
{
    std::size_t at = 0;
    if (size >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf)
        at = 3; // a byte order mark
    while (at < size && (data[at] == ' ' || data[at] == '\t' || data[at] == '\r' || data[at] == '\n'))
        ++at;
    if (size < at + text.size())
        return false;
    for (std::size_t i = 0; i < text.size(); ++i)
        if (std::tolower(data[at + i]) != std::tolower(static_cast<unsigned char>(text[i])))
            return false;
    return true;
}

const char *kind_of(const unsigned char *data, std::size_t size)
{
    if (size == 0)
        return "nothing";
    if (text_starts_with(data, size, "#EXTM3U"))
        return "HLS or M3U playlist";
    if (data[0] == 0x47 && (size <= 188 || data[188] == 0x47))
        return "MPEG-TS";
    if (size > 192 && data[4] == 0x47 && data[196 > size - 1 ? size - 1 : 196] == 0x47)
        return "MPEG-TS with 192-byte packets (not supported)";
    if (starts_with(data, size, "\x1a\x45\xdf\xa3"))
        return "WebM or Matroska";
    if (starts_with(data, size, "FLV"))
        return "FLV (not supported)";
    if (starts_with(data, size, "ftyp", 4) || starts_with(data, size, "styp", 4) ||
        starts_with(data, size, "moof", 4) || starts_with(data, size, "moov", 4))
        return "MP4 or fragmented MP4 (not supported)";
    if (starts_with(data, size, "\xff\xd8\xff"))
        return "JPEG picture: a camera's still or MJPEG (not supported)";
    if (starts_with(data, size, "--"))
        return "multipart answer: probably MJPEG from a camera (not supported)";
    if (starts_with(data, size, "RTSP/"))
        return "RTSP answer (not supported)";
    if (starts_with(data, size, std::string_view("\0\0\0\1", 4)) ||
        starts_with(data, size, std::string_view("\0\0\1", 3)))
        return "raw H.264 or HEVC without a container (not supported)";
    if (starts_with(data, size, "ID3") || (size > 1 && data[0] == 0xff && (data[1] & 0xf0) == 0xf0))
        return "audio only (MP3 or AAC)";
    if (starts_with(data, size, "RIFF"))
        return "RIFF (AVI or WAV, not supported)";
    if (starts_with(data, size, "\x30\x26\xb2\x75"))
        return "ASF or WMV (not supported)";
    if (text_starts_with(data, size, "<MPD") || (text_starts_with(data, size, "<?xml") &&
                                                 std::string_view(reinterpret_cast<const char *>(data), size)
                                                         .find("<MPD") != std::string_view::npos))
        return "DASH manifest (not supported)";
    if (text_starts_with(data, size, "<!doctype") || text_starts_with(data, size, "<html") ||
        text_starts_with(data, size, "<?xml"))
        return "a web page, not a stream";
    if (text_starts_with(data, size, "{") || text_starts_with(data, size, "["))
        return "JSON, not a stream";
    return "not recognised";
}

} // namespace

std::string redact_address(std::string_view address)
{
    const std::size_t scheme_end = address.find("://");
    if (scheme_end == std::string_view::npos || scheme_end == 0 || scheme_end > 16)
        return "<" + std::to_string(address.size()) + " bytes, no scheme>";
    std::string out(address.substr(0, scheme_end + 3));
    std::string_view rest = address.substr(scheme_end + 3);
    const std::size_t authority_end = rest.find_first_of("/?#");
    std::string_view authority = rest.substr(0, authority_end);
    rest = authority_end == std::string_view::npos ? std::string_view{} : rest.substr(authority_end);
    const std::size_t at = authority.rfind('@');
    if (at != std::string_view::npos)
    {
        out += "<user>@";
        authority = authority.substr(at + 1);
    }
    out += authority;

    const std::size_t query_at = rest.find_first_of("?#");
    const std::string_view path = rest.substr(0, query_at);
    if (!path.empty())
    {
        int parts = 0;
        for (std::size_t i = 0; i < path.size(); ++i)
            if (path[i] == '/' && i + 1 < path.size() && path[i + 1] != '/')
                ++parts;
        out += "/<" + std::to_string(parts) + (parts == 1 ? " part>" : " parts>");
        // The extension says what the provider calls the stream.
        const std::size_t slash = path.rfind('/');
        const std::size_t dot = path.rfind('.');
        if (dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash) &&
            path.size() - dot <= 6 && path.size() - dot > 1)
        {
            bool plain = true;
            for (const char c : path.substr(dot + 1))
                plain = plain && std::isalnum(static_cast<unsigned char>(c)) != 0;
            if (plain)
                out += path.substr(dot);
        }
    }
    if (query_at != std::string_view::npos)
        out += "?<" + std::to_string(rest.size() - query_at - 1) + " bytes>";
    return out;
}

std::string describe_bytes(const unsigned char *data, std::size_t size)
{
    std::string out = kind_of(data, size);
    if (size != 0)
    {
        out += " [";
        char hex[4];
        for (std::size_t i = 0; i < size && i < 16; ++i)
        {
            std::snprintf(hex, sizeof(hex), i == 0 ? "%02x" : " %02x", data[i]);
            out += hex;
        }
        out += "]";
    }
    return out;
}

} // namespace ptv
