// ProsperoTV - MP4/Matroska into the existing native transport-stream player.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "iptv_subtitles.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace iptv
{
// All callbacks run on the caller's thread. Read returns bytes, 0 at EOF or
// -1 on failure. Seek is absolute; size returns -1 when unknown.
struct MediaInput
{
    void *context = nullptr;
    int (*read)(void *, std::uint8_t *, int) = nullptr;
    std::int64_t (*seek)(void *, std::int64_t) = nullptr;
    std::int64_t (*size)(void *) = nullptr;
    bool (*cancelled)(void *) = nullptr;
    // HLS only: the application opens HTTP(S) resources with its own credential
    // policy. Range end is exclusive; -1 means no end. A successfully returned
    // resource belongs to the reader until its close callback. FFmpeg never
    // opens sockets or local files directly. A returned resource may set url to
    // its final URL after redirects (valid until close), for relative segments.
    // Leave both fields empty for ordinary files.
    const char *url = nullptr;
    bool (*open_resource)(void *, const char *, std::int64_t begin, std::int64_t end,
                          MediaInput *) = nullptr;
    void (*close)(void *) = nullptr;
};
struct MediaAudioTrack
{
    std::uint32_t pid = 0;
    std::string language, title;
    std::uint8_t audio_type = 0; // ISO 639 accessibility type, as in a TS descriptor.
};
struct MediaChapter
{
    std::int64_t position_us = 0;
    std::string title;
};
struct MediaTimeline
{
    std::int64_t duration_us = 0; // Zero when unknown.
    bool seekable = false;
    std::vector<MediaChapter> chapters; // At most 128; titles at most 128 bytes.
};
struct MediaVp9Packet
{
    const std::uint8_t *data = nullptr;
    std::size_t bytes = 0;
    std::uint64_t pts_us = 0;
    std::uint32_t width = 0, height = 0;
    bool keyframe = false;
};
struct MediaOutput
{
    void *context = nullptr;
    bool (*write)(void *, const std::uint8_t *, std::size_t) = nullptr;
    void (*subtitle_tracks)(void *, const std::vector<SubtitleTrack> &) = nullptr;
    void (*subtitle_packet)(void *, std::uint32_t, const std::uint8_t *, std::size_t,
                            std::int64_t pts_us, std::int64_t duration_us) = nullptr;
    void (*audio_tracks)(void *, const std::vector<MediaAudioTrack> &) = nullptr;
    void (*timeline)(void *, const MediaTimeline &) = nullptr;
    // Optional video-only VP9 path; packet storage is borrowed for this call.
    bool (*vp9_packet)(void *, const MediaVp9Packet &) = nullptr;
};
// 0: complete, 1: cancelled, -1: error. Streaming I/O, no transcoding.
// start_us is relative to the beginning; seek to the preceding decodable keyframe.
// Every call creates a fresh mux timeline; the caller must reset its decoder too.
int ReadMedia(const MediaInput &input, const MediaOutput &output, std::string *error,
              std::int64_t start_us = 0);
bool LooksLikeMedia(const std::uint8_t *bytes, std::size_t count);
// HLS WebVTT uses a local cue clock mapped to a wrapping 90 kHz transport clock.
// Normalize before FFmpeg reads the segment (its WebVTT demuxer ignores the map).
bool NormalizeHlsWebVtt(std::string_view segment, std::string &normalized);
} // namespace iptv
