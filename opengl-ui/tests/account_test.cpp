// ProsperoTV - Account metadata and offline expiry notices.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/library.hpp"
#include "iptv_xtream.h"
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
