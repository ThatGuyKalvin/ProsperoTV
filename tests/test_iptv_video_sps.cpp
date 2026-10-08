/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_video_sps.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <iterator>
#include <vector>

namespace
{

// A 1080p H.264 High profile SPS (level 4.2) and PPS, as an Annex-B access unit with an
// access unit delimiter in front.
const std::uint8_t kAccessUnit[] = {
    0x00, 0x00, 0x00, 0x01, 0x09, 0xf0, 0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x2a,
    0xac, 0xb4, 0x03, 0xc0, 0x11, 0x3f, 0x2e, 0x02, 0xd4, 0x08, 0x08, 0x05, 0x00, 0x00,
    0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x78, 0x8f, 0x18, 0x32, 0xa0, 0x00, 0x00,
    0x01, 0x68, 0xef, 0x03, 0xb2, 0xc8, 0xb0, 0x00, 0x00, 0x01, 0x65, 0x88, 0x80,
};

TEST(IptvVideoSpsTest, FindsAndParsesAnH264Sps)
{
    const std::uint8_t *nal = nullptr;
    std::size_t bytes = 0;
    ASSERT_TRUE(iptv::video::FindSps(kAccessUnit, sizeof(kAccessUnit), IPTV_STREAM_VIDEO_H264, &nal,
                                     &bytes));
    EXPECT_EQ(nal, kAccessUnit + 10);
    EXPECT_EQ(bytes, 30u);

    iptv::video::SpsInfo sps;
    const char *error = nullptr;
    ASSERT_EQ(iptv::video::ParseH264Sps(nal, bytes, &sps, &error), IPTV_STREAM_OK) << error;
    EXPECT_EQ(sps.profile, 100u);
    EXPECT_EQ(sps.level, 42u);
    EXPECT_EQ(sps.visible_width, 1920u);
    EXPECT_EQ(sps.visible_height, 1080u);
    EXPECT_EQ(sps.coded_height, 1088u);
    EXPECT_EQ(sps.bit_depth, 8u);
    EXPECT_EQ(sps.chroma, static_cast<std::uint32_t>(IPTV_STREAM_CHROMA_420));

    // An access unit without an SPS, and an HEVC search of an H.264 unit, find nothing.
    EXPECT_FALSE(iptv::video::FindSps(kAccessUnit + 49, sizeof(kAccessUnit) - 49,
                                      IPTV_STREAM_VIDEO_H264, &nal, &bytes));
    EXPECT_FALSE(iptv::video::FindSps(kAccessUnit, sizeof(kAccessUnit), IPTV_STREAM_VIDEO_HEVC,
                                      &nal, &bytes));
}

TEST(IptvVideoSpsTest, ReportsWhyAnSpsIsRefused)
{
    iptv::video::SpsInfo sps;
    const char *error = nullptr;
    const std::uint8_t not_sps[] = {0x68, 0xef, 0x03, 0xb2};
    EXPECT_EQ(iptv::video::ParseH264Sps(not_sps, sizeof(not_sps), &sps, &error),
              IPTV_STREAM_MALFORMED_TS);
    EXPECT_STREQ(error, "invalid H.264 SPS");
    // High 10 (profile 110) with 10-bit luma is not decodable by the console.
    const std::uint8_t high10[] = {0x67, 0x6e, 0x00, 0x1f, 0xa6, 0xc0, 0x80, 0x00};
    EXPECT_EQ(iptv::video::ParseH264Sps(high10, sizeof(high10), &sps, &error),
              IPTV_STREAM_UNSUPPORTED_FORMAT);
    EXPECT_STREQ(error, "H.264 must be 8-bit 4:2:0");
    const std::uint8_t short_hevc[] = {0x42, 0x01, 0x01};
    EXPECT_EQ(iptv::video::ParseHevcSps(short_hevc, sizeof(short_hevc), &sps, &error),
              IPTV_STREAM_MALFORMED_TS);
}

TEST(IptvVideoSpsTest, FindsThePictureTypeOfAnAccessUnit)
{
    EXPECT_EQ(iptv::video::PictureType(kAccessUnit, sizeof(kAccessUnit), IPTV_STREAM_VIDEO_H264),
              5);
    // HEVC: parameter sets (32-34) are skipped; a CRA (21) and a RASL picture (8).
    const std::uint8_t cra[] = {0, 0, 0, 1, 0x40, 0x01, 0, 0, 1, 0x42, 0x01, 0, 0, 1, 0x2a, 0x01};
    EXPECT_EQ(iptv::video::PictureType(cra, sizeof(cra), IPTV_STREAM_VIDEO_HEVC), 21);
    const std::uint8_t rasl[] = {0, 0, 1, 0x10, 0x01, 0xaf};
    EXPECT_EQ(iptv::video::PictureType(rasl, sizeof(rasl), IPTV_STREAM_VIDEO_HEVC), 8);
    const std::uint8_t no_slice[] = {0, 0, 1, 0x09, 0xf0};
    EXPECT_EQ(iptv::video::PictureType(no_slice, sizeof(no_slice), IPTV_STREAM_VIDEO_H264), -1);
}

} // namespace
