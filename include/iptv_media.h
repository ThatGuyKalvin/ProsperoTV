/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_MEDIA_H
#define IPTV_MEDIA_H

#include "iptv_http.h"
#include "iptv_media_pack.h"
#include "iptv_stream.h"

#include <cstddef>
#include <cstdint>

// Plays a film or episode file (MKV or MP4) with FFmpeg's demuxers. FFmpeg reads through the
// app's own HTTP layer, reopening the file with Range requests to seek (an MP4 whose index is
// at the end) or to carry on after a dropped connection. Video goes to the hardware decoder as
// Annex-B access units; AAC, AC-3, E-AC-3 and MP2 audio as single frames. Subtitles (text, and
// Blu-ray, DVD and DVB pictures) are decoded here and handed to iptv_subtitle, which draws them.
namespace iptv::media
{

// What the viewer asked for during playback.
enum class Command : std::uint8_t
{
    none,
    toggle_pause,
    back,         // 10 s
    forward,      // 10 s
    back_long,    // 60 s
    forward_long, // 60 s
    next_audio,
    previous_audio,
    next_subtitle, // Off, then each subtitle track in turn
    previous_subtitle,
    start_over,
    select_audio,    // the track Request::value names (1-based)
    select_subtitle, // the track Request::value names (1-based; 0 turns subtitles off)
};

// A command and what it is about.
struct Request
{
    Command command = Command::none;
    unsigned value = 0;
};

// How many track names Status lists.
constexpr unsigned kListedTracks = 8;

// Where playback is, for the on-screen controls.
struct Status
{
    std::int64_t position_us = -1; // the picture on screen, or the target of a pending seek
    std::int64_t duration_us = -1;
    bool paused = false;
    bool seeking = false;     // the position is a seek target that is not playing yet
    unsigned audio_track = 0; // 1-based; 0 when there is no audio
    unsigned audio_tracks = 0;
    char audio_label[64] = {};
    unsigned subtitle_track = 0; // 1-based; 0 when subtitles are off
    unsigned subtitle_tracks = 0;
    char subtitle_label[64] = {};
    char subtitle_language[16] = {}; // the chosen track's language code, if it has one
    // Every track's name, for choosing one (the first kListedTracks).
    char audio_labels[kListedTracks][48] = {};
    char subtitle_labels[kListedTracks][48] = {};
};

// Implemented by the player. Video and audio receive one access unit or frame; audio errors
// are not fatal.
struct Sink
{
    void *context = nullptr;
    bool (*stop)(void *context) = nullptr;
    int (*open)(void *context, const iptv_stream_format_t *format) = nullptr;
    int (*video)(void *context, const std::uint8_t *data, std::size_t bytes,
                 std::uint64_t pts_us) = nullptr;
    int (*audio)(void *context, const std::uint8_t *data, std::size_t bytes,
                 std::uint64_t pts_us) = nullptr;
    std::uint64_t (*presented)(void *context) = nullptr;
    // Optional controls. command returns Command::none when nothing is waiting.
    Request (*command)(void *context) = nullptr;
    void (*status)(void *context, const Status *status) = nullptr;
    int (*pause)(void *context, bool paused) = nullptr;
    // Drops what the decoder holds after a seek; a non-zero audio type switches the audio.
    int (*seek_reset)(void *context, std::uint32_t audio_stream_type) = nullptr;
    // The PTS of the picture on screen, and the decoder generation it belongs to.
    std::uint64_t (*position)(void *context, std::uint32_t *generation) = nullptr;
    std::uint32_t (*generation)(void *context) = nullptr;
};

struct Source
{
    // The open response, positioned after `prefix`. Play may close it to reopen elsewhere;
    // closing it again afterwards is harmless.
    iptv::http::StreamRequest *request = nullptr;
    const char *url = nullptr; // as requested, for a reopen when the redirect target expires
    const char *accept = nullptr;
    const iptv::http::RequestHeaders *headers = nullptr;
    const std::uint8_t *prefix = nullptr; // the first bytes, already read from `request`
    std::size_t prefix_bytes = 0;
    Container container = Container::none; // matroska or mp4
    std::int64_t start_position_us = 0;    // resume here
    // Subtitles to start with: "off", a language code ("eng") to pick that language's track,
    // or empty to show only a track marked as forced.
    const char *subtitle_language = nullptr;
};

// Returns 0 when the file played to its end, 1 when playback was stopped, or a negative value
// with `error` describing the failure.
int Play(const Source &source, const Sink &sink, char *error, std::size_t error_capacity);

} // namespace iptv::media

#endif
