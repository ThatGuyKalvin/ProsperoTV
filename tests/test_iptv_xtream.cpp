/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_http.h"
#include "iptv_source_state.h"
#include "iptv_xtream.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace
{

iptv::XtreamCredentials Credentials()
{
    return {"https://provider.example:25461", "test user", "p@ss&word"};
}

TEST(IptvXtreamTest, NormalizesServerAndBuildsEncodedEndpoints)
{
    std::string server;
    ASSERT_TRUE(
        iptv::NormalizeXtreamServerUrl("HTTPS://Provider.Example:25461/player_api.php/", &server));
    EXPECT_EQ(server, "https://provider.example:25461");

    const iptv::XtreamCredentials credentials = Credentials();
    std::string url;
    ASSERT_TRUE(iptv::BuildXtreamApiUrl(credentials, "get_live_streams", &url));
    EXPECT_EQ(url, "https://provider.example:25461/player_api.php?username=test%20user&password="
                   "p%40ss%26word&action=get_live_streams");
    ASSERT_TRUE(iptv::BuildXtreamLiveUrl(credentials, "42", "m3u8", &url));
    EXPECT_EQ(url, "https://provider.example:25461/live/test%20user/p%40ss%26word/42.m3u8");
}

TEST(IptvXtreamTest, ParsesAuthenticationAndExplainsAccountFailures)
{
    iptv::XtreamAuth auth;
    EXPECT_EQ(
        iptv::ParseXtreamAuth(
            R"({"user_info":{"auth":1,"status":"Active","message":"Welcome"},"server_info":{}})",
            &auth),
        iptv::XtreamStatus::ok);
    EXPECT_TRUE(auth.authenticated);
    EXPECT_EQ(auth.message, "Welcome");

    EXPECT_EQ(iptv::ParseXtreamAuth(
                  R"({"user_info":{"auth":"0","status":"Disabled","message":"Bad login"}})", &auth),
              iptv::XtreamStatus::authentication_failed);
    EXPECT_EQ(auth.message, "Bad login");

    EXPECT_EQ(iptv::ParseXtreamAuth(R"({"user_info":{"auth":true,"status":"Expired"}})", &auth),
              iptv::XtreamStatus::account_inactive);
}

TEST(IptvXtreamTest, AcceptsBomAndNullableProviderFieldsWithoutInventingIdsOrExtensions)
{
    iptv::XtreamAuth auth;
    EXPECT_EQ(
        iptv::ParseXtreamAuth("\xef\xbb\xbf"
                              R"({"user_info":{"auth":1,"status":null,"message":"Hello\u000aTV"}})",
                              &auth),
        iptv::XtreamStatus::ok);
    EXPECT_EQ(auth.message, "Hello TV");
    EXPECT_EQ(iptv::ParseXtreamAuth(R"({"user_info":{"auth":null}})", &auth),
              iptv::XtreamStatus::authentication_failed);
    std::vector<iptv::XtreamCategory> categories;
    ASSERT_EQ(
        iptv::ParseXtreamCategories(
            "\xef\xbb\xbf"
            R"([{"category_id":1,"category_name":"News\u0009HD","parent_id":null},{"category_id":null,"category_name":"Missing"}])",
            &categories),
        iptv::XtreamStatus::ok);
    ASSERT_EQ(categories.size(), 1u);
    EXPECT_EQ(categories[0].name, "News HD");
    EXPECT_TRUE(categories[0].parent_id.empty());
    const std::string answer =
        "\xef\xbb\xbf"
        R"([{"stream_id":1,"name":null,"container_extension":null,"epg_channel_id":null,"direct_source":null,"stream_url":"https://cdn.example/1.ts","category_id":1},{"stream_id":null,"name":"Missing"}])";
    for (std::size_t piece = 1; piece <= answer.size(); ++piece)
    {
        iptv::Catalog catalog;
        iptv::ParseReport report;
        iptv::XtreamStreamsParser parser(Credentials(), categories, 1, &catalog, &report);
        for (std::size_t at = 0; at < answer.size(); at += piece)
            ASSERT_TRUE(parser.Feed(std::string_view(answer).substr(at, piece)));
        ASSERT_EQ(parser.Finish(), iptv::XtreamStatus::ok);
        ASSERT_EQ(catalog.size(), 1u);
        EXPECT_EQ(catalog[0].name, "Channel 1");
        EXPECT_TRUE(catalog[0].tvg_id.empty());
        EXPECT_EQ(catalog[0].url, "https://cdn.example/1.ts");
        ASSERT_EQ(catalog[0].alternate_urls.size(), 1u);
        EXPECT_TRUE(catalog[0].alternate_urls[0].ends_with("/1.ts"));
        EXPECT_EQ(report.skipped, 1u);
    }
}

TEST(IptvXtreamTest, ConvertsLiveJsonIntoTheSharedCatalog)
{
    const iptv::XtreamCredentials credentials = Credentials();
    std::vector<iptv::XtreamCategory> categories;
    ASSERT_EQ(
        iptv::ParseXtreamCategories(
            R"({"data":[{"category_id":"7","category_name":"Not\u00edcias"},{"category_id":8,"category_name":"Sports"}]})",
            &categories),
        iptv::XtreamStatus::ok);
    ASSERT_EQ(categories.size(), 2u);
    EXPECT_EQ(categories[0].name, "Not\xc3\xad"
                                  "cias");

    constexpr std::string_view streams =
        R"([{"stream_id":101,"num":701,"name":"Canal \u00c1","stream_icon":"https:\/\/images.example\/101.png","epg_channel_id":"canal.a","category_id":"7","container_extension":"m3u8","direct_source":"https:\/\/cdn.example\/live\/master.m3u8","ignored":{"nested":[1,true,null]}},{"stream_id":"102","name":"Sports HD","category_id":8,"container_extension":"ts","stream_url":"https:\/\/cdn.example\/sports.ts"},{"name":"Missing id"}])";
    iptv::Catalog catalog;
    iptv::ParseReport report;
    ASSERT_EQ(iptv::ParseXtreamLiveStreams(streams, credentials, categories, 0x5854000000001234u,
                                           &catalog, &report),
              iptv::XtreamStatus::ok);
    ASSERT_EQ(catalog.size(), 2u);
    EXPECT_EQ(catalog[0].name, "Canal \xc3\x81");
    EXPECT_EQ(catalog[0].group_title, "Not\xc3\xad"
                                      "cias");
    EXPECT_EQ(catalog[0].tvg_id, "canal.a");
    EXPECT_EQ(catalog[0].channel_number, "701");
    EXPECT_EQ(catalog[0].url, "https://cdn.example/live/master.m3u8");
    ASSERT_EQ(catalog[0].alternate_urls.size(), 1u);
    EXPECT_EQ(catalog[0].alternate_urls[0],
              "https://provider.example:25461/live/test%20user/p%40ss%26word/101.m3u8");
    EXPECT_EQ(catalog[1].group_title, "Sports");
    EXPECT_EQ(catalog[1].url, "https://cdn.example/sports.ts");
    EXPECT_EQ(report.accepted, 2u);
    EXPECT_EQ(report.skipped, 1u);
}

// Newer panels name every category of a stream in "category_ids"; "category_id"
// is only the first. A package added to a plan puts channels the account already
// had into its category this way, so that category is otherwise never seen.
TEST(IptvXtreamTest, AStreamIsInEveryCategoryItsProviderNames)
{
    const iptv::XtreamCredentials credentials = Credentials();
    std::vector<iptv::XtreamCategory> categories{
        {"10", "US| AT&T"}, {"20", "LA / GENERAL"}, {"30", "Adults"}, {"40", "Mexico"}};
    categories[2].adult = true;
    constexpr std::string_view streams =
        R"([{"stream_id":1,"name":"TELE EL SALVADOR","category_id":"10","category_ids":[10,20,"20",999,null],"container_extension":"ts"},)"
        R"({"stream_id":2,"name":"Canal Mexico","category_id":null,"category_ids":["40"],"container_extension":"ts"},)"
        R"({"stream_id":3,"name":"Late","category_id":"10","category_ids":[30],"container_extension":"ts"},)"
        R"({"stream_id":4,"name":"Plain","category_id":"10","category_ids":[],"container_extension":"ts"}])";
    iptv::Catalog catalog;
    iptv::ParseReport report;
    ASSERT_EQ(iptv::ParseXtreamLiveStreams(streams, credentials, categories, 0x5854000000001234u,
                                           &catalog, &report),
              iptv::XtreamStatus::ok);
    ASSERT_EQ(catalog.size(), 4u);
    EXPECT_EQ(report.skipped, 0u);

    EXPECT_EQ(catalog[0].group_title, "US| AT&T");
    ASSERT_EQ(catalog[0].alternate_group_titles.size(), 1u);
    EXPECT_EQ(catalog[0].alternate_group_titles[0], "LA / GENERAL");

    // Without "category_id" the first listed category is the channel's own.
    EXPECT_EQ(catalog[1].group_title, "Mexico");
    EXPECT_TRUE(catalog[1].alternate_group_titles.empty());

    // A channel in any adult category is an adult channel.
    EXPECT_FALSE(catalog[0].adult);
    EXPECT_TRUE(catalog[2].adult);
    ASSERT_EQ(catalog[2].alternate_group_titles.size(), 1u);
    EXPECT_EQ(catalog[2].alternate_group_titles[0], "Adults");

    EXPECT_EQ(catalog[3].group_title, "US| AT&T");
    EXPECT_TRUE(catalog[3].alternate_group_titles.empty());
}

// The list only adds categories: one a panel writes some other way is passed
// over, and the stream keeps the category it has.
TEST(IptvXtreamTest, ACategoryListOfAnotherShapeIsPassedOver)
{
    const iptv::XtreamCredentials credentials = Credentials();
    const std::vector<iptv::XtreamCategory> categories{{"10", "News"}, {"20", "Sports"}};
    constexpr std::string_view streams =
        R"([{"stream_id":1,"name":"A","category_id":"10","category_ids":[{"id":20},[20],"20"],"container_extension":"ts"},)"
        R"({"stream_id":2,"name":"B","category_id":"10","category_ids":"20","container_extension":"ts"},)"
        R"({"stream_id":3,"name":"C","category_id":"10","category_ids":null,"container_extension":"ts"}])";
    iptv::Catalog catalog;
    iptv::ParseReport report;
    ASSERT_EQ(iptv::ParseXtreamLiveStreams(streams, credentials, categories, 1u, &catalog, &report),
              iptv::XtreamStatus::ok);
    ASSERT_EQ(catalog.size(), 3u);
    EXPECT_EQ(report.skipped, 0u);
    ASSERT_EQ(catalog[0].alternate_group_titles.size(), 1u);
    EXPECT_EQ(catalog[0].alternate_group_titles[0], "Sports");
    for (std::size_t index = 1; index < 3; ++index)
    {
        EXPECT_EQ(catalog[index].group_title, "News");
        EXPECT_TRUE(catalog[index].alternate_group_titles.empty());
    }
}

TEST(IptvXtreamTest, ProviderParentIdsBuildNestedPathsWithoutFollowingCycles)
{
    std::vector<iptv::XtreamCategory> categories;
    ASSERT_EQ(
        iptv::ParseXtreamCategories(
            R"([{"category_id":3,"category_name":"Football","parent_id":2},{"category_id":1,"category_name":"US","parent_id":0},{"category_id":2,"category_name":"Sports","parent_id":1},{"category_id":4,"category_name":"Loop","parent_id":4}])",
            &categories),
        iptv::XtreamStatus::ok);
    ASSERT_EQ(categories.size(), 4u);
    EXPECT_EQ(categories[0].name, "US / Sports / Football");
    EXPECT_EQ(categories[2].name, "US / Sports");
    EXPECT_EQ(categories[3].name, "Loop");
}

// The answer a provider gives for that many live streams, about 600 bytes
// each: most of it is fields the app has no use for.
std::string StreamsAnswer(int streams)
{
    const std::string unused(330, 'x');
    std::string answer = "[";
    answer.reserve(static_cast<std::size_t>(streams) * 620u);
    for (int i = 0; i < streams; ++i)
    {
        if (i != 0)
            answer += ',';
        const std::string number = std::to_string(i + 1);
        answer += R"({"num":)" + number + R"(,"name":"Channel )" + number +
                  R"(","stream_type":"live","stream_id":)" + number +
                  R"(,"stream_icon":"https:\/\/images.example\/logos\/)" + number +
                  R"(.png","epg_channel_id":"channel.)" + number +
                  R"(","added":"1700000000","is_adult":"0","category_id":")" +
                  std::to_string(i % 40) + R"(","custom_sid":")" + unused +
                  R"(","tv_archive":0,"direct_source":"","tv_archive_duration":0})";
    }
    answer += "]";
    return answer;
}

constexpr std::uint64_t kSource = 0x5854000000001234u;

void ExpectSameChannels(const iptv::Catalog &expected, const iptv::Catalog &actual)
{
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const iptv::ChannelView one = expected[index];
        const iptv::ChannelView other = actual[index];
        EXPECT_EQ(other.id, one.id) << index;
        EXPECT_EQ(other.name, one.name) << index;
        EXPECT_EQ(other.url, one.url) << index;
        EXPECT_EQ(other.tvg_id, one.tvg_id) << index;
        EXPECT_EQ(other.tvg_logo, one.tvg_logo) << index;
        EXPECT_EQ(other.group_title, one.group_title) << index;
        EXPECT_TRUE(other.alternate_urls == one.alternate_urls) << index;
        EXPECT_EQ(other.source_line, one.source_line) << index;
    }
}

TEST(IptvXtreamTest, LoadsAProviderWithMoreThanAHundredThousandChannels)
{
    // 120,000 streams: an answer of about 70 MiB, more than the 64 MiB the
    // app once held whole. It is read the way a download delivers it, in
    // pieces, and nothing but the channels is kept.
    constexpr int kStreams = 120000;
    const std::string answer = StreamsAnswer(kStreams);
    ASSERT_GT(answer.size(), 64u * 1024u * 1024u);
    ASSERT_LE(answer.size(), iptv::kMaxXtreamResponseBytes);
    std::vector<iptv::XtreamCategory> categories;
    for (int i = 0; i < 40; ++i)
        categories.push_back({std::to_string(i), "Category " + std::to_string(i)});

    iptv::Catalog catalog;
    iptv::ParseReport report;
    iptv::XtreamStreamsParser parser(Credentials(), categories, kSource, &catalog, &report);
    constexpr std::size_t kPiece = 64u * 1024u;
    for (std::size_t at = 0; at < answer.size(); at += kPiece)
        ASSERT_TRUE(parser.Feed(std::string_view(answer).substr(at, kPiece))) << at;
    ASSERT_EQ(parser.Finish(), iptv::XtreamStatus::ok);
    EXPECT_FALSE(parser.full());
    ASSERT_EQ(catalog.size(), static_cast<std::size_t>(kStreams));
    EXPECT_EQ(report.accepted, static_cast<std::size_t>(kStreams));
    EXPECT_EQ(report.skipped, 0u);
    EXPECT_FALSE(report.catalog_full);
    EXPECT_EQ(catalog.front().name, "Channel 1");
    EXPECT_EQ(catalog.back().name, "Channel " + std::to_string(kStreams));
    EXPECT_EQ(catalog[77776].group_title, "Category 16");
    EXPECT_EQ(catalog[77776].url,
              "https://provider.example:25461/live/test%20user/p%40ss%26word/77777.ts");
    // Any channel is found by its id at once, and a channel costs a fraction
    // of what the answer spent on it.
    const std::string id(catalog[77776].id);
    EXPECT_EQ(catalog.Find(id), 77776u);
    EXPECT_EQ(catalog.Find("xtream:none"), iptv::Catalog::npos);
    EXPECT_LT(catalog.MemoryBytes() / catalog.size(), 320u);
}

TEST(IptvXtreamTest, AWholePictureForALogoLeavesOutOnlyTheLogo)
{
    // Some providers send a channel's logo as the picture itself, a data: address many
    // kilobytes long; the channel is kept without it, and so is the rest of the list.
    const std::string picture = "data:image/jpeg;base64," + std::string(6000, 'A');
    const std::string answer =
        R"([{"num":1,"name":"Board 1","stream_id":11,"stream_icon":")" + picture +
        R"(","direct_source":")" + picture +
        R"("},{"num":2,"name":"Board 2","stream_id":12,"stream_icon":"http://img.invalid/2.png"}])";
    iptv::Catalog catalog;
    iptv::ParseReport report;
    ASSERT_EQ(iptv::ParseXtreamLiveStreams(answer, Credentials(), {}, kSource, &catalog, &report),
              iptv::XtreamStatus::ok);
    ASSERT_EQ(catalog.size(), 2u);
    EXPECT_EQ(report.accepted, 2u);
    EXPECT_EQ(catalog[0].name, "Board 1");
    EXPECT_TRUE(catalog[0].tvg_logo.empty());
    EXPECT_EQ(catalog[0].url.find("data:"), std::string_view::npos);
    EXPECT_EQ(catalog[1].tvg_logo, "http://img.invalid/2.png");
}

TEST(IptvXtreamTest, KeepsTheFirstChannelsOfAProviderTooLargeToHold)
{
    const std::string answer = StreamsAnswer(1500);
    iptv::Catalog catalog;
    iptv::ParseReport report;
    ASSERT_EQ(
        iptv::ParseXtreamLiveStreams(answer, Credentials(), {}, kSource, &catalog, &report, 1000u),
        iptv::XtreamStatus::ok);
    // Every channel there is room for; the rest are counted, not an error.
    EXPECT_EQ(catalog.size(), 1000u);
    EXPECT_EQ(report.accepted, 1000u);
    EXPECT_EQ(report.skipped, 500u);
    EXPECT_TRUE(report.catalog_full);
    EXPECT_EQ(catalog.back().name, "Channel 1000");

    // A download stops as soon as the catalog is full: what it has by then is
    // the list, although the answer never reached its end.
    iptv::Catalog stopped;
    iptv::XtreamStreamsParser parser(Credentials(), {}, kSource, &stopped, nullptr, 1000u);
    std::size_t at = 0;
    while (at < answer.size() && !parser.full())
    {
        ASSERT_TRUE(parser.Feed(std::string_view(answer).substr(at, 4096u)));
        at += 4096u;
    }
    ASSERT_LT(at, answer.size());
    EXPECT_EQ(parser.Finish(), iptv::XtreamStatus::ok);
    EXPECT_EQ(stopped.size(), 1000u);
}

TEST(IptvXtreamTest, ReadsAnAnswerInPiecesOfAnySize)
{
    const std::vector<iptv::XtreamCategory> categories = {{"7", "News"}, {"8", "Sports"}};
    // Both shapes providers send: the list itself, and the list as "data".
    const std::string list =
        R"( [ {"stream_id":101,"num":701,"name":"Canal Á \"uno\"","stream_icon":"https:\/\/images.example\/101.png","epg_channel_id":"canal.a","category_id":"7","container_extension":"m3u8","direct_source":"https:\/\/cdn.example\/live\/master.m3u8","ignored":{"nested":[1,true,null,"]}"]}} ,
 {"stream_id":"102","name":"Sports [HD] {1}","category_id":8,"container_extension":"ts","stream_url":"https:\/\/cdn.example\/sports.ts"},{"name":"Missing id"},
 {"stream_id":101,"name":"Twice"} ] )";
    const std::string wrapped =
        R"({"status":"data","page":{"data":[0]},"data":)" + list + R"(,"more":[{"x":"]"}]})";
    for (const std::string &answer : {list, wrapped})
    {
        iptv::Catalog whole;
        iptv::ParseReport whole_report;
        ASSERT_EQ(iptv::ParseXtreamLiveStreams(answer, Credentials(), categories, kSource, &whole,
                                               &whole_report),
                  iptv::XtreamStatus::ok);
        ASSERT_EQ(whole.size(), 2u);
        EXPECT_EQ(whole[0].name, "Canal \xc3\x81 \"uno\"");
        EXPECT_EQ(whole[1].name, "Sports [HD] {1}");
        EXPECT_EQ(whole_report.skipped, 2u);
        for (const std::size_t piece : {1u, 2u, 3u, 7u, 64u, 1000u})
        {
            iptv::Catalog pieces;
            iptv::ParseReport report;
            iptv::XtreamStreamsParser parser(Credentials(), categories, kSource, &pieces, &report);
            for (std::size_t at = 0; at < answer.size(); at += piece)
                ASSERT_TRUE(parser.Feed(std::string_view(answer).substr(at, piece))) << piece;
            ASSERT_EQ(parser.Finish(), iptv::XtreamStatus::ok) << piece;
            ExpectSameChannels(whole, pieces);
            EXPECT_EQ(report.accepted, whole_report.accepted) << piece;
            EXPECT_EQ(report.skipped, whole_report.skipped) << piece;
        }
    }
}

TEST(IptvXtreamTest, AnAnswerCutShortOrOfAnotherKindIsNoList)
{
    const std::string answer = StreamsAnswer(50);
    iptv::Catalog catalog;
    iptv::XtreamStreamsParser cut(Credentials(), {}, kSource, &catalog);
    EXPECT_TRUE(cut.Feed(std::string_view(answer).substr(0, answer.size() / 2u)));
    EXPECT_EQ(cut.Finish(), iptv::XtreamStatus::malformed_json);
    EXPECT_TRUE(catalog.empty());

    // What a panel answers when the account is refused, and a web page.
    for (const char *other : {R"({"user_info":{"auth":0}})", "<html><body>503</body></html>",
                              R"([{"stream_id":1,"name":"One"},17])", R"([] trailing)"})
    {
        iptv::Catalog none;
        EXPECT_EQ(iptv::ParseXtreamLiveStreams(other, Credentials(), {}, kSource, &none),
                  iptv::XtreamStatus::malformed_json)
            << other;
        EXPECT_TRUE(none.empty()) << other;
    }
    iptv::Catalog empty;
    EXPECT_EQ(iptv::ParseXtreamLiveStreams("[]", Credentials(), {}, kSource, &empty),
              iptv::XtreamStatus::no_channels);
}

TEST(IptvXtreamTest, SmallAnswersAreReadWholeAndTheListIsNot)
{
    iptv::http::ListBuffer buffer = iptv::http::AllocateListBuffer(iptv::kMaxXtreamReplyBytes);
    ASSERT_NE(buffer.data(), nullptr);
    EXPECT_EQ(buffer.max_bytes, iptv::kMaxXtreamReplyBytes);
    EXPECT_EQ(buffer.size(), buffer.max_bytes + 1u);
    // The list of streams is read as it arrives, up to what a download may be.
    EXPECT_GE(iptv::http::kMaxListBytes, iptv::kMaxXtreamResponseBytes);
    // Never less than asked for as the least.
    iptv::http::ListBuffer small = iptv::http::AllocateListBuffer(1024u, 4096u);
    ASSERT_NE(small.data(), nullptr);
    EXPECT_EQ(small.max_bytes, 4096u);
}

TEST(IptvXtreamTest, RejectsMalformedDataAndPersistsCredentialsAndSource)
{
    const iptv::XtreamCredentials credentials = Credentials();
    iptv::Catalog catalog;
    EXPECT_EQ(iptv::ParseXtreamLiveStreams("[{", credentials, {}, 1u, &catalog),
              iptv::XtreamStatus::malformed_json);

    const std::string prefix = std::string(::testing::TempDir()) + "prosperotv-xtream-test-";
    const std::string credentials_path = prefix + "credentials";
    const std::string source_path = prefix + "source";
    ASSERT_EQ(iptv::SaveXtreamCredentials(credentials_path, credentials), iptv::XtreamStatus::ok);
    iptv::XtreamCredentials loaded;
    ASSERT_EQ(iptv::LoadXtreamCredentials(credentials_path, &loaded), iptv::XtreamStatus::ok);
    EXPECT_EQ(loaded.server_url, credentials.server_url);
    EXPECT_EQ(loaded.username, credentials.username);
    EXPECT_EQ(loaded.password, credentials.password);

    ASSERT_EQ(iptv::SaveActiveSource(source_path, iptv::SourceKind::Xtream),
              iptv::SourceStateStatus::ok);
    iptv::SourceKind source = iptv::SourceKind::BuiltIn;
    ASSERT_EQ(iptv::LoadActiveSource(source_path, &source), iptv::SourceStateStatus::ok);
    EXPECT_EQ(source, iptv::SourceKind::Xtream);
    std::remove(credentials_path.c_str());
    std::remove(source_path.c_str());
}

} // namespace
