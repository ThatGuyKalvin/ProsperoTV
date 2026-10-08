/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_http.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace
{

std::string Describe(iptv::http::Status status, int http_status = 0, int native_error = 0,
                     const char *response = nullptr)
{
    char text[192]{};
    iptv::http::DescribeFailure(status, http_status, native_error, response, text, sizeof(text));
    return text;
}

TEST(IptvHttpTest, DetectsGeoIpBlockCaseInsensitively)
{
    constexpr char response[] = "This channel is GEOIP BLOCKED in your region";
    EXPECT_TRUE(iptv::http::ResponseIndicatesGeographicBlock(response, std::strlen(response)));
    EXPECT_EQ(Describe(iptv::http::Status::http_status_error, 403, 0, response),
              "unavailable in your region (GeoIP blocked; HTTP 403)");
}

TEST(IptvHttpTest, DoesNotGuessThatEveryForbiddenResponseIsGeographic)
{
    EXPECT_EQ(Describe(iptv::http::Status::http_status_error, 403, 0, "Forbidden"),
              "access denied by the channel provider (HTTP 403)");
}

TEST(IptvHttpTest, ExplainsStandardHttpFailures)
{
    EXPECT_EQ(Describe(iptv::http::Status::http_status_error, 404),
              "stream is offline or no longer exists (HTTP 404)");
    EXPECT_EQ(Describe(iptv::http::Status::http_status_error, 451),
              "unavailable for legal or regional restrictions (HTTP 451)");
    EXPECT_EQ(Describe(iptv::http::Status::http_status_error, 503),
              "the channel provider is unavailable (HTTP 503)");
}

TEST(IptvHttpTest, PreservesNativeConnectionErrorForLogsAndUi)
{
    EXPECT_EQ(Describe(iptv::http::Status::request_failed, 0, -42),
              "connection failed (native error 0xFFFFFFD6)");
}

TEST(IptvHttpTest, PostFormValidatesItsArgumentsBeforeTheNetwork)
{
    using iptv::http::Status;
    std::string buffer(65, '\0');
    const auto post = [&](const char *url, const char *body, std::size_t bytes,
                          std::size_t max_bytes = 64u) {
        return iptv::http::PostForm(url, body, bytes, buffer.data(), buffer.size(), max_bytes)
            .status;
    };
    constexpr char form[] = "username=a&password=b";
    const std::size_t length = sizeof(form) - 1u;

    // The host build has no network, so a valid request stops at platform_unavailable.
    EXPECT_EQ(post("https://panel.example/play/b2c/v1/auth", form, length),
              Status::platform_unavailable);
    EXPECT_EQ(post("ftp://panel.example/auth", form, length), Status::unsupported_url);
    EXPECT_EQ(post("https://panel.example/auth", nullptr, 0), Status::invalid_argument);
    EXPECT_EQ(post("https://panel.example/auth", form, 0), Status::invalid_argument);
    EXPECT_EQ(post("https://panel.example/auth", "user name=a", 11u), Status::invalid_argument);
    EXPECT_EQ(post("https://panel.example/auth", "a=b\nc", 5u), Status::invalid_argument);
    const std::string oversized(iptv::http::kMaxFormBodyBytes + 1u, 'a');
    EXPECT_EQ(post("https://panel.example/auth", oversized.c_str(), oversized.size()),
              Status::invalid_argument);
    EXPECT_EQ(post("https://panel.example/auth", form, length, 65u), Status::invalid_argument);
}

TEST(IptvHttpTest, ParsesContentRangeAndLength)
{
    std::int64_t first = 0;
    std::int64_t last = 0;
    std::int64_t total = 0;
    const auto range = [&](const char *text)
    { return iptv::http::ParseContentRange(text, std::strlen(text), &first, &last, &total); };
    ASSERT_TRUE(range("bytes 0-4095/1048576"));
    EXPECT_EQ(first, 0);
    EXPECT_EQ(last, 4095);
    EXPECT_EQ(total, 1048576);
    ASSERT_TRUE(range("  Bytes 5000000000-5999999999/7000000000 "));
    EXPECT_EQ(first, INT64_C(5000000000));
    EXPECT_EQ(total, INT64_C(7000000000));
    ASSERT_TRUE(range("bytes 10-20/*"));
    EXPECT_EQ(total, -1);
    for (const char *bad :
         {"", "bytes", "bytes */100", "bytes 5-4/100", "bytes 0-100/100", "items 0-1/2",
          "bytes 0-1/2 x", "bytes 0-1", "bytes 0-1/9999999999999999999"})
        EXPECT_FALSE(range(bad)) << bad;

    std::int64_t length = 0;
    ASSERT_TRUE(iptv::http::ParseContentLength(" 123456789012 ", 14, &length));
    EXPECT_EQ(length, INT64_C(123456789012));
    EXPECT_FALSE(iptv::http::ParseContentLength("12a", 3, &length));
    EXPECT_FALSE(iptv::http::ParseContentLength("", 0, &length));
    EXPECT_FALSE(iptv::http::ParseContentLength("-5", 2, &length));
}

} // namespace
