// ProsperoTV - Conservative, bounded background source checks.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "iptv_catalog.h"
#include <atomic>
#include <memory>
#include <string>
struct sqlite3;
namespace ptv
{
struct PlayRequest;
enum class ProbeOutcome
{
    unknown,
    frame,
    missing,
    unsupported,
    timeout,
    cancelled,
    service
};
struct ProbeResult
{
    ProbeOutcome outcome = ProbeOutcome::unknown;
    int http = 0;
};
// Only channel-entry HTTP 404/410 is definitive; segment errors are inconclusive.
ProbeResult probe_http(int status, bool entry, bool timed_out = false);
ProbeResult combine_probe(ProbeResult prior, ProbeResult next, bool first);
std::uint64_t check_fingerprint(const iptv::ChannelView &channel);
class CheckStore
{
  public:
    ~CheckStore();
    bool open(const std::string &path, std::uint64_t source, unsigned capacity = 250000);
    bool save(std::string_view channel, std::uint64_t fingerprint, ProbeResult result,
              std::uint64_t now);
    bool flush(std::uint64_t now = 0);
    void close();
    static void apply(const std::string &path, iptv::Catalog &catalog, std::uint64_t identity = 0);

  private:
    sqlite3 *db_ = nullptr;
    unsigned pending_ = 0, count_ = 0, capacity_ = 250000;
    std::uint64_t flushed_ = 0;
};
class SourceCheck
{
  public:
    ~SourceCheck();
    bool start(const PlayRequest &request);
    bool take(ProbeResult &result);
    void stop();
    bool busy() const
    {
        return thread_ != nullptr;
    }

  private:
    static void *work(void *self);
    std::shared_ptr<PlayRequest> request_;
    std::atomic<bool> cancel_{false}, done_{false}, frame_{false};
    void *thread_ = nullptr;
    ProbeResult result_;
};
} // namespace ptv
