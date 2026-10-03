// ProsperoTV - What a channel record says, in the words the screens show.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/channel_text.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>

namespace ptv
{

namespace
{

char lower(char value)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

bool is_space(char value)
{
    return std::isspace(static_cast<unsigned char>(value)) != 0;
}

bool is_separator(char value)
{
    return value == ',' || value == ';' || value == '|';
}

std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && is_space(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && is_space(text.back()))
        text.remove_suffix(1);
    return text;
}

// The name, the playlist's own name and the address: where a size or a codec
// is written when it is written anywhere.
bool mentions(const iptv::Channel &channel, std::string_view needle)
{
    return contains_nocase(channel.name, needle) || contains_nocase(channel.tvg_name, needle) ||
           contains_nocase(channel.url, needle);
}

// "1080p", "576i", "2160p": digits and one letter, as a playlist note.
bool is_size_note(std::string_view note)
{
    if (note.size() < 4 || note.size() > 6)
        return false;
    const char last = lower(note.back());
    if (last != 'p' && last != 'i')
        return false;
    for (std::size_t i = 0; i + 1 < note.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(note[i])))
            return false;
    return true;
}

} // namespace

bool contains_nocase(std::string_view text, std::string_view needle)
{
    if (needle.empty())
        return true;
    if (needle.size() > text.size())
        return false;
    for (std::size_t start = 0; start + needle.size() <= text.size(); ++start)
    {
        std::size_t at = 0;
        while (at < needle.size() && lower(text[start + at]) == lower(needle[at]))
            ++at;
        if (at == needle.size())
            return true;
    }
    return false;
}

bool equals_nocase(std::string_view left, std::string_view right)
{
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (lower(left[index]) != lower(right[index]))
            return false;
    return true;
}

bool field_has_value(std::string_view field, std::string_view value)
{
    if (value.empty())
        return true;
    std::size_t start = 0;
    while (start < field.size())
    {
        while (start < field.size() && (is_separator(field[start]) || is_space(field[start])))
            ++start;
        std::size_t end = start;
        while (end < field.size() && !is_separator(field[end]))
            ++end;
        if (equals_nocase(trimmed(field.substr(start, end - start)), value))
            return true;
        start = end + 1u;
    }
    return false;
}

std::string first_value(std::string_view field)
{
    std::size_t end = 0;
    while (end < field.size() && !is_separator(field[end]))
        ++end;
    return std::string(trimmed(field.substr(0, end)));
}

unsigned quality_of(const iptv::Channel &channel)
{
    if (mentions(channel, "2160p") || mentions(channel, "3840x2160") || mentions(channel, " 4k") ||
        mentions(channel, "uhd"))
        return kQualityUhd;
    if (mentions(channel, "1080p") || mentions(channel, "1920x1080") || mentions(channel, "fhd"))
        return kQualityFullHd;
    if (mentions(channel, "720p") || mentions(channel, "1280x720") || mentions(channel, " hd"))
        return kQualityHd;
    if (mentions(channel, "576p") || mentions(channel, "480p") || mentions(channel, "360p") ||
        mentions(channel, "240p") || mentions(channel, " sd"))
        return kQualitySd;
    return kQualityAny;
}

const char *quality_filter_name(unsigned quality)
{
    static constexpr const char *names[kQualityCount] = {"Any quality", "SD", "720p", "1080p",
                                                         "4K"};
    return quality < kQualityCount ? names[quality] : names[0];
}

std::string resolution_label(const iptv::Channel &channel)
{
    static constexpr struct
    {
        const char *needle;
        const char *label;
    } sizes[] = {{"3840x2160", "4K"},  {"2160p", "4K"},        {"2560x1440", "1440p"},
                 {"1440p", "1440p"},   {"1920x1080", "1080p"}, {"1080p", "1080p"},
                 {"1280x720", "720p"}, {"720p", "720p"},       {"720x576", "576p"},
                 {"576p", "576p"},     {"720x480", "480p"},    {"480p", "480p"},
                 {"360p", "360p"},     {"270p", "270p"},       {"240p", "240p"}};
    for (const auto &size : sizes)
        if (mentions(channel, size.needle))
            return size.label;
    return {};
}

const char *codec_label(const iptv::Channel &channel)
{
    if (mentions(channel, ".webm") || mentions(channel, "vp9"))
        return "VP9";
    if (mentions(channel, "hevc") || mentions(channel, "h265") || mentions(channel, "h.265"))
        return "HEVC";
    if (mentions(channel, "h264") || mentions(channel, "h.264") || mentions(channel, "avc"))
        return "H.264";
    return "";
}

std::string display_name(const iptv::Channel &channel, std::vector<std::string> *notes)
{
    std::string_view rest = trimmed(channel.name);
    // Notes sit at the end, each in its own brackets: peel them off from the right.
    for (;;)
    {
        if (rest.empty())
            break;
        const char close = rest.back();
        if (close != ')' && close != ']')
            break;
        const char open = close == ')' ? '(' : '[';
        const std::size_t at = rest.rfind(open);
        // A name that is nothing but a bracket keeps it.
        if (at == std::string_view::npos || at == 0)
            break;
        const std::string_view note = trimmed(rest.substr(at + 1, rest.size() - at - 2));
        // Only the playlist's notes go: a size in round brackets, any remark
        // in square ones. "(HD)" and "(Pluto TV)" are part of the name.
        if (close == ')' && !is_size_note(note))
            break;
        if (notes != nullptr && close == ']' && !note.empty())
            notes->insert(notes->begin(), std::string(note));
        rest = trimmed(rest.substr(0, at));
    }
    if (rest.empty())
        return "Unnamed channel";
    return std::string(rest);
}

std::string monogram(const iptv::Channel &channel)
{
    const std::string &source = !channel.tvg_name.empty() ? channel.tvg_name
                                : !channel.tvg_id.empty() ? channel.tvg_id
                                                          : channel.name;
    std::string letters;
    bool word_start = true;
    for (const char raw : source)
    {
        const unsigned char value = static_cast<unsigned char>(raw);
        const bool letter = value < 0x80 && std::isalnum(value) != 0;
        if (letter && word_start && letters.size() < 2)
            letters.push_back(static_cast<char>(std::toupper(value)));
        word_start = !letter;
    }
    if (letters.size() < 2)
    {
        // One word: its first two letters.
        bool skipped_first = letters.empty();
        for (const char raw : source)
        {
            const unsigned char value = static_cast<unsigned char>(raw);
            if (value >= 0x80 || std::isalnum(value) == 0)
                continue;
            if (!skipped_first)
            {
                skipped_first = true;
                continue;
            }
            letters.push_back(static_cast<char>(std::toupper(value)));
            if (letters.size() == 2)
                break;
        }
    }
    return letters.empty() ? "TV" : letters;
}

std::string category_of(const iptv::Channel &channel)
{
    const std::string first = first_value(channel.group_title);
    return first.empty() ? "Uncategorized" : first;
}

std::string place_line(const iptv::Channel &channel)
{
    std::string line = first_value(channel.tvg_country);
    if (line.empty())
        line = "World";
    const std::string language = first_value(channel.tvg_language);
    if (!language.empty())
        line += "  \xC2\xB7  " + language;
    return line;
}

namespace
{

// One code point of UTF-8; a byte that is not the start of one counts as itself.
std::uint32_t next_code(std::string_view text, std::size_t *index)
{
    const auto byte = [&](std::size_t at) { return static_cast<unsigned char>(text[at]); };
    const std::size_t at = *index;
    const unsigned char lead = byte(at);
    const std::size_t length = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (length == 1 || at + length > text.size())
    {
        ++*index;
        return lead;
    }
    std::uint32_t code = lead & (0xFFu >> (length + 1));
    for (std::size_t i = 1; i < length; ++i)
        code = (code << 6) | (byte(at + i) & 0x3Fu);
    *index += length;
    return code;
}

// The capital A to Z a Latin letter is filed under (accents put aside), or 0.
char latin_base(std::uint32_t code)
{
    if (code < 0x80)
        return std::isalpha(static_cast<int>(code)) != 0
                   ? static_cast<char>(std::toupper(static_cast<int>(code)))
                   : '\0';
    if (code >= 0xC0 && code <= 0xFF)
    {
        // U+00C0..U+00DF; the small letters of U+00E0..U+00FF repeat them.
        static constexpr char kLatin1[] = "AAAAAAACEEEEIIIIDNOOOOO\0OUUUUYTS";
        return code == 0xFF ? 'Y' : kLatin1[(code - 0xC0) % 32];
    }
    if (code >= 0x100 && code <= 0x17F)
    {
        // Latin Extended-A: the last code of each run of one letter.
        static constexpr struct
        {
            std::uint16_t last;
            char base;
        } kRuns[] = {{0x105, 'A'}, {0x10D, 'C'}, {0x111, 'D'}, {0x11B, 'E'}, {0x123, 'G'},
                     {0x127, 'H'}, {0x133, 'I'}, {0x135, 'J'}, {0x138, 'K'}, {0x142, 'L'},
                     {0x14B, 'N'}, {0x153, 'O'}, {0x159, 'R'}, {0x161, 'S'}, {0x167, 'T'},
                     {0x173, 'U'}, {0x175, 'W'}, {0x178, 'Y'}, {0x17E, 'Z'}, {0x17F, 'S'}};
        for (const auto &run : kRuns)
            if (code <= run.last)
                return run.base;
    }
    return '\0';
}

} // namespace

std::string sort_key(const iptv::Channel &channel)
{
    const std::string name = display_name(channel);
    std::string folded;
    folded.reserve(name.size());
    for (std::size_t index = 0; index < name.size();)
    {
        const std::size_t start = index;
        const std::uint32_t code = next_code(name, &index);
        if (const char base = latin_base(code); base != '\0')
            folded.push_back(base);
        else if (code >= 0x80 || std::isdigit(static_cast<int>(code)) != 0)
            folded.append(name, start, index - start);
        else if (!folded.empty() && code == ' ' && folded.back() != ' ')
            folded.push_back(' '); // punctuation is passed over; one space keeps words apart
    }
    if (!folded.empty() && folded.back() == ' ')
        folded.pop_back();
    const char first = folded.empty() ? '\0' : folded.front();
    // '#' holds what starts with a digit or in another script, and sorts first.
    return std::string(1, first >= 'A' && first <= 'Z' ? first : '#') + folded;
}

int letter_of_key(std::string_view key)
{
    return key.empty() || key.front() < 'A' || key.front() > 'Z' ? 0 : key.front() - 'A' + 1;
}

char letter_char(int letter)
{
    return letter >= 1 && letter < kLetterCount ? static_cast<char>('A' + letter - 1) : '#';
}

std::string group_digits(unsigned value)
{
    char digits[16];
    std::snprintf(digits, sizeof(digits), "%u", value);
    std::string text(digits);
    for (int at = static_cast<int>(text.size()) - 3; at > 0; at -= 3)
        text.insert(static_cast<std::size_t>(at), ",");
    return text;
}

} // namespace ptv
