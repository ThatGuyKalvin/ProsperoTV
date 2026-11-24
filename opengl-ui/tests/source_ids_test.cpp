// ProsperoTV - Source IDs survive deletion, migration and failed writes.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/library.hpp"

#include <gtest/gtest.h>
#include <sqlite3.h>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <memory>

namespace
{
class SourceIdTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        char path[] = "/tmp/prosperotv-source-ids-XXXXXX";
        const char *created = mkdtemp(path);
        ASSERT_NE(created, nullptr);
        directory = created;
        db_path = directory + "/library.sqlite3";
    }

    void TearDown() override
    {
        if (!directory.empty())
            std::filesystem::remove_all(directory);
    }

    bool run_sql(const char *sql)
    {
        sqlite3 *db = nullptr;
        const int opened = sqlite3_open(db_path.c_str(), &db);
        const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> connection(db, sqlite3_close);
        return opened == SQLITE_OK && sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    }

    std::string directory, db_path;
};

TEST_F(SourceIdTest, NumbersShareOneSequenceAcrossSourceKinds)
{
    ptv::Library library;
    ASSERT_TRUE(library.open(db_path));

    ptv::SavedSource sources[] = {
        {0, 1, {}, "https://example.invalid/list.m3u", {}, {}, {}},
        {0, 2, {}, "https://example.invalid", "viewer", "test", {}},
        {0, 3, {}, "https://example.invalid/portal.php", {}, {}, "00:1A:79:12:34:56"},
        {0, 4, {}, "http://192.0.2.4", {}, {}, {}},
        {0, 5, {}, "http://192.0.2.5:9981", {}, {}, {}},
        {0, 1, "usb-list.m3u8", "/mnt/usb0/usb-list.m3u8", {}, {}, {}},
        {0, 1, "Named playlist", "https://example.invalid/named.m3u", {}, {}, {}}};
    const char *names[] = {"Playlist #5",   "Xtream account #6", "MAC-code portal #7",
                           "HDHomeRun #8",  "Tvheadend #9",      "usb-list.m3u8",
                           "Named playlist"};
    for (std::size_t i = 0; i < std::size(sources); ++i)
    {
        ASSERT_TRUE(library.save_source(&sources[i]));
        EXPECT_EQ(sources[i].id, static_cast<std::int64_t>(i) + 5);
        EXPECT_EQ(sources[i].name, names[i]);
        ASSERT_TRUE(library.remove_source(sources[i].id));
    }
}

TEST_F(SourceIdTest, OpensLegacyDatabaseAndRemembersDeletedIdsAfterReopening)
{
    ASSERT_TRUE(run_sql(
        "CREATE TABLE sources (id INTEGER PRIMARY KEY,kind INTEGER NOT NULL,name TEXT NOT NULL,"
        "url TEXT NOT NULL,username TEXT NOT NULL,password TEXT NOT NULL,mac TEXT NOT NULL,"
        "schedule INTEGER NOT NULL CHECK(schedule BETWEEN 0 AND 2));"
        "INSERT INTO sources VALUES(2,1,'Custom playlist','','','','',0),"
        "(16,1,'Legacy list','https://example.invalid/old.m3u','','','',2),"
        "(17,1,'Deleted list','https://example.invalid/deleted.m3u','','','',2);"));

    ptv::Library library;
    ASSERT_TRUE(library.open(db_path));
    auto saved = library.sources();
    ASSERT_EQ(saved.size(), 3u);
    EXPECT_EQ(saved[0].id, 2);
    EXPECT_EQ(saved[1].id, 16);
    EXPECT_EQ(saved[1].name, "Legacy list");
    ASSERT_TRUE(library.remove_source(17));
    ASSERT_TRUE(library.close());

    ASSERT_TRUE(library.open(db_path));
    ptv::SavedSource playlist{0, 1, {}, "https://example.invalid/new.m3u", {}, {}, {}};
    ASSERT_TRUE(library.save_source(&playlist));
    EXPECT_EQ(playlist.id, 18);
    EXPECT_EQ(playlist.name, "Playlist #18");
    playlist.schedule = ptv::RefreshSchedule::weekly;
    ASSERT_TRUE(library.save_source(&playlist));
    EXPECT_EQ(playlist.id, 18);
    ASSERT_TRUE(library.remove_source(18));

    ptv::SavedSource next{0, 1, {}, "https://example.invalid/next.m3u", {}, {}, {}};
    ASSERT_TRUE(library.save_source(&next));
    EXPECT_EQ(next.id, 19);
    EXPECT_EQ(next.name, "Playlist #19");
}

TEST_F(SourceIdTest, AFailedSaveRollsBackTheSourceAndSequence)
{
    ptv::Library library;
    ASSERT_TRUE(library.open(db_path));
    ASSERT_TRUE(run_sql("CREATE TRIGGER refuse_headers BEFORE INSERT ON source_extra_headers "
                        "BEGIN SELECT RAISE(ABORT,'Refuse headers'); END;"));
    ptv::SavedSource source{0, 1, {}, "https://example.invalid/input.m3u", {}, {}, {}};
    EXPECT_FALSE(library.save_source(&source));
    EXPECT_EQ(source.id, 0);
    EXPECT_TRUE(source.name.empty());
    EXPECT_TRUE(library.sources().empty());

    ASSERT_TRUE(run_sql("DROP TRIGGER refuse_headers;"));
    ASSERT_TRUE(library.save_source(&source));
    EXPECT_EQ(source.id, 5);
    EXPECT_EQ(source.name, "Playlist #5");
}
} // namespace
