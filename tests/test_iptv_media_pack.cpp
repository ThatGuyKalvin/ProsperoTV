/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_media_pack.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace
{

using iptv::media::AacConfig;
using iptv::media::Container;

std::vector<std::uint8_t> EbmlHeader(const char *doctype)
{
    std::vector<std::uint8_t> bytes = {0x1a, 0x45, 0xdf, 0xa3, 0x00, 0x42,
                                       0x86, 0x81, 0x01, 0x42, 0x82};
    const std::size_t length = std::strlen(doctype);
    bytes.push_back(static_cast<std::uint8_t>(0x80u | length));
    bytes.insert(bytes.end(), doctype, doctype + length);
    bytes[4] = static_cast<std::uint8_t>(0x80u | (bytes.size() - 5u));
    bytes.insert(bytes.end(), {0x18, 0x53, 0x80, 0x67, 0x01, 0x00, 0x00, 0x00});
    return bytes;
}

TEST(IptvMediaPackTest, RecognisesContainersFromBytesAndUrls)
{
    const auto sniff = [](const std::vector<std::uint8_t> &bytes)
    { return iptv::media::SniffContainer(bytes.data(), bytes.size()); };
    EXPECT_EQ(sniff(EbmlHeader("webm")), Container::webm);
    EXPECT_EQ(sniff(EbmlHeader("matroska")), Container::matroska);
    // A header cut off before DocType is still Matroska.
    std::vector<std::uint8_t> cut = EbmlHeader("webm");
    cut.resize(9);
    EXPECT_EQ(sniff(cut), Container::matroska);

    EXPECT_EQ(sniff({0, 0, 0, 0x20, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm'}), Container::mp4);
    EXPECT_EQ(sniff({0, 0, 0x10, 0, 'm', 'o', 'o', 'v'}), Container::mp4);
    EXPECT_EQ(sniff({0, 0, 0, 0x18, 's', 't', 'y', 'p'}), Container::none);
    EXPECT_EQ(sniff({0x47, 0x40, 0x00, 0x10, 0, 0, 0xb0, 0x0d}), Container::none);
    EXPECT_EQ(sniff({0, 0, 0, 4, 'f', 't', 'y', 'p'}), Container::none);

    EXPECT_EQ(iptv::media::ContainerFromUrl("http://p/series/u/p/77.MKV"), Container::matroska);
    EXPECT_EQ(iptv::media::ContainerFromUrl("http://cdn/a.mp4?sig=1.ts"), Container::mp4);
    EXPECT_EQ(iptv::media::ContainerFromUrl("http://cdn/v.webm#t=4"), Container::webm);
    EXPECT_EQ(iptv::media::ContainerFromUrl("http://p/live/u/p/1.ts"), Container::none);
    EXPECT_EQ(iptv::media::ContainerFromUrl("http://cdn.mkv/stream"), Container::none);
}

TEST(IptvMediaPackTest, BuildsAdtsHeadersFromAudioSpecificConfig)
{
    AacConfig config;
    const std::uint8_t lc_48k_stereo[] = {0x11, 0x90};
    ASSERT_TRUE(iptv::media::ParseAudioSpecificConfig(lc_48k_stereo, 2, &config));
    EXPECT_EQ(config.object_type, 2u);
    EXPECT_EQ(config.frequency_index, 3u);
    EXPECT_EQ(config.channel_config, 2u);

    std::uint8_t header[iptv::media::kAdtsHeaderBytes] = {};
    ASSERT_TRUE(iptv::media::WriteAdtsHeader(config, 371, header));
    const std::uint8_t expected[] = {0xff, 0xf1, 0x4c, 0x80, 0x2f, 0x5f, 0xfc};
    EXPECT_EQ(std::vector<std::uint8_t>(header, header + 7),
              std::vector<std::uint8_t>(expected, expected + 7));
    // The 13-bit frame length covers header and payload.
    const unsigned length = ((header[3] & 3u) << 11) | (header[4] << 3) | (header[5] >> 5);
    EXPECT_EQ(length, 378u);
    EXPECT_TRUE(iptv::media::WriteAdtsHeader(config, 8191 - 7, header));
    EXPECT_FALSE(iptv::media::WriteAdtsHeader(config, 8191 - 6, header));

    // HE-AAC with explicit SBR: sent as its LC core at 22.05 kHz.
    const std::uint8_t he_aac[] = {0x2b, 0x92, 0x08, 0x00};
    ASSERT_TRUE(iptv::media::ParseAudioSpecificConfig(he_aac, 4, &config));
    EXPECT_EQ(config.object_type, 2u);
    EXPECT_EQ(config.frequency_index, 7u);
    EXPECT_EQ(config.channel_config, 2u);

    // An explicit 44.1 kHz rate maps to its index; 5.1 is channel configuration 6.
    const std::uint8_t explicit_rate[] = {0x17, 0x80, 0x56, 0x22, 0x30};
    ASSERT_TRUE(iptv::media::ParseAudioSpecificConfig(explicit_rate, 5, &config));
    EXPECT_EQ(config.frequency_index, 4u);
    EXPECT_EQ(config.channel_config, 6u);

    // A PCE-defined layout (channel configuration 0) or an unknown rate cannot be described.
    const std::uint8_t pce[] = {0x11, 0x80};
    EXPECT_FALSE(iptv::media::ParseAudioSpecificConfig(pce, 2, &config));
    const std::uint8_t odd_rate[] = {0x17, 0x80, 0x00, 0x01, 0x10};
    EXPECT_FALSE(iptv::media::ParseAudioSpecificConfig(odd_rate, 5, &config));
    EXPECT_FALSE(iptv::media::ParseAudioSpecificConfig(lc_48k_stereo, 1, &config));
}

TEST(IptvMediaPackTest, NormalizesTimestampsAgainstOneBase)
{
    EXPECT_EQ(iptv::media::NormalizePtsUs(1'500'000, 500'000), 1'000'000u);
    EXPECT_EQ(iptv::media::NormalizePtsUs(-40'000, 0), 0u);
    EXPECT_EQ(iptv::media::NormalizePtsUs(100, 100), 0u);
}

} // namespace
