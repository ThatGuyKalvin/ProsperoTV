// ProsperoTV - Bounded channel and guide pages for an authenticated phone.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/model.hpp"
#include "tv/platform.hpp"
#include <algorithm>
#include "iptv_json.h"
#include <charconv>
namespace ptv
{
namespace
{
std::string quote(std::string_view text, std::size_t limit = 512)
{
    if (text.size() > limit)
    {
        while (limit && (static_cast<unsigned char>(text[limit]) & 0xc0) == 0x80)
            --limit;
        text = text.substr(0, limit);
    }
    std::string out = "\"";
    for (unsigned char c : text)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        out += c < 32 ? ' ' : static_cast<char>(c);
    }
    return out + '"';
}
} // namespace
int Model::remote_browse(std::string_view input, std::string &output)
{
    output = "Invalid browse request.";
    if (input.empty() || input.size() > 2048)
        return 400;
    std::map<std::string, std::string> fields;
    iptv::json::JsonReader json(input);
    if (!json.Consume('{') || json.Consume('}'))
        return 400;
    do
    {
        std::string key, value;
        if (!json.String(&key, 16) || !json.Consume(':') || !json.StringOrScalar(&value, 256) ||
            fields.contains(key) || fields.size() >= 5 ||
            (key != "operation" && key != "source" && key != "channel" && key != "offset" &&
             key != "query"))
            return 400;
        fields.emplace(std::move(key), std::move(value));
    } while (json.Consume(','));
    if (!json.Consume('}') || !json.Finished())
        return 400;
    std::size_t offset = 0;
    if (fields.contains("offset"))
    {
        const auto &value = fields["offset"];
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), offset);
        if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
            return 400;
    }
    const auto &operation = fields["operation"];
    if (operation != "list" && operation != "guide" && operation != "play")
        return 400;
    if (pin_prompt())
    {
        output = "Finish the parental prompt on the TV first.";
        return 403;
    }
    if (home_waiting_)
    {
        output = "Finish or cancel episode loading on the TV first.";
        return 409;
    }
    if (!has_catalog() || refreshing())
    {
        output = "Wait for the TV channel list to load.";
        return 409;
    }
    const auto source =
        std::to_string(selected_source_id_) + ":" + std::to_string(catalog_.source_id);
    if (operation != "list" || !fields["source"].empty())
        if (fields["source"] != source)
        {
            output = "The source changed. Reload the channel list.";
            return 409;
        }
    const auto allowed = [&](std::size_t index)
    {
        if (index >= catalog_.size())
            return false;
        const auto item = catalog_[index];
        if (item.media_kind != iptv::MediaKind::live || !content_allowed(item) ||
            hidden_channels_.contains(std::string(item.id)) ||
            (hide_failed_ && item.playback_status == iptv::PlaybackStatus::failed) ||
            category_hidden(item.group_title))
            return false;
        for (const auto group : item.alternate_group_titles)
            if (category_hidden(group))
                return false;
        return true;
    };
    const auto now = static_cast<std::int64_t>(platform::unix_time());
    if (operation == "play" || operation == "guide")
    {
        const auto index = catalog_.Find(fields["channel"]);
        if (!allowed(index))
        {
            output = "That channel is no longer available.";
            return 404;
        }
        if (operation == "play")
        {
            if (!play(static_cast<unsigned>(index)))
            {
                output = "The channel could not be opened.";
                return 409;
            }
            output = "{\"playing\":true}";
            return 200;
        }
        const auto programmes = guide_.programmes(fields["channel"]);
        if (offset > programmes.size())
            return 400;
        output = "{\"source\":" + quote(source) + ",\"channel\":" + quote(fields["channel"]) +
                 ",\"name\":" + quote(channel(static_cast<unsigned>(index)).name) +
                 ",\"programmes\":[";
        unsigned count = 0;
        while (offset < programmes.size() && count < 48)
        {
            const auto &p = programmes[offset++];
            if (p.end <= now)
                continue;
            if (count++)
                output += ',';
            output += "{\"start\":" + std::to_string(p.start) +
                      ",\"end\":" + std::to_string(p.end) + ",\"title\":" + quote(p.title) + "}";
        }
        output +=
            "],\"next\":" + (offset < programmes.size() ? std::to_string(offset) : "null") + "}";
        return 200;
    }
    const auto &query = fields["query"];
    if (query.size() > 156 ||
        std::any_of(query.begin(), query.end(),
                    [](unsigned char c) { return c < 32 || c == 127; }) ||
        offset > index_.order.size())
        return 400;
    output = "{\"source\":" + quote(source) + ",\"channels\":[";
    unsigned count = 0;
    while (offset < index_.order.size() && count < 40)
    {
        const auto index = index_.order[offset++];
        if (!allowed(index))
            continue;
        const auto item = channel(index);
        if (!query.empty() && !contains_nocase(item.name, query) &&
            !contains_nocase(item.group_title, query) && !guide_.matches_now(item.id, query, now))
            continue;
        if (count++)
            output += ',';
        const auto *current = guide_.now(item.id, now), *next = guide_.next(item.id, now);
        output += "{\"id\":" + quote(item.id) + ",\"name\":" + quote(item.name) +
                  ",\"group\":" + quote(item.group_title, 256) +
                  ",\"now\":" + quote(current ? current->title : "") +
                  ",\"next\":" + quote(next ? next->title : "") + "}";
        if (output.size() > 96u * 1024u)
            break;
    }
    output +=
        "],\"next\":" + (offset < index_.order.size() ? std::to_string(offset) : "null") + "}";
    return 200;
}
} // namespace ptv
