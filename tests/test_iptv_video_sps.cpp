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

// Writes an H.264 SPS bit by bit: Baseline, 720x576, with only a pixel shape in its VUI.
std::vector<std::uint8_t> PalSps(unsigned aspect_idc, unsigned sar_num, unsigned sar_den)
{
    std::vector<bool> bits;
    const auto put = [&](unsigned value, unsigned count)
    {
        for (unsigned bit = count; bit-- > 0;)
            bits.push_back((value >> bit) & 1u);
    };
    const auto put_ue = [&](unsigned value)
    {
        unsigned length = 0;
        while ((value + 1u) >> length)
            ++length;
        put(0, length - 1u);
        put(value + 1u, length);
    };
    put(66, 8); // profile
    put(0, 8);
    put(30, 8); // level 3.0
    put_ue(0);  // seq_parameter_set_id
    put_ue(0);  // log2_max_frame_num_minus4
    put_ue(2);  // pic_order_cnt_type
    put_ue(1);  // max_num_ref_frames
    put(0, 1);  // gaps
    put_ue(44); // 45 macroblocks wide
    put_ue(35); // 36 high
    put(1, 1);  // frame_mbs_only
    put(1, 1);  // direct_8x8_inference
    put(0, 1);  // no crop
    put(1, 1);  // VUI present
    put(1, 1);  // aspect_ratio_info_present
    put(aspect_idc, 8);
    if (aspect_idc == 255u)
    {
        put(sar_num, 16);
        put(sar_den, 16);
    }
    put(0, 1); // overscan_info_present
    put(1, 1); // rbsp stop bit
    while (bits.size() % 8u)
        bits.push_back(false);
    std::vector<std::uint8_t> nal{0x67};
    for (std::size_t at = 0; at < bits.size(); at += 8u)
    {
        std::uint8_t byte = 0;
        for (std::size_t bit = 0; bit < 8u; ++bit)
            byte = static_cast<std::uint8_t>((byte << 1) | (bits[at + bit] ? 1u : 0u));
        nal.push_back(byte);
    }
    return nal;
}

TEST(IptvVideoSpsTest, ReadsThePixelShapeFromTheVui)
{
    iptv::video::SpsInfo sps;
    const char *error = nullptr;
    auto nal = PalSps(4u, 0u, 0u); // 16:11
    ASSERT_EQ(iptv::video::ParseH264Sps(nal.data(), nal.size(), &sps, &error), IPTV_STREAM_OK)
        << error;
    EXPECT_EQ(sps.visible_width, 720u);
    EXPECT_EQ(sps.visible_height, 576u);
    EXPECT_EQ(sps.sar_num, 16u);
    EXPECT_EQ(sps.sar_den, 11u);

    sps = {};
    nal = PalSps(255u, 64u, 45u);
    ASSERT_EQ(iptv::video::ParseH264Sps(nal.data(), nal.size(), &sps, &error), IPTV_STREAM_OK);
    EXPECT_EQ(sps.sar_num, 64u);
    EXPECT_EQ(sps.sar_den, 45u);

    // Square pixels are reported as no shape at all.
    sps = {};
    nal = PalSps(1u, 0u, 0u);
    ASSERT_EQ(iptv::video::ParseH264Sps(nal.data(), nal.size(), &sps, &error), IPTV_STREAM_OK);
    EXPECT_EQ(sps.sar_num, 0u);
    EXPECT_EQ(sps.sar_den, 0u);
}

} // namespace
