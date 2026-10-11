// ProsperoTV - A shared, immutable menu translation catalogue.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/i18n.hpp"
#include <array>
#include <atomic>
#include <map>
#include <string_view>

namespace ptv
{
namespace
{
std::atomic<unsigned> selected{0};
unsigned console_index = 0;
constexpr bool kRtlReady = true;
constexpr const char *codes[] = {"en", "es", "fr", "de", "it", "pt", "nl", "ar", "he"};
} // namespace

void set_console_language(int language)
{
    // SystemService's public language IDs, including regional variants:
    // OpenOrbis/include/orbis/_types/sys_service.h (OrbisSystemParamLanguage).
    unsigned index = 0;
    switch (language)
    {
    case 3:
    case 20:
        index = 1;
        break;
    case 2:
    case 22:
        index = 2;
        break;
    case 4:
        index = 3;
        break;
    case 5:
        index = 4;
        break;
    case 7:
    case 17:
        index = 5;
        break;
    case 6:
        index = 6;
        break;
    case 21:
        index = kRtlReady ? 7 : 0;
        break;
    default:
        break;
    }
    console_index = index;
    selected.store(index);
}

void set_interface_language(unsigned choice)
{
    const unsigned index = choice ? choice - 1 : console_index;
    selected.store(index < 9 && (index < 7 || kRtlReady) ? index : 0);
}
bool interface_rtl()
{
    return selected.load() >= 7;
}

const char *interface_language()
{
    return codes[selected.load()];
}

const char *tr(const char *english)
{
    const unsigned index = selected.load();
    if (!english)
        return "";
    if (!index)
        return english;
    if (index >= 7)
    {
        static const std::map<std::string_view, std::array<const char *, 2>, std::less<>>
            rtl_entries = {
#include "tv/translations_rtl.inc"
            };
        const auto found = rtl_entries.find(english);
        return found == rtl_entries.end() ? english : found->second[index - 7];
    }
    static const std::map<std::string_view, std::array<const char *, 6>, std::less<>> entries = {
#include "tv/translations.inc"
#include "tv/translations_share.inc"
    };
    const auto found = entries.find(english);
    if (found == entries.end())
        return english;
    const auto text = found->second[index - 1];
    return text && *text ? text : english;
}
} // namespace ptv
