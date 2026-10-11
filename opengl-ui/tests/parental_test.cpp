// ProsperoTV - Parental policy, persistent retry delay and playback entry points.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/model.hpp"
#include "tv/parental.hpp"
#include "tv/preview.hpp"
#include "host_preview.hpp"
#include "tv/backup.hpp"
#include "host_platform.hpp"
#include "iptv_store.h"
#include "iptv_ime.h"
#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <thread>

namespace
{
class ParentalTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        host::reset();
        char pattern[] = "/tmp/prosperotv-parental-XXXXXX";
        dir = mkdtemp(pattern);
        file = dir + "/prosperotv-parental-v1.txt";
    }
    void TearDown() override
    {
        host::reset();
        std::filesystem::remove_all(dir);
    }
    static void answer(ptv::Model &model, const char *pin)
    {
        model.poll();
        host::set_keyboard_text(pin);
        model.poll();
    }
    static void unlock(ptv::Model &model)
    {
        model.parental_action(ptv::Model::ParentalAction::unlock);
        answer(model, "836295");
        ASSERT_TRUE(model.parental().unlocked());
    }
    static void settle(ptv::Model &model)
    {
        for (unsigned i = 0; i < 400 && model.refreshing(); ++i)
        {
            model.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ASSERT_FALSE(model.refreshing());
    }
    std::string dir, file;
};

TEST_F(ParentalTest, PinHashRetryDelayAndKidsModeSurviveRestartAndBackup)
{
    ptv::Parental pin;
    ASSERT_TRUE(pin.load(file));
    EXPECT_TRUE(pin.unlocked());
    EXPECT_FALSE(pin.set_pin("abc"));
    ASSERT_TRUE(pin.set_pin("836295"));
    EXPECT_FALSE(pin.unlocked());
    {
        std::ifstream in(file);
        const std::string text{std::istreambuf_iterator<char>(in), {}};
        EXPECT_EQ(text.find("836295"), std::string::npos);
    }
    for (int i = 0; i < 5; ++i)
        EXPECT_FALSE(pin.unlock("0000", 1000));
    EXPECT_EQ(pin.wait_until(), 1030u);
    ASSERT_TRUE(pin.load(file));
    EXPECT_FALSE(pin.unlock("836295", 1029));
    ASSERT_TRUE(pin.unlock("836295", 1030));
    ASSERT_TRUE(pin.set_kids(true));
    EXPECT_FALSE(pin.unlocked());
    ASSERT_TRUE(pin.load(file));
    EXPECT_TRUE(pin.kids_only());
    EXPECT_FALSE(pin.set_kids(false));
    EXPECT_FALSE(pin.remove());
    EXPECT_FALSE(pin.set_pin("1234"));
    std::string error;
    ASSERT_TRUE(ptv::backup_settings(dir, dir + "/backup.sqlite3", error)) << error;
    ASSERT_TRUE(pin.unlock("836295", 1100));
    ASSERT_TRUE(pin.remove());
    EXPECT_FALSE(pin.enabled());
    ASSERT_TRUE(ptv::restore_settings(dir, dir + "/backup.sqlite3", error)) << error;
    ASSERT_TRUE(pin.load(file));
    EXPECT_TRUE(pin.kids_only());
    EXPECT_FALSE(pin.unlocked());
    ASSERT_TRUE(pin.unlock("836295", 1200));
    ASSERT_TRUE(pin.set_kids(false));
}

TEST_F(ParentalTest, CorruptLinkedAndUnwritableSettingsFailClosed)
{
    std::ofstream(file) << "PTV_PARENTAL_1\ncorrupt";
    ptv::Parental pin;
    EXPECT_FALSE(pin.load(file));
    EXPECT_FALSE(pin.unlocked());
    EXPECT_FALSE(pin.set_pin("1234"));
    std::filesystem::remove(file);
    std::filesystem::create_symlink(dir + "/absent", file);
    EXPECT_FALSE(pin.load(file));
    EXPECT_FALSE(pin.unlocked());
    std::filesystem::remove(file);
    ASSERT_TRUE(pin.load(file));
    ASSERT_TRUE(pin.set_pin("836295"));
    std::filesystem::create_directory(file + ".tmp");
    EXPECT_FALSE(pin.unlock("836295", 1000));
    EXPECT_FALSE(pin.valid());
    EXPECT_FALSE(pin.unlocked());
}

TEST_F(ParentalTest, PinAndKidsPolicyCoverListsFavoritesDirectPlayPreviewGuideResumeAndPhone)
{
    const auto playlist = dir + "/list.m3u";
    std::ofstream(playlist)
        << "#EXTM3U\n"
           "#EXTINF:-1 group-title=\"US / Adult\",Restricted\nhttps://test.invalid/a\n"
           "#EXTINF:-1 group-title=\"US / Kids / Cartoons\",Cartoon\nhttps://test.invalid/b\n"
           "#EXTINF:-1 group-title=\"Movies\" is-adult=\"1\",Flagged\nhttps://test.invalid/c\n"
           "#EXTINF:-1 group-title=\"General\",General\nhttps://test.invalid/d\n";
    host::set_network(true, playlist);
    ptv::Model model(dir);
    ASSERT_TRUE(model.open());
    settle(model);
    ASSERT_EQ(model.channel_count(), 4u);
    EXPECT_EQ(model.toggle_favorite(0), ptv::Model::Starred::added);
    ASSERT_TRUE(model.play(0));
    ptv::PlayRequest request;
    ASSERT_TRUE(model.take_play_request(&request));
    model.parental_action(ptv::Model::ParentalAction::set_pin);
    answer(model, "836295");
    answer(model, "123456");
    EXPECT_FALSE(model.parental().enabled());
    EXPECT_FALSE(model.pin_prompt());
    model.parental_action(ptv::Model::ParentalAction::set_pin);
    answer(model, "836295");
    EXPECT_EQ(host::keyboard_title(), "Confirm parent PIN");
    answer(model, "836295");
    ASSERT_TRUE(model.parental().enabled());
    EXPECT_FALSE(model.parental().unlocked());
    EXPECT_EQ(model.visible_count(), 2u);
    EXPECT_FALSE(model.play(0));
    EXPECT_FALSE(model.play(2));
    EXPECT_FALSE(model.preview_request(model.channel(0).id));
    ptv::Programme programme;
    programme.start = 1;
    programme.end = INT64_MAX;
    EXPECT_FALSE(model.play_programme(0, programme));
    model.resume_last(true);
    EXPECT_FALSE(model.take_play_request(&request));
    model.set_group(ptv::Group::favorites);
    EXPECT_EQ(model.visible_count(), 0u);
    model.set_query("Restricted");
    EXPECT_EQ(model.visible_count(), 0u);
    std::string output;
    EXPECT_EQ(model.remote_sources({}, output), 403);
    EXPECT_EQ(model.remote_sources(
                  R"({"op":"save","kind":1,"name":"X","url":"https://other.invalid"})", output),
              403);
    model.add_source(iptv::SourceKind::Custom);
    EXPECT_FALSE(iptv_ime_busy());
    unlock(model);
    ASSERT_TRUE(model.play(0));
    EXPECT_TRUE(model.take_play_request(&request));
    ASSERT_TRUE(model.set_category_rule("General", 2));
    model.parental_action(ptv::Model::ParentalAction::kids);
    EXPECT_TRUE(model.parental().kids_only());
    EXPECT_FALSE(model.parental().unlocked());
    EXPECT_FALSE(model.play(0));
    EXPECT_FALSE(model.play(2));
    EXPECT_TRUE(model.play(1));
    EXPECT_TRUE(model.play(3));
    EXPECT_FALSE(model.set_category_rule("US / Adult", 2));
    unlock(model);
    ASSERT_TRUE(model.set_category_rule("US / Adult", 2));
    EXPECT_FALSE(model.play(0)); // Adult names/flags take precedence over kids approval.
    model.parental_action(ptv::Model::ParentalAction::kids);
    EXPECT_FALSE(model.parental().kids_only());
    EXPECT_TRUE(model.play(0));
    model.close();
    ptv::Model reopened(dir);
    ASSERT_TRUE(reopened.open());
    settle(reopened);
    EXPECT_FALSE(reopened.parental().unlocked());
    EXPECT_FALSE(reopened.play(0));
    EXPECT_EQ(reopened.category_rule("General"), 2);
    reopened.close();
}

TEST_F(ParentalTest, AdultMetadataMergesAndSurvivesCatalogCopiesAndCache)
{
    const auto catalog = iptv::ParseExtendedM3u(
        "#EXTM3U\n#EXTINF:-1 tvg-id=\"one\",One\nhttps://test.invalid/1\n"
        "#EXTINF:-1 tvg-id=\"one\" is-adult=\"1\",One\nhttps://test.invalid/2\n",
        123);
    ASSERT_EQ(catalog.size(), 1u);
    EXPECT_TRUE(catalog[0].adult);
    EXPECT_TRUE(catalog[0].Copy().adult);
    iptv::Catalog copy(catalog);
    EXPECT_TRUE(copy[0].adult);
    ASSERT_EQ(iptv::SaveCatalog(dir + "/cache.sqlite3", copy), iptv::StoreStatus::ok);
    iptv::Catalog loaded;
    iptv::StoreReport report;
    ASSERT_EQ(iptv::LoadCatalog(dir + "/cache.sqlite3", &loaded, {}, &report),
              iptv::StoreStatus::ok);
    EXPECT_TRUE(loaded[0].adult);
    EXPECT_EQ(report.catalog_version, 7u);
    std::vector<iptv::XtreamCategory> categories;
    ASSERT_EQ(
        iptv::ParseXtreamCategories(
            R"([{"category_id":1,"category_name":"Cinema","is_adult":1},{"category_id":2,"parent_id":1,"category_name":"HD"}])",
            &categories),
        iptv::XtreamStatus::ok);
    EXPECT_TRUE(categories[1].adult);
    ASSERT_EQ(
        iptv::ParseXtreamLiveStreams(
            R"([{"stream_id":1,"name":"One","category_id":2},{"stream_id":2,"name":"Two","is_adult":"1"}])",
            {"https://test.invalid", "u", "p"}, categories, 123, &loaded),
        iptv::XtreamStatus::ok);
    EXPECT_TRUE(loaded[0].adult);
    EXPECT_TRUE(loaded[1].adult);
    EXPECT_TRUE(ptv::adult_category("US / 18+ / Movies"));
    EXPECT_FALSE(ptv::adult_category("Adult Swim"));
    EXPECT_FALSE(ptv::kids_category("Kidnap movies"));
}

std::uint64_t local_clock(int day, int hour, int minute = 0)
{
    std::tm value{};
    value.tm_year = 126;
    value.tm_mon = 9;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_min = minute;
    value.tm_isdst = -1;
    return std::mktime(&value);
}
TEST_F(ParentalTest, ViewingReservationsSurviveRestartAndRefundOnlyOnOrderlyStop)
{
    const auto now = local_clock(10, 12);
    ptv::Parental pin;
    ASSERT_TRUE(pin.load(file));
    ASSERT_TRUE(pin.set_pin("836295"));
    EXPECT_FALSE(pin.set_limits(1, -1, 360));
    ASSERT_TRUE(pin.unlock("836295", now));
    ASSERT_TRUE(pin.set_limits(1, -1, 360));
    ASSERT_TRUE(pin.set_kids(true));
    ASSERT_TRUE(pin.viewing_tick(now, 1000000, true));
    EXPECT_EQ(pin.remaining_seconds(), 60u);
    ptv::Parental crashed;
    ASSERT_TRUE(crashed.load(file));
    EXPECT_EQ(crashed.remaining_seconds(), 30u);
    ASSERT_TRUE(pin.viewing_tick(now + 10, 11000000, false));
    ASSERT_TRUE(crashed.load(file));
    EXPECT_EQ(crashed.remaining_seconds(), 50u);
    ASSERT_TRUE(crashed.viewing_tick(now + 10, 1000000, true));
    // A stalled UI/control poll still charges all elapsed session time.
    EXPECT_FALSE(crashed.viewing_tick(now + 65, 56000000, true));
    EXPECT_FALSE(crashed.viewing_allowed(now + 65));
    ASSERT_TRUE(pin.load(file));
    EXPECT_EQ(pin.remaining_seconds(), 0u);
    // Another profile has independent allowance.
    ptv::Parental other;
    ASSERT_TRUE(other.load(dir + "/other"));
    EXPECT_TRUE(other.viewing_allowed(now));
}
TEST_F(ParentalTest, BedtimeWakeClockRollbackAndNewDay)
{
    const auto noon = local_clock(10, 12);
    ptv::Parental pin;
    ASSERT_TRUE(pin.load(file));
    ASSERT_TRUE(pin.set_pin("836295"));
    ASSERT_TRUE(pin.unlock("836295", noon));
    EXPECT_FALSE(pin.set_limits(1, 360, 360));
    ASSERT_TRUE(pin.set_limits(1, 1200, 360));
    ASSERT_TRUE(pin.set_kids(true));
    EXPECT_FALSE(pin.viewing_allowed(0));
    ASSERT_TRUE(pin.viewing_tick(noon, 1000000, false));
    EXPECT_FALSE(pin.viewing_allowed(noon - 121)); // Idle menu time also detects rollback.

    ASSERT_TRUE(pin.viewing_tick(noon, 1000000, true));
    EXPECT_FALSE(pin.viewing_tick(noon + 60, 61000000, true));
    EXPECT_FALSE(pin.viewing_allowed(local_clock(10, 20)));
    EXPECT_FALSE(pin.viewing_allowed(local_clock(11, 0)));
    EXPECT_FALSE(pin.viewing_allowed(local_clock(11, 5, 59)));
    ASSERT_TRUE(pin.viewing_tick(local_clock(11, 6), 62000000, true));
    EXPECT_EQ(pin.remaining_seconds(), 60u);
    EXPECT_FALSE(pin.viewing_allowed(noon));
    ASSERT_TRUE(pin.viewing_tick(local_clock(11, 6), 63000000, false));
    ASSERT_TRUE(pin.load(file));
    EXPECT_FALSE(pin.viewing_allowed(noon));
    // Small clock adjustment does not reset or add quota.
    EXPECT_TRUE(pin.viewing_allowed(local_clock(11, 6) + 30));
}
TEST_F(ParentalTest, ViewingWriteFailureAndMalformedLimitRecordFailClosed)
{
    const auto now = local_clock(10, 12);
    ptv::Parental pin;
    ASSERT_TRUE(pin.load(file));
    ASSERT_TRUE(pin.set_pin("836295"));
    ASSERT_TRUE(pin.unlock("836295", now));
    ASSERT_TRUE(pin.set_limits(1, -1, 360));
    ASSERT_TRUE(pin.set_kids(true));
    std::filesystem::create_directory(file + ".tmp");
    EXPECT_FALSE(pin.viewing_tick(now, 1000000, true));
    EXPECT_FALSE(pin.valid());
    EXPECT_FALSE(pin.viewing_allowed(now));
    std::filesystem::remove(file + ".tmp");
    std::ofstream(file, std::ios::app) << "junk";
    EXPECT_FALSE(pin.load(file));
    EXPECT_FALSE(pin.viewing_allowed(now));
}
TEST_F(ParentalTest, ViewingQuotaBlocksLivePreviewQueuedResumeAndPhoneWithoutHidingLists)
{
    const auto now = local_clock(10, 12);
    host::set_unix_time(now);
    host::set_monotonic_us(1000000);
    ptv::Parental pin;
    ASSERT_TRUE(pin.load(file));
    ASSERT_TRUE(pin.set_pin("836295"));
    ASSERT_TRUE(pin.unlock("836295", now));
    ASSERT_TRUE(pin.set_limits(1, -1, 360));
    ASSERT_TRUE(pin.set_kids(true));
    const auto playlist = dir + "/list.m3u";
    std::ofstream(playlist)
        << "#EXTM3U\n#EXTINF:-1 group-title=\"Kids\",Cartoon\nhttps://test.invalid/b\n";
    host::set_network(true, playlist);
    ptv::Model model(dir);
    ASSERT_TRUE(model.open());
    settle(model);
    ASSERT_EQ(model.visible_count(), 1u);
    ASSERT_TRUE(model.play(0));
    EXPECT_TRUE(model.preview_request(model.channel(0).id));
    std::vector<std::thread> ticks;
    for (int i = 0; i < 4; ++i)
        ticks.emplace_back([&] { EXPECT_EQ(model.viewing_control(), 0); });
    for (auto &thread : ticks)
        thread.join();
    host::set_monotonic_us(61000000);
    host::set_unix_time(now + 60);
    EXPECT_FALSE(model.viewing_tick(true));
    ptv::PlayRequest request;
    EXPECT_FALSE(model.take_play_request(&request));
    EXPECT_FALSE(model.play(0));
    EXPECT_FALSE(model.preview_request(model.channel(0).id));
    EXPECT_EQ(model.visible_count(), 1u);
    EXPECT_TRUE(model.probe_request(model.channel(0).id));
    model.resume_last(true);
    EXPECT_FALSE(model.take_play_request(&request));
    auto phone = model.remote_browse_view();
    phone.selected = model.channel(0).id;
    std::string response;
    EXPECT_EQ(model.remote_browse(phone.selection_request(), response), 409);
    EXPECT_EQ(response, "The channel could not be opened.");
    ptv::Programme current;
    current.start = now;
    current.end = now + 120;
    EXPECT_FALSE(model.play_programme(0, current));
    model.close();
}

TEST_F(ParentalTest, LegacyParentalFileMigratesWithoutResettingPolicy)
{
    ptv::Parental pin;
    ASSERT_TRUE(pin.load(file));
    ASSERT_TRUE(pin.set_pin("836295"));
    ASSERT_TRUE(pin.unlock("836295", 1000));
    ASSERT_TRUE(pin.set_kids(true));
    // Keep the original kids/failure row, discard only v2 limits.
    std::ifstream fresh(file);
    std::vector<std::string> lines;
    for (std::string line; std::getline(fresh, line);)
        lines.push_back(line);
    std::ofstream out(file);
    out << "PTV_PARENTAL_1\n" << lines[1] << '\n' << lines[2] << '\n' << lines[3] << '\n';
    out.close();
    ASSERT_TRUE(pin.load(file));
    EXPECT_TRUE(pin.kids_only());
    EXPECT_EQ(pin.allowance(), 0u);
    EXPECT_EQ(pin.bedtime(), -1);
    ASSERT_TRUE(pin.unlock("836295", 1001));
    ASSERT_TRUE(pin.set_limits(30, -1, 360));
    ASSERT_TRUE(pin.load(file));
    EXPECT_EQ(pin.allowance(), 30u);
}

TEST_F(ParentalTest, ShortPreviewFocusChangesAreFreeAndWorkerStartReservesBeforeDecoding)
{
    const auto wall = local_clock(10, 12);
    std::uint64_t mono = 1000000;
    ptv::Parental pin;
    ASSERT_TRUE(pin.load(file));
    ASSERT_TRUE(pin.set_pin("836295"));
    ASSERT_TRUE(pin.unlock("836295", wall));
    ASSERT_TRUE(pin.set_limits(1, -1, 360));
    ASSERT_TRUE(pin.set_kids(true));
    host::set_preview(true);
    ptv::LivePreview preview;
    unsigned reservations = 0;
    preview.configure([](auto, const auto &) { return 1u; }, [](auto) {}, false,
                      [&]
                      {
                          ++reservations;
                          return pin.viewing_tick(wall, mono, true);
                      });
    ptv::PlayRequest request;
    request.urls = {"https://test.invalid/channel.ts"};
    for (unsigned i = 0; i < 20; ++i)
    {
        request.channel_id = std::to_string(i);
        preview.update(request, .6f);
        mono += 600000;
    }
    EXPECT_EQ(reservations, 0u);
    EXPECT_EQ(host::preview_starts(), 0u);
    EXPECT_EQ(pin.remaining_seconds(), 60u);
    preview.update(request, .6f);
    ASSERT_EQ(reservations, 1u);
    ptv::Parental disk;
    ASSERT_TRUE(disk.load(file));
    EXPECT_EQ(disk.remaining_seconds(), 30u); // Committed before the worker starts.
    ASSERT_TRUE(preview.running());
    mono += 10000000;
    ASSERT_TRUE(pin.viewing_tick(wall + 10, mono, true));
    preview.clear();
    ASSERT_TRUE(pin.viewing_tick(wall + 10, mono, false));
    ASSERT_TRUE(disk.load(file));
    EXPECT_EQ(disk.remaining_seconds(), 50u);
    // No worker starts if the reservation cannot be acquired.
    const auto started = host::preview_starts();
    preview.configure([](auto, const auto &) { return 1u; }, [](auto) {}, true,
                      [] { return false; });
    preview.update(request, 0);
    EXPECT_FALSE(preview.running());
    EXPECT_EQ(host::preview_starts(), started);
}
TEST_F(ParentalTest, RepeatedDstHourDoesNotReopenBedtimeAndOrderlyStopRefunds)
{
    struct RestoreTimezone
    {
        bool present = std::getenv("TZ") != nullptr;
        std::string value = present ? std::getenv("TZ") : "";
        ~RestoreTimezone()
        {
            if (present)
                setenv("TZ", value.c_str(), 1);
            else
                unsetenv("TZ");
            tzset();
        }
    } restore;
    setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1);
    tzset();
    std::tm time{};
    time.tm_year = 126;
    time.tm_mon = 10;
    time.tm_mday = 1;
    time.tm_hour = 1;
    time.tm_min = 29;
    time.tm_sec = 50;
    time.tm_isdst = 1;
    const auto first = std::uint64_t(std::mktime(&time));
    ptv::Parental pin;
    ASSERT_TRUE(pin.load(file));
    ASSERT_TRUE(pin.set_pin("836295"));
    ASSERT_TRUE(pin.unlock("836295", first));
    ASSERT_TRUE(pin.set_limits(1, 90, 360));
    ASSERT_TRUE(pin.set_kids(true));
    ASSERT_TRUE(pin.viewing_tick(first, 1000000, true));
    EXPECT_FALSE(pin.viewing_tick(first + 10, 11000000, true));
    EXPECT_EQ(pin.remaining_seconds(), 50u);
    ASSERT_TRUE(pin.load(file));
    EXPECT_EQ(pin.remaining_seconds(), 50u);
    EXPECT_FALSE(pin.viewing_allowed(first + 2420)); // 01:10 again after DST ends.
    time.tm_hour = 6;
    time.tm_min = time.tm_sec = 0;
    time.tm_isdst = 0;
    const auto wake = std::uint64_t(std::mktime(&time));
    EXPECT_TRUE(pin.viewing_allowed(wake));
    EXPECT_EQ(pin.remaining_seconds(wake), 60u);
}
TEST_F(ParentalTest, LockedProfileCannotExportSharingCredentials)
{
    ptv::Model model(dir);
    ASSERT_TRUE(model.open());
    model.parental_action(ptv::Model::ParentalAction::set_pin);
    answer(model, "836295");
    answer(model, "836295");
    ASSERT_TRUE(model.parental().enabled());
    ASSERT_FALSE(model.parental().unlocked());
    ptv::SharedLibrary library;
    EXPECT_FALSE(model.export_shared_library(library));
    EXPECT_TRUE(library.sources.empty());
    EXPECT_TRUE(library.favorites.empty());
    model.parental_action(ptv::Model::ParentalAction::unlock);
    answer(model, "836295");
    ASSERT_TRUE(model.parental().unlocked());
    EXPECT_TRUE(model.export_shared_library(library));
}
} // namespace
