// ProsperoTV - Guide actions remain independent of the current source and playback.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/model.hpp"
#include "tv/i18n.hpp"
#include "tv/usb_playlist.hpp"
#include "tv/platform.hpp"
#include "tv/backup.hpp"
#include "tv/local_tv.hpp"
#include "iptv_store.h"
#include <algorithm>
namespace ptv
{
bool Model::schedule_programme(unsigned index, const Programme &p, bool record)
{
    if (index >= channel_count() || !content_allowed(catalog_[index]) || pin_prompt())
        return false;
    if (!parental_.valid() || parental_.kids_only() ||
        (parental_.enabled() && !parental_.unlocked()))
    {
        announce(Level::warning, tr("Unlock parental controls before scheduling programmes"), "",
                 5);
        return false;
    }
    const auto now = static_cast<std::int64_t>(platform::unix_time());
    if (p.end <= now || p.start > now + 14 * 86400 || p.end - p.start > 86400)
        return false;
    for (const auto &job : programme_jobs_.list())
        if (job.source == selected_source_id_ && job.channel == catalog_[index].id &&
            job.start == p.start && job.recording == record && job.state < 2)
        {
            cancel_programme_job(job.id);
            return true;
        }
    ProgrammeJob job;
    job.source = selected_source_id_;
    job.channel = catalog_[index].id;
    job.title = p.title;
    job.start = p.start;
    job.end = p.end;
    job.recording = record;
    if (record)
    {
        const auto drives = usb_drives();
        if (drives.empty())
        {
            announce(Level::error, tr("Connect a writable USB drive"),
                     tr("Recording uses /mnt/usb0 through /mnt/usb7"), 5);
            return false;
        }
        job.drive = drives.front();
    }
    if (!programme_jobs_.save(job))
    {
        announce(Level::error, tr("Schedule could not be saved"),
                 tr("Recordings cannot overlap. Keep at most 128 programme jobs."), 5);
        return false;
    }
    announce(Level::ready, tr(record ? "Recording scheduled" : "Reminder set"),
             tr("Keep ProsperoTV running; the console cannot wake for a programme."), 5);
    next_job_check_ = 0;
    poll_programme_jobs();
    return true;
}
void Model::cancel_programme_job(std::int64_t id)
{
    if (recording_job_ == id && recorder_.active())
    {
        recorder_.cancel();
        programme_jobs_.finish(id, 4, "Stopping recording");
        announce(Level::ready, tr("Stopping recording"), "", 3);
        return;
    }
    if (programme_jobs_.remove(id))
        announce(Level::ready, tr("Programme job removed"), "", 3);
}
void Model::toggle_recording(std::string_view channel)
{
    if (recorder_.active())
    {
        recorder_.cancel();
        next_job_check_ = 0;
        return;
    }
    const auto index = catalog_.Find(channel);
    if (index == iptv::Catalog::npos)
        return;
    const auto now = static_cast<std::int64_t>(platform::unix_time());
    if (const auto *p = guide_.now(channel, now))
    {
        schedule_programme(static_cast<unsigned>(index), *p, true);
        return;
    }
    schedule_programme(static_cast<unsigned>(index),
                       {now, now + 2 * 3600, std::string(catalog_[index].name), {}, {}}, true);
}
void Model::poll_programme_jobs()
{
    const auto now = platform::unix_time();
    if (now < next_job_check_)
        return;
    next_job_check_ = now + 1;
    const auto notice = [&](bool ok, const std::string &title, const std::string &body)
    {
        programme_notice_ = title + ": " + body;
        announce(ok ? Level::ready : Level::error, title, body, 8);
    };
    if (!parental_.valid() || parental_.kids_only() ||
        (parental_.enabled() && !parental_.unlocked()))
    {
        programme_notice_.clear();
        if (recorder_.active())
            recorder_.cancel();
    }
    if (recorder_.active() && recorder_.done())
    {
        recorder_.join();
        const auto message = recorder_.result();
        const bool ok = recorder_.succeeded();
        programme_jobs_.finish(recording_job_, ok ? 2 : 3, message);
        recording_job_ = 0;
        notice(ok, tr(ok ? "Recording saved" : "Recording failed"), tr(message.c_str()));
    }
    for (const auto &job : programme_jobs_.list())
    {
        if (job.state != 0 || job.start > static_cast<std::int64_t>(now))
            continue;
        if (job.end <= static_cast<std::int64_t>(now))
        {
            programme_jobs_.finish(job.id, 3, "Missed while the app was closed");
            continue;
        }
        // Schedules must never bypass a PIN lock after restarting or switching profiles.
        if (!parental_.valid() || parental_.kids_only() ||
            (parental_.enabled() && !parental_.unlocked()))
        {
            programme_jobs_.finish(job.id, 3, "Parental controls locked this programme");
            continue;
        }
        const auto *source = saved_source(job.source);
        if (!source)
        {
            programme_jobs_.finish(job.id, 3, "This source was removed");
            continue;
        }
        if (!job.recording)
        {
            if (programme_jobs_.finish(job.id, 2, "Reminder shown"))
                notice(true, tr("Programme starting"), job.title);
            continue;
        }
        if (recorder_.active())
        {
            programme_jobs_.finish(job.id, 3, "Another recording is running");
            notice(false, tr("Recording conflict"), job.title);
            continue;
        }
        iptv::Catalog cache;
        const iptv::Catalog *channels = &catalog_;
        if (job.source != selected_source_id_)
        {
            const std::string file =
                job.source > 3    ? "prosperotv-source-" + std::to_string(job.source) + ".sqlite3"
                : job.source == 3 ? "prosperotv-xtream-catalog.sqlite3"
                : job.source == 2 ? "prosperotv-custom-catalog.sqlite3"
                                  : "prosperotv-catalog.sqlite3";
            if (iptv::LoadCatalog(cache_dir_ + "/" + file, &cache) != iptv::StoreStatus::ok)
            {
                programme_jobs_.finish(job.id, 3, "The source cache is unavailable");
                continue;
            }
            channels = &cache;
        }
        const auto expected_source =
            source->kind == 1 ? iptv::CustomSourceId(source->url)
            : source->kind == 2
                ? iptv::XtreamSourceId({source->url, source->username, source->password})
            : source->kind == 3 ? portal_source_id({source->url, source->mac})
            : source->kind >= 4 ? local_tv_source_id(*source)
                                : source_id(iptv::SourceKind::BuiltIn);
        if (channels->source_id != expected_source)
        {
            programme_jobs_.finish(job.id, 3,
                                   "The source changed since this programme was scheduled");
            continue;
        }
        const auto index = channels->Find(job.channel);
        if (index == iptv::Catalog::npos)
        {
            programme_jobs_.finish(job.id, 3, "This source or channel was removed");
            continue;
        }
        const auto drives = usb_drives();
        if (std::find(drives.begin(), drives.end(), job.drive) == drives.end())
        {
            programme_jobs_.finish(job.id, 3, "The selected USB drive is unavailable");
            notice(false, tr("Recording failed"), tr("Connect the selected USB drive"));
            continue;
        }
        const auto channel = (*channels)[index];
        RecordingInput input;
        input.url = channel.url;
        input.extra_headers = source->extra_headers;
        input.credential_origin = usb_playlist_path(source->url) ? std::string{} : source->url;
        input.user_agent = channel.http_user_agent.empty() ? source->user_agent
                                                           : std::string(channel.http_user_agent);
        input.referrer =
            channel.http_referrer.empty() ? source->referrer : std::string(channel.http_referrer);
        if (source->kind == 5)
        {
            input.authorization = local_tv_authorization(*source);
            input.credential_origin = source->url;
        }
        if (source->kind == 3)
        {
            input.portal_url = source->url;
            input.portal_mac = source->mac;
            input.portal_command = channel.portal_command;
        }
        if (!programme_jobs_.finish(job.id, 1, "Recording"))
            continue;
        cancel_source_check("Check stopped for recording");
        if (recorder_.start(std::move(input), job.drive, job.end, now, job.id))
        {
            recording_job_ = job.id;
            notice(true, tr("Recording started"), job.title);
        }
        else
        {
            programme_jobs_.finish(job.id, 3, "Recording could not start");
            notice(false, tr("Recording failed"), job.title);
        }
    }
}
} // namespace ptv
