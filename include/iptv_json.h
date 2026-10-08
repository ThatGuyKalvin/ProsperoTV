/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_JSON_H
#define IPTV_JSON_H

#include "iptv_catalog.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

// Bounded JSON pull parser and the small text helpers shared by the account sources
// (Xtream accounts). Header-only so that every build that compiles iptv_xtream.cpp
// keeps working without a new source file.
namespace iptv::json
{

inline constexpr std::size_t kMaxJsonDepth = 24u;

inline bool EqualsCi(std::string_view left, std::string_view right)
{
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index])))
            return false;
    return true;
}

inline bool EndsWithCi(std::string_view value, std::string_view suffix)
{
    return value.size() >= suffix.size() &&
           EqualsCi(value.substr(value.size() - suffix.size()), suffix);
}

inline bool SafeCredential(std::string_view value, std::size_t maximum)
{
    if (value.empty() || value.size() > maximum)
        return false;
    for (const unsigned char byte : value)
        if (byte < 0x20u || byte == 0x7fu)
            return false;
    return true;
}

// Keeps [A-Za-z0-9-_.~] and encodes every other byte as %XX. Valid in URL paths, query
// values and application/x-www-form-urlencoded bodies.
inline bool PercentEncode(std::string_view input, std::string *output)
{
    if (!output)
        return false;
    static constexpr char digits[] = "0123456789ABCDEF";
    output->clear();
    output->reserve(input.size() * 3u);
    for (const unsigned char byte : input)
    {
        if (std::isalnum(byte) || byte == '-' || byte == '_' || byte == '.' || byte == '~')
        {
            output->push_back(static_cast<char>(byte));
        }
        else
        {
            output->push_back('%');
            output->push_back(digits[byte >> 4u]);
            output->push_back(digits[byte & 0x0fu]);
        }
    }
    return true;
}

// Moves a fully written temporary file over the destination.
inline bool ReplaceFile(const std::string &temporary, const std::string &path)
{
#ifdef _WIN32
    return MoveFileExA(temporary.c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(temporary.c_str(), path.c_str()) == 0;
#endif
}

inline int HexDigit(char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

inline bool AppendUtf8(std::uint32_t codepoint, std::string *output, std::size_t maximum)
{
    if (!output)
        return true;
    if (codepoint < 0x20u || codepoint > 0x10ffffu ||
        (codepoint >= 0xd800u && codepoint <= 0xdfffu))
        return false;
    const std::size_t bytes = codepoint < 0x80u      ? 1u
                              : codepoint < 0x800u   ? 2u
                              : codepoint < 0x10000u ? 3u
                                                     : 4u;
    if (output->size() + bytes > maximum)
        return false;
    char encoded[4]{};
    if (bytes == 1u)
        encoded[0] = static_cast<char>(codepoint);
    else if (bytes == 2u)
    {
        encoded[0] = static_cast<char>(0xc0u | (codepoint >> 6u));
        encoded[1] = static_cast<char>(0x80u | (codepoint & 0x3fu));
    }
    else if (bytes == 3u)
    {
        encoded[0] = static_cast<char>(0xe0u | (codepoint >> 12u));
        encoded[1] = static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu));
        encoded[2] = static_cast<char>(0x80u | (codepoint & 0x3fu));
    }
    else
    {
        encoded[0] = static_cast<char>(0xf0u | (codepoint >> 18u));
        encoded[1] = static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu));
        encoded[2] = static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu));
        encoded[3] = static_cast<char>(0x80u | (codepoint & 0x3fu));
    }
    output->append(encoded, bytes);
    return true;
}

class JsonReader
{
  public:
    explicit JsonReader(std::string_view input) : input_(input)
    {
    }

    void Whitespace()
    {
        while (position_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[position_])))
            ++position_;
    }

    char Peek()
    {
        Whitespace();
        return position_ < input_.size() ? input_[position_] : '\0';
    }

    bool Consume(char expected)
    {
        Whitespace();
        if (position_ >= input_.size() || input_[position_] != expected)
            return false;
        ++position_;
        return true;
    }

    bool Finished()
    {
        Whitespace();
        return position_ == input_.size();
    }

    bool String(std::string *output, std::size_t maximum = kDefaultMaxFieldBytes)
    {
        Whitespace();
        if (position_ >= input_.size() || input_[position_++] != '"')
            return false;
        if (output)
            output->clear();
        while (position_ < input_.size())
        {
            const unsigned char byte = static_cast<unsigned char>(input_[position_++]);
            if (byte == '"')
                return true;
            if (byte < 0x20u)
                return false;
            if (byte != '\\')
            {
                if (output)
                {
                    if (output->size() >= maximum)
                        return false;
                    output->push_back(static_cast<char>(byte));
                }
                continue;
            }
            if (position_ >= input_.size())
                return false;
            const char escaped = input_[position_++];
            char simple = '\0';
            switch (escaped)
            {
            case '"':
            case '\\':
            case '/':
                simple = escaped;
                break;
            case 'b':
                simple = ' ';
                break;
            case 'f':
                simple = ' ';
                break;
            case 'n':
                simple = ' ';
                break;
            case 'r':
                simple = ' ';
                break;
            case 't':
                simple = ' ';
                break;
            case 'u':
            {
                std::uint32_t codepoint = 0;
                if (!UnicodeEscape(&codepoint))
                    return false;
                if (codepoint >= 0xd800u && codepoint <= 0xdbffu)
                {
                    if (position_ + 2u > input_.size() || input_[position_] != '\\' ||
                        input_[position_ + 1u] != 'u')
                        return false;
                    position_ += 2u;
                    std::uint32_t low = 0;
                    if (!UnicodeEscape(&low) || low < 0xdc00u || low > 0xdfffu)
                        return false;
                    codepoint = 0x10000u + ((codepoint - 0xd800u) << 10u) + (low - 0xdc00u);
                }
                else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu)
                {
                    return false;
                }
                if (!AppendUtf8(codepoint, output, maximum))
                    return false;
                continue;
            }
            default:
                return false;
            }
            if (output)
            {
                if (output->size() >= maximum)
                    return false;
                output->push_back(simple);
            }
        }
        return false;
    }

    bool Scalar(std::string *output)
    {
        Whitespace();
        const std::size_t start = position_;
        while (position_ < input_.size())
        {
            const char value = input_[position_];
            if (value == ',' || value == ']' || value == '}' ||
                std::isspace(static_cast<unsigned char>(value)))
                break;
            ++position_;
        }
        if (position_ == start)
            return false;
        if (output)
            *output = std::string(input_.substr(start, position_ - start));
        return true;
    }

    bool StringOrScalar(std::string *output, std::size_t maximum = kDefaultMaxFieldBytes)
    {
        if (Peek() == '"')
            return String(output, maximum);
        if (!Scalar(output))
            return false;
        return !output || output->size() <= maximum;
    }

    bool SkipValue(std::size_t depth = 0)
    {
        if (depth >= kMaxJsonDepth)
            return false;
        const char value = Peek();
        if (value == '"')
            return String(nullptr);
        if (value == '{')
        {
            if (!Consume('{'))
                return false;
            if (Consume('}'))
                return true;
            for (;;)
            {
                if (!String(nullptr) || !Consume(':') || !SkipValue(depth + 1u))
                    return false;
                if (Consume('}'))
                    return true;
                if (!Consume(','))
                    return false;
            }
        }
        if (value == '[')
        {
            if (!Consume('['))
                return false;
            if (Consume(']'))
                return true;
            for (;;)
            {
                if (!SkipValue(depth + 1u))
                    return false;
                if (Consume(']'))
                    return true;
                if (!Consume(','))
                    return false;
            }
        }
        return Scalar(nullptr);
    }

  private:
    bool UnicodeEscape(std::uint32_t *codepoint)
    {
        if (!codepoint || position_ + 4u > input_.size())
            return false;
        std::uint32_t value = 0;
        for (unsigned index = 0; index < 4u; ++index)
        {
            const int digit = HexDigit(input_[position_++]);
            if (digit < 0)
                return false;
            value = (value << 4u) | static_cast<std::uint32_t>(digit);
        }
        *codepoint = value;
        return true;
    }

    std::string_view input_;
    std::size_t position_ = 0;
};

template <typename Handler> bool ReadObject(JsonReader *reader, Handler handler)
{
    if (!reader || !reader->Consume('{'))
        return false;
    if (reader->Consume('}'))
        return true;
    for (;;)
    {
        std::string key;
        if (!reader->String(&key, 128u) || !reader->Consume(':') || !handler(key, reader))
            return false;
        if (reader->Consume('}'))
            return true;
        if (!reader->Consume(','))
            return false;
    }
}

template <typename Handler> bool ReadArray(JsonReader *reader, Handler handler)
{
    if (!reader || !reader->Consume('['))
        return false;
    if (reader->Consume(']'))
        return true;
    for (;;)
    {
        if (!handler(reader))
            return false;
        if (reader->Consume(']'))
            return true;
        if (!reader->Consume(','))
            return false;
    }
}

// Reads a whole response that is either a top-level array or an object whose `key` member
// is the array ("data" or "content" on some panels).
template <typename Handler>
bool ReadArrayResponse(JsonReader *reader, Handler handler, bool *found,
                       std::string_view wrapper_key = "data")
{
    if (!reader || !found)
        return false;
    *found = false;
    if (reader->Peek() == '[')
    {
        *found = true;
        return ReadArray(reader, handler) && reader->Finished();
    }
    if (reader->Peek() != '{')
        return false;
    const bool valid = ReadObject(reader,
                                  [&](const std::string &key, JsonReader *value)
                                  {
                                      if (key == wrapper_key && value->Peek() == '[')
                                      {
                                          *found = true;
                                          return ReadArray(value, handler);
                                      }
                                      return value->SkipValue();
                                  });
    return valid && reader->Finished();
}

inline std::uint32_t ParseUnsigned(std::string_view text, std::uint32_t maximum)
{
    std::uint64_t value = 0;
    for (const char character : text)
    {
        if (character < '0' || character > '9')
            break;
        value = value * 10u + static_cast<std::uint64_t>(character - '0');
        if (value >= maximum)
            return maximum;
    }
    return static_cast<std::uint32_t>(value);
}

// Finds the first run of four digits that looks like a release year ("2021", "2021-05-01").
inline std::uint16_t ParseYear(std::string_view text)
{
    for (std::size_t i = 0; i + 4u <= text.size(); ++i)
    {
        if (!std::isdigit(static_cast<unsigned char>(text[i])))
            continue;
        std::size_t end = i;
        while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])))
            ++end;
        if (end - i == 4u)
        {
            const std::uint32_t year = ParseUnsigned(text.substr(i, 4u), 9999u);
            return year >= 1850u && year <= 2200u ? static_cast<std::uint16_t>(year) : 0u;
        }
        i = end;
    }
    return 0;
}

// "7.5" -> 75, "8" -> 80; anything else (including "N/A") -> 0.
inline std::uint16_t ParseRatingTenths(std::string_view text)
{
    if (text.empty() || !std::isdigit(static_cast<unsigned char>(text.front())))
        return 0;
    const std::size_t dot = text.find('.');
    const std::uint32_t whole = ParseUnsigned(text.substr(0, dot), 10u);
    std::uint32_t tenths = 0;
    if (dot != std::string_view::npos && dot + 1u < text.size() &&
        std::isdigit(static_cast<unsigned char>(text[dot + 1u])))
        tenths = static_cast<std::uint32_t>(text[dot + 1u] - '0');
    return static_cast<std::uint16_t>(std::min(whole * 10u + tenths, 100u));
}

inline bool ValidExtension(std::string_view extension)
{
    return extension.size() <= 12u &&
           std::all_of(extension.begin(), extension.end(),
                       [](unsigned char value) { return std::isalnum(value) != 0; });
}

} // namespace iptv::json

#endif
