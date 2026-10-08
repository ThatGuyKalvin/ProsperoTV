/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_XMLTV_H
#define IPTV_XMLTV_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// A streaming reader for XMLTV guides (<tv><programme start stop channel><title><desc>).
// Guides are fed in pieces as they download, so nothing larger than one tag is buffered.
// Only what a TV guide shows is read: each programme's channel, times, title and
// description. <channel> elements and other programme details are skipped.
namespace iptv::xmltv
{

inline constexpr std::size_t kMaxTitleBytes = 200u;
inline constexpr std::size_t kMaxDescriptionBytes = 600u;
inline constexpr std::size_t kMaxChannelBytes = 128u;
// The longest single construct (tag, comment, CDATA section) accepted.
inline constexpr std::size_t kMaxConstructBytes = 64u * 1024u;

struct Programme
{
    std::string channel;
    std::int64_t start = 0; // Unix seconds, UTC
    std::int64_t stop = 0;
    std::string title;
    std::string description;
};

enum class Status : std::uint8_t
{
    ok,
    malformed, // unterminated or oversized markup
    stopped,   // the callback asked to stop
};

struct Report
{
    std::uint64_t programmes = 0; // passed to the callback
    std::uint64_t skipped = 0;    // missing channel, bad times or no title
};

// Returns false to stop parsing.
using ProgrammeFunction = bool (*)(void *context, const Programme &programme);

// "20261005143000 +0100" -> Unix seconds. Seconds and the offset are optional; a time
// without an offset is UTC.
bool ParseTime(std::string_view text, std::int64_t *unix_seconds);

class Parser
{
  public:
    Parser(ProgrammeFunction on_programme, void *context);

    Status Feed(const char *data, std::size_t size);
    // Call once after the last piece.
    Status Finish();
    const Report &report() const
    {
        return report_;
    }

  private:
    enum class Field : std::uint8_t
    {
        none,
        title,
        description,
    };

    Status Process(bool final);
    bool Construct(std::string_view construct);
    void Text(std::string_view text, bool raw);
    bool StartTag(std::string_view body);
    bool EndTag(std::string_view name);

    ProgrammeFunction on_programme_;
    void *context_;
    std::string pending_;
    Status status_ = Status::ok;
    Report report_;
    bool in_programme_ = false;
    bool programme_valid_ = false;
    bool have_title_ = false;
    bool have_description_ = false;
    Field field_ = Field::none;
    Programme programme_;
};

} // namespace iptv::xmltv

#endif
