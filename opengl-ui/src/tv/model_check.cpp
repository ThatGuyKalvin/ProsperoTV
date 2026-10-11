// ProsperoTV - Explicit source checks, scheduled only while menu resources are free.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/model.hpp"
#include "tv/platform.hpp"
#include "tv/i18n.hpp"
namespace ptv
{
std::uint64_t Model::source_check_identity() const
{
    auto identity = catalog_.source_id;
    if (const auto *source = saved_source(selected_source_id_))
        for (const auto &value : {source->user_agent, source->referrer, source->extra_headers})
        {
            for (const unsigned char c : value)
            {
                identity ^= c;
                identity *= 1099511628211ull;
            }
            identity ^= 255;
            identity *= 1099511628211ull;
        }
    return identity ? identity : 1;
}
std::string Model::source_check_status() const
{
    return tr(check_message_.empty() ? "Checks visible live channels only"
                                     : check_message_.c_str()) +
           std::string(" | ") + tr("Passed") + " " + std::to_string(check_passed_) + " / " +
           tr("Failed") + " " + std::to_string(check_failed_) + " / " + tr("Skipped") + " " +
           std::to_string(check_skipped_) + " / " + tr("Total") + " " +
           std::to_string(check_total_);
}
void Model::cancel_source_check(const char *reason)
{
    const bool was_active = check_active_;
    source_check_.stop();
    const bool saved = check_store_.flush();
    check_store_.close();
    check_active_ = false;
    check_channel_.clear();
    check_order_.clear();
    if (was_active)
    {
        check_message_ = saved ? reason : "Check results could not be saved";
        ++revision_;
    }
}
void Model::block_source_check(bool blocked)
{
    check_blocked_ = blocked;
    if (blocked && check_active_)
        cancel_source_check("Check stopped for playback or preview");
}
void Model::toggle_source_check()
{
    if (check_active_)
    {
        cancel_source_check();
        return;
    }
    if (!has_catalog() || refreshing() || pin_prompt() || home_waiting_ || recording() ||
        check_blocked_ || sleep_timer.sleeping())
    {
        announce(Level::warning, tr("Source check is unavailable"),
                 tr("Stop playback, previews and recording first."), 4);
        return;
    }
    check_source_ = source_check_identity();
    if (!check_store_.open(cache_path(active_source_) + ".checks.sqlite3", check_source_))
    {
        announce(Level::error, tr("Check results could not be saved"), "", 4);
        return;
    }
    check_order_.clear();
    for (unsigned i = 0; i < marks_.size(); ++i)
        if ((marks_[i] & 4u) == 0)
            check_order_.push_back(i);
    check_total_ = check_order_.size();
    check_cursor_ = check_passed_ = check_failed_ = check_skipped_ = 0;
    check_channel_.clear();
    check_message_ = "Checking source";
    check_next_ = 0;
    check_active_ = true;
    ++revision_;
}
void Model::load_source_checks()
{
    if (catalog_loaded_)
        CheckStore::apply(cache_path(active_source_) + ".checks.sqlite3", catalog_,
                          source_check_identity());
}
void Model::poll_source_check()
{
    if (!check_active_)
        return;
    if (check_source_ != source_check_identity() || refreshing() || pin_prompt() || recording() ||
        home_waiting_ || check_blocked_ || sleep_timer.sleeping())
    {
        cancel_source_check();
        return;
    }
    if (!check_store_.flush(platform::unix_time()))
    {
        cancel_source_check("Check results could not be saved");
        return;
    }
    const auto index = check_channel_.empty() ? iptv::Catalog::npos : catalog_.Find(check_channel_);
    const bool current = index < marks_.size() && (marks_[index] & 4u) == 0 &&
                         content_allowed(catalog_[index]) &&
                         check_fingerprint(catalog_[index]) == check_fingerprint_;
    if (source_check_.busy() && !current)
    {
        source_check_.stop();
        ++check_skipped_;
        check_channel_.clear();
        ++revision_;
    }
    ProbeResult result;
    if (source_check_.take(result))
    {
        if (result.outcome == ProbeOutcome::frame)
            ++check_passed_;
        else if (result.outcome == ProbeOutcome::missing)
            ++check_failed_;
        else
            ++check_skipped_;
        const auto now = platform::unix_time();
        if (current && result.outcome != ProbeOutcome::cancelled &&
            !check_store_.save(check_channel_, check_fingerprint_, result, now))
        {
            cancel_source_check("Check results could not be saved");
            return;
        }
        if (current &&
            (result.outcome == ProbeOutcome::frame || result.outcome == ProbeOutcome::missing) &&
            now > catalog_[index].playback_checked_unix)
        {
            catalog_.SetPlayback(index,
                                 result.outcome == ProbeOutcome::frame
                                     ? iptv::PlaybackStatus::playable
                                     : iptv::PlaybackStatus::failed,
                                 -result.http, now);
            if (hide_failed_ && result.outcome == ProbeOutcome::missing)
            {
                marks_[index] |= 4u;
                recount_groups();
                rebuild_visible();
            }
        }
        check_channel_.clear();
        check_next_ = platform::monotonic_us() + 1000000;
        ++revision_;
        if (result.outcome == ProbeOutcome::service)
        {
            cancel_source_check("Check stopped: provider refused a request");
            return;
        }
    }
    if (source_check_.busy() || platform::monotonic_us() < check_next_)
        return;
    while (check_cursor_ < check_order_.size())
    {
        const auto next = check_order_[check_cursor_++];
        if (next >= marks_.size() || (marks_[next] & 4u) != 0 || !content_allowed(catalog_[next]))
        {
            ++check_skipped_;
            ++revision_;
            continue;
        }
        const auto request = probe_request(catalog_[next].id);
        if (!request)
        {
            ++check_skipped_;
            ++revision_;
            continue;
        }
        check_channel_ = catalog_[next].id;
        check_fingerprint_ = check_fingerprint(catalog_[next]);
        if (!source_check_.start(*request))
        {
            cancel_source_check("Check worker could not start");
        }
        return;
    }
    cancel_source_check("Check finished");
}
} // namespace ptv
