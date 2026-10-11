// ProsperoTV - Home lists and saved on-demand playback.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/model.hpp"
#include "tv/i18n.hpp"
#include "tv/platform.hpp"
#include <algorithm>

namespace ptv
{
std::array<std::vector<Model::HomeItem>, 3> Model::home_items() const
{
    std::array<std::vector<HomeItem>, 3> rows;
    const auto live = [&](unsigned at)
    {
        const auto item = channel(at);
        const auto *programme =
            guide_.now(item.id, static_cast<std::int64_t>(platform::unix_time()));
        return HomeItem{std::string(item.id), std::string(item.name),
                        programme ? programme->title : std::string(tr("No programme information")),
                        std::string(item.tvg_logo)};
    };
    for (const auto &item : library_.unfinished_vod(selected_source_id_, source_id(active_source_)))
        if (home_vod_allowed(item))
            rows[0].push_back({item.channel.id, item.channel.name, item.channel.group_title,
                               item.channel.tvg_logo, true, item.position_us, item.duration_us});
    const auto last = library_.last_channel();
    const auto at = catalog_.Find(last.second);
    if (last.first == selected_source_id_ && at != iptv::Catalog::npos && channel_visible(at))
        rows[0].push_back(live(at));
    std::unordered_set<unsigned> saved_favorites;
    for (const auto &id : favorite_order_)
    {
        const auto index = catalog_.Find(id);
        if (index != iptv::Catalog::npos && channel_visible(index) && is_favorite(channel(index)))
        {
            rows[1].push_back(live(index));
            saved_favorites.insert(index);
            if (rows[1].size() == 50)
                break;
        }
    }
    std::vector<unsigned> usual;
    for (unsigned index : index_.order)
    {
        if (!channel_visible(index))
            continue;
        if (is_favorite(channel(index)) && !saved_favorites.contains(index) && rows[1].size() < 50)
            rows[1].push_back(live(index));
        if (index < channel_history_.size() && channel_history_[index].watches > 0)
            usual.push_back(index);
    }
    std::partial_sort(usual.begin(), usual.begin() + std::min<std::size_t>(20, usual.size()),
                      usual.end(),
                      [&](unsigned a, unsigned b)
                      {
                          return channel_history_[a].watches == channel_history_[b].watches
                                     ? a < b
                                     : channel_history_[a].watches > channel_history_[b].watches;
                      });
    for (unsigned index : usual)
    {
        rows[2].push_back(live(index));
        if (rows[2].size() == 20)
            break;
    }
    for (const auto &column : rows)
        for (const auto &item : column)
        {
            note_scripts(item.title, &home_east_asian_, &home_korean_);
            note_scripts(item.detail, &home_east_asian_, &home_korean_);
        }
    return rows;
}
bool Model::home_vod_allowed(const ResumeItem &item) const
{
    if (hidden_channels_.contains(item.channel.id) || !search_vod_allowed(item.channel))
        return false;
    // A newer loaded provider list can strengthen the saved item's restrictions.
    for (const auto *catalog :
         {&catalog_, &vod_.catalog(), &search_movies_.catalog(), &search_shows_.catalog()})
        if (const auto at = catalog->Find(item.channel.id);
            at != iptv::Catalog::npos && !search_vod_allowed((*catalog)[at]))
            return false;
    return true;
}
bool Model::play_home(std::string_view id, bool on_demand)
{
    if (pin_prompt())
        return false;
    if (!on_demand)
        return play_channel(id);
    for (const auto &item : library_.unfinished_vod(selected_source_id_, source_id(active_source_)))
    {
        if (item.channel.id != id || !home_vod_allowed(item))
            continue;
        // The snapshot already includes its show's policy; leave the VOD browser intact.
        return queue_vod(item.channel, item.series, false);
    }
    return false;
}
} // namespace ptv
