/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_store.h"

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{
namespace fs = std::filesystem;

constexpr std::uint64_t kSourceId = 0x123456789abcdef0ULL;

iptv::Channel MakeChannel(std::size_t index, std::uint64_t source_id = kSourceId)
{
    iptv::Channel channel;
    channel.id = "channel-" + std::to_string(index);
    channel.source_id = source_id;
    channel.name = "Channel " + std::to_string(index);
    channel.url = "https://primary.example/live/" + std::to_string(index) + ".m3u8";
    channel.alternate_urls = {
        "https://backup-one.example/live/" + std::to_string(index),
        "https://backup-two.example/live/" + std::to_string(index),
    };
    channel.tvg_id = "station." + std::to_string(index);
    channel.tvg_name = "Guide name " + std::to_string(index);
    channel.tvg_logo = "https://logos.example/" + std::to_string(index) + ".png";
    channel.group_title = "News";
    channel.alternate_group_titles = {"Local", "International"};
    channel.tvg_country = "US";
    channel.tvg_language = "English";
    channel.http_user_agent = "ProsperoTV-test/1.0 (channel " + std::to_string(index) + ")";
    channel.http_referrer = "https://portal.example/watch/" + std::to_string(index);
    channel.source_line = static_cast<std::uint32_t>(index * 2u + 2u);
    channel.kind = static_cast<iptv::MediaKind>(index % 4u);
    channel.duration_secs = static_cast<std::uint32_t>(index % 5000u);
    channel.year = static_cast<std::uint16_t>(1990u + index % 30u);
    channel.rating_tenths = static_cast<std::uint16_t>(index % 101u);
    channel.container_ext = "mkv";
    channel.series_id = "series-" + std::to_string(index);
    channel.season = static_cast<std::uint16_t>(index % 20u);
    channel.episode = static_cast<std::uint16_t>(index % 50u);
    return channel;
}

iptv::Catalog MakeCatalog(std::size_t count, std::uint64_t source_id = kSourceId)
{
    iptv::Catalog catalog;
    catalog.source_id = source_id;
    for (std::size_t index = 0; index < count; ++index)
        EXPECT_TRUE(catalog.Add(MakeChannel(index, source_id)));
    return catalog;
}

void ExpectChannelEquals(const iptv::ChannelView &expected, const iptv::ChannelView &actual)
{
    EXPECT_EQ(actual.id, expected.id);
    EXPECT_EQ(actual.source_id, expected.source_id);
    EXPECT_EQ(actual.name, expected.name);
    EXPECT_EQ(actual.url, expected.url);
    EXPECT_EQ(actual.alternate_urls, expected.alternate_urls);
    EXPECT_EQ(actual.tvg_id, expected.tvg_id);
    EXPECT_EQ(actual.tvg_name, expected.tvg_name);
    EXPECT_EQ(actual.tvg_logo, expected.tvg_logo);
    EXPECT_EQ(actual.group_title, expected.group_title);
    EXPECT_EQ(actual.alternate_group_titles, expected.alternate_group_titles);
    EXPECT_EQ(actual.tvg_country, expected.tvg_country);
    EXPECT_EQ(actual.tvg_language, expected.tvg_language);
    EXPECT_EQ(actual.http_user_agent, expected.http_user_agent);
    EXPECT_EQ(actual.http_referrer, expected.http_referrer);
    EXPECT_EQ(actual.source_line, expected.source_line);
    EXPECT_EQ(actual.kind, expected.kind);
    EXPECT_EQ(actual.duration_secs, expected.duration_secs);
    EXPECT_EQ(actual.year, expected.year);
    EXPECT_EQ(actual.rating_tenths, expected.rating_tenths);
    EXPECT_EQ(actual.container_ext, expected.container_ext);
    EXPECT_EQ(actual.series_id, expected.series_id);
    EXPECT_EQ(actual.season, expected.season);
    EXPECT_EQ(actual.episode, expected.episode);
    EXPECT_EQ(actual.playback_status, expected.playback_status);
    EXPECT_EQ(actual.playback_result, expected.playback_result);
    EXPECT_EQ(actual.playback_checked_unix, expected.playback_checked_unix);
}

class IptvStoreTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        static std::atomic<unsigned long long> sequence{0};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = fs::temp_directory_path() / ("prosperotv-store-test-" + std::to_string(tick) + "-" +
                                             std::to_string(sequence.fetch_add(1)));
        ASSERT_TRUE(fs::create_directories(root_));
        path_ = root_ / "catalog.sqlite3";
        history_path_ = root_ / "playback-history.sqlite3";
    }

    void TearDown() override
    {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    fs::path StagingPath() const
    {
        return fs::path(path_.string() + ".new");
    }
    fs::path BackupPath() const
    {
        return fs::path(path_.string() + ".bak");
    }

    static void WriteBytes(const fs::path &path, const std::string &bytes)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(output.good());
    }

    static void RunSql(const fs::path &path, const char *sql)
    {
        sqlite3 *database = nullptr;
        ASSERT_EQ(sqlite3_open(path.string().c_str(), &database), SQLITE_OK);
        char *message = nullptr;
        EXPECT_EQ(sqlite3_exec(database, sql, nullptr, nullptr, &message), SQLITE_OK)
            << (message ? message : "");
        sqlite3_free(message);
        sqlite3_close(database);
    }

    fs::path root_;
    fs::path path_;
    fs::path history_path_;
};

TEST_F(IptvStoreTest, RoundTripsEveryChannelFieldAlternatesAndHttpHeaders)
{
    const iptv::Catalog expected = MakeCatalog(2);
    iptv::StoreReport save_report;

    ASSERT_EQ(iptv::SaveCatalog(path_.string(), expected, {}, &save_report), iptv::StoreStatus::ok);
    EXPECT_EQ(save_report.status, iptv::StoreStatus::ok);
    EXPECT_EQ(save_report.records, expected.size());
    EXPECT_EQ(save_report.bytes, fs::file_size(path_));

    iptv::Catalog actual;
    iptv::StoreReport load_report;
    ASSERT_EQ(iptv::LoadCatalog(path_.string(), &actual, {}, &load_report), iptv::StoreStatus::ok);
    EXPECT_EQ(actual.source_id, expected.source_id);
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
        ExpectChannelEquals(expected[index], actual[index]);
    EXPECT_EQ(load_report.status, iptv::StoreStatus::ok);
    EXPECT_EQ(load_report.records, actual.size());
    EXPECT_EQ(load_report.bytes, save_report.bytes);
    EXPECT_GT(load_report.saved_unix, 0u);
}

TEST_F(IptvStoreTest, KeepsPlaylistOrderWhenIdsSortDifferently)
{
    // Rows are read in id order and put back in playlist order.
    iptv::Catalog expected;
    expected.source_id = kSourceId;
    for (std::size_t index = 0; index < 40; ++index)
    {
        iptv::Channel channel = MakeChannel(index);
        channel.id = "id-" + std::to_string(1000 - index * 7 % 41);
        ASSERT_TRUE(expected.Add(channel));
    }
    ASSERT_EQ(iptv::SaveCatalog(path_.string(), expected), iptv::StoreStatus::ok);
    iptv::Catalog loaded;
    ASSERT_EQ(iptv::LoadCatalog(path_.string(), &loaded), iptv::StoreStatus::ok);
    ASSERT_EQ(loaded.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
        ExpectChannelEquals(expected[index], loaded[index]);
}

TEST_F(IptvStoreTest, RestoresAValidBackupWhenPrimaryIsCorrupt)
{
    const iptv::Catalog expected = MakeCatalog(2, 55);
    ASSERT_EQ(iptv::SaveCatalog(path_.string(), expected), iptv::StoreStatus::ok);
    ASSERT_TRUE(fs::copy_file(path_, BackupPath()));
    WriteBytes(path_, "not a sqlite database");

    iptv::Catalog recovered;
    ASSERT_EQ(iptv::LoadCatalog(path_.string(), &recovered), iptv::StoreStatus::ok);
    EXPECT_EQ(recovered.source_id, expected.source_id);
    EXPECT_EQ(recovered.size(), expected.size());
    EXPECT_TRUE(fs::is_regular_file(path_));
    EXPECT_FALSE(fs::exists(BackupPath()));
}

TEST_F(IptvStoreTest, SuccessfulReplacementCleansStagingAndBackupFiles)
{
    const iptv::Catalog first = MakeCatalog(1, 11);
    ASSERT_EQ(iptv::SaveCatalog(path_.string(), first), iptv::StoreStatus::ok);

    WriteBytes(StagingPath(), "stale staging");
    WriteBytes(BackupPath(), "stale backup");
    const iptv::Catalog replacement = MakeCatalog(3, 22);
    ASSERT_EQ(iptv::SaveCatalog(path_.string(), replacement), iptv::StoreStatus::ok);

    EXPECT_TRUE(fs::is_regular_file(path_));
    EXPECT_FALSE(fs::exists(StagingPath()));
    EXPECT_FALSE(fs::exists(BackupPath()));

    iptv::Catalog loaded;
    ASSERT_EQ(iptv::LoadCatalog(path_.string(), &loaded), iptv::StoreStatus::ok);
    EXPECT_EQ(loaded.source_id, replacement.source_id);
    ASSERT_EQ(loaded.size(), replacement.size());
    ExpectChannelEquals(replacement.back(), loaded.back());
}

TEST_F(IptvStoreTest, FailedSaveRemovesStagingAndPreservesLastGoodPrimary)
{
    const iptv::Catalog good = MakeCatalog(1);
    ASSERT_EQ(iptv::SaveCatalog(path_.string(), good), iptv::StoreStatus::ok);
    WriteBytes(StagingPath(), "stale staging");

    iptv::StoreLimits limits;
    limits.max_file_bytes = 1u;
    iptv::StoreReport report;
    EXPECT_EQ(iptv::SaveCatalog(path_.string(), MakeCatalog(2, 22), limits, &report),
              iptv::StoreStatus::too_large);
    EXPECT_EQ(report.status, iptv::StoreStatus::too_large);
    EXPECT_FALSE(fs::exists(StagingPath()));

    iptv::Catalog loaded;
    ASSERT_EQ(iptv::LoadCatalog(path_.string(), &loaded), iptv::StoreStatus::ok);
    ASSERT_EQ(loaded.size(), 1u);
    ExpectChannelEquals(good.front(), loaded.front());
}

TEST_F(IptvStoreTest, RejectsCorruptDatabaseWithoutMutatingOutputAndSaveRecovers)
{
    WriteBytes(path_, "this is not a sqlite database");
    iptv::Catalog output = MakeCatalog(1, 77);
    const iptv::Catalog sentinel = output;
    iptv::StoreReport corrupt_report;

    EXPECT_EQ(iptv::LoadCatalog(path_.string(), &output, {}, &corrupt_report),
              iptv::StoreStatus::corrupt);
    EXPECT_EQ(corrupt_report.status, iptv::StoreStatus::corrupt);
    EXPECT_EQ(corrupt_report.records, 0u);
    ASSERT_EQ(output.size(), sentinel.size());
    EXPECT_EQ(output.source_id, sentinel.source_id);
    ExpectChannelEquals(sentinel.front(), output.front());

    const iptv::Catalog recovered = MakeCatalog(2, 88);
    ASSERT_EQ(iptv::SaveCatalog(path_.string(), recovered), iptv::StoreStatus::ok);
    EXPECT_FALSE(fs::exists(StagingPath()));
    EXPECT_FALSE(fs::exists(BackupPath()));

    iptv::Catalog loaded;
    ASSERT_EQ(iptv::LoadCatalog(path_.string(), &loaded), iptv::StoreStatus::ok);
    EXPECT_EQ(loaded.source_id, recovered.source_id);
    ASSERT_EQ(loaded.size(), recovered.size());
    ExpectChannelEquals(recovered.front(), loaded.front());
}

TEST_F(IptvStoreTest, EnforcesSaveAndLoadFileSizeLimits)
{
    const iptv::Catalog original = MakeCatalog(2);
    ASSERT_EQ(iptv::SaveCatalog(path_.string(), original), iptv::StoreStatus::ok);
    const std::uintmax_t original_size = fs::file_size(path_);

    iptv::StoreLimits load_limits;
    load_limits.max_file_bytes = static_cast<std::size_t>(original_size - 1u);
    iptv::Catalog output = MakeCatalog(1, 999);
    iptv::StoreReport load_report;
    EXPECT_EQ(iptv::LoadCatalog(path_.string(), &output, load_limits, &load_report),
              iptv::StoreStatus::too_large);
    EXPECT_EQ(load_report.bytes, original_size);
    EXPECT_EQ(output.source_id, 999u);

    iptv::StoreLimits save_limits;
    save_limits.max_file_bytes = 1u;
    iptv::StoreReport save_report;
    EXPECT_EQ(iptv::SaveCatalog(path_.string(), MakeCatalog(3, 333), save_limits, &save_report),
              iptv::StoreStatus::too_large);
    EXPECT_GT(save_report.bytes, save_limits.max_file_bytes);
    EXPECT_FALSE(fs::exists(StagingPath()));

    iptv::Catalog loaded;
    ASSERT_EQ(iptv::LoadCatalog(path_.string(), &loaded), iptv::StoreStatus::ok);
    EXPECT_EQ(loaded.source_id, original.source_id);
    EXPECT_EQ(loaded.size(), original.size());
}

TEST_F(IptvStoreTest, EnforcesChannelLimitsOnSaveAndLoad)
{
    const iptv::Catalog two_channels = MakeCatalog(2);
    ASSERT_EQ(iptv::SaveCatalog(path_.string(), two_channels), iptv::StoreStatus::ok);

    iptv::StoreLimits limits;
    limits.max_channels = 1u;
    iptv::StoreReport save_report;
    EXPECT_EQ(iptv::SaveCatalog(path_.string(), two_channels, limits, &save_report),
              iptv::StoreStatus::invalid_argument);
    EXPECT_EQ(save_report.status, iptv::StoreStatus::invalid_argument);

    iptv::Catalog output = MakeCatalog(1, 444);
    iptv::StoreReport load_report;
    EXPECT_EQ(iptv::LoadCatalog(path_.string(), &output, limits, &load_report),
              iptv::StoreStatus::corrupt);
    EXPECT_EQ(load_report.status, iptv::StoreStatus::corrupt);
    EXPECT_EQ(output.source_id, 444u);
}

TEST_F(IptvStoreTest, RoundTripsRepresentativeMultiThousandChannelCache)
{
    constexpr std::size_t kChannelCount = 4096u;
    const iptv::Catalog expected = MakeCatalog(kChannelCount);
    iptv::StoreReport save_report;
    ASSERT_EQ(iptv::SaveCatalog(path_.string(), expected, {}, &save_report), iptv::StoreStatus::ok);
    EXPECT_EQ(save_report.records, kChannelCount);

    iptv::Catalog actual;
    iptv::StoreReport load_report;
    ASSERT_EQ(iptv::LoadCatalog(path_.string(), &actual, {}, &load_report), iptv::StoreStatus::ok);
    EXPECT_EQ(load_report.records, kChannelCount);
    ASSERT_EQ(actual.size(), kChannelCount);
    ExpectChannelEquals(expected.front(), actual.front());
    ExpectChannelEquals(expected[kChannelCount / 2u], actual[kChannelCount / 2u]);
    ExpectChannelEquals(expected.back(), actual.back());
}

TEST_F(IptvStoreTest, PersistsPlaybackResultsAcrossCatalogRefreshesAndSources)
{
    ASSERT_EQ(iptv::RecordPlaybackResult(history_path_.string(), kSourceId, "channel-0", true, 0),
              iptv::StoreStatus::ok);
    ASSERT_EQ(iptv::RecordPlaybackResult(history_path_.string(), kSourceId, "channel-1", false, -6),
              iptv::StoreStatus::ok);

    iptv::Catalog refreshed = MakeCatalog(3);
    ASSERT_EQ(iptv::LoadPlaybackResults(history_path_.string(), kSourceId, &refreshed),
              iptv::StoreStatus::ok);
    EXPECT_EQ(refreshed[0].playback_status, iptv::PlaybackStatus::playable);
    EXPECT_EQ(refreshed[0].playback_result, 0);
    EXPECT_GT(refreshed[0].playback_checked_unix, 0u);
    EXPECT_EQ(refreshed[1].playback_status, iptv::PlaybackStatus::failed);
    EXPECT_EQ(refreshed[1].playback_result, -6);
    EXPECT_GT(refreshed[1].playback_checked_unix, 0u);
    EXPECT_EQ(refreshed[2].playback_status, iptv::PlaybackStatus::unknown);

    iptv::Catalog other_source = MakeCatalog(1, 99);
    ASSERT_EQ(iptv::LoadPlaybackResults(history_path_.string(), 99, &other_source),
              iptv::StoreStatus::ok);
    EXPECT_EQ(other_source[0].playback_status, iptv::PlaybackStatus::unknown);
}

TEST_F(IptvStoreTest, LoadsVersionOneCachesWithDefaultMediaFields)
{
    // Exactly what the previous release wrote: no movie or series columns.
    RunSql(path_,
           "CREATE TABLE metadata(key TEXT PRIMARY KEY NOT NULL,value INTEGER NOT NULL) "
           "WITHOUT ROWID;"
           "CREATE TABLE channels(id TEXT PRIMARY KEY NOT NULL,source_id INTEGER NOT NULL,"
           "position INTEGER NOT NULL,source_line INTEGER NOT NULL,name TEXT NOT NULL,"
           "url TEXT NOT NULL,tvg_id TEXT NOT NULL,tvg_name TEXT NOT NULL,tvg_logo TEXT NOT NULL,"
           "group_title TEXT NOT NULL,tvg_country TEXT NOT NULL,tvg_language TEXT NOT NULL,"
           "user_agent TEXT NOT NULL,referrer TEXT NOT NULL) WITHOUT ROWID;"
           "CREATE UNIQUE INDEX channels_position ON channels(position);"
           "CREATE TABLE alternate_urls(channel_id TEXT NOT NULL,position INTEGER NOT NULL,"
           "url TEXT NOT NULL,PRIMARY KEY(channel_id,position)) WITHOUT ROWID;"
           "CREATE TABLE alternate_groups(channel_id TEXT NOT NULL,position INTEGER NOT NULL,"
           "value TEXT NOT NULL,PRIMARY KEY(channel_id,position)) WITHOUT ROWID;"
           "INSERT INTO metadata VALUES('source_id',77),('saved_unix',1700000000);"
           "INSERT INTO channels VALUES('old-1',77,0,4,'Old Channel','https://old.example/1.m3u8',"
           "'','','','News','','','','');"
           "INSERT INTO alternate_urls VALUES('old-1',0,'https://old.example/backup');"
           "PRAGMA user_version=1;");

    iptv::Catalog loaded;
    ASSERT_EQ(iptv::LoadCatalog(path_.string(), &loaded), iptv::StoreStatus::ok);
    EXPECT_EQ(loaded.source_id, 77u);
    ASSERT_EQ(loaded.size(), 1u);
    const iptv::ChannelView channel = loaded[0];
    EXPECT_EQ(channel.name, "Old Channel");
    EXPECT_EQ(channel.alternate_urls, std::vector<std::string>{"https://old.example/backup"});
    EXPECT_EQ(channel.kind, iptv::MediaKind::live);
    EXPECT_EQ(channel.duration_secs, 0u);
    EXPECT_EQ(channel.year, 0u);
    EXPECT_TRUE(channel.container_ext.empty());
    EXPECT_TRUE(channel.series_id.empty());

    // Saving rewrites the cache in the current format and keeps it loadable.
    ASSERT_EQ(iptv::SaveCatalog(path_.string(), loaded), iptv::StoreStatus::ok);
    iptv::Catalog reloaded;
    ASSERT_EQ(iptv::LoadCatalog(path_.string(), &reloaded), iptv::StoreStatus::ok);
    ExpectChannelEquals(channel, reloaded[0]);
}

TEST_F(IptvStoreTest, RejectsUnknownSchemaVersions)
{
    ASSERT_EQ(iptv::SaveCatalog(path_.string(), MakeCatalog(1)), iptv::StoreStatus::ok);
    RunSql(path_, "PRAGMA user_version=4;");
    iptv::Catalog loaded;
    EXPECT_EQ(iptv::LoadCatalog(path_.string(), &loaded), iptv::StoreStatus::unsupported_version);
}

TEST_F(IptvStoreTest, RejectsOutOfRangeMediaFieldsOnSaveAndLoad)
{
    const auto expect_unsaved = [&](auto mutate)
    {
        iptv::Channel changed = MakeChannel(1, kSourceId);
        mutate(changed);
        iptv::Catalog catalog = MakeCatalog(1);
        EXPECT_TRUE(catalog.Add(changed));
        iptv::StoreReport report;
        EXPECT_EQ(iptv::SaveCatalog(path_.string(), catalog, {}, &report),
                  iptv::StoreStatus::invalid_argument);
        EXPECT_EQ(report.status, iptv::StoreStatus::invalid_argument);
        EXPECT_FALSE(fs::exists(path_));
        EXPECT_FALSE(fs::exists(StagingPath()));
    };
    expect_unsaved([](iptv::Channel &channel) { channel.container_ext = "thirteenchars"; });
    expect_unsaved([](iptv::Channel &channel) { channel.series_id.assign(65u, 's'); });
    expect_unsaved([](iptv::Channel &channel) { channel.kind = static_cast<iptv::MediaKind>(9); });
    expect_unsaved([](iptv::Channel &channel) { channel.year = 3000u; });
    expect_unsaved([](iptv::Channel &channel) { channel.rating_tenths = 101u; });
    expect_unsaved([](iptv::Channel &channel) { channel.duration_secs = 24u * 3600u + 1u; });

    // A damaged file with an impossible value is reported as corrupt, not clamped.
    ASSERT_EQ(iptv::SaveCatalog(path_.string(), MakeCatalog(2)), iptv::StoreStatus::ok);
    RunSql(path_, "UPDATE channels SET kind=9 WHERE position=1;");
    iptv::Catalog loaded;
    EXPECT_EQ(iptv::LoadCatalog(path_.string(), &loaded), iptv::StoreStatus::corrupt);
}

TEST_F(IptvStoreTest, LibraryLimitsHoldAWholeLibrary)
{
    const iptv::StoreLimits library = iptv::LibraryStoreLimits();
    EXPECT_GE(library.max_channels, iptv::kDefaultMaxLibraryEntries);
    EXPECT_GE(library.max_file_bytes, iptv::kDefaultMaxStoreBytes);

    const iptv::Catalog catalog = MakeCatalog(3);
    iptv::StoreLimits small = library;
    small.max_channels = 2;
    EXPECT_EQ(iptv::SaveCatalog(path_.string(), catalog, small),
              iptv::StoreStatus::invalid_argument);
    ASSERT_EQ(iptv::SaveCatalog(path_.string(), catalog, library), iptv::StoreStatus::ok);
    iptv::Catalog loaded;
    EXPECT_EQ(iptv::LoadCatalog(path_.string(), &loaded, small), iptv::StoreStatus::corrupt);
    ASSERT_EQ(iptv::LoadCatalog(path_.string(), &loaded, library), iptv::StoreStatus::ok);
    ExpectChannelEquals(catalog.back(), loaded.back());
}

TEST_F(IptvStoreTest, ResumePositionsRoundTripAndClearWhenStartedOrFinished)
{
    const std::string path = history_path_.string();
    iptv::ResumeEntry entry;
    EXPECT_EQ(iptv::LoadResumePosition(path, kSourceId, "movie-1", &entry),
              iptv::StoreStatus::not_found);

    ASSERT_EQ(iptv::SaveResumePosition(path, kSourceId, "movie-1", 1200u, 6000u),
              iptv::StoreStatus::ok);
    ASSERT_EQ(iptv::LoadResumePosition(path, kSourceId, "movie-1", &entry), iptv::StoreStatus::ok);
    EXPECT_EQ(entry.channel_id, "movie-1");
    EXPECT_EQ(entry.position_secs, 1200u);
    EXPECT_EQ(entry.duration_secs, 6000u);
    EXPECT_GT(entry.updated_unix, 0u);

    // Updating replaces the stored position.
    ASSERT_EQ(iptv::SaveResumePosition(path, kSourceId, "movie-1", 1800u, 6000u),
              iptv::StoreStatus::ok);
    ASSERT_EQ(iptv::LoadResumePosition(path, kSourceId, "movie-1", &entry), iptv::StoreStatus::ok);
    EXPECT_EQ(entry.position_secs, 1800u);

    // Another source's identical id is independent.
    EXPECT_EQ(iptv::LoadResumePosition(path, kSourceId + 1u, "movie-1", &entry),
              iptv::StoreStatus::not_found);

    // Barely started, inside the final 30 seconds, and inside the last 5% all clear it.
    for (const std::uint32_t position : {5u, 10u, 5975u, 5800u, 6000u, 7000u})
    {
        ASSERT_EQ(iptv::SaveResumePosition(path, kSourceId, "movie-1", 1800u, 6000u),
                  iptv::StoreStatus::ok);
        ASSERT_EQ(iptv::SaveResumePosition(path, kSourceId, "movie-1", position, 6000u),
                  iptv::StoreStatus::ok)
            << position;
        EXPECT_EQ(iptv::LoadResumePosition(path, kSourceId, "movie-1", &entry),
                  iptv::StoreStatus::not_found)
            << position;
    }

    // An unknown duration only applies the "not started" rule.
    ASSERT_EQ(iptv::SaveResumePosition(path, kSourceId, "movie-2", 4000u, 0u),
              iptv::StoreStatus::ok);
    EXPECT_EQ(iptv::LoadResumePosition(path, kSourceId, "movie-2", &entry), iptv::StoreStatus::ok);

    EXPECT_EQ(iptv::SaveResumePosition(path, kSourceId, "", 100u, 200u),
              iptv::StoreStatus::invalid_argument);
}

TEST_F(IptvStoreTest, ResumePositionsListNewestFirstAndStayBounded)
{
    const std::string path = history_path_.string();
    std::vector<iptv::ResumeEntry> entries;
    EXPECT_EQ(iptv::LoadResumePositions(path, kSourceId, 10u, &entries),
              iptv::StoreStatus::not_found);

    // Resume rows can share a database with playback results that already exist.
    ASSERT_EQ(iptv::RecordPlaybackResult(path, kSourceId, "channel-0", true, 0),
              iptv::StoreStatus::ok);
    EXPECT_EQ(iptv::LoadResumePositions(path, kSourceId, 10u, &entries), iptv::StoreStatus::ok);
    EXPECT_TRUE(entries.empty());

    for (std::size_t index = 0; index < iptv::kMaxResumeEntries + 20u; ++index)
        ASSERT_EQ(iptv::SaveResumePosition(path, kSourceId, "title-" + std::to_string(index),
                                           100u + static_cast<std::uint32_t>(index), 100000u),
                  iptv::StoreStatus::ok);
    ASSERT_EQ(iptv::LoadResumePositions(path, kSourceId, 1000u, &entries), iptv::StoreStatus::ok);
    EXPECT_EQ(entries.size(), iptv::kMaxResumeEntries);
    // Saves in one second still come back strictly newest first.
    const std::string newest = "title-" + std::to_string(iptv::kMaxResumeEntries + 19u);
    EXPECT_EQ(entries.front().channel_id, newest);
    EXPECT_EQ(entries.back().channel_id, "title-20");
    for (std::size_t index = 1; index < entries.size(); ++index)
        EXPECT_GE(entries[index - 1].updated_unix, entries[index].updated_unix);
    iptv::ResumeEntry entry;
    EXPECT_EQ(iptv::LoadResumePosition(path, kSourceId, newest, &entry), iptv::StoreStatus::ok);
    EXPECT_EQ(iptv::LoadResumePosition(path, kSourceId, "title-0", &entry),
              iptv::StoreStatus::not_found);

    ASSERT_EQ(iptv::LoadResumePositions(path, kSourceId, 5u, &entries), iptv::StoreStatus::ok);
    EXPECT_EQ(entries.size(), 5u);
    EXPECT_EQ(iptv::LoadResumePositions(path, kSourceId, 0u, &entries),
              iptv::StoreStatus::invalid_argument);

    // The playback-result table in the same file is untouched.
    iptv::Catalog catalog = MakeCatalog(1);
    ASSERT_EQ(iptv::LoadPlaybackResults(path, kSourceId, &catalog), iptv::StoreStatus::ok);
    EXPECT_EQ(catalog[0].playback_status, iptv::PlaybackStatus::playable);
}
TEST_F(IptvStoreTest, ResumePositionsWorkInAPlaybackHistoryFromThePreviousRelease)
{
    RunSql(history_path_,
           "CREATE TABLE playback_results(source_id INTEGER NOT NULL,channel_id TEXT NOT NULL,"
           "playable INTEGER NOT NULL CHECK(playable IN(0,1)),result INTEGER NOT NULL,"
           "checked_unix INTEGER NOT NULL,PRIMARY KEY(source_id,channel_id)) WITHOUT ROWID;"
           "INSERT INTO playback_results VALUES(1,'channel-0',1,0,1700000000);"
           "PRAGMA user_version=1;");
    const std::string path = history_path_.string();
    std::vector<iptv::ResumeEntry> entries;
    iptv::ResumeEntry entry;
    EXPECT_EQ(iptv::LoadResumePositions(path, 1u, 10u, &entries), iptv::StoreStatus::not_found);
    EXPECT_EQ(iptv::LoadResumePosition(path, 1u, "movie-1", &entry), iptv::StoreStatus::not_found);

    ASSERT_EQ(iptv::SaveResumePosition(path, 1u, "movie-1", 600u, 7200u), iptv::StoreStatus::ok);
    ASSERT_EQ(iptv::LoadResumePosition(path, 1u, "movie-1", &entry), iptv::StoreStatus::ok);
    EXPECT_EQ(entry.position_secs, 600u);

    // Existing playback results survive the table being added.
    iptv::Catalog catalog = MakeCatalog(1, 1);
    ASSERT_EQ(iptv::LoadPlaybackResults(path, 1u, &catalog), iptv::StoreStatus::ok);
    EXPECT_EQ(catalog[0].playback_status, iptv::PlaybackStatus::playable);
}
TEST_F(IptvStoreTest, RoundTripsMoreThanAHundredThousandChannels)
{
    constexpr std::size_t kChannelCount = 120000u;
    iptv::Catalog expected;
    expected.source_id = kSourceId;
    for (std::size_t index = 0; index < kChannelCount; ++index)
    {
        iptv::Channel channel = MakeChannel(index);
        // One channel in ten has other addresses, as in a real list.
        if (index % 10u != 0)
        {
            channel.alternate_urls.clear();
            channel.alternate_group_titles.clear();
        }
        ASSERT_TRUE(expected.Add(channel));
    }
    iptv::StoreReport save_report;
    ASSERT_EQ(iptv::SaveCatalog(path_.string(), expected, {}, &save_report), iptv::StoreStatus::ok);
    EXPECT_EQ(save_report.records, kChannelCount);
    // The copy on the console is about the size of what it holds.
    EXPECT_LT(save_report.bytes / kChannelCount, 420u);

    iptv::Catalog actual;
    iptv::StoreReport load_report;
    ASSERT_EQ(iptv::LoadCatalog(path_.string(), &actual, {}, &load_report), iptv::StoreStatus::ok);
    ASSERT_EQ(actual.size(), kChannelCount);
    for (const std::size_t index :
         {std::size_t{0}, std::size_t{9}, std::size_t{10}, kChannelCount / 2u, kChannelCount - 1u})
        ExpectChannelEquals(expected[index], actual[index]);
    EXPECT_EQ(actual.Find("channel-119999"), kChannelCount - 1u);
}

TEST_F(IptvStoreTest, AChannelThatCannotBeStoredFailsTheSaveAndKeepsTheOldCopy)
{
    ASSERT_EQ(iptv::SaveCatalog(path_.string(), MakeCatalog(2)), iptv::StoreStatus::ok);
    iptv::Catalog broken = MakeCatalog(1, 31);
    iptv::Channel nameless = MakeChannel(1, 31);
    nameless.name.clear();
    ASSERT_TRUE(broken.Add(nameless));
    iptv::StoreReport report;
    EXPECT_EQ(iptv::SaveCatalog(path_.string(), broken, {}, &report),
              iptv::StoreStatus::invalid_argument);
    EXPECT_EQ(report.records, 0u);
    EXPECT_FALSE(fs::exists(StagingPath()));

    iptv::Catalog loaded;
    ASSERT_EQ(iptv::LoadCatalog(path_.string(), &loaded), iptv::StoreStatus::ok);
    EXPECT_EQ(loaded.source_id, kSourceId);
    EXPECT_EQ(loaded.size(), 2u);
}

// The copy an earlier release left on the console: every table keyed by the
// channel's id, a source id on every row, and indexes nothing read.
void WriteVersionOneFile(const fs::path &path)
{
    sqlite3 *database = nullptr;
    ASSERT_EQ(sqlite3_open(path.string().c_str(), &database), SQLITE_OK);
    const char *sql =
        "CREATE TABLE metadata(key TEXT PRIMARY KEY NOT NULL,value INTEGER NOT NULL) WITHOUT ROWID;"
        "CREATE TABLE channels("
        "id TEXT PRIMARY KEY NOT NULL,source_id INTEGER NOT NULL,position INTEGER NOT NULL,"
        "source_line INTEGER NOT NULL,name TEXT NOT NULL,url TEXT NOT NULL,tvg_id TEXT NOT NULL,"
        "tvg_name TEXT NOT NULL,tvg_logo TEXT NOT NULL,group_title TEXT NOT NULL,"
        "tvg_country TEXT NOT NULL,tvg_language TEXT NOT NULL,user_agent TEXT NOT NULL,"
        "referrer TEXT NOT NULL) WITHOUT ROWID;"
        "CREATE UNIQUE INDEX channels_position ON channels(position);"
        "CREATE INDEX channels_name ON channels(name COLLATE NOCASE);"
        "CREATE TABLE alternate_urls(channel_id TEXT NOT NULL,position INTEGER NOT NULL,"
        "url TEXT NOT NULL,PRIMARY KEY(channel_id,position)) WITHOUT ROWID;"
        "CREATE TABLE alternate_groups(channel_id TEXT NOT NULL,position INTEGER NOT NULL,"
        "value TEXT NOT NULL,PRIMARY KEY(channel_id,position)) WITHOUT ROWID;"
        "PRAGMA user_version=1;"
        "INSERT INTO metadata VALUES('source_id',77),('saved_unix',1700000000);"
        // Written out of order: the position says where each one goes.
        "INSERT INTO channels VALUES('zeta',77,1,4,'Zeta','https://z.example/live','z.id','Zeta',"
        "'','Sports','US','English','','');"
        "INSERT INTO channels VALUES('alpha',77,0,2,'Alpha','https://a.example/live','a.id',"
        "'Alpha "
        "TV','https://logos.example/a.png','News','GB','English','Agent/1','https://r.example');"
        "INSERT INTO alternate_urls VALUES('alpha',1,'https://a2.example/live'),"
        "('alpha',0,'https://a1.example/live');"
        "INSERT INTO alternate_groups VALUES('zeta',0,'Local');";
    char *message = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &message);
    EXPECT_EQ(result, SQLITE_OK) << (message != nullptr ? message : "");
    sqlite3_free(message);
    sqlite3_close(database);
}

TEST_F(IptvStoreTest, ReadsTheCopyAnEarlierReleaseSaved)
{
    WriteVersionOneFile(path_);
    iptv::Catalog loaded;
    iptv::StoreReport report;
    ASSERT_EQ(iptv::LoadCatalog(path_.string(), &loaded, {}, &report), iptv::StoreStatus::ok);
    EXPECT_EQ(loaded.source_id, 77u);
    EXPECT_EQ(report.saved_unix, 1700000000u);
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].id, "alpha");
    EXPECT_EQ(loaded[0].tvg_name, "Alpha TV");
    EXPECT_EQ(loaded[0].tvg_logo, "https://logos.example/a.png");
    EXPECT_EQ(loaded[0].http_user_agent, "Agent/1");
    EXPECT_EQ(loaded[0].http_referrer, "https://r.example");
    EXPECT_EQ(loaded[0].source_line, 2u);
    ASSERT_EQ(loaded[0].alternate_urls.size(), 2u);
    EXPECT_EQ(loaded[0].alternate_urls[0], "https://a1.example/live");
    EXPECT_EQ(loaded[0].alternate_urls[1], "https://a2.example/live");
    EXPECT_EQ(loaded[1].id, "zeta");
    ASSERT_EQ(loaded[1].alternate_group_titles.size(), 1u);
    EXPECT_EQ(loaded[1].alternate_group_titles[0], "Local");

    // Saved again, it is in today's form and reads the same.
    ASSERT_EQ(iptv::SaveCatalog(path_.string(), loaded), iptv::StoreStatus::ok);
    iptv::Catalog again;
    ASSERT_EQ(iptv::LoadCatalog(path_.string(), &again), iptv::StoreStatus::ok);
    ASSERT_EQ(again.size(), 2u);
    ExpectChannelEquals(loaded[0], again[0]);
    ExpectChannelEquals(loaded[1], again[1]);
}

} // namespace
