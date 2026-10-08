/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_xmltv.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{

using iptv::xmltv::Programme;
using iptv::xmltv::Status;

struct Collector
{
    std::vector<Programme> programmes;
    std::size_t stop_after = 0;

    static bool Add(void *context, const Programme &programme)
    {
        auto *collector = static_cast<Collector *>(context);
        collector->programmes.push_back(programme);
        return collector->stop_after == 0 || collector->programmes.size() < collector->stop_after;
    }
};

// Parses `xml` fed in pieces of `piece` bytes.
Status Parse(const std::string &xml, std::size_t piece, Collector *collector,
             iptv::xmltv::Report *report = nullptr)
{
    iptv::xmltv::Parser parser(&Collector::Add, collector);
    Status status = Status::ok;
    for (std::size_t offset = 0; offset < xml.size() && status == Status::ok; offset += piece)
        status = parser.Feed(xml.data() + offset, std::min(piece, xml.size() - offset));
    if (status == Status::ok)
        status = parser.Finish();
    if (report)
        *report = parser.report();
    return status;
}

TEST(IptvXmltvTest, ParsesTimesWithAndWithoutOffsets)
{
    std::int64_t time = 0;
    ASSERT_TRUE(iptv::xmltv::ParseTime("20261005143000 +0100", &time));
    EXPECT_EQ(time, 1791207000);
    ASSERT_TRUE(iptv::xmltv::ParseTime("202610051430", &time));
    EXPECT_EQ(time, 1791210600);
    ASSERT_TRUE(iptv::xmltv::ParseTime("20261005090000 -0500", &time));
    EXPECT_EQ(time, 1791208800);
    ASSERT_TRUE(iptv::xmltv::ParseTime("19700101000000", &time));
    EXPECT_EQ(time, 0);
    ASSERT_TRUE(iptv::xmltv::ParseTime("20240229235959+0000", &time));
    EXPECT_EQ(time, 1709251199);
    for (const char *bad : {"", "2026100514", "20261305143000", "20261000143000", "20261005250000",
                            "1969123123590", "abc"})
        EXPECT_FALSE(iptv::xmltv::ParseTime(bad, &time)) << bad;
}

const std::string kGuide =
    "\xef\xbb\xbf<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!DOCTYPE tv SYSTEM \"xmltv.dtd\">\n"
    "<tv generator-info-name=\"panel\">\n"
    "  <channel id=\"bbc1.uk\"><display-name>BBC One</display-name></channel>\n"
    "  <!-- <programme channel=\"ignored\"> in a comment -->\n"
    "  <programme start=\"20261005143000 +0100\" stop='20261005153000 +0100' "
    "channel=\"bbc1.uk\" note=\"a > b\">\n"
    "    <title lang=\"en\">News &amp; Weather</title>\n"
    "    <title lang=\"fr\">Nouvelles</title>\n"
    "    <sub-title>Evening</sub-title>\n"
    "    <desc lang=\"en\">Caf&#233; and caf&#xE9;\n   headlines   today.</desc>\n"
    "    <category>News</category>\n"
    "  </programme>\n"
    "  <programme start=\"20261005153000 +0100\" stop=\"20261005160000 +0100\" "
    "channel=\"bbc1.uk\"><title><![CDATA[Tom & Jerry <Classic>]]></title><desc/></programme>\n"
    "  <programme start=\"20261005160000 +0100\" channel=\"bbc1.uk\"><title>No stop</title>"
    "</programme>\n"
    "  <programme start=\"20261005160000\" stop=\"20261005170000\" channel=\"x\"><title/>"
    "</programme>\n"
    "  <programme start=\"20261005170000\" stop=\"20261005160000\" channel=\"x\">"
    "<title>Backwards</title></programme>\n"
    "  <programme start=\"20261005170000\" stop=\"20261005180000\" channel=\"Chan &amp; Co\">"
    "<title>Unknown &nbsp; entity</title></programme>\n"
    "</tv>\n";

TEST(IptvXmltvTest, ReadsProgrammesWhicheverWayTheGuideIsSplit)
{
    for (const std::size_t piece : {kGuide.size(), std::size_t{1}, std::size_t{7}, std::size_t{64}})
    {
        Collector collector;
        iptv::xmltv::Report report;
        ASSERT_EQ(Parse(kGuide, piece, &collector, &report), Status::ok) << piece;
        ASSERT_EQ(collector.programmes.size(), 3u) << piece;
        EXPECT_EQ(report.programmes, 3u);
        EXPECT_EQ(report.skipped, 3u);

        const Programme &news = collector.programmes[0];
        EXPECT_EQ(news.channel, "bbc1.uk");
        EXPECT_EQ(news.start, 1791207000);
        EXPECT_EQ(news.stop, 1791210600);
        EXPECT_EQ(news.title, "News & Weather");
        EXPECT_EQ(news.description, "Caf\xc3\xa9 and caf\xc3\xa9 headlines today.");

        EXPECT_EQ(collector.programmes[1].title, "Tom & Jerry <Classic>");
        EXPECT_TRUE(collector.programmes[1].description.empty());
        EXPECT_EQ(collector.programmes[2].channel, "Chan & Co");
        EXPECT_EQ(collector.programmes[2].title, "Unknown &nbsp; entity");
    }
}

TEST(IptvXmltvTest, BoundsLongTextWithoutSplittingCharacters)
{
    std::string title;
    for (int index = 0; index < 150; ++index)
        title += "\xc3\xa9"; // 300 bytes of two-byte characters
    const std::string xml = "<tv><programme start=\"20261005160000\" stop=\"20261005170000\" "
                            "channel=\"c\"><title>" +
                            title + "</title></programme></tv>";
    Collector collector;
    ASSERT_EQ(Parse(xml, 5, &collector), Status::ok);
    ASSERT_EQ(collector.programmes.size(), 1u);
    const std::string &parsed = collector.programmes[0].title;
    EXPECT_LE(parsed.size(), iptv::xmltv::kMaxTitleBytes);
    EXPECT_EQ(parsed.size() % 2u, 0u);
    EXPECT_EQ(parsed, title.substr(0, parsed.size()));
}

TEST(IptvXmltvTest, StopsOnRequestAndRejectsBrokenMarkup)
{
    Collector stopping;
    stopping.stop_after = 1;
    EXPECT_EQ(Parse(kGuide, 13, &stopping), Status::stopped);
    EXPECT_EQ(stopping.programmes.size(), 1u);

    Collector open;
    EXPECT_EQ(Parse("<tv><programme channel=\"a\"", 4, &open), Status::malformed);

    Collector huge;
    const std::string comment =
        "<tv><!--" + std::string(2u * iptv::xmltv::kMaxConstructBytes, 'x') + "--></tv>";
    EXPECT_EQ(Parse(comment, 4096, &huge), Status::malformed);

    Collector empty;
    EXPECT_EQ(Parse("", 1, &empty), Status::ok);
    EXPECT_TRUE(empty.programmes.empty());
}

} // namespace
