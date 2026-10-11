// ProsperoTV - Console-language mapping and stable translation fallback.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/i18n.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <string_view>

namespace
{
struct Translation
{
    const char *english;
    std::array<const char *, 6> texts;
};
const Translation translations[] = {
#include "tv/translations.inc"
#include "tv/translations_share.inc"
};
struct RtlTranslation
{
    const char *english;
    std::array<const char *, 2> texts;
};
const RtlTranslation rtl_translations[] = {
#include "tv/translations_rtl.inc"
};

TEST(InterfaceLanguage, RtlCatalogCoversEveryKeyWithoutChangingFormatOrFragments)
{
    std::set<std::string_view> expected, actual;
    for (const auto &entry : translations)
        expected.emplace(entry.english);
    for (const auto &entry : rtl_translations)
    {
        const std::string_view key(entry.english);
        ASSERT_TRUE(actual.emplace(key).second) << key;
        for (const char *translated : entry.texts)
        {
            ASSERT_NE(translated, nullptr) << key;
            const std::string_view text(translated);
            ASSERT_FALSE(text.empty()) << key;
            EXPECT_EQ(std::count(key.begin(), key.end(), '%'),
                      std::count(text.begin(), text.end(), '%'))
                << key;
            EXPECT_EQ(key.find_first_not_of(' '), text.find_first_not_of(' ')) << key;
            EXPECT_EQ(key.size() - key.find_last_not_of(' '),
                      text.size() - text.find_last_not_of(' '))
                << key;
            // Literal percentages (25%, 125%, 150%) are not printf placeholders.
            // These are the conversion specifiers used by the current catalog.
            for (const std::string_view token : {"%u", "%d", "%s", "%%"})
            {
                const auto count = [token](std::string_view value)
                {
                    std::size_t result = 0, pos = 0;
                    while ((pos = value.find(token, pos)) != std::string_view::npos)
                    {
                        ++result;
                        pos += token.size();
                    }
                    return result;
                };
                EXPECT_EQ(count(key), count(text)) << key << " token " << token;
            }
            // Keep logical Unicode order; do not bake visual order or bidi control
            // overrides into translations. Direction belongs to the renderer.
            for (const std::string_view forbidden :
                 {"\xef\xbf\xbd", "\xe2\x80\xaa", "\xe2\x80\xab", "\xe2\x80\xac", "\xe2\x80\xad",
                  "\xe2\x80\xae"})
                EXPECT_EQ(text.find(forbidden), std::string_view::npos) << key;
        }
    }
    EXPECT_EQ(expected, actual);
}

std::string placeholders(std::string_view text)
{
    std::string result;
    for (std::size_t pos = 0; pos < text.size(); ++pos)
        if (text[pos] == '%')
        {
            result += '%';
            if (++pos < text.size())
                result += text[pos];
        }
    return result;
}

TEST(InterfaceLanguage, RegionalVariantsAndUnknownValues)
{
    for (const int id : {3, 20})
    {
        ptv::set_console_language(id);
        EXPECT_STREQ(ptv::interface_language(), "es");
        EXPECT_STREQ(ptv::tr("Settings"), "Ajustes");
    }
    for (const int id : {7, 17})
    {
        ptv::set_console_language(id);
        EXPECT_STREQ(ptv::interface_language(), "pt");
    }
    for (const int id : {2, 22})
    {
        ptv::set_console_language(id);
        EXPECT_STREQ(ptv::interface_language(), "fr");
    }
    for (const int id : {-1, 1, 18, 999})
    {
        ptv::set_console_language(id);
        EXPECT_STREQ(ptv::interface_language(), "en");
        EXPECT_STREQ(ptv::tr("Settings"), "Settings");
    }
    EXPECT_STREQ(ptv::tr(nullptr), "");
}

TEST(InterfaceLanguage, EveryEntryHasAllTranslationsAndUnknownTextSurvives)
{
    std::set<std::string_view> keys;
    for (const auto &entry : translations)
        EXPECT_TRUE(keys.emplace(entry.english).second) << entry.english;
    const int languages[] = {3, 2, 4, 5, 7, 6};
    for (unsigned language = 0; language < std::size(languages); ++language)
    {
        ptv::set_console_language(languages[language]);
        for (const auto &entry : translations)
        {
            const auto text = entry.texts[language];
            EXPECT_TRUE(text && *text);
            if (text)
            {
                EXPECT_STREQ(ptv::tr(entry.english), text);
                EXPECT_EQ(placeholders(entry.english), placeholders(text)) << entry.english;
            }
        }
        EXPECT_STREQ(ptv::tr("Provider Channel 1"), "Provider Channel 1");
    }
    ptv::set_console_language(1);
}
} // namespace
