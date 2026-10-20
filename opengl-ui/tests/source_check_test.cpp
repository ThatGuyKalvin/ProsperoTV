// ProsperoTV - Probe uncertainty must never turn supported streams into dead channels.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/model.hpp"
#include "tv/source_check.hpp"
#include "tv/preview.hpp"
#include "host_platform.hpp"
#include "host_preview.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <ctime>
#include <fstream>
#include <thread>
#include <sqlite3.h>
namespace
{
using namespace ptv;
TEST(SourceCheckTest, ConservativeOutcomeAggregation)
{
    EXPECT_EQ(probe_http(404, true).outcome, ProbeOutcome::missing);
    EXPECT_EQ(probe_http(410, true).outcome, ProbeOutcome::missing);
    EXPECT_EQ(probe_http(404, false).outcome, ProbeOutcome::unknown);
    EXPECT_EQ(probe_http(200, true).outcome, ProbeOutcome::unknown);
    EXPECT_EQ(probe_http(0, true, true).outcome, ProbeOutcome::timeout);
    for (int status : {401, 403, 429, 503})
        EXPECT_EQ(probe_http(status, true).outcome, ProbeOutcome::service);
    const ProbeResult missing{ProbeOutcome::missing, 404};
    EXPECT_EQ(combine_probe(missing, missing, false).outcome, ProbeOutcome::missing);
    EXPECT_EQ(combine_probe(missing, {ProbeOutcome::frame, 0}, false).outcome, ProbeOutcome::frame);
    for (auto kind : {ProbeOutcome::unsupported, ProbeOutcome::timeout, ProbeOutcome::unknown})
        EXPECT_EQ(combine_probe(missing, {kind, 0}, false).outcome, ProbeOutcome::unknown);
}
TEST(SourceCheckTest, RequiresRealFrameAndStopsSingleWorker)
{
    host::reset();
    host::set_preview(true);
    PlayRequest request;
    request.channel_id = "test";
    request.urls = {"https://test.invalid/live.ts"};
    SourceCheck check;
    ASSERT_TRUE(check.start(request));
    EXPECT_FALSE(check.start(request));
    ProbeResult result;
    bool got = false;
    for (unsigned i = 0; i < 400 && !(got = check.take(result)); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_TRUE(got);
    EXPECT_EQ(result.outcome, ProbeOutcome::frame);
    EXPECT_EQ(host::preview_starts(), 1u);
    EXPECT_EQ(host::preview_stops(), 1u);
    host::set_preview_probe(static_cast<int>(ProbeOutcome::unsupported));
    ASSERT_TRUE(check.start(request));
    got = false;
    for (unsigned i = 0; i < 400 && !(got = check.take(result)); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_TRUE(got);
    EXPECT_EQ(result.outcome, ProbeOutcome::unsupported);
    host::set_preview_probe(static_cast<int>(ProbeOutcome::missing), 404, 1000);
    ASSERT_TRUE(check.start(request));
    for (unsigned i = 0; i < 200 && host::preview_starts() < 3; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_EQ(host::preview_starts(), 3u);
    check.stop();
    EXPECT_FALSE(check.busy());
    EXPECT_FALSE(check.take(result));
    host::set_preview(false);
}
TEST(SourceCheckTest, DeadlineCancelsStalledProbeWithoutDeclaringItDead)
{
    host::reset();
    host::set_preview(false);
    host::set_preview_probe(static_cast<int>(ProbeOutcome::missing), 404, 20000);
    PlayRequest request;
    request.channel_id = "slow";
    request.urls = {"https://test.invalid/slow.ts"};
    SourceCheck check;
    ASSERT_TRUE(check.start(request));
    ProbeResult result;
    bool got = false;
    for (unsigned i = 0; i < 4000 && !(got = check.take(result)); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_TRUE(got);
    EXPECT_EQ(result.outcome, ProbeOutcome::timeout);
    EXPECT_EQ(host::preview_starts(), host::preview_stops());
    host::set_preview(false);
}
class CheckStoreTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        char name[] = "/tmp/ptv-check-XXXXXX";
        dir = mkdtemp(name);
    }
    void TearDown() override
    {
        std::filesystem::remove_all(dir);
    }
    std::string dir;
};
TEST_F(CheckStoreTest, BatchCapSourceFingerprintAndManualHistoryPrecedence)
{
    auto catalog = iptv::ParseExtendedM3u("#EXTM3U\n#EXTINF:-1,One\nhttps://test.invalid/"
                                          "1.ts\n#EXTINF:-1,Two\nhttps://test.invalid/2.ts\n",
                                          42);
    const auto path = dir + "/checks.sqlite3";
    CheckStore store;
    ASSERT_TRUE(store.open(path, 42, 2));
    ASSERT_TRUE(store.save(catalog[0].id, check_fingerprint(catalog[0]),
                           {ProbeOutcome::missing, 404}, 100));
    ASSERT_TRUE(
        store.save(catalog[1].id, check_fingerprint(catalog[1]), {ProbeOutcome::frame, 0}, 100));
    EXPECT_FALSE(store.save("third", 1, {ProbeOutcome::frame, 0}, 100));
    ASSERT_TRUE(store.flush());
    catalog.SetPlayback(0, iptv::PlaybackStatus::playable, 0, 101);
    CheckStore::apply(path, catalog);
    EXPECT_EQ(catalog[0].playback_status, iptv::PlaybackStatus::playable);
    EXPECT_EQ(catalog[1].playback_status, iptv::PlaybackStatus::playable);
    ASSERT_TRUE(store.save(catalog[1].id, check_fingerprint(catalog[1]),
                           {ProbeOutcome::unsupported, 0}, 102));
    ASSERT_TRUE(store.flush());
    catalog.SetPlayback(1, iptv::PlaybackStatus::unknown, 0, 0);
    CheckStore::apply(path, catalog);
    EXPECT_EQ(catalog[1].playback_status, iptv::PlaybackStatus::playable);
    ASSERT_TRUE(store.save(catalog[1].id, 999, {ProbeOutcome::missing, 404}, 103));
    ASSERT_TRUE(store.flush());
    catalog.SetPlayback(1, iptv::PlaybackStatus::unknown, 0, 0);
    CheckStore::apply(path, catalog);
    EXPECT_EQ(catalog[1].playback_status, iptv::PlaybackStatus::unknown);
    store.close();
    ASSERT_TRUE(store.open(path, 43, 2));
    store.close();
    CheckStore::apply(path, catalog);
    EXPECT_EQ(catalog[1].playback_status, iptv::PlaybackStatus::unknown);
}
TEST_F(CheckStoreTest, BoundsExistingLargePagesAndRejectsMalformedHealth)
{
    const auto path = dir + "/pages.sqlite3";
    sqlite3 *db = nullptr;
    ASSERT_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db,
                           "PRAGMA page_size=65536;CREATE TABLE filler(data);INSERT INTO filler "
                           "VALUES(zeroblob(66060288));",
                           nullptr, nullptr, nullptr),
              SQLITE_OK);
    sqlite3_close(db);
    CheckStore store;
    ASSERT_TRUE(store.open(path, 42));
    bool full = false;
    for (unsigned i = 0; i < 10000; ++i)
    {
        const auto id = std::to_string(i) + std::string(240, 'x');
        if (!store.save(id, 1, {ProbeOutcome::frame, 0}, 100))
        {
            full = true;
            break;
        }
    }
    EXPECT_TRUE(full);
    store.close();
    EXPECT_LE(std::filesystem::file_size(path), 64u * 1024u * 1024u);

    const auto malformed = dir + "/malformed.sqlite3";
    auto catalog =
        iptv::ParseExtendedM3u("#EXTM3U\n#EXTINF:-1,One\nhttps://test.invalid/1.ts\n", 42);
    ASSERT_TRUE(store.open(malformed, 42));
    ASSERT_TRUE(store.save(catalog[0].id, check_fingerprint(catalog[0]),
                           {ProbeOutcome::missing, 404}, 100));
    store.close();
    ASSERT_EQ(sqlite3_open(malformed.c_str(), &db), SQLITE_OK);
    for (const char *sql :
         {"UPDATE checks SET http=-2147483648", "UPDATE checks SET http=404,outcome=4294967298",
          "UPDATE checks SET outcome=2,checked=253402300800",
          "UPDATE checks SET checked=100,channel=channel||char(0)||'suffix'",
          "UPDATE checks SET channel=printf('%0300d',1)"})
    {
        ASSERT_EQ(sqlite3_exec(db, sql, nullptr, nullptr, nullptr), SQLITE_OK);
        CheckStore::apply(malformed, catalog);
        EXPECT_EQ(catalog[0].playback_status, iptv::PlaybackStatus::unknown);
    }
    sqlite3_close(db);
}
TEST_F(CheckStoreTest, RejectsOversizedStoreWithoutTouchingIt)
{
    const auto path = dir + "/large.sqlite3";
    std::ofstream(path).put('x');
    std::filesystem::resize_file(path, 64u * 1024u * 1024u + 1);
    CheckStore store;
    EXPECT_FALSE(store.open(path, 42));
    EXPECT_EQ(std::filesystem::file_size(path), 64u * 1024u * 1024u + 1);
}
TEST_F(CheckStoreTest, ThirtyTwoResultsCommitAsBoundedBatch)
{
    CheckStore store;
    const auto path = dir + "/checks.sqlite3";
    ASSERT_TRUE(store.open(path, 42));
    ASSERT_TRUE(store.save("first", 1, {ProbeOutcome::frame, 0}, 100)); // First result flushes.
    for (unsigned i = 0; i < 31; ++i)
        ASSERT_TRUE(store.save(std::to_string(i), 1, {ProbeOutcome::frame, 0}, 101));
    const auto count = [&]
    {
        sqlite3 *db = nullptr;
        sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
        sqlite3_stmt *s = nullptr;
        sqlite3_prepare_v2(db, "SELECT count(*) FROM checks", -1, &s, nullptr);
        const int n = sqlite3_step(s) == SQLITE_ROW ? sqlite3_column_int(s, 0) : -1;
        sqlite3_finalize(s);
        sqlite3_close(db);
        return n;
    };
    EXPECT_EQ(count(), 1);
    ASSERT_TRUE(store.save("last", 1, {ProbeOutcome::frame, 0}, 101));
    EXPECT_EQ(count(), 33);
    ASSERT_TRUE(store.save("timed", 1, {ProbeOutcome::frame, 0}, 102));
    ASSERT_TRUE(store.flush(105));
    EXPECT_EQ(count(), 33);
    ASSERT_TRUE(store.flush(106));
    EXPECT_EQ(count(), 34);
}
TEST_F(CheckStoreTest, ModelChecksRespectPolicyAndCancelBeforeAnotherResourceOwner)
{
    host::reset();
    host::set_preview(false);
    std::ofstream(dir + "/list.m3u")
        << "#EXTM3U\n#EXTINF:-1 "
           "group-title=\"Private\",Hidden\nhttps://test.invalid/1.ts\n#EXTINF:-1 "
           "group-title=\"General\",Visible\nhttps://test.invalid/2.ts\n";
    host::set_network(true, dir + "/list.m3u");
    host::add_playlist_source(dir);
    Model model(dir);
    ASSERT_TRUE(model.open());
    for (unsigned i = 0; i < 1000 && model.refreshing(); ++i)
    {
        model.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_EQ(model.channel_count(), 2u);
    ASSERT_TRUE(model.hide_category("Private", true));
    host::set_preview_probe(static_cast<int>(ProbeOutcome::missing), 404, 1000);
    model.toggle_source_check();
    ASSERT_TRUE(model.checking_source());
    model.poll();
    ASSERT_TRUE(model.hide_channel(model.channel(1).id, true));
    model.poll();
    EXPECT_EQ(model.channel(1).playback_status, iptv::PlaybackStatus::unknown);
    EXPECT_EQ(model.source_check_status().find("Failed 1"), std::string::npos);
    model.cancel_source_check();
    ASSERT_TRUE(model.hide_channel(model.channel(1).id, false));
    model.toggle_source_check();
    model.poll();
    model.block_source_check(true);
    EXPECT_FALSE(model.checking_source());
    EXPECT_EQ(model.channel(1).playback_status, iptv::PlaybackStatus::unknown);
    model.toggle_source_check();
    EXPECT_FALSE(model.checking_source());
    model.block_source_check(false);
    model.toggle_source_check();
    model.poll();
    model.refresh();
    EXPECT_FALSE(model.checking_source());
    for (unsigned i = 0; i < 1000 && model.refreshing(); ++i)
    {
        model.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    host::set_preview_probe(static_cast<int>(ProbeOutcome::service), 429);
    model.toggle_source_check();
    for (unsigned i = 0; i < 200 && model.checking_source(); ++i)
    {
        model.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_FALSE(model.checking_source());
    EXPECT_NE(model.source_check_status().find("provider refused"), std::string::npos);
    EXPECT_EQ(model.channel(1).playback_status, iptv::PlaybackStatus::unknown);
    host::set_preview_probe(static_cast<int>(ProbeOutcome::missing), 404);
    model.toggle_source_check();
    for (unsigned i = 0;
         i < 200 && model.channel(1).playback_status != iptv::PlaybackStatus::failed; ++i)
    {
        model.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_EQ(model.channel(1).playback_status, iptv::PlaybackStatus::failed);
    model.cancel_source_check();
    EXPECT_FALSE(std::filesystem::exists(dir + "/prosperotv-playback-history.sqlite3"));
    model.close();
    host::set_preview(false);
    host::reset();
}

TEST_F(CheckStoreTest, InvisibleChecksDoNotSpendViewingQuotaButVisiblePreviewsDo)
{
    host::reset();
    host::set_preview(true);
    std::tm noon{};
    noon.tm_year = 126;
    noon.tm_mon = 9;
    noon.tm_mday = 10;
    noon.tm_hour = 12;
    noon.tm_isdst = -1;
    const auto wall = static_cast<std::uint64_t>(std::mktime(&noon));
    std::uint64_t mono = 1000000;
    const auto advance = [&](std::uint64_t elapsed)
    {
        mono += elapsed;
        host::set_monotonic_us(mono);
        host::set_unix_time(wall + (mono - 1000000) / 1000000);
    };
    advance(0);
    const auto parental_file = dir + "/prosperotv-parental-v1.txt";
    Parental policy;
    ASSERT_TRUE(policy.load(parental_file));
    ASSERT_TRUE(policy.set_pin("1234"));
    ASSERT_TRUE(policy.unlock("1234", wall));
    ASSERT_TRUE(policy.set_limits(1, -1, 0));
    ASSERT_TRUE(policy.set_kids(true));
    std::ofstream(dir + "/list.m3u")
        << "#EXTM3U\n#EXTINF:-1 group-title=\"Kids\",Cartoon\nhttps://test.invalid/kids.ts\n"
           "#EXTINF:-1 group-title=\"Adult\",Restricted\nhttps://test.invalid/adult.ts\n";
    host::set_network(true, dir + "/list.m3u");
    host::add_playlist_source(dir);
    Model model(dir);
    struct Cleanup
    {
        Model &model;
        ~Cleanup()
        {
            model.close();
            host::set_preview(false);
            host::reset();
        }
    } cleanup{model};
    ASSERT_TRUE(model.open());
    for (unsigned i = 0; i < 600 && model.refreshing(); ++i)
    {
        model.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_FALSE(model.refreshing());
    ASSERT_EQ(model.channel_count(), 2u);
    ASSERT_TRUE(model.probe_request(model.channel(0).id));
    EXPECT_FALSE(model.probe_request(model.channel(1).id));
    const auto saved_policy = [&]
    {
        std::ifstream in(parental_file);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    const auto check = [&]
    {
        model.toggle_source_check();
        EXPECT_TRUE(model.checking_source());
        for (unsigned i = 0; i < 600 && model.checking_source(); ++i)
        {
            model.poll();
            advance(100000);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EXPECT_FALSE(model.checking_source());
    };
    const auto before_check = saved_policy();
    check();
    EXPECT_EQ(host::preview_starts(), 1u);
    EXPECT_EQ(model.channel(0).playback_status, iptv::PlaybackStatus::playable);
    EXPECT_EQ(model.channel(1).playback_status, iptv::PlaybackStatus::unknown);
    EXPECT_EQ(saved_policy(), before_check); // No reservation or other usage write.
    EXPECT_EQ(model.parental().remaining_seconds(), 60u);

    LivePreview preview;
    preview.configure([](auto, const auto &) { return 1u; }, [](auto) {}, false,
                      [&] { return model.viewing_tick(true); });
    preview.update(model.preview_request(model.channel(0).id), 1.2f);
    ASSERT_TRUE(preview.running());
    ASSERT_TRUE(policy.load(parental_file));
    EXPECT_EQ(policy.remaining_seconds(), 30u); // Reserved before visible decoding starts.
    advance(15000000);
    ASSERT_TRUE(model.viewing_tick(true));
    preview.clear();
    ASSERT_TRUE(model.viewing_tick(false));
    ASSERT_TRUE(policy.load(parental_file));
    EXPECT_EQ(policy.remaining_seconds(), 45u);

    ASSERT_TRUE(model.viewing_tick(true));
    advance(45000000);
    EXPECT_FALSE(model.viewing_tick(true));
    EXPECT_FALSE(model.preview_request(model.channel(0).id));
    EXPECT_FALSE(model.preview_request(model.channel(1).id));
    EXPECT_TRUE(model.probe_request(model.channel(0).id));
    EXPECT_FALSE(model.probe_request(model.channel(1).id));
    const auto exhausted = saved_policy();
    const auto probes = host::preview_starts();
    check(); // Explicit invisible checks still work after the viewing allowance is exhausted.
    EXPECT_EQ(host::preview_starts(), probes + 1);
    EXPECT_EQ(saved_policy(), exhausted);
    EXPECT_EQ(model.parental().remaining_seconds(), 0u);
    EXPECT_EQ(model.channel(1).playback_status, iptv::PlaybackStatus::unknown);
}
} // namespace
