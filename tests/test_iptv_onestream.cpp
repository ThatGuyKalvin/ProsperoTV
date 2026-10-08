/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_onestream.h"
#include "iptv_source_state.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace
{

using iptv::XtreamFetchOutcome;
using iptv::XtreamStatus;

constexpr std::uint64_t kSource = 0x4f53000000001234u;

iptv::OneStreamCredentials Credentials()
{
    return {"https://panel.example:8080/", "test user", "p@ss&word"};
}

iptv::OneStreamSession Session()
{
    return {"tok+en/1"};
}

// Scripted stand-in for the network. GETs are keyed by URL, POSTs by URL and form.
// Unknown requests fail with no body.
struct FakePanel
{
    struct Reply
    {
        XtreamFetchOutcome outcome = XtreamFetchOutcome::ok;
        std::string body;
    };
    std::map<std::string, Reply> gets;
    std::map<std::string, Reply> posts;
    std::vector<std::string> requested;
    std::string current;

    static XtreamFetchOutcome Answer(FakePanel *panel, const std::map<std::string, Reply> &replies,
                                     const std::string &key, std::string_view *body)
    {
        panel->requested.push_back(key);
        *body = {};
        const auto found = replies.find(key);
        if (found == replies.end())
            return XtreamFetchOutcome::failed;
        panel->current = found->second.body;
        *body = panel->current;
        return found->second.outcome;
    }
    static XtreamFetchOutcome Get(void *context, const std::string &url, std::string_view *body)
    {
        auto *panel = static_cast<FakePanel *>(context);
        return Answer(panel, panel->gets, url, body);
    }
    static XtreamFetchOutcome Post(void *context, const std::string &url, const std::string &form,
                                   std::string_view *body)
    {
        auto *panel = static_cast<FakePanel *>(context);
        return Answer(panel, panel->posts, "POST " + url + " " + form, body);
    }
    iptv::OneStreamTransport Transport()
    {
        return {&FakePanel::Get, &FakePanel::Post, this};
    }

    void AddGet(const std::string &url, std::string body,
                XtreamFetchOutcome outcome = XtreamFetchOutcome::ok)
    {
        gets[url] = {outcome, std::move(body)};
    }
    void AddLogin(std::string auth_body = R"({"auth_token":"tok+en/1"})",
                  XtreamFetchOutcome outcome = XtreamFetchOutcome::ok)
    {
        std::string url;
        std::string form;
        EXPECT_TRUE(iptv::BuildOneStreamAuthRequest(Credentials(), &url, &form));
        posts["POST " + url + " " + form] = {outcome, std::move(auth_body)};
    }
};

std::string UserInfoUrl()
{
    std::string url;
    EXPECT_TRUE(iptv::BuildOneStreamUserInfoUrl(Credentials(), Session(), &url));
    return url;
}

std::string CategoriesUrl(iptv::OneStreamContent content)
{
    std::string url;
    EXPECT_TRUE(iptv::BuildOneStreamCategoriesUrl(Credentials(), Session(), content, &url));
    return url;
}

std::string ContentUrl(iptv::OneStreamContent content, std::string_view category = "all")
{
    std::string url;
    EXPECT_TRUE(iptv::BuildOneStreamContentUrl(Credentials(), Session(), content, category, &url));
    return url;
}

constexpr char kActiveUser[] =
    R"({"user_info":{"auth":1,"status":"Active","username":"test user","message":"",)"
    R"("expire_at":"2026-12-31 00:00:00","active_connections":"0","created_at":null,)"
    R"("max_connections":"2","allowed_output_formats":["m3u8","ts"]},)"
    R"("server_info":{"time_now":"2026-10-05 12:00:00","timezone":"UTC"}})";

TEST(IptvOneStreamTest, NormalizesPanelAddresses)
{
    std::string server;
    ASSERT_TRUE(iptv::NormalizeOneStreamServerUrl("panel.example", &server));
    EXPECT_EQ(server, "http://panel.example/");
    ASSERT_TRUE(iptv::NormalizeOneStreamServerUrl("  HTTPS://Panel.Example:8080  ", &server));
    EXPECT_EQ(server, "https://panel.example:8080/");
    ASSERT_TRUE(iptv::NormalizeOneStreamServerUrl("http://panel.example/Sub/Path//", &server));
    EXPECT_EQ(server, "http://panel.example/Sub/Path/");
    ASSERT_TRUE(
        iptv::NormalizeOneStreamServerUrl("https://panel.example/play/b2c/v1/auth", &server));
    EXPECT_EQ(server, "https://panel.example/");
    ASSERT_TRUE(iptv::NormalizeOneStreamServerUrl("https://panel.example/x/PLAY/B2C/V1", &server));
    EXPECT_EQ(server, "https://panel.example/x/");

    for (const char *rejected :
         {"", "   ", "ftp://panel.example", "http://panel.example/?a=1", "http://panel.example/#x",
          "http://user@panel.example", "http://panel .example", "http://pan\x01el.example"})
        EXPECT_FALSE(iptv::NormalizeOneStreamServerUrl(rejected, &server)) << rejected;
    EXPECT_FALSE(iptv::NormalizeOneStreamServerUrl(
        "http://panel.example/" + std::string(iptv::kMaxOneStreamServerBytes, 'a'), &server));
}

TEST(IptvOneStreamTest, BuildsEncodedRequests)
{
    const iptv::OneStreamCredentials credentials = Credentials();
    ASSERT_TRUE(iptv::ValidateOneStreamCredentials(credentials));
    std::string url;
    std::string form;
    ASSERT_TRUE(iptv::BuildOneStreamAuthRequest(credentials, &url, &form));
    EXPECT_EQ(url, "https://panel.example:8080/play/b2c/v1/auth");
    EXPECT_EQ(form, "username=test%20user&password=p%40ss%26word");

    iptv::OneStreamCredentials unicode = credentials;
    unicode.username = "jos\xc3\xa9=+";
    ASSERT_TRUE(iptv::BuildOneStreamAuthRequest(unicode, &url, &form));
    EXPECT_EQ(form, "username=jos%C3%A9%3D%2B&password=p%40ss%26word");

    ASSERT_TRUE(iptv::BuildOneStreamUserInfoUrl(credentials, Session(), &url));
    EXPECT_EQ(url, "https://panel.example:8080/play/b2c/v1/user-info?token=tok%2Ben%2F1");
    ASSERT_TRUE(iptv::BuildOneStreamCategoriesUrl(credentials, Session(),
                                                  iptv::OneStreamContent::vod, &url));
    EXPECT_EQ(url, "https://panel.example:8080/play/b2c/v1/categories/vod?token=tok%2Ben%2F1");
    ASSERT_TRUE(iptv::BuildOneStreamContentUrl(credentials, Session(),
                                               iptv::OneStreamContent::series, "a b", &url));
    EXPECT_EQ(url, "https://panel.example:8080/play/b2c/v1/content/series"
                   "?token=tok%2Ben%2F1&category_id=a%20b");
    ASSERT_TRUE(iptv::BuildOneStreamSeriesInfoUrl(credentials, Session(), "s/1", &url));
    EXPECT_EQ(url, "https://panel.example:8080/play/b2c/v1/content/series/s%2F1"
                   "?token=tok%2Ben%2F1");

    EXPECT_FALSE(iptv::BuildOneStreamUserInfoUrl(credentials, {""}, &url));
    EXPECT_FALSE(iptv::BuildOneStreamUserInfoUrl(credentials, {"has space"}, &url));
    EXPECT_FALSE(iptv::BuildOneStreamContentUrl(credentials, Session(),
                                                iptv::OneStreamContent::live, "", &url));
    EXPECT_FALSE(
        iptv::BuildOneStreamSeriesInfoUrl(credentials, Session(), std::string(65, '1'), &url));
    iptv::OneStreamCredentials unnormalized = credentials;
    unnormalized.server_url = "https://panel.example:8080";
    EXPECT_FALSE(iptv::ValidateOneStreamCredentials(unnormalized));
    EXPECT_FALSE(iptv::BuildOneStreamAuthRequest(unnormalized, &url, &form));
}

TEST(IptvOneStreamTest, SourceIdIgnoresTheTokenAndDiffersFromXtream)
{
    const iptv::OneStreamCredentials credentials = Credentials();
    const std::uint64_t id = iptv::OneStreamSourceId(credentials);
    EXPECT_EQ(id >> 48u, 0x4f53u);
    EXPECT_EQ(id, iptv::OneStreamSourceId(credentials));
    iptv::OneStreamCredentials other = credentials;
    other.password = "different";
    EXPECT_NE(id, iptv::OneStreamSourceId(other));
    const iptv::XtreamCredentials xtream{credentials.server_url, credentials.username,
                                         credentials.password};
    EXPECT_NE(id, iptv::XtreamSourceId(xtream));
}

TEST(IptvOneStreamTest, ParsesAuthResponses)
{
    iptv::OneStreamSession session;
    std::string message;
    EXPECT_EQ(iptv::ParseOneStreamAuth(R"({"auth_token":"abc.def"})", &session, &message),
              XtreamStatus::ok);
    EXPECT_EQ(session.token, "abc.def");
    EXPECT_EQ(iptv::ParseOneStreamAuth(R"({"auth_token":""})", &session),
              XtreamStatus::authentication_failed);
    EXPECT_TRUE(session.token.empty());
    EXPECT_EQ(iptv::ParseOneStreamAuth(R"({"message":"Invalid credentials"})", &session, &message),
              XtreamStatus::authentication_failed);
    EXPECT_EQ(message, "Invalid credentials");
    EXPECT_EQ(iptv::ParseOneStreamAuth(R"({"auth_token":null})", &session),
              XtreamStatus::authentication_failed);
    EXPECT_EQ(iptv::ParseOneStreamAuth(R"({"auth_token":"a b"})", &session),
              XtreamStatus::malformed_json);
    const std::string long_token =
        R"({"auth_token":")" + std::string(iptv::kMaxOneStreamTokenBytes + 1u, 'a') + R"("})";
    EXPECT_EQ(iptv::ParseOneStreamAuth(long_token, &session), XtreamStatus::malformed_json);
    EXPECT_EQ(iptv::ParseOneStreamAuth("not json", &session), XtreamStatus::malformed_json);
    EXPECT_EQ(iptv::ParseOneStreamAuth("", &session), XtreamStatus::malformed_json);
}

TEST(IptvOneStreamTest, ParsesUserInfoAndExplainsAccountFailures)
{
    iptv::OneStreamAccount account;
    ASSERT_EQ(iptv::ParseOneStreamUserInfo(kActiveUser, &account), XtreamStatus::ok);
    EXPECT_EQ(account.status, "Active");
    EXPECT_EQ(account.expire_at, "2026-12-31 00:00:00");
    EXPECT_TRUE(account.created_at.empty());
    EXPECT_EQ(account.max_connections, 2u);
    EXPECT_EQ(account.active_connections, 0u);
    EXPECT_EQ(account.timezone, "UTC");

    // Only auth and status decide; missing optional fields are fine.
    EXPECT_EQ(
        iptv::ParseOneStreamUserInfo(R"({"user_info":{"auth":"1","status":"active"}})", &account),
        XtreamStatus::ok);
    EXPECT_EQ(iptv::ParseOneStreamUserInfo(R"({"user_info":{}})", &account), XtreamStatus::ok);
    EXPECT_EQ(iptv::ParseOneStreamUserInfo(
                  R"({"user_info":{"auth":1,"status":"Active","max_connections":3}})", &account),
              XtreamStatus::ok);
    EXPECT_EQ(account.max_connections, 3u);

    EXPECT_EQ(
        iptv::ParseOneStreamUserInfo(R"({"user_info":{"auth":0,"status":"Active"}})", &account),
        XtreamStatus::authentication_failed);
    EXPECT_EQ(iptv::ParseOneStreamUserInfo(R"({"user_info":{"auth":"0"}})", &account),
              XtreamStatus::authentication_failed);
    EXPECT_EQ(iptv::ParseOneStreamUserInfo(
                  R"({"user_info":{"auth":1,"status":"Expired","message":"Renew"}})", &account),
              XtreamStatus::account_inactive);
    EXPECT_EQ(account.message, "Renew");
    EXPECT_EQ(iptv::ParseOneStreamUserInfo(R"({"user_info":{"status":"Banned"}})", &account),
              XtreamStatus::account_inactive);
    EXPECT_EQ(iptv::ParseOneStreamUserInfo(R"({"server_info":{}})", &account),
              XtreamStatus::malformed_json);
    EXPECT_EQ(iptv::ParseOneStreamUserInfo(R"({"user_info":[]})", &account),
              XtreamStatus::malformed_json);
    EXPECT_EQ(iptv::ParseOneStreamUserInfo("{", &account), XtreamStatus::malformed_json);
}

TEST(IptvOneStreamTest, MapsLiveStreamsAndChoosesLinks)
{
    const std::vector<iptv::XtreamCategory> categories = {{"12", "News"}, {"15", "UK"}, {"20", ""}};
    constexpr char json[] = R"({"content":[
        {"num":"1","name":"BBC One","stream_type":"live","stream_id":"abc123",
         "stream_icon":"http://img.example/bbc.png","epg_channel_id":"bbc1.uk","is_adult":"0",
         "links":{"ts":"http://cdn.example/x.ts","m3u8":"http://cdn.example/x.m3u8"},
         "categories":["12",15]},
        {"stream_id":"str","name":"Text links","links":"{\"m3u8\":\"http://cdn.example/s.m3u8\"}",
         "categories":["20"]},
        {"stream_id":"other","name":"Odd format","links":{"mp4":"http://cdn.example/o.mp4"},
         "category_id":"99"},
        {"stream_id":"abc123","name":"Duplicate","links":{"ts":"http://cdn.example/d.ts"}},
        {"stream_id":"nolinks","name":"No links","links":{}},
        {"stream_id":"bad","name":"Bad link","links":{"ts":"ftp://cdn.example/b.ts"}},
        {"name":"No id","links":{"ts":"http://cdn.example/n.ts"}},
        {"stream_id":"many","links":{"a":"http://c.example/1","b":"http://c.example/2",
         "c":"http://c.example/3","d":"http://c.example/4","e":"http://c.example/5"}}
    ]})";
    iptv::Catalog catalog;
    iptv::ParseReport report;
    ASSERT_EQ(iptv::ParseOneStreamLiveStreams(json, categories, kSource, &catalog, &report),
              XtreamStatus::ok);
    EXPECT_EQ(catalog.source_id, kSource);
    ASSERT_EQ(catalog.size(), 4u);
    EXPECT_EQ(report.accepted, 4u);
    EXPECT_EQ(report.skipped, 4u);

    const iptv::ChannelView bbc = catalog[0];
    EXPECT_EQ(bbc.id, "onestream:4f53000000001234:abc123");
    EXPECT_EQ(bbc.kind, iptv::MediaKind::live);
    EXPECT_EQ(bbc.name, "BBC One");
    EXPECT_EQ(bbc.tvg_name, "BBC One");
    EXPECT_EQ(bbc.tvg_id, "bbc1.uk");
    EXPECT_EQ(bbc.tvg_logo, "http://img.example/bbc.png");
    EXPECT_EQ(bbc.group_title, "News");
    EXPECT_EQ(bbc.alternate_group_titles, std::vector<std::string>{"UK"});
    EXPECT_EQ(bbc.url, "http://cdn.example/x.m3u8");
    EXPECT_EQ(bbc.alternate_urls, std::vector<std::string>{"http://cdn.example/x.ts"});

    EXPECT_EQ(catalog[1].url, "http://cdn.example/s.m3u8");
    EXPECT_EQ(catalog[1].group_title, "Live TV");
    // Fields are never inherited from the previous entry.
    EXPECT_TRUE(catalog[1].tvg_logo.empty());
    EXPECT_TRUE(catalog[1].tvg_id.empty());
    EXPECT_EQ(catalog[2].url, "http://cdn.example/o.mp4");
    EXPECT_EQ(catalog[2].group_title, "Live TV");
    EXPECT_EQ(catalog[3].name, "Channel many");
    EXPECT_EQ(catalog[3].alternate_urls.size(), iptv::kDefaultMaxAlternateUrls);

    EXPECT_EQ(iptv::ParseOneStreamLiveStreams(R"({"content":[]})", {}, kSource, &catalog),
              XtreamStatus::no_channels);
    EXPECT_EQ(iptv::ParseOneStreamLiveStreams(R"([])", {}, kSource, &catalog),
              XtreamStatus::no_channels);
    EXPECT_EQ(iptv::ParseOneStreamLiveStreams(R"({"content":{}})", {}, kSource, &catalog),
              XtreamStatus::malformed_json);
    EXPECT_EQ(iptv::ParseOneStreamLiveStreams(R"({"data":[]})", {}, kSource, &catalog),
              XtreamStatus::malformed_json);
    EXPECT_EQ(iptv::ParseOneStreamLiveStreams(R"({"content":[{)", {}, kSource, &catalog),
              XtreamStatus::malformed_json);
    EXPECT_TRUE(catalog.empty());
    EXPECT_EQ(iptv::ParseOneStreamLiveStreams(R"({"content":[]})", {}, 0u, &catalog),
              XtreamStatus::invalid_argument);
}

TEST(IptvOneStreamTest, MapsMoviesAndSeries)
{
    const std::vector<iptv::XtreamCategory> categories = {{"3", "Action"}};
    iptv::Catalog movies;
    ASSERT_EQ(
        iptv::ParseOneStreamVodStreams(
            R"({"content":[{"stream_id":7,"name":"Film","rating":"7.5","added":"1700000000",)"
            R"("stream_icon":"http://img.example/f.jpg","categories":["3"],)"
            R"("links":{"mp4":"http://cdn.example/f.MP4?sig=1","m3u8":"http://cdn.example/f.m3u8"}},)"
            R"({"stream_id":8,"links":{"ts":"http://cdn.example/g.ts"}}]})",
            categories, kSource, &movies),
        XtreamStatus::ok);
    ASSERT_EQ(movies.size(), 2u);
    EXPECT_EQ(movies[0].id, "onestream:4f53000000001234:vod:7");
    EXPECT_EQ(movies[0].kind, iptv::MediaKind::movie);
    EXPECT_EQ(movies[0].group_title, "Action");
    EXPECT_EQ(movies[0].rating_tenths, 75u);
    EXPECT_EQ(movies[0].year, 0u);
    EXPECT_EQ(movies[0].container_ext, "m3u8");
    EXPECT_EQ(movies[0].alternate_urls, std::vector<std::string>{"http://cdn.example/f.MP4?sig=1"});
    EXPECT_EQ(movies[1].name, "Movie 8");
    EXPECT_EQ(movies[1].group_title, "Movies");
    EXPECT_EQ(movies[1].container_ext, "ts");
    EXPECT_EQ(iptv::ParseOneStreamVodStreams(R"({"content":[]})", {}, kSource, &movies),
              XtreamStatus::no_movies);

    iptv::Catalog series;
    ASSERT_EQ(
        iptv::ParseOneStreamSeriesList(
            R"({"content":[{"series_id":"s/1","name":"Show","cover":"http://img.example/s.jpg",)"
            R"("rating":"8","release_date":"2019-04-01","categories":["3"]},{"name":"No id"}]})",
            Credentials(), categories, kSource, &series),
        XtreamStatus::ok);
    ASSERT_EQ(series.size(), 1u);
    const iptv::ChannelView show = series[0];
    EXPECT_EQ(show.id, "onestream:4f53000000001234:series:s/1");
    EXPECT_EQ(show.kind, iptv::MediaKind::series);
    EXPECT_EQ(show.series_id, "s/1");
    EXPECT_EQ(show.year, 2019u);
    EXPECT_EQ(show.rating_tenths, 80u);
    EXPECT_EQ(show.group_title, "Action");
    // The stored address never carries a token.
    EXPECT_EQ(show.url, "https://panel.example:8080/play/b2c/v1/content/series/s%2F1");
    EXPECT_EQ(show.url.find("token"), std::string::npos);
    EXPECT_EQ(
        iptv::ParseOneStreamSeriesList(R"({"content":[]})", Credentials(), {}, kSource, &series),
        XtreamStatus::no_series);
}

TEST(IptvOneStreamTest, ParsesSeriesEpisodesInBothLayouts)
{
    constexpr char keyed[] = R"({"seasons":[],"info":{"backdrop_path":[]},"episodes":{
        "1":[{"id":"e1","season":1,"episode_num":1,"title":"Pilot",
              "links":{"m3u8":"http://cdn.example/e1.m3u8"},
              "info":{"movie_image":"http://img.example/e1.jpg","duration":"00:45:30"}},
             {"id":"e2","episode_num":"2","links":{"ts":"http://cdn.example/e2.ts"},
              "info":{"duration_secs":"2700"}},
             {"id":"e3","episode_num":3,"info":[]}],
        "2":[{"id":"e4","episode_num":1,"links":{"m3u8":"http://cdn.example/e4.m3u8"}}]}})";
    iptv::Catalog episodes;
    iptv::ParseReport report;
    ASSERT_EQ(iptv::ParseOneStreamSeriesInfo(keyed, "s1", "Show", kSource, &episodes, &report),
              XtreamStatus::ok);
    ASSERT_EQ(episodes.size(), 3u);
    EXPECT_EQ(report.skipped, 1u);
    const iptv::ChannelView pilot = episodes[0];
    EXPECT_EQ(pilot.id, "onestream:4f53000000001234:ep:e1");
    EXPECT_EQ(pilot.kind, iptv::MediaKind::episode);
    EXPECT_EQ(pilot.name, "S01 E01 Pilot");
    EXPECT_EQ(pilot.tvg_name, "Show");
    EXPECT_EQ(pilot.group_title, "Season 1");
    EXPECT_EQ(pilot.duration_secs, 2730u);
    EXPECT_EQ(pilot.tvg_logo, "http://img.example/e1.jpg");
    EXPECT_EQ(pilot.container_ext, "m3u8");
    EXPECT_EQ(episodes[1].name, "S01 E02");
    EXPECT_EQ(episodes[1].duration_secs, 2700u);
    EXPECT_EQ(episodes[1].url, "http://cdn.example/e2.ts");
    EXPECT_EQ(episodes[2].season, 2u);
    EXPECT_EQ(episodes[2].group_title, "Season 2");

    constexpr char nested[] =
        R"({"episodes":[[{"id":"a","episode_num":1,"links":{"ts":"http://cdn.example/a.ts"}}],)"
        R"([{"id":"b","episode_num":1,"links":{"ts":"http://cdn.example/b.ts"}}]]})";
    ASSERT_EQ(iptv::ParseOneStreamSeriesInfo(nested, "s1", "Show", kSource, &episodes),
              XtreamStatus::ok);
    ASSERT_EQ(episodes.size(), 2u);
    EXPECT_EQ(episodes[1].name, "S02 E01");

    EXPECT_EQ(
        iptv::ParseOneStreamSeriesInfo(R"({"episodes":{}})", "s1", "Show", kSource, &episodes),
        XtreamStatus::no_episodes);
    EXPECT_EQ(iptv::ParseOneStreamSeriesInfo(R"({"info":{}})", "s1", "Show", kSource, &episodes),
              XtreamStatus::malformed_json);
    EXPECT_EQ(iptv::ParseOneStreamSeriesInfo(keyed, "", "Show", kSource, &episodes),
              XtreamStatus::invalid_argument);
}

TEST(IptvOneStreamTest, LoginPostsThenChecksTheAccount)
{
    FakePanel panel;
    panel.AddLogin();
    panel.AddGet(UserInfoUrl(), kActiveUser);
    iptv::OneStreamSession session;
    iptv::OneStreamAccount account;
    std::string message;
    ASSERT_EQ(iptv::OneStreamLogin(Credentials(), panel.Transport(), &session, &account, &message),
              XtreamStatus::ok);
    EXPECT_EQ(session.token, "tok+en/1");
    EXPECT_EQ(account.max_connections, 2u);
    ASSERT_EQ(panel.requested.size(), 2u);
    EXPECT_EQ(panel.requested[0].rfind("POST ", 0), 0u);
    EXPECT_EQ(panel.requested[1], UserInfoUrl());
}

TEST(IptvOneStreamTest, LoginReportsRejectedAndInactiveAccounts)
{
    iptv::OneStreamSession session;
    std::string message;
    {
        // The panel refuses with an error status and a JSON message.
        FakePanel panel;
        panel.AddLogin(R"({"message":"Wrong password"})", XtreamFetchOutcome::failed);
        EXPECT_EQ(
            iptv::OneStreamLogin(Credentials(), panel.Transport(), &session, nullptr, &message),
            XtreamStatus::authentication_failed);
        EXPECT_EQ(message, "Wrong password");
        EXPECT_EQ(panel.requested.size(), 1u);
    }
    {
        // An error page that is not JSON is a server failure, not a rejected password.
        FakePanel panel;
        panel.AddLogin("<html>502</html>", XtreamFetchOutcome::failed);
        EXPECT_EQ(
            iptv::OneStreamLogin(Credentials(), panel.Transport(), &session, nullptr, &message),
            XtreamStatus::fetch_failed);
    }
    {
        FakePanel panel;
        panel.AddLogin(R"({"auth_token":""})");
        EXPECT_EQ(iptv::OneStreamLogin(Credentials(), panel.Transport(), &session, nullptr),
                  XtreamStatus::authentication_failed);
        EXPECT_EQ(panel.requested.size(), 1u);
    }
    {
        FakePanel panel;
        panel.AddLogin();
        panel.AddGet(UserInfoUrl(),
                     R"({"user_info":{"auth":1,"status":"Expired","message":"Renew now"}})");
        iptv::OneStreamAccount account;
        EXPECT_EQ(
            iptv::OneStreamLogin(Credentials(), panel.Transport(), &session, &account, &message),
            XtreamStatus::account_inactive);
        EXPECT_EQ(message, "Renew now");
        EXPECT_EQ(account.status, "Expired");
        EXPECT_TRUE(session.token.empty());
    }
    {
        FakePanel panel;
        panel.AddLogin("", XtreamFetchOutcome::cancelled);
        EXPECT_EQ(iptv::OneStreamLogin(Credentials(), panel.Transport(), &session, nullptr),
                  XtreamStatus::cancelled);
    }
    {
        FakePanel panel;
        panel.AddLogin();
        panel.AddGet(UserInfoUrl(), "", XtreamFetchOutcome::cancelled);
        EXPECT_EQ(iptv::OneStreamLogin(Credentials(), panel.Transport(), &session, nullptr),
                  XtreamStatus::cancelled);
        EXPECT_TRUE(session.token.empty());
    }
}

TEST(IptvOneStreamTest, FetchesLiveLineupWithCategoryNames)
{
    FakePanel panel;
    panel.AddGet(CategoriesUrl(iptv::OneStreamContent::live),
                 R"({"content":[{"category_id":"12","category_name":"News","parent_id":0}]})");
    panel.AddGet(ContentUrl(iptv::OneStreamContent::live),
                 R"({"content":[{"stream_id":"1","name":"One","categories":["12"],)"
                 R"("links":{"ts":"http://cdn.example/1.ts"}}]})");
    iptv::Catalog catalog;
    iptv::XtreamLibraryReport report;
    ASSERT_EQ(iptv::FetchOneStreamLive(Credentials(), Session(), kSource, panel.Transport(),
                                       &catalog, &report),
              XtreamStatus::ok);
    ASSERT_EQ(catalog.size(), 1u);
    EXPECT_EQ(catalog[0].group_title, "News");
    EXPECT_EQ(report.requests, 2u);
    EXPECT_EQ(report.categories, 1u);
    EXPECT_FALSE(report.used_category_fallback);

    // A broken category list is not fatal; a failed lineup request is.
    FakePanel broken;
    broken.AddGet(CategoriesUrl(iptv::OneStreamContent::live), "oops");
    broken.AddGet(ContentUrl(iptv::OneStreamContent::live),
                  R"({"content":[{"stream_id":"1","links":{"ts":"http://cdn.example/1.ts"}}]})");
    ASSERT_EQ(
        iptv::FetchOneStreamLive(Credentials(), Session(), kSource, broken.Transport(), &catalog),
        XtreamStatus::ok);
    EXPECT_EQ(catalog[0].group_title, "Live TV");

    FakePanel failing;
    failing.AddGet(CategoriesUrl(iptv::OneStreamContent::live), R"({"content":[]})");
    EXPECT_EQ(
        iptv::FetchOneStreamLive(Credentials(), Session(), kSource, failing.Transport(), &catalog),
        XtreamStatus::fetch_failed);
    EXPECT_TRUE(catalog.empty());
    EXPECT_EQ(iptv::FetchOneStreamLive(Credentials(), {""}, kSource, failing.Transport(), &catalog),
              XtreamStatus::invalid_argument);
}

TEST(IptvOneStreamTest, SplitsOversizedLibrariesByCategory)
{
    FakePanel panel;
    panel.AddGet(
        CategoriesUrl(iptv::OneStreamContent::vod),
        R"({"content":[{"category_id":"1","category_name":"A"},)"
        R"({"category_id":"2","category_name":"B"},{"category_id":"3","category_name":"C"}]})");
    panel.AddGet(ContentUrl(iptv::OneStreamContent::vod), "", XtreamFetchOutcome::too_large);
    panel.AddGet(ContentUrl(iptv::OneStreamContent::vod, "1"),
                 R"({"content":[{"stream_id":"10","categories":["1"],)"
                 R"("links":{"m3u8":"http://cdn.example/10.m3u8"}}]})");
    panel.AddGet(
        ContentUrl(iptv::OneStreamContent::vod, "2"),
        R"({"content":[{"stream_id":"10","categories":["1","2"],)"
        R"("links":{"m3u8":"http://cdn.example/10.m3u8"}},)"
        R"({"stream_id":"20","categories":["2"],"links":{"ts":"http://cdn.example/20.ts"}}]})");
    panel.AddGet(ContentUrl(iptv::OneStreamContent::vod, "3"), "", XtreamFetchOutcome::too_large);
    iptv::Catalog library;
    iptv::XtreamLibraryReport report;
    ASSERT_EQ(iptv::FetchOneStreamLibrary(Credentials(), Session(), kSource,
                                          iptv::XtreamLibraryKind::movies, panel.Transport(),
                                          &library, &report),
              XtreamStatus::ok);
    ASSERT_EQ(library.size(), 2u);
    EXPECT_EQ(library.source_id, kSource);
    EXPECT_TRUE(report.used_category_fallback);
    EXPECT_EQ(report.requests, 5u);
    EXPECT_EQ(report.categories_skipped, 1u);

    // A failed category request aborts so the caller keeps its previous cache.
    panel.AddGet(ContentUrl(iptv::OneStreamContent::vod, "3"), "", XtreamFetchOutcome::failed);
    EXPECT_EQ(iptv::FetchOneStreamLibrary(Credentials(), Session(), kSource,
                                          iptv::XtreamLibraryKind::movies, panel.Transport(),
                                          &library),
              XtreamStatus::fetch_failed);
    EXPECT_TRUE(library.empty());
}

TEST(IptvOneStreamTest, FetchesEpisodesWithAFreshLogin)
{
    FakePanel panel;
    panel.AddLogin();
    panel.AddGet(UserInfoUrl(), kActiveUser);
    std::string url;
    ASSERT_TRUE(iptv::BuildOneStreamSeriesInfoUrl(Credentials(), Session(), "s1", &url));
    panel.AddGet(url, R"({"episodes":{"1":[{"id":"e1","episode_num":1,)"
                      R"("links":{"m3u8":"http://cdn.example/e1.m3u8"}}]}})");
    iptv::Catalog episodes;
    ASSERT_EQ(iptv::FetchOneStreamEpisodes(Credentials(), "s1", "Show", kSource, panel.Transport(),
                                           &episodes),
              XtreamStatus::ok);
    ASSERT_EQ(episodes.size(), 1u);
    EXPECT_EQ(panel.requested.size(), 3u);

    FakePanel refused;
    refused.AddLogin(R"({"auth_token":""})");
    EXPECT_EQ(iptv::FetchOneStreamEpisodes(Credentials(), "s1", "Show", kSource,
                                           refused.Transport(), &episodes),
              XtreamStatus::authentication_failed);
}

TEST(IptvOneStreamTest, PersistsCredentialsWithoutTheToken)
{
    const iptv::OneStreamCredentials credentials = Credentials();
    const std::string path =
        std::string(::testing::TempDir()) + "prosperotv-onestream-test-credentials";
    ASSERT_EQ(iptv::SaveOneStreamCredentials(path, credentials), XtreamStatus::ok);
    iptv::OneStreamCredentials loaded;
    ASSERT_EQ(iptv::LoadOneStreamCredentials(path, &loaded), XtreamStatus::ok);
    EXPECT_EQ(loaded.server_url, credentials.server_url);
    EXPECT_EQ(loaded.username, credentials.username);
    EXPECT_EQ(loaded.password, credentials.password);
    {
        std::ifstream input(path, std::ios::binary);
        const std::string contents((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
        EXPECT_EQ(contents, "PROSPEROTV-ONESTREAM-1\nhttps://panel.example:8080/\ntest user\n"
                            "p@ss&word\n");
    }

    const auto write = [&](const std::string &contents)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << contents;
    };
    write("PROSPEROTV-XTREAM-1\nhttps://panel.example:8080/\nu\np\n");
    EXPECT_EQ(iptv::LoadOneStreamCredentials(path, &loaded), XtreamStatus::corrupt);
    write("PROSPEROTV-ONESTREAM-1\nhttps://panel.example:8080/\nu\n");
    EXPECT_EQ(iptv::LoadOneStreamCredentials(path, &loaded), XtreamStatus::corrupt);
    write("PROSPEROTV-ONESTREAM-1\nhttps://panel.example:8080\nu\np\n");
    EXPECT_EQ(iptv::LoadOneStreamCredentials(path, &loaded), XtreamStatus::corrupt);
    write(std::string(4096, 'a'));
    EXPECT_EQ(iptv::LoadOneStreamCredentials(path, &loaded), XtreamStatus::too_large);
    std::remove(path.c_str());
    EXPECT_EQ(iptv::LoadOneStreamCredentials(path, &loaded), XtreamStatus::not_found);

    iptv::OneStreamCredentials invalid = credentials;
    invalid.password.clear();
    EXPECT_EQ(iptv::SaveOneStreamCredentials(path, invalid), XtreamStatus::invalid_argument);
}

TEST(IptvOneStreamTest, FetchesMediaInfoReusingAndRenewingTheToken)
{
    std::string vod_url;
    ASSERT_TRUE(iptv::BuildOneStreamVodInfoUrl(Credentials(), Session(), "7", &vod_url));
    EXPECT_EQ(vod_url, "https://panel.example:8080/play/b2c/v1/content/vod/7?token=tok%2Ben%2F1");
    constexpr char info[] = R"({"info":{"plot":"Film plot","duration_secs":5400},"vod":{}})";
    iptv::MediaDetails details;
    {
        // No kept token: sign in first, then fetch.
        FakePanel panel;
        panel.AddLogin();
        panel.AddGet(UserInfoUrl(), kActiveUser);
        panel.AddGet(vod_url, info);
        iptv::OneStreamSession session;
        ASSERT_EQ(iptv::FetchOneStreamMediaInfo(Credentials(), &session,
                                                iptv::OneStreamContent::vod, "7", panel.Transport(),
                                                &details),
                  XtreamStatus::ok);
        EXPECT_EQ(details.plot, "Film plot");
        EXPECT_EQ(details.duration_secs, 5400u);
        EXPECT_EQ(session.token, "tok+en/1");
        EXPECT_EQ(panel.requested.size(), 3u);
    }
    {
        // A kept token is used directly.
        FakePanel panel;
        panel.AddGet(vod_url, info);
        iptv::OneStreamSession session = Session();
        ASSERT_EQ(iptv::FetchOneStreamMediaInfo(Credentials(), &session,
                                                iptv::OneStreamContent::vod, "7", panel.Transport(),
                                                &details),
                  XtreamStatus::ok);
        EXPECT_EQ(panel.requested.size(), 1u);
    }
    {
        // A refused token is renewed once.
        FakePanel panel;
        std::string stale_url;
        ASSERT_TRUE(iptv::BuildOneStreamVodInfoUrl(Credentials(), {"old"}, "7", &stale_url));
        panel.AddGet(stale_url, "", XtreamFetchOutcome::failed);
        panel.AddLogin();
        panel.AddGet(UserInfoUrl(), kActiveUser);
        panel.AddGet(vod_url, info);
        iptv::OneStreamSession session{"old"};
        ASSERT_EQ(iptv::FetchOneStreamMediaInfo(Credentials(), &session,
                                                iptv::OneStreamContent::vod, "7", panel.Transport(),
                                                &details),
                  XtreamStatus::ok);
        EXPECT_EQ(session.token, "tok+en/1");
        EXPECT_EQ(panel.requested.size(), 4u);
    }
    {
        // A series uses content/series/{id}; a failure after a fresh sign-in is final.
        FakePanel panel;
        panel.AddLogin();
        panel.AddGet(UserInfoUrl(), kActiveUser);
        iptv::OneStreamSession session;
        EXPECT_EQ(iptv::FetchOneStreamMediaInfo(Credentials(), &session,
                                                iptv::OneStreamContent::series, "s1",
                                                panel.Transport(), &details),
                  XtreamStatus::fetch_failed);
        EXPECT_EQ(panel.requested.size(), 3u);
        EXPECT_EQ(iptv::FetchOneStreamMediaInfo(Credentials(), &session,
                                                iptv::OneStreamContent::live, "s1",
                                                panel.Transport(), &details),
                  XtreamStatus::invalid_argument);
    }
}

TEST(IptvOneStreamTest, RemembersOneStreamAsTheActiveSource)
{
    const std::string path = std::string(::testing::TempDir()) + "prosperotv-onestream-source";
    ASSERT_EQ(iptv::SaveActiveSource(path, iptv::SourceKind::OneStream),
              iptv::SourceStateStatus::ok);
    iptv::SourceKind source = iptv::SourceKind::BuiltIn;
    ASSERT_EQ(iptv::LoadActiveSource(path, &source), iptv::SourceStateStatus::ok);
    EXPECT_EQ(source, iptv::SourceKind::OneStream);
    std::remove(path.c_str());
}

TEST(IptvOneStreamTest, DescribesStatusesForTheUser)
{
    EXPECT_STREQ(iptv::OneStreamStatusDescription(XtreamStatus::authentication_failed),
                 "OneStream username or password was rejected");
    EXPECT_STREQ(iptv::OneStreamStatusDescription(XtreamStatus::account_inactive),
                 "OneStream account is expired or inactive");
}

TEST(IptvOneStreamTest, BuildsTheGuideAddress)
{
    std::string url;
    ASSERT_TRUE(iptv::BuildOneStreamGuideUrl(Credentials(), Session(), &url));
    EXPECT_EQ(url, "https://panel.example:8080/play/b2c/v1/xml-epg?token=tok%2Ben%2F1");
    EXPECT_FALSE(iptv::BuildOneStreamGuideUrl(Credentials(), {""}, &url));
}

} // namespace
