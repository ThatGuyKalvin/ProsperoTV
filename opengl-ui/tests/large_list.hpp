// ProsperoTV - A playlist the size a large provider sends, for the tests.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdio>
#include <string>

namespace large_list
{

// How many of `count` channels have `index % every == rest`.
constexpr unsigned share(unsigned count, unsigned every, unsigned rest)
{
    return count / every + (count % every > rest ? 1u : 0u);
}

// Channel `index` is "<letter>-Net <index>": the letters A to Z in turn, so
// every letter of the alphabet has its share and a channel's place in the
// order of the alphabet can be worked out. One in eight is sport, one news,
// one for children; one in five says FHD and one HD; four countries.
inline std::string playlist(unsigned count)
{
    static constexpr const char *kCountries[] = {"US", "GB", "DE", "FR"};
    static constexpr const char *kSizes[] = {" FHD", " HD", "", "", ""};
    std::string text = "#EXTM3U\n";
    text.reserve(static_cast<std::size_t>(count) * 300u);
    char group[48];
    char entry[512];
    for (unsigned index = 0; index < count; ++index)
    {
        switch (index % 8u)
        {
        case 0:
            std::snprintf(group, sizeof(group), "Sports | UK");
            break;
        case 1:
            std::snprintf(group, sizeof(group), "News");
            break;
        case 2:
            std::snprintf(group, sizeof(group), "Kids & Family");
            break;
        default:
            std::snprintf(group, sizeof(group), "General %u", index % 500u);
            break;
        }
        std::snprintf(entry, sizeof(entry),
                      "#EXTINF:-1 tvg-id=\"c%u.example\" tvg-name=\"%c-Net %06u%s\" "
                      "tvg-logo=\"http://logos.provider.example:8080/images/channels/logo_%u.png\" "
                      "tvg-country=\"%s\" group-title=\"%s\",%c-Net %06u%s\n"
                      "http://line.provider.example:8080/someuser1234/somepass5678/%u\n",
                      index, 'A' + static_cast<int>(index % 26u), index, kSizes[index % 5u], index,
                      kCountries[index % 4u], group, 'A' + static_cast<int>(index % 26u), index,
                      kSizes[index % 5u], 100000u + index);
        text += entry;
    }
    return text;
}

} // namespace large_list
