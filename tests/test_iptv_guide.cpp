/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_guide.h"

#include "gzip_fixtures.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace
{

constexpr std::uint64_t kSource = 0x5854000000000042u;
constexpr std::int64_t kNow = 1791210600; // 2026-10-05 14:30 UTC

iptv::xmltv::Programme Show(const char *channel, std::int64_t start, std::int64_t stop,
                            const char *title)
{
    iptv::xmltv::Programme programme;
    programme.channel = channel;
    programme.start = start;
    programme.stop = stop;
    programme.title = title;
    programme.description = std::string(title) + " description";
    return programme;
}

std::string GuidePath()
{
    return std::string(::testing::TempDir()) + "prosperotv-guide-test.sqlite3";
}

TEST(IptvGuideTest, StoresTheSourcesChannelsInTheWindowAndAnswersNowNext)
{
    const std::string path = GuidePath();
    iptv::GuideWriter writer;
    ASSERT_EQ(writer.Open(path, kSource, {"bbc1.uk", "news.us"}, kNow - 3600, kNow + 86400),
              iptv::GuideStatus::ok);
    // Guide ids are matched without regard to case.
    EXPECT_TRUE(writer.Add(Show("BBC1.uk", kNow - 1800, kNow + 1800, "Afternoon")));
    EXPECT_TRUE(writer.Add(Show("bbc1.uk", kNow + 1800, kNow + 3600, "Evening")));
    EXPECT_TRUE(writer.Add(Show("bbc1.uk", kNow + 3600, kNow + 7200, "Night")));
    EXPECT_TRUE(writer.Add(Show("bbc1.uk", kNow - 7200, kNow - 3600, "Too old")));
    EXPECT_TRUE(writer.Add(Show("bbc1.uk", kNow + 90000, kNow + 93600, "Too late")));
    EXPECT_TRUE(writer.Add(Show("other", kNow - 60, kNow + 60, "Not ours")));
    EXPECT_TRUE(writer.Add(Show("news.us", kNow + 600, kNow + 1200, "Later only")));
    EXPECT_EQ(writer.stored(), 4u);
    EXPECT_EQ(writer.filtered(), 3u);
    ASSERT_EQ(writer.Commit(kNow), iptv::GuideStatus::ok);

    iptv::GuideReader reader;
    ASSERT_EQ(reader.Open(path, kSource), iptv::GuideStatus::ok);
    EXPECT_EQ(reader.saved_unix(), kNow);
    iptv::GuideEntry now;
    iptv::GuideEntry next;
    ASSERT_TRUE(reader.NowNext("bbc1.UK", kNow, &now, &next));
    EXPECT_EQ(now.title, "Afternoon");
    EXPECT_EQ(now.description, "Afternoon description");
    EXPECT_EQ(now.start, kNow - 1800);
    EXPECT_EQ(next.title, "Evening");

    // Between two programmes nothing is on, but the next one is known.
    ASSERT_TRUE(reader.NowNext("news.us", kNow, &now, &next));
    EXPECT_TRUE(now.title.empty());
    EXPECT_EQ(next.title, "Later only");

    // At the very end of a programme the following one is on air.
    ASSERT_TRUE(reader.NowNext("bbc1.uk", kNow + 1800, &now, &next));
    EXPECT_EQ(now.title, "Evening");
    EXPECT_EQ(next.title, "Night");

    EXPECT_FALSE(reader.NowNext("bbc1.uk", kNow + 7200, &now, &next));
    EXPECT_FALSE(reader.NowNext("missing", kNow, &now, &next));
    EXPECT_FALSE(reader.NowNext("", kNow, &now, &next));
    reader.Close();
    std::remove(path.c_str());
}

TEST(IptvGuideTest, ReplacesOnlyOnCommitAndChecksTheSource)
{
    const std::string path = GuidePath();
    {
        iptv::GuideWriter first;
        ASSERT_EQ(first.Open(path, kSource, {"a"}, kNow - 60, kNow + 3600), iptv::GuideStatus::ok);
        first.Add(Show("a", kNow - 30, kNow + 30, "Kept"));
        ASSERT_EQ(first.Commit(kNow), iptv::GuideStatus::ok);
    }
    {
        // An abandoned download leaves the previous guide in place.
        iptv::GuideWriter second;
        ASSERT_EQ(second.Open(path, kSource, {"a"}, kNow - 60, kNow + 3600), iptv::GuideStatus::ok);
        second.Add(Show("a", kNow - 30, kNow + 30, "Discarded"));
        second.Abort();
    }
    iptv::GuideReader reader;
    ASSERT_EQ(reader.Open(path, kSource), iptv::GuideStatus::ok);
    iptv::GuideEntry now;
    ASSERT_TRUE(reader.NowNext("a", kNow, &now, nullptr));
    EXPECT_EQ(now.title, "Kept");
    reader.Close();

    EXPECT_EQ(reader.Open(path, kSource + 1u), iptv::GuideStatus::corrupt);
    EXPECT_FALSE(reader.is_open());
    std::remove(path.c_str());
    EXPECT_EQ(reader.Open(path, kSource), iptv::GuideStatus::not_found);

    iptv::GuideWriter invalid;
    EXPECT_EQ(invalid.Open(path, 0u, {}, kNow, kNow + 1), iptv::GuideStatus::invalid_argument);
    EXPECT_EQ(invalid.Open(path, kSource, {}, kNow, kNow), iptv::GuideStatus::invalid_argument);
    EXPECT_FALSE(invalid.Add(Show("a", kNow, kNow + 1, "x")));
}

// Serves bytes in small pieces, optionally failing partway.
struct Source
{
    std::string data;
    std::size_t position = 0;
    std::size_t fail_at = 0; // 0: never

    static long Read(void *context, unsigned char *buffer, std::size_t capacity)
    {
        auto *source = static_cast<Source *>(context);
        if (source->fail_at && source->position >= source->fail_at)
            return -1;
        const std::size_t count =
            std::min({capacity, std::size_t{9}, source->data.size() - source->position});
        std::copy_n(source->data.data() + source->position, count, buffer);
        source->position += count;
        return static_cast<long>(count);
    }
};

constexpr std::int64_t kGuideNow = 1791208800 + 600; // 2026-10-05 14:10 UTC

bool Import(const std::string &data, const std::string &path, iptv::GuideImportReport *report,
            std::uint64_t max_bytes = 1u << 20, std::size_t fail_at = 0)
{
    iptv::GuideWriter writer;
    EXPECT_EQ(writer.Open(path, kSource, {"bbc1.uk"}, kGuideNow - 3600, kGuideNow + 86400),
              iptv::GuideStatus::ok);
    Source source{data, 0, fail_at};
    const bool imported = iptv::ImportGuide(&Source::Read, &source, &writer, max_bytes, report);
    if (imported)
        EXPECT_EQ(writer.Commit(kGuideNow), iptv::GuideStatus::ok);
    return imported;
}

TEST(IptvGuideTest, ImportsPlainAndCompressedGuides)
{
    const std::string path = GuidePath();
    const std::string compressed(reinterpret_cast<const char *>(kGuideGz), sizeof(kGuideGz));
    for (const std::string &data : {std::string(kGuideXml), compressed})
    {
        iptv::GuideImportReport report;
        ASSERT_TRUE(Import(data, path, &report));
        EXPECT_EQ(report.compressed, data[0] == '\x1f');
        EXPECT_EQ(report.programmes, 3u);
        EXPECT_EQ(report.bytes, std::string(kGuideXml).size());
        iptv::GuideReader reader;
        ASSERT_EQ(reader.Open(path, kSource), iptv::GuideStatus::ok);
        iptv::GuideEntry now;
        iptv::GuideEntry next;
        ASSERT_TRUE(reader.NowNext("bbc1.uk", kGuideNow, &now, &next));
        EXPECT_EQ(now.title, "Afternoon");
        EXPECT_EQ(now.description, "Live news.");
        EXPECT_EQ(next.title, "Evening");
    }
    std::remove(path.c_str());
}

TEST(IptvGuideTest, RefusesIncompleteOversizedOrBrokenGuides)
{
    const std::string path = GuidePath() + "-bad";
    iptv::GuideImportReport report;
    EXPECT_FALSE(Import(kGuideXml, path, &report, 100u));
    EXPECT_TRUE(report.too_large);

    EXPECT_FALSE(Import(kGuideXml, path, &report, 1u << 20, 50u));
    EXPECT_TRUE(report.read_failed);

    const std::string compressed(reinterpret_cast<const char *>(kGuideGz), sizeof(kGuideGz));
    EXPECT_FALSE(Import(compressed.substr(0, compressed.size() - 10u), path, &report));
    EXPECT_EQ(report.gzip, iptv::gzip::Status::truncated);

    EXPECT_FALSE(Import("<tv><programme start=\"2026", path, &report));
    EXPECT_EQ(report.xml, iptv::xmltv::Status::malformed);

    iptv::GuideReader reader;
    EXPECT_EQ(reader.Open(path, kSource), iptv::GuideStatus::not_found);
}

} // namespace
