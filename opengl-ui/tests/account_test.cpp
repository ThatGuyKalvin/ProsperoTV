// ProsperoTV - Account metadata and offline expiry notices.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/library.hpp"
#include "iptv_xtream.h"
#include "iptv_http.h"
#include <gtest/gtest.h>

TEST(AccountInfo, ParsesOptionalProviderValuesWithoutRejectingValidAuthentication)
{
    iptv::XtreamAuth auth;
    ASSERT_EQ(iptv::ParseXtreamAuth(
                  R"({"user_info":{"auth":1,"exp_date":"1900000000","max_connections":3}})", &auth),
              iptv::XtreamStatus::ok);
    EXPECT_EQ(auth.expires_unix, 1900000000);
    EXPECT_EQ(auth.max_connections, 3);
    for (const auto *value : {"null", "false", "-1", "1.5", "1e3", "{}", "[]",
                              "\"999999999999999999999999999999999999999999\""})
    {
        const auto json = std::string(R"({"user_info":{"auth":1,"exp_date":)") + value +
                          ",\"max_connections\":" + value + "}}";
        ASSERT_EQ(iptv::ParseXtreamAuth(json, &auth), iptv::XtreamStatus::ok) << json;
        EXPECT_EQ(auth.expires_unix, -1);
        EXPECT_EQ(auth.max_connections, -1);
    }
    ASSERT_EQ(iptv::ParseXtreamAuth(
                  R"({"user_info":{"auth":1,"exp_date":"0","max_connections":"0"}})", &auth),
              iptv::XtreamStatus::ok);
    EXPECT_EQ(auth.expires_unix, 0);
    EXPECT_EQ(auth.max_connections, 0);
    EXPECT_EQ(iptv::ParseXtreamAuth(R"({"user_info":{"auth":0,"exp_date":1900000000}})", &auth),
              iptv::XtreamStatus::authentication_failed);
}

TEST(AccountInfo, PersistsBySourceAndInvalidatesWhenCredentialsChange)
{
    ptv::Library library;
    ASSERT_TRUE(library.open(":memory:"));
    ptv::SavedSource a{10, 2, "One", "https://provider.invalid", "one", "secret", {}};
    ptv::SavedSource b{11, 2, "Two", "https://provider.invalid", "two", "secret", {}};
    ASSERT_TRUE(library.save_source(&a));
    ASSERT_TRUE(library.save_source(&b));
    ASSERT_TRUE(library.save_account_info(a.id, {1900000000, 2, 1800000000}));
    auto sources = library.sources();
    ASSERT_EQ(sources.size(), 2u);
    EXPECT_EQ(sources[0].account.expires_unix, 1900000000);
    EXPECT_EQ(sources[0].account.max_connections, 2);
    EXPECT_EQ(sources[1].account.checked_unix, 0);
    a.schedule = ptv::RefreshSchedule::manual;
    a.name = "Renamed";
    ASSERT_TRUE(library.save_source(&a));
    EXPECT_EQ(library.sources()[0].account.checked_unix, 1800000000);
    a.password = "replacement";
    ASSERT_TRUE(library.save_source(&a));
    EXPECT_EQ(library.sources()[0].account.checked_unix, 0);
    EXPECT_FALSE(library.save_account_info(999, {1900000000, 2, 1800000000}));
    EXPECT_FALSE(library.save_account_info(a.id, {1900000000, 1000001, 1800000000}));
    ASSERT_TRUE(library.save_account_info(a.id, {1900000000, 2, 1800000000}));
    ASSERT_TRUE(library.remove_source(a.id));
    ASSERT_TRUE(library.save_source(&a));
    EXPECT_EQ(library.sources()[0].account.checked_unix, 0);
}

TEST(AccountInfo, WarnsWithinSevenDaysAndHandlesUnknownExpiryAndClock)
{
    constexpr std::uint64_t now = 1800000000;
    EXPECT_EQ(ptv::account_expiry_notice({static_cast<std::int64_t>(now + 7 * 86400), 2, 1}, now),
              7);
    EXPECT_EQ(
        ptv::account_expiry_notice({static_cast<std::int64_t>(now + 7 * 86400 + 1), 2, 1}, now), 0);
    EXPECT_EQ(ptv::account_expiry_notice({static_cast<std::int64_t>(now + 1), 2, 1}, now), 1);
    EXPECT_EQ(ptv::account_expiry_notice({static_cast<std::int64_t>(now), 2, 1}, now), -1);
    EXPECT_EQ(ptv::account_expiry_notice({static_cast<std::int64_t>(now - 1), 2, 1}, now), -1);
    EXPECT_EQ(ptv::account_expiry_notice({-1, 2, 1}, now), 0);
    EXPECT_EQ(ptv::account_expiry_notice({0, 2, 1}, now), 0);
    EXPECT_EQ(ptv::account_expiry_notice({1900000000, 2, 1}, 0), 0);
}

TEST(SourceHttp, StoresPerSourceDefaultsAndRejectsUnsafeHeaderValues)
{
    ptv::Library library;
    ASSERT_TRUE(library.open(":memory:"));
    ptv::SavedSource source{10, 1, "Playlist", "https://provider.invalid/list.m3u", {}, {}, {}};
    source.user_agent = "Example player/1.0";
    source.referrer = "https://provider.invalid/watch";
    ASSERT_TRUE(library.save_source(&source));
    ASSERT_EQ(library.sources().size(), 1u);
    EXPECT_EQ(library.sources()[0].user_agent, source.user_agent);
    EXPECT_EQ(library.sources()[0].referrer, source.referrer);
    source.user_agent = "bad\r\nAuthorization: leaked";
    EXPECT_FALSE(library.save_source(&source));
    EXPECT_EQ(library.sources()[0].user_agent, "Example player/1.0");
    source.user_agent.assign(513, 'x');
    EXPECT_FALSE(library.save_source(&source));
    source.user_agent.clear();
    source.referrer = "https://user:password@provider.invalid/";
    EXPECT_FALSE(library.save_source(&source));
    source.referrer.clear();
    ASSERT_TRUE(library.save_source(&source));
    EXPECT_TRUE(library.sources()[0].user_agent.empty());
    EXPECT_TRUE(library.sources()[0].referrer.empty());
}

TEST(SourceHttp, NamedHeadersAreBoundedValidatedAndOriginScoped)
{
    using namespace iptv::http;
    std::vector<NamedHeader> fields;
    ASSERT_TRUE(
        ParseExtraHeaders("X-Key: secret\nAuthorization: Bearer custom\nCookie: custom", &fields));
    ASSERT_EQ(fields.size(), 3u);
    EXPECT_EQ(fields[0].value, "secret");
    for (const auto *bad :
         {"Host: other", "Range: bytes=1-", "Content-Length: 1", "Connection: X-Key",
          "Transfer-Encoding: chunked", "X-Key: a\r\nX-Next: b", "X-Key: a\n bad", "X-Key: a\nBad",
          "x-key: a\nX-Key: b", "X Key: value", "X: "})
        EXPECT_FALSE(ParseExtraHeaders(bad)) << bad;
    EXPECT_FALSE(ParseExtraHeaders(std::string("X: a\0b", 6)));
    EXPECT_FALSE(ParseExtraHeaders("X: " + std::string(1025, 'x')));
    EXPECT_FALSE(ParseExtraHeaders(std::string(4097, 'x')));
    std::string many;
    for (unsigned i = 0; i < 17; ++i)
        many += "X-" + std::to_string(i) + ": value\n";
    EXPECT_FALSE(ParseExtraHeaders(many));
    RequestHeaders headers;
    headers.extra_headers = "X-Key: secret\nAuthorization: Bearer custom\nCookie: custom";
    headers.extra_headers = "X-Key: secret\nAuthorization: Bearer custom\nCookie: "
                            "custom\nUser-Agent: other\nReferer: https://other.test/";
    headers.user_agent = "BuiltinAgent";
    headers.referrer = "https://origin.test/page";
    headers.authorization = "Bearer builtin";
    headers.cookie = "builtin";
    headers.credential_origin = "https://origin.test:443/account";
    ASSERT_TRUE(ExtraHeadersForRequest(
        HeadersForUrl("https://origin.test/list", "https://origin.test/segment", headers),
        &fields));
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_EQ(fields[0].name, "X-Key");
    for (const char *url : {"https://cdn.test/segment", "http://origin.test/segment",
                            "https://origin.test:8443/segment"})
    {
        const auto scoped = HeadersForUrl("https://origin.test/list", url, headers);
        EXPECT_EQ(scoped.extra_headers, nullptr);
        EXPECT_EQ(scoped.authorization, nullptr);
        EXPECT_EQ(scoped.cookie, nullptr);
        ASSERT_TRUE(ExtraHeadersForRequest(scoped, &fields));
        EXPECT_TRUE(fields.empty());
    }
}

TEST(SourceHttp, CustomHeadersPersistAtomicallyAndRejectInjection)
{
    ptv::Library library;
    ASSERT_TRUE(library.open(":memory:"));
    ptv::SavedSource source{10, 1, "Playlist", "https://provider.invalid/list.m3u", {}, {}, {}};
    source.extra_headers = "Authorization: Bearer secret\nX-Device: television";
    ASSERT_TRUE(library.save_source(&source));
    ASSERT_EQ(library.sources().size(), 1u);
    EXPECT_EQ(library.sources()[0].extra_headers, source.extra_headers);
    source.extra_headers = "X-Key: secret\r\nInjected: value";
    EXPECT_FALSE(library.save_source(&source));
    EXPECT_EQ(library.sources()[0].extra_headers,
              "Authorization: Bearer secret\nX-Device: television");
    source.extra_headers.clear();
    ASSERT_TRUE(library.save_source(&source));
    EXPECT_TRUE(library.sources()[0].extra_headers.empty());
}
