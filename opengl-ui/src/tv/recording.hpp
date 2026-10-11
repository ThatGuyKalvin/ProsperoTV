// ProsperoTV - Persistent programme jobs and bounded recording files.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "iptv_http.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
struct sqlite3;
namespace ptv
{
struct ProgrammeJob
{
    std::int64_t id = 0, source = 0, start = 0, end = 0;
    std::string channel, title, drive;
    bool recording = false;
    int state = 0; // pending, running, completed, failed, cancelled
    std::string result;
};
class ProgrammeJobs
{
  public:
    ~ProgrammeJobs();
    bool open(const std::string &path);
    bool close();
    std::vector<ProgrammeJob> list() const;
    bool save(ProgrammeJob &job);
    bool finish(std::int64_t id, int state, const std::string &result);
    bool remove(std::int64_t id);

  private:
    sqlite3 *db_ = nullptr;
};
struct RecordingInput
{
    std::string url, user_agent, referrer, authorization, credential_origin;
    std::string portal_url, portal_mac, portal_command;
};
// The stream worker calls the sink in bounded pieces and checks control while waiting.
namespace platform
{
int record_stream(const RecordingInput &, const iptv::http::ListSink &,
                  const iptv::http::RequestControl &, std::string &error);
}
class Recorder
{
  public:
    ~Recorder();
    bool start(RecordingInput input, const std::string &drive, std::int64_t end, std::uint64_t now,
               std::int64_t job);
    void cancel();
    bool active() const
    {
        return thread_ != nullptr;
    }
    bool done() const
    {
        return done_.load(std::memory_order_acquire);
    }
    void join();
    std::uint64_t bytes() const
    {
        return bytes_.load();
    }
    const std::string &result() const
    {
        return result_;
    }
    bool succeeded() const
    {
        return success_;
    }

  private:
    static void *run(void *);
    void *thread_ = nullptr;
    std::atomic<bool> stop_{false}, done_{false};
    std::atomic<std::uint64_t> bytes_{0};
    RecordingInput input_;
    std::string drive_, filename_, result_;
    std::int64_t end_ = 0;
    bool success_ = false;
};
} // namespace ptv
