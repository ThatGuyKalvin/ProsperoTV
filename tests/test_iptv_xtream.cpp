/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_http.h"
#include "iptv_source_state.h"
#include "iptv_xtream.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <map>
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
        R"([{"stream_id":101,"name":"Canal \u00c1","stream_icon":"https:\/\/images.example\/101.png","epg_channel_id":"canal.a","category_id":"7","container_extension":"m3u8","direct_source":"https:\/\/cdn.example\/live\/master.m3u8","ignored":{"nested":[1,true,null]}},{"stream_id":"102","name":"Sports HD","category_id":8,"container_extension":"ts","stream_url":"https:\/\/cdn.example\/sports.ts"},{"name":"Missing id"}])";
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
    EXPECT_EQ(catalog[0].url, "https://cdn.example/live/master.m3u8");
    ASSERT_EQ(catalog[0].alternate_urls.size(), 1u);
    EXPECT_EQ(catalog[0].alternate_urls[0],
              "https://provider.example:25461/live/test%20user/p%40ss%26word/101.m3u8");
    EXPECT_EQ(catalog[1].group_title, "Sports");
    EXPECT_EQ(catalog[1].url, "https://cdn.example/sports.ts");
    EXPECT_EQ(report.accepted, 2u);
    EXPECT_EQ(report.skipped, 1u);
}

TEST(IptvXtreamTest, BuildsParameterizedApiAndMediaUrls)
{
    const iptv::XtreamCredentials credentials = Credentials();
    std::string url;
    ASSERT_TRUE(
        iptv::BuildXtreamApiUrlWithParam(credentials, "get_series_info", "series_id", "55", &url));
    EXPECT_EQ(url, "https://provider.example:25461/player_api.php?username=test%20user&password="
                   "p%40ss%26word&action=get_series_info&series_id=55");
    EXPECT_FALSE(iptv::BuildXtreamApiUrlWithParam(credentials, "a", "bad key", "1", &url));
    EXPECT_FALSE(iptv::BuildXtreamApiUrlWithParam(credentials, "a", "category_id", "", &url));

    ASSERT_TRUE(iptv::BuildXtreamVodUrl(credentials, "9", "", &url));
    EXPECT_EQ(url, "https://provider.example:25461/movie/test%20user/p%40ss%26word/9.m3u8");
    ASSERT_TRUE(iptv::BuildXtreamEpisodeUrl(credentials, "77", "ts", &url));
    EXPECT_EQ(url, "https://provider.example:25461/series/test%20user/p%40ss%26word/77.ts");
    EXPECT_FALSE(iptv::BuildXtreamVodUrl(credentials, "9", "m/p4", &url));
    EXPECT_FALSE(iptv::BuildXtreamVodUrl(credentials, "", "ts", &url));
    // Live URLs keep their MPEG-TS default.
    ASSERT_TRUE(iptv::BuildXtreamLiveUrl(credentials, "42", "", &url));
    EXPECT_EQ(url, "https://provider.example:25461/live/test%20user/p%40ss%26word/42.ts");

    EXPECT_TRUE(iptv::XtreamContainerStreamable(""));
    EXPECT_TRUE(iptv::XtreamContainerStreamable("TS"));
    EXPECT_TRUE(iptv::XtreamContainerStreamable("m3u8"));
    EXPECT_FALSE(iptv::XtreamContainerStreamable("mkv"));
    EXPECT_FALSE(iptv::XtreamContainerStreamable("mp4"));
}

TEST(IptvXtreamTest, ConvertsVodJsonIntoMovieEntries)
{
    const iptv::XtreamCredentials credentials = Credentials();
    const std::vector<iptv::XtreamCategory> categories = {{"3", "Action"}};
    constexpr std::string_view movies =
        R"([{"stream_id":501,"name":"Film One","stream_icon":"https:\/\/images.example\/1.jpg","category_id":"3","container_extension":"mkv","rating":"7.5","year":"2021-05-01","added":"1700000000"},{"stream_id":"502","name":"","category_id":"99","rating":8,"container_extension":"ts"},{"stream_id":501,"name":"Duplicate"},{"name":"Missing id"}])";
    iptv::Catalog catalog;
    iptv::ParseReport report;
    ASSERT_EQ(iptv::ParseXtreamVodStreams(movies, credentials, categories, 0x5854000000001234u,
                                          &catalog, &report),
              iptv::XtreamStatus::ok);
    ASSERT_EQ(catalog.size(), 2u);
    EXPECT_EQ(report.accepted, 2u);
    EXPECT_EQ(report.skipped, 2u);

    const iptv::ChannelView first = catalog[0];
    EXPECT_EQ(first.kind, iptv::MediaKind::movie);
    EXPECT_EQ(first.id, "xtream:5854000000001234:vod:501");
    EXPECT_EQ(first.name, "Film One");
    EXPECT_EQ(first.group_title, "Action");
    EXPECT_EQ(first.container_ext, "mkv");
    EXPECT_EQ(first.rating_tenths, 75u);
    EXPECT_EQ(first.year, 2021u);
    EXPECT_EQ(first.tvg_logo, "https://images.example/1.jpg");
    EXPECT_EQ(first.url, "https://provider.example:25461/movie/test%20user/p%40ss%26word/501.m3u8");
    ASSERT_EQ(first.alternate_urls.size(), 1u);
    EXPECT_EQ(first.alternate_urls[0],
              "https://provider.example:25461/movie/test%20user/p%40ss%26word/501.ts");

    const iptv::ChannelView second = catalog[1];
    EXPECT_EQ(second.name, "Movie 502");
    EXPECT_EQ(second.group_title, "Movies");
    EXPECT_EQ(second.rating_tenths, 80u);
    EXPECT_EQ(second.year, 0u);

    EXPECT_EQ(iptv::ParseXtreamVodStreams("[]", credentials, categories, 1u, &catalog),
              iptv::XtreamStatus::no_movies);
    EXPECT_EQ(iptv::ParseXtreamVodStreams("[{", credentials, categories, 1u, &catalog),
              iptv::XtreamStatus::malformed_json);
    EXPECT_EQ(iptv::ParseXtreamVodStreams(R"({"data":[{"stream_id":1,"name":"Wrapped"}]})",
                                          credentials, categories, 1u, &catalog),
              iptv::XtreamStatus::ok);
}

TEST(IptvXtreamTest, ConvertsSeriesListIntoLazyEntries)
{
    const iptv::XtreamCredentials credentials = Credentials();
    constexpr std::string_view list =
        R"([{"series_id":12,"name":"Show","cover":"https:\/\/images.example\/s.jpg","category_id":"4","rating":"9.1","releaseDate":"2019-02-03"},{"series_id":"13","name":"Other","year":"2020"},{"series_id":12,"name":"Dup"}])";
    iptv::Catalog catalog;
    iptv::ParseReport report;
    ASSERT_EQ(iptv::ParseXtreamSeriesList(list, credentials, {{"4", "Drama"}}, 0x5854000000001234u,
                                          &catalog, &report),
              iptv::XtreamStatus::ok);
    ASSERT_EQ(catalog.size(), 2u);
    EXPECT_EQ(report.skipped, 1u);
    const iptv::ChannelView show = catalog[0];
    EXPECT_EQ(show.kind, iptv::MediaKind::series);
    EXPECT_EQ(show.id, "xtream:5854000000001234:series:12");
    EXPECT_EQ(show.series_id, "12");
    EXPECT_EQ(show.group_title, "Drama");
    EXPECT_EQ(show.year, 2019u);
    EXPECT_EQ(show.rating_tenths, 91u);
    EXPECT_EQ(show.url, "https://provider.example:25461/player_api.php?username=test%20user&"
                        "password=p%40ss%26word&action=get_series_info&series_id=12");
    EXPECT_EQ(catalog[1].group_title, "Series");
    EXPECT_EQ(catalog[1].year, 2020u);
}

TEST(IptvXtreamTest, ConvertsSeriesInfoIntoEpisodes)
{
    const iptv::XtreamCredentials credentials = Credentials();
    constexpr std::string_view keyed =
        R"({"seasons":[],"info":{"name":"Show"},"episodes":{"1":[{"id":"900","episode_num":1,"title":"Pilot","container_extension":"mkv","info":{"duration_secs":2580,"bitrate":1000},"season":1},{"id":"901","episode_num":"2","title":"","info":[]}],"2":[{"id":"910","episode_num":1,"title":"Back","container_extension":"ts","info":{"duration_secs":"3000"}},{"id":"900","episode_num":9,"title":"Duplicate"}]}})";
    iptv::Catalog catalog;
    iptv::ParseReport report;
    ASSERT_EQ(iptv::ParseXtreamSeriesInfo(keyed, credentials, "12", "Show", 0x5854000000001234u,
                                          &catalog, &report),
              iptv::XtreamStatus::ok);
    ASSERT_EQ(catalog.size(), 3u);
    EXPECT_EQ(report.skipped, 1u);
    EXPECT_EQ(catalog[0].kind, iptv::MediaKind::episode);
    EXPECT_EQ(catalog[0].id, "xtream:5854000000001234:ep:900");
    EXPECT_EQ(catalog[0].name, "S01 E01 Pilot");
    EXPECT_EQ(catalog[0].group_title, "Season 1");
    EXPECT_EQ(catalog[0].duration_secs, 2580u);
    EXPECT_EQ(catalog[0].series_id, "12");
    EXPECT_EQ(catalog[0].tvg_name, "Show");
    EXPECT_EQ(catalog[0].container_ext, "mkv");
    EXPECT_EQ(catalog[0].url,
              "https://provider.example:25461/series/test%20user/p%40ss%26word/900.m3u8");
    EXPECT_EQ(catalog[1].name, "S01 E02");
    EXPECT_EQ(catalog[1].duration_secs, 0u);
    EXPECT_EQ(catalog[2].season, 2u);
    EXPECT_EQ(catalog[2].duration_secs, 3000u);

    // Some providers send episodes as an array of seasons instead of an object.
    constexpr std::string_view arrays =
        R"({"episodes":[[{"id":"1","episode_num":1,"title":"A"}],[{"id":"2","episode_num":1,"title":"B"}]]})";
    ASSERT_EQ(iptv::ParseXtreamSeriesInfo(arrays, credentials, "12", "Show", 7u, &catalog),
              iptv::XtreamStatus::ok);
    ASSERT_EQ(catalog.size(), 2u);
    EXPECT_EQ(catalog[0].season, 1u);
    EXPECT_EQ(catalog[1].season, 2u);

    EXPECT_EQ(
        iptv::ParseXtreamSeriesInfo(R"({"episodes":[]})", credentials, "12", "Show", 7u, &catalog),
        iptv::XtreamStatus::no_episodes);
    EXPECT_EQ(
        iptv::ParseXtreamSeriesInfo(R"({"info":{}})", credentials, "12", "Show", 7u, &catalog),
        iptv::XtreamStatus::malformed_json);
    EXPECT_EQ(iptv::ParseXtreamSeriesInfo("{", credentials, "12", "Show", 7u, &catalog),
              iptv::XtreamStatus::malformed_json);
    EXPECT_EQ(iptv::ParseXtreamSeriesInfo(keyed, credentials, "", "Show", 7u, &catalog),
              iptv::XtreamStatus::invalid_argument);
}

// Scripted stand-in for the network: url -> outcome and body. Unknown urls fail.
struct FakeServer
{
    struct Reply
    {
        iptv::XtreamFetchOutcome outcome = iptv::XtreamFetchOutcome::ok;
        std::string body;
    };
    std::map<std::string, Reply> replies;
    std::vector<std::string> requested;
    std::string current;

    static iptv::XtreamFetchOutcome Fetch(void *context, const std::string &url,
                                          std::string_view *body)
    {
        auto *server = static_cast<FakeServer *>(context);
        server->requested.push_back(url);
        const auto found = server->replies.find(url);
        if (found == server->replies.end())
            return iptv::XtreamFetchOutcome::failed;
        server->current = found->second.body;
        *body = server->current;
        return found->second.outcome;
    }
    iptv::XtreamFetcher Fetcher()
    {
        return {&FakeServer::Fetch, this};
    }

    void Add(const std::string &url, std::string body,
             iptv::XtreamFetchOutcome outcome = iptv::XtreamFetchOutcome::ok)
    {
        replies[url] = {outcome, std::move(body)};
    }
};

std::string ApiUrl(std::string_view action)
{
    std::string url;
    EXPECT_TRUE(iptv::BuildXtreamApiUrl(Credentials(), action, &url));
    return url;
}

std::string CategoryUrl(std::string_view action, std::string_view category)
{
    std::string url;
    EXPECT_TRUE(
        iptv::BuildXtreamApiUrlWithParam(Credentials(), action, "category_id", category, &url));
    return url;
}

constexpr std::uint64_t kLibrarySource = 0x5854000000001234u;

TEST(IptvXtreamTest, FetchesASmallMovieLibraryWithOneListRequest)
{
    FakeServer server;
    server.Add(ApiUrl("get_vod_categories"), R"([{"category_id":"3","category_name":"Action"}])");
    server.Add(ApiUrl("get_vod_streams"),
               R"([{"stream_id":1,"name":"One","category_id":"3"},{"stream_id":2,"name":"Two"}])");
    iptv::Catalog library;
    iptv::XtreamLibraryReport report;
    ASSERT_EQ(iptv::FetchXtreamLibrary(Credentials(), kLibrarySource,
                                       iptv::XtreamLibraryKind::movies, server.Fetcher(), &library,
                                       &report),
              iptv::XtreamStatus::ok);
    ASSERT_EQ(library.size(), 2u);
    EXPECT_EQ(library.source_id, kLibrarySource);
    EXPECT_EQ(library[0].group_title, "Action");
    EXPECT_EQ(library[1].group_title, "Movies");
    EXPECT_EQ(report.requests, 2u);
    EXPECT_FALSE(report.used_category_fallback);
    EXPECT_EQ(server.requested.size(), 2u);
}

TEST(IptvXtreamTest, SplitsAnOversizedMovieListByCategoryAndDeduplicates)
{
    FakeServer server;
    server.Add(
        ApiUrl("get_vod_categories"),
        R"([{"category_id":"1","category_name":"Drama"},{"category_id":"2","category_name":"Empty"},{"category_id":"3","category_name":"Comedy"}])");
    server.Add(ApiUrl("get_vod_streams"), "", iptv::XtreamFetchOutcome::too_large);
    server.Add(
        CategoryUrl("get_vod_streams", "1"),
        R"([{"stream_id":10,"name":"A","category_id":"1"},{"stream_id":11,"name":"B","category_id":"1"}])");
    server.Add(CategoryUrl("get_vod_streams", "2"), "[]");
    // Stream 11 appears in two categories and must be imported once.
    server.Add(
        CategoryUrl("get_vod_streams", "3"),
        R"([{"stream_id":11,"name":"B","category_id":"3"},{"stream_id":12,"name":"C","category_id":"3"}])");
    iptv::Catalog library;
    iptv::XtreamLibraryReport report;
    ASSERT_EQ(iptv::FetchXtreamLibrary(Credentials(), kLibrarySource,
                                       iptv::XtreamLibraryKind::movies, server.Fetcher(), &library,
                                       &report),
              iptv::XtreamStatus::ok);
    ASSERT_EQ(library.size(), 3u);
    EXPECT_EQ(library[0].id, "xtream:5854000000001234:vod:10");
    EXPECT_EQ(library[1].group_title, "Drama"); // first listing wins
    EXPECT_EQ(library[2].id, "xtream:5854000000001234:vod:12");
    EXPECT_TRUE(report.used_category_fallback);
    EXPECT_EQ(report.requests, 5u);
    EXPECT_EQ(report.categories, 3u);
    EXPECT_EQ(report.categories_skipped, 0u);
}

TEST(IptvXtreamTest, CategoryFallbackSkipsBadCategoriesButAbortsOnNetworkFailure)
{
    const auto make = [](iptv::XtreamFetchOutcome third)
    {
        FakeServer server;
        server.Add(
            ApiUrl("get_vod_categories"),
            R"([{"category_id":"1","category_name":"A"},{"category_id":"2","category_name":"B"},{"category_id":"3","category_name":"C"}])");
        server.Add(ApiUrl("get_vod_streams"), "", iptv::XtreamFetchOutcome::too_large);
        server.Add(CategoryUrl("get_vod_streams", "1"), R"([{"stream_id":1,"name":"One"}])");
        server.Add(CategoryUrl("get_vod_streams", "2"), "{not json");
        server.Add(CategoryUrl("get_vod_streams", "3"), "[]", third);
        return server;
    };

    // Malformed and oversized categories are skipped; the rest is kept.
    FakeServer tolerant = make(iptv::XtreamFetchOutcome::too_large);
    iptv::Catalog library;
    iptv::XtreamLibraryReport report;
    ASSERT_EQ(iptv::FetchXtreamLibrary(Credentials(), kLibrarySource,
                                       iptv::XtreamLibraryKind::movies, tolerant.Fetcher(),
                                       &library, &report),
              iptv::XtreamStatus::ok);
    EXPECT_EQ(library.size(), 1u);
    EXPECT_EQ(report.categories_skipped, 2u);

    // A failed request aborts the whole download so the old cache is kept.
    FakeServer failing = make(iptv::XtreamFetchOutcome::failed);
    EXPECT_EQ(iptv::FetchXtreamLibrary(Credentials(), kLibrarySource,
                                       iptv::XtreamLibraryKind::movies, failing.Fetcher(),
                                       &library),
              iptv::XtreamStatus::fetch_failed);
    EXPECT_TRUE(library.empty());

    FakeServer cancelled = make(iptv::XtreamFetchOutcome::cancelled);
    EXPECT_EQ(iptv::FetchXtreamLibrary(Credentials(), kLibrarySource,
                                       iptv::XtreamLibraryKind::movies, cancelled.Fetcher(),
                                       &library),
              iptv::XtreamStatus::cancelled);
}

TEST(IptvXtreamTest, LibraryFetchHandlesMissingCategoriesAndEmptyProviders)
{
    // Without a category list an oversized response cannot be split.
    FakeServer no_categories;
    no_categories.Add(ApiUrl("get_vod_categories"), "garbage");
    no_categories.Add(ApiUrl("get_vod_streams"), "", iptv::XtreamFetchOutcome::too_large);
    iptv::Catalog library;
    EXPECT_EQ(iptv::FetchXtreamLibrary(Credentials(), kLibrarySource,
                                       iptv::XtreamLibraryKind::movies, no_categories.Fetcher(),
                                       &library),
              iptv::XtreamStatus::too_large);

    // A bad category list does not stop a list that fits in one response.
    FakeServer tolerant;
    tolerant.Add(ApiUrl("get_vod_categories"), "garbage");
    tolerant.Add(ApiUrl("get_vod_streams"), R"([{"stream_id":1,"name":"One"}])");
    ASSERT_EQ(iptv::FetchXtreamLibrary(Credentials(), kLibrarySource,
                                       iptv::XtreamLibraryKind::movies, tolerant.Fetcher(),
                                       &library),
              iptv::XtreamStatus::ok);
    EXPECT_EQ(library.size(), 1u);

    // Providers without VOD return an empty list or fail to answer the category request.
    FakeServer empty;
    empty.Add(ApiUrl("get_vod_categories"), "[]");
    empty.Add(ApiUrl("get_vod_streams"), "[]");
    EXPECT_EQ(iptv::FetchXtreamLibrary(Credentials(), kLibrarySource,
                                       iptv::XtreamLibraryKind::movies, empty.Fetcher(), &library),
              iptv::XtreamStatus::no_movies);
    FakeServer silent;
    EXPECT_EQ(iptv::FetchXtreamLibrary(Credentials(), kLibrarySource,
                                       iptv::XtreamLibraryKind::movies, silent.Fetcher(), &library),
              iptv::XtreamStatus::fetch_failed);

    EXPECT_EQ(iptv::FetchXtreamLibrary(Credentials(), 0u, iptv::XtreamLibraryKind::movies,
                                       empty.Fetcher(), &library),
              iptv::XtreamStatus::invalid_argument);
    EXPECT_EQ(iptv::FetchXtreamLibrary(Credentials(), kLibrarySource,
                                       iptv::XtreamLibraryKind::movies, {nullptr, nullptr},
                                       &library),
              iptv::XtreamStatus::invalid_argument);
}

TEST(IptvXtreamTest, FetchesSeriesLibrariesThroughTheSeriesActions)
{
    FakeServer server;
    server.Add(ApiUrl("get_series_categories"), R"([{"category_id":"4","category_name":"Drama"}])");
    server.Add(ApiUrl("get_series"),
               R"([{"series_id":7,"name":"Show","category_id":"4","year":"2019"}])");
    iptv::Catalog library;
    ASSERT_EQ(iptv::FetchXtreamLibrary(Credentials(), kLibrarySource,
                                       iptv::XtreamLibraryKind::series, server.Fetcher(), &library),
              iptv::XtreamStatus::ok);
    ASSERT_EQ(library.size(), 1u);
    EXPECT_EQ(library[0].kind, iptv::MediaKind::series);
    EXPECT_EQ(library[0].group_title, "Drama");
    EXPECT_EQ(library[0].year, 2019u);
    ASSERT_EQ(server.requested.size(), 2u);
    EXPECT_EQ(server.requested[0], ApiUrl("get_series_categories"));

    FakeServer empty;
    empty.Add(ApiUrl("get_series_categories"), "[]");
    empty.Add(ApiUrl("get_series"), "[]");
    EXPECT_EQ(iptv::FetchXtreamLibrary(Credentials(), kLibrarySource,
                                       iptv::XtreamLibraryKind::series, empty.Fetcher(), &library),
              iptv::XtreamStatus::no_series);
}

iptv::Catalog Entries(std::initializer_list<std::pair<const char *, const char *>> entries)
{
    iptv::Catalog catalog;
    for (const auto &[id, name] : entries)
    {
        iptv::Channel channel;
        channel.id = id;
        channel.name = name;
        EXPECT_TRUE(catalog.Add(channel));
    }
    return catalog;
}

TEST(IptvXtreamTest, MergeKeepsFirstEntryPerId)
{
    iptv::Catalog library;
    std::unordered_set<std::string> seen;
    EXPECT_EQ(iptv::MergeXtreamLibraryPart(&library, Entries({{"a", ""}, {"b", ""}}), &seen), 2u);
    EXPECT_EQ(
        iptv::MergeXtreamLibraryPart(&library, Entries({{"b", "second b"}, {"c", ""}}), &seen), 1u);
    ASSERT_EQ(library.size(), 3u);
    EXPECT_TRUE(library[1].name.empty());
    EXPECT_EQ(library[2].id, "c");
    EXPECT_EQ(iptv::MergeXtreamLibraryPart(nullptr, {}, &seen), 0u);
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
        R"( [ {"stream_id":101,"name":"Canal Á \"uno\"","stream_icon":"https:\/\/images.example\/101.png","epg_channel_id":"canal.a","category_id":"7","container_extension":"m3u8","direct_source":"https:\/\/cdn.example\/live\/master.m3u8","ignored":{"nested":[1,true,null,"]}"]}} ,
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

TEST(IptvXtreamTest, ParsesMediaInfoFromVodAndSeriesResponses)
{
    iptv::MediaDetails details;
    ASSERT_EQ(iptv::ParseMediaInfo(
                  R"({"info":{"plot":"A hero rises.","genre":"Action, Drama","cast":"A, B",)"
                  R"("director":"C","releasedate":"2021-10-22","duration_secs":9300,)"
                  R"("duration":"02:35:00","rating":"7.8","backdrop_path":["x"],"tmdb_id":1},)"
                  R"("movie_data":{"stream_id":1}})",
                  &details),
              iptv::XtreamStatus::ok);
    EXPECT_EQ(details.plot, "A hero rises.");
    EXPECT_EQ(details.genre, "Action, Drama");
    EXPECT_EQ(details.cast, "A, B");
    EXPECT_EQ(details.director, "C");
    EXPECT_EQ(details.release_date, "2021-10-22");
    EXPECT_EQ(details.duration_secs, 9300u);
    EXPECT_EQ(details.rating_tenths, 78u);
    EXPECT_EQ(details.video_width, 0u);

    // The provider's measurement of the file.
    ASSERT_EQ(iptv::ParseMediaInfo(
                  R"({"info":{"plot":"P","video":{"index":0,"codec_name":"hevc",)"
                  R"("width":3840,"height":"1600","tags":{"x":"y"}},"audio":{"channels":6}}})",
                  &details),
              iptv::XtreamStatus::ok);
    EXPECT_EQ(details.video_width, 3840u);
    EXPECT_EQ(details.video_height, 1600u);
    EXPECT_EQ(details.video_codec, "hevc");
    ASSERT_EQ(iptv::ParseMediaInfo(R"({"info":{"video":[]}})", &details), iptv::XtreamStatus::ok);
    EXPECT_EQ(details.video_width, 0u);

    // Series info: releaseDate, episode_run_time in minutes, actors instead of cast.
    ASSERT_EQ(iptv::ParseMediaInfo(
                  R"({"seasons":[],"info":{"description":"Show plot","actors":"D",)"
                  R"("releaseDate":"2008","episode_run_time":"47","rating":8},"episodes":{}})",
                  &details),
              iptv::XtreamStatus::ok);
    EXPECT_EQ(details.plot, "Show plot");
    EXPECT_EQ(details.cast, "D");
    EXPECT_EQ(details.release_date, "2008");
    EXPECT_EQ(details.duration_secs, 47u * 60u);
    EXPECT_EQ(details.rating_tenths, 80u);

    ASSERT_EQ(iptv::ParseMediaInfo(R"({"info":{"duration":"01:30:00","plot":null}})", &details),
              iptv::XtreamStatus::ok);
    EXPECT_EQ(details.duration_secs, 5400u);
    EXPECT_TRUE(details.plot.empty());

    // Long plots are cut on a character boundary.
    const std::string long_plot(700, 'a');
    ASSERT_EQ(
        iptv::ParseMediaInfo(R"({"info":{"plot":")" + long_plot + "\xc3\xa9" + R"("}})", &details),
        iptv::XtreamStatus::ok);
    EXPECT_LE(details.plot.size(), 643u);
    EXPECT_EQ(details.plot.substr(details.plot.size() - 3u), "...");

    EXPECT_EQ(iptv::ParseMediaInfo(R"({"info":[],"movie_data":{}})", &details),
              iptv::XtreamStatus::ok);
    EXPECT_TRUE(details.plot.empty());
    EXPECT_EQ(iptv::ParseMediaInfo("{", &details), iptv::XtreamStatus::malformed_json);
    EXPECT_EQ(iptv::ParseMediaInfo("", &details), iptv::XtreamStatus::malformed_json);
}

TEST(IptvXtreamTest, BuildsTheGuideAddress)
{
    std::string url;
    ASSERT_TRUE(iptv::BuildXtreamGuideUrl(Credentials(), &url));
    EXPECT_EQ(url, "https://provider.example:25461/xmltv.php"
                   "?username=test%20user&password=p%40ss%26word");
    EXPECT_FALSE(iptv::BuildXtreamGuideUrl({}, &url));
}

} // namespace
