/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_xmltv.h"

#include <cctype>
#include <cstring>

namespace iptv::xmltv
{
namespace
{

bool IsSpace(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

void AppendUtf8(std::uint32_t codepoint, std::string *output)
{
    if (codepoint == 0 || codepoint > 0x10ffffu || (codepoint >= 0xd800u && codepoint <= 0xdfffu))
        return;
    if (codepoint < 0x80u)
        output->push_back(static_cast<char>(codepoint));
    else if (codepoint < 0x800u)
    {
        output->push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
        output->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    }
    else if (codepoint < 0x10000u)
    {
        output->push_back(static_cast<char>(0xe0u | (codepoint >> 12u)));
        output->push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
        output->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    }
    else
    {
        output->push_back(static_cast<char>(0xf0u | (codepoint >> 18u)));
        output->push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu)));
        output->push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
        output->push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    }
}

// Decodes XML entities. Unknown ones are kept as written.
std::string Decode(std::string_view text)
{
    std::string output;
    output.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        if (text[index] != '&')
        {
            output.push_back(text[index]);
            continue;
        }
        const std::size_t end = text.find(';', index);
        if (end == std::string_view::npos || end - index > 10u)
        {
            output.push_back('&');
            continue;
        }
        const std::string_view name = text.substr(index + 1u, end - index - 1u);
        if (name == "amp")
            output.push_back('&');
        else if (name == "lt")
            output.push_back('<');
        else if (name == "gt")
            output.push_back('>');
        else if (name == "quot")
            output.push_back('"');
        else if (name == "apos")
            output.push_back('\'');
        else if (name.size() > 1u && name[0] == '#')
        {
            const bool hex = name[1] == 'x' || name[1] == 'X';
            std::uint32_t value = 0;
            bool valid = name.size() > (hex ? 2u : 1u);
            for (std::size_t digit = hex ? 2u : 1u; valid && digit < name.size(); ++digit)
            {
                const char character = name[digit];
                const int number =
                    std::isdigit(static_cast<unsigned char>(character)) ? character - '0'
                    : hex && std::isxdigit(static_cast<unsigned char>(character))
                        ? (std::tolower(static_cast<unsigned char>(character)) - 'a' + 10)
                        : -1;
                valid = number >= 0 && value < 0x110000u;
                value = value * (hex ? 16u : 10u) + static_cast<std::uint32_t>(number);
            }
            if (!valid)
            {
                output.append(text.substr(index, end - index + 1u));
                index = end;
                continue;
            }
            AppendUtf8(value, &output);
        }
        else
        {
            output.append(text.substr(index, end - index + 1u));
        }
        index = end;
    }
    return output;
}

// Appends text, folding runs of whitespace into one space and stopping at `maximum` bytes
// without splitting a UTF-8 sequence.
void AppendBounded(std::string *target, std::string_view text, std::size_t maximum)
{
    for (const char character : text)
    {
        if (IsSpace(character))
        {
            if (!target->empty() && target->back() != ' ' && target->size() < maximum)
                target->push_back(' ');
            continue;
        }
        if (target->size() >= maximum)
        {
            // Drop a partial multi-byte character at the cut.
            while (!target->empty() &&
                   (static_cast<unsigned char>(target->back()) & 0xc0u) == 0x80u)
                target->pop_back();
            if (!target->empty() && (static_cast<unsigned char>(target->back()) & 0x80u))
                target->pop_back();
            return;
        }
        target->push_back(character);
    }
}

std::string Trimmed(std::string value)
{
    while (!value.empty() && value.back() == ' ')
        value.pop_back();
    return value;
}

// Reads name="value" pairs from a start tag's body (after the element name).
template <typename Handler> void Attributes(std::string_view body, Handler handler)
{
    std::size_t index = 0;
    while (index < body.size())
    {
        while (index < body.size() && IsSpace(body[index]))
            ++index;
        const std::size_t name_start = index;
        while (index < body.size() && body[index] != '=' && !IsSpace(body[index]))
            ++index;
        const std::string_view name = body.substr(name_start, index - name_start);
        while (index < body.size() && IsSpace(body[index]))
            ++index;
        if (index >= body.size() || body[index] != '=')
        {
            ++index;
            continue;
        }
        ++index;
        while (index < body.size() && IsSpace(body[index]))
            ++index;
        if (index >= body.size() || (body[index] != '"' && body[index] != '\''))
            continue;
        const char quote = body[index++];
        const std::size_t value_end = body.find(quote, index);
        if (value_end == std::string_view::npos)
            return;
        handler(name, body.substr(index, value_end - index));
        index = value_end + 1u;
    }
}

// Howard Hinnant's days_from_civil: days since 1970-01-01 for a proleptic Gregorian date.
std::int64_t DaysFromCivil(std::int64_t year, int month, int day)
{
    year -= month <= 2 ? 1 : 0;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const std::int64_t year_of_era = year - era * 400;
    const std::int64_t day_of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const std::int64_t day_of_era =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

} // namespace

bool ParseTime(std::string_view text, std::int64_t *unix_seconds)
{
    if (!unix_seconds)
        return false;
    std::size_t digits = 0;
    while (digits < text.size() && digits < 14u &&
           std::isdigit(static_cast<unsigned char>(text[digits])))
        ++digits;
    if (digits != 12u && digits != 14u)
        return false;
    const auto number = [&](std::size_t start, std::size_t count)
    {
        unsigned value = 0;
        for (std::size_t index = start; index < start + count; ++index)
            value = value * 10u + static_cast<unsigned>(text[index] - '0');
        return value;
    };
    const unsigned year = number(0, 4), month = number(4, 2), day = number(6, 2);
    const unsigned hour = number(8, 2), minute = number(10, 2);
    const unsigned second = digits == 14u ? number(12, 2) : 0u;
    if (year < 1970u || month < 1u || month > 12u || day < 1u || day > 31u || hour > 23u ||
        minute > 59u || second > 60u)
        return false;
    std::int64_t seconds =
        DaysFromCivil(year, static_cast<int>(month), static_cast<int>(day)) * 86400 +
        static_cast<std::int64_t>(hour) * 3600 + static_cast<std::int64_t>(minute) * 60 +
        static_cast<std::int64_t>(second);
    std::string_view rest = text.substr(digits);
    while (!rest.empty() && IsSpace(rest.front()))
        rest.remove_prefix(1);
    if (rest.size() >= 5u && (rest[0] == '+' || rest[0] == '-') &&
        std::isdigit(static_cast<unsigned char>(rest[1])) &&
        std::isdigit(static_cast<unsigned char>(rest[2])) &&
        std::isdigit(static_cast<unsigned char>(rest[3])) &&
        std::isdigit(static_cast<unsigned char>(rest[4])))
    {
        const int offset = ((rest[1] - '0') * 10 + (rest[2] - '0')) * 3600 +
                           ((rest[3] - '0') * 10 + (rest[4] - '0')) * 60;
        // Local time = UTC + offset.
        seconds += rest[0] == '+' ? -offset : offset;
    }
    *unix_seconds = seconds;
    return true;
}

Parser::Parser(ProgrammeFunction on_programme, void *context)
    : on_programme_(on_programme), context_(context)
{
}

Status Parser::Feed(const char *data, std::size_t size)
{
    if (status_ != Status::ok)
        return status_;
    if (data && size)
        pending_.append(data, size);
    return Process(false);
}

Status Parser::Finish()
{
    if (status_ != Status::ok)
        return status_;
    status_ = Process(true);
    // Markup left open at the end of the guide.
    if (status_ == Status::ok && pending_.find('<') != std::string::npos)
        status_ = Status::malformed;
    pending_.clear();
    return status_;
}

Status Parser::Process(bool final)
{
    std::size_t position = 0;
    while (position < pending_.size() && status_ == Status::ok)
    {
        const std::size_t open = pending_.find('<', position);
        if (open == std::string::npos)
        {
            // Keep an entity that may continue in the next piece ("&am" + "p;").
            std::size_t end = pending_.size();
            const std::size_t ampersand = pending_.rfind('&');
            if (!final && ampersand != std::string::npos && ampersand >= position &&
                pending_.find(';', ampersand) == std::string::npos &&
                pending_.size() - ampersand <= 10u)
                end = ampersand;
            Text(std::string_view(pending_).substr(position, end - position), false);
            position = end;
            break;
        }
        if (open > position)
            Text(std::string_view(pending_).substr(position, open - position), false);
        position = open;
        const std::string_view rest = std::string_view(pending_).substr(open);
        std::size_t end = std::string_view::npos;
        std::size_t skip = 0;
        if (rest.substr(0, 4) == "<!--")
        {
            end = rest.find("-->", 4);
            skip = 3;
        }
        else if (rest.substr(0, 9) == "<![CDATA[")
        {
            end = rest.find("]]>", 9);
            skip = 3;
        }
        else if (rest.size() < 9u && std::string_view("<![CDATA[").substr(0, rest.size()) == rest)
        {
            end = std::string_view::npos; // need more input to tell
        }
        else
        {
            // A tag ends at the first '>' outside a quoted attribute value.
            char quote = 0;
            for (std::size_t index = 1; index < rest.size(); ++index)
            {
                const char character = rest[index];
                if (quote)
                {
                    if (character == quote)
                        quote = 0;
                }
                else if (character == '"' || character == '\'')
                    quote = character;
                else if (character == '>')
                {
                    end = index;
                    break;
                }
            }
            skip = 1;
        }
        if (end == std::string_view::npos)
        {
            if (rest.size() > kMaxConstructBytes || final)
                status_ = Status::malformed;
            break;
        }
        if (!Construct(rest.substr(0, end + skip)))
            break;
        position = open + end + skip;
    }
    pending_.erase(0, position);
    if (pending_.size() > kMaxConstructBytes)
        status_ = Status::malformed;
    return status_;
}

bool Parser::Construct(std::string_view construct)
{
    if (construct.substr(0, 4) == "<!--" || construct.substr(0, 2) == "<?" ||
        (construct.substr(0, 2) == "<!" && construct.substr(0, 9) != "<![CDATA["))
        return true;
    if (construct.substr(0, 9) == "<![CDATA[")
    {
        Text(construct.substr(9, construct.size() - 12u), true);
        return true;
    }
    std::string_view body = construct.substr(1, construct.size() - 2u);
    if (!body.empty() && body.front() == '/')
    {
        body.remove_prefix(1);
        while (!body.empty() && IsSpace(body.back()))
            body.remove_suffix(1);
        return EndTag(body);
    }
    const bool empty_element = !body.empty() && body.back() == '/';
    if (empty_element)
        body.remove_suffix(1);
    if (!StartTag(body))
        return false;
    if (empty_element)
    {
        std::size_t name_end = 0;
        while (name_end < body.size() && !IsSpace(body[name_end]))
            ++name_end;
        return EndTag(body.substr(0, name_end));
    }
    return true;
}

void Parser::Text(std::string_view text, bool raw)
{
    if (!in_programme_ || field_ == Field::none)
        return;
    const std::string decoded = raw ? std::string(text) : Decode(text);
    if (field_ == Field::title)
        AppendBounded(&programme_.title, decoded, kMaxTitleBytes);
    else
        AppendBounded(&programme_.description, decoded, kMaxDescriptionBytes);
}

bool Parser::StartTag(std::string_view body)
{
    std::size_t name_end = 0;
    while (name_end < body.size() && !IsSpace(body[name_end]))
        ++name_end;
    const std::string_view name = body.substr(0, name_end);
    if (name == "programme")
    {
        in_programme_ = true;
        programme_ = {};
        have_title_ = have_description_ = false;
        field_ = Field::none;
        bool start = false, stop = false;
        Attributes(body.substr(name_end),
                   [&](std::string_view key, std::string_view value)
                   {
                       if (key == "channel")
                           programme_.channel = Decode(value);
                       else if (key == "start")
                           start = ParseTime(value, &programme_.start);
                       else if (key == "stop")
                           stop = ParseTime(value, &programme_.stop);
                   });
        programme_valid_ = start && stop && programme_.stop > programme_.start &&
                           !programme_.channel.empty() &&
                           programme_.channel.size() <= kMaxChannelBytes;
        return true;
    }
    if (!in_programme_)
        return true;
    // The first title and description win; providers sometimes add translations.
    if (name == "title" && !have_title_)
        field_ = Field::title;
    else if (name == "desc" && !have_description_)
        field_ = Field::description;
    else
        field_ = Field::none;
    return true;
}

bool Parser::EndTag(std::string_view name)
{
    if (!in_programme_)
        return true;
    if (name == "title" && field_ == Field::title)
    {
        have_title_ = true;
        field_ = Field::none;
        return true;
    }
    if (name == "desc" && field_ == Field::description)
    {
        have_description_ = true;
        field_ = Field::none;
        return true;
    }
    if (name != "programme")
        return true;
    in_programme_ = false;
    field_ = Field::none;
    programme_.title = Trimmed(std::move(programme_.title));
    programme_.description = Trimmed(std::move(programme_.description));
    if (!programme_valid_ || programme_.title.empty())
    {
        ++report_.skipped;
        return true;
    }
    ++report_.programmes;
    if (on_programme_ && !on_programme_(context_, programme_))
    {
        status_ = Status::stopped;
        return false;
    }
    return true;
}

} // namespace iptv::xmltv
