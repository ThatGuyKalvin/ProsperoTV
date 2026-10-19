// ProsperoTV - Real file writes, durable schedules and cancellation regressions.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/recording.hpp"
#include "tv/model.hpp"
#include "host_platform.hpp"
#include "iptv_store.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <csignal>
#include <sys/resource.h>
#include <cstdlib>
namespace
{
namespace fs = std::filesystem;
class RecordingTest : public ::testing::Test
{
  protected:
    std::string dir;
    void SetUp() override
    {
        host::reset();
        char name[] = "/tmp/prosperotv-record-XXXXXX";
        dir = mkdtemp(name);
        host::set_unix_time(1000);
        fs::create_directory(dir + "/usb0");
        std::ofstream(dir + "/stream.ts") << std::string(188 * 4000, 'G');
        host::set_network(true, dir + "/stream.ts");
    }
    void TearDown() override
    {
        host::reset();
        fs::remove_all(dir);
    }
    static void wait(ptv::Recorder &r)
    {
        for (unsigned i = 0; i < 600 && !r.done(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        ASSERT_TRUE(r.done());
        r.join();
    }
};
TEST_F(RecordingTest, WritesBytesAndFinalizesOnlyAfterSuccessfulFlush)
{
    ptv::Recorder recorder;
    ptv::RecordingInput input;
    input.url = "https://test.invalid/live.ts";
    ASSERT_TRUE(recorder.start(input, dir + "/usb0", 1100, 1000, 1));
    wait(recorder);
    ASSERT_TRUE(recorder.succeeded()) << recorder.result();
    EXPECT_EQ(recorder.bytes(), 188u * 4000u);
    EXPECT_EQ(fs::file_size(recorder.result()), recorder.bytes());
    EXPECT_FALSE(fs::exists(recorder.result() + ".partial"));
    // The same identity must not replace an earlier recording.
    ASSERT_TRUE(recorder.start(input, dir + "/usb0", 1100, 1000, 1));
    wait(recorder);
    EXPECT_FALSE(recorder.succeeded());
    EXPECT_EQ(fs::file_size(dir + "/usb0/ProsperoTV-1000-1.ts"), 188u * 4000u);
}
TEST_F(RecordingTest, CancelAndUnplugDoNotClaimSuccess)
{
    host::set_network(true, dir + "/stream.ts", 200);
    ptv::Recorder recorder;
    ptv::RecordingInput input;
    input.url = "https://test.invalid/live.ts";
    ASSERT_TRUE(recorder.start(input, dir + "/usb0", 1100, 1000, 2));
    recorder.cancel();
    wait(recorder);
    EXPECT_FALSE(recorder.succeeded());
    ASSERT_TRUE(recorder.start(input, dir + "/usb0", 1100, 1000, 3));
    for (unsigned i = 0; i < 100 && !fs::exists(dir + "/usb0/ProsperoTV-1000-3.ts.partial"); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    fs::rename(dir + "/usb0", dir + "/removed");
    wait(recorder);
    EXPECT_FALSE(recorder.succeeded());
    EXPECT_NE(recorder.result().find("disconnected"), std::string::npos);
}
TEST_F(RecordingTest, FileWriteFailureKeepsPartialAndReportsFailure)
{
    struct rlimit previous
    {
    };
    ASSERT_EQ(getrlimit(RLIMIT_FSIZE, &previous), 0);
    struct rlimit limit = previous;
    limit.rlim_cur = 1024;
    const auto handler = std::signal(SIGXFSZ, SIG_IGN);
    ASSERT_EQ(setrlimit(RLIMIT_FSIZE, &limit), 0);
    ptv::Recorder recorder;
    ptv::RecordingInput input;
    input.url = "https://test.invalid/live.ts";
    const bool started = recorder.start(input, dir + "/usb0", 1100, 1000, 4);
    if (started)
        wait(recorder);
    EXPECT_EQ(setrlimit(RLIMIT_FSIZE, &previous), 0);
    std::signal(SIGXFSZ, handler);
    ASSERT_TRUE(started);
    EXPECT_FALSE(recorder.succeeded());
    EXPECT_TRUE(fs::exists(dir + "/usb0/ProsperoTV-1000-4.ts.partial"));
}
TEST_F(RecordingTest, PersistsSchedulesRejectsConflictsAndMarksInterruptedJobs)
{
    const auto database = dir + "/jobs.sqlite3";
    {
        ptv::ProgrammeJobs jobs;
        ASSERT_TRUE(jobs.open(database));
        ptv::ProgrammeJob first;
        first.source = 1;
        first.channel = "a";
        first.title = "Programme";
        first.start = 1100;
        first.end = 1200;
        first.recording = true;
        first.drive = dir + "/usb0";
        ASSERT_TRUE(jobs.save(first));
        auto overlap = first;
        overlap.start = 1150;
        overlap.end = 1300;
        EXPECT_FALSE(jobs.save(overlap));
        auto reminder = first;
        reminder.recording = false;
        ASSERT_TRUE(jobs.save(reminder));
        ASSERT_TRUE(jobs.finish(first.id, 1, "Recording"));
    }
    ptv::ProgrammeJobs jobs;
    ASSERT_TRUE(jobs.open(database));
    const auto rows = jobs.list();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].state, 3);
    EXPECT_EQ(rows[1].state, 0);
    ASSERT_TRUE(jobs.remove(rows[0].id));
    EXPECT_EQ(jobs.list().size(), 1u);
}
TEST_F(RecordingTest, ReminderFiresOnceAfterReopeningProfile)
{
    std::ofstream(dir + "/list.m3u")
        << "#EXTM3U\n#EXTINF:-1,Channel\nhttps://test.invalid/live.ts\n";
    host::set_network(true, dir + "/list.m3u");
    host::add_playlist_source(dir);
    {
        ptv::Model model(dir);
        ASSERT_TRUE(model.open());
        for (unsigned i = 0; i < 400 && model.refreshing(); ++i)
        {
            model.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        model.poll();
        ASSERT_EQ(model.channel_count(), 1u);
        ASSERT_TRUE(model.schedule_programme(0, {1050, 1100, "Next show", {}, {}}, false));
        model.close();
    }
    host::set_unix_time(1050);
    ptv::Model reopened(dir);
    ASSERT_TRUE(reopened.open());
    reopened.poll_programme_jobs();
    EXPECT_EQ(reopened.programme_notice(), "Programme starting: Next show");
    const auto jobs = reopened.programme_jobs();
    ASSERT_EQ(jobs.size(), 1u);
    EXPECT_EQ(jobs[0].state, 2);
    (void)reopened.take_notices();
    host::set_unix_time(1051);
    reopened.poll_programme_jobs();
    EXPECT_TRUE(reopened.take_notices().empty());
    reopened.close();
}
TEST_F(RecordingTest, LockedProfileDoesNotShowOrRunSavedProgrammeJobs)
{
    {
        ptv::ProgrammeJobs jobs;
        ASSERT_TRUE(jobs.open(dir + "/prosperotv-programmes.sqlite3"));
        ptv::ProgrammeJob job;
        job.source = 1;
        job.channel = "protected";
        job.title = "Private title";
        job.start = 1000;
        job.end = 1100;
        ASSERT_TRUE(jobs.save(job));
        ptv::Parental parental;
        ASSERT_TRUE(parental.load(dir + "/prosperotv-parental-v1.txt"));
        ASSERT_TRUE(parental.set_pin("1234"));
    }
    ptv::Model model(dir);
    ASSERT_TRUE(model.open());
    model.poll_programme_jobs();
    EXPECT_TRUE(model.programme_jobs().empty());
    EXPECT_TRUE(model.programme_notice().empty());
    model.close();
}
TEST_F(RecordingTest, ChangedInactiveSourceCannotRecordItsOldCachedAccount)
{
    const auto catalog =
        iptv::ParseExtendedM3u("#EXTM3U\n#EXTINF:-1,Old channel\nhttps://old.invalid/live.ts\n",
                               iptv::CustomSourceId("https://old.invalid/list.m3u"));
    ASSERT_EQ(iptv::SaveCatalog(dir + "/prosperotv-source-4.sqlite3", catalog),
              iptv::StoreStatus::ok);
    {
        ptv::Library library;
        ASSERT_TRUE(library.open(dir + "/prosperotv-library.sqlite3"));
        ptv::SavedSource source{
            4,  1,  "Changed playlist",          "https://new.invalid/list.m3u", {},
            {}, {}, ptv::RefreshSchedule::manual};
        ASSERT_TRUE(library.save_source(&source));
        ptv::ProgrammeJobs jobs;
        ASSERT_TRUE(jobs.open(dir + "/prosperotv-programmes.sqlite3"));
        ptv::ProgrammeJob job;
        job.source = 4;
        job.channel = catalog[0].id;
        job.title = "Old programme";
        job.start = 1000;
        job.end = 1100;
        job.recording = true;
        job.drive = dir + "/usb0";
        ASSERT_TRUE(jobs.save(job));
        job.source = 99;
        job.recording = false;
        ASSERT_TRUE(jobs.save(job));
    }
    host::set_network(false, "");
    ptv::Model model(dir);
    ASSERT_TRUE(model.open());
    model.poll_programme_jobs();
    const auto jobs = model.programme_jobs();
    ASSERT_EQ(jobs.size(), 2u);
    EXPECT_EQ(jobs[0].state, 3);
    EXPECT_EQ(jobs[0].result, "The source changed since this programme was scheduled");
    EXPECT_EQ(jobs[1].state, 3);
    EXPECT_EQ(jobs[1].result, "This source was removed");
    EXPECT_TRUE(model.programme_notice().empty());
    EXPECT_TRUE(fs::is_empty(dir + "/usb0"));
    model.close();
}
} // namespace
