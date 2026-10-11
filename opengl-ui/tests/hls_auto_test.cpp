#include "iptv_hls_auto.hpp"
#include <gtest/gtest.h>

TEST(HlsAuto, FragmentedMediaHeadersAreRecognizedButInitAndPlaylistsAreNot)
{
    const std::uint8_t media[] = {0, 0, 0, 24, 'm', 'o', 'o', 'f', 1, 2};
    for (unsigned split = 1; split < 8; ++split)
    {
        iptv::HlsMediaPrefix prefix;
        EXPECT_EQ(prefix.observe(media, split), 0);
        EXPECT_EQ(prefix.observe(media + split, sizeof(media) - split), 1);
        EXPECT_EQ(prefix.observe(media, 1), 1);
    }
    const std::uint8_t init[] = {0, 0, 0, 24, 'f', 't', 'y', 'p'};
    iptv::HlsMediaPrefix prefix;
    EXPECT_EQ(prefix.observe(init, sizeof(init)), -1);
    iptv::HlsMediaPrefix list;
    EXPECT_EQ(list.observe(reinterpret_cast<const std::uint8_t *>("#EXTM3U\n"), 8), -1);
}

TEST(HlsAuto, LowerSelectionSurvivesMasterReorderAndSkipsFailedRates)
{
    iptv::HlsAutoReduction policy;
    iptv_hls_quality_t variants[4]{};
    variants[0].bandwidth = 300000;
    variants[1].bandwidth = 1200000;
    variants[2].bandwidth = 600000;
    EXPECT_EQ(policy.lower(variants, 4, 1), 2u);
    policy.failed(600000);
    EXPECT_EQ(policy.lower(variants, 4, 1), 0u);
    std::swap(variants[0], variants[1]);
    EXPECT_EQ(policy.lower(variants, 4, 0), 1u);
    EXPECT_EQ(policy.lower(variants, 4, 1), IPTV_HLS_NO_VARIANT);
    EXPECT_EQ(policy.lower(variants, 4, 3), IPTV_HLS_NO_VARIANT);
}

TEST(HlsAuto, RequiresSustainedNetworkDeficitAndStarvation)
{
    iptv::HlsAutoReduction policy;
    EXPECT_FALSE(policy.sample(1, 100000, 2000000, 2000000, 1, 0, false));
    EXPECT_FALSE(policy.sample(5000001, 100000, 2000000, 2000000, 100, 0, false));
    EXPECT_FALSE(policy.sample(7000001, 100000, 2000000, 2000000, 120, 0, false));
    // Both windows are slow, but playback has not starved.
    EXPECT_FALSE(policy.sample(9000001, 100000, 2000000, 2000000, 140, 0, false));
    EXPECT_TRUE(policy.sample(11000001, 100000, 2000000, 2000000, 140, 1, false));
}

TEST(HlsAuto, FastDownloadsNeverTreatDecoderStallsAsBandwidthFailure)
{
    iptv::HlsAutoReduction policy;
    for (std::uint64_t t = 1; t < 40000000; t += 2000000)
        EXPECT_FALSE(policy.sample(t, 2000000, 2000000, 2000000, 1, t, false));
}

TEST(HlsAuto, SeverelySlowMediaStillReducesButTinyResponsesDoNot)
{
    iptv::HlsAutoReduction policy;
    EXPECT_FALSE(policy.sample(1, 16384, 2000000, 1000000, 1, 0, false));
    EXPECT_FALSE(policy.sample(5000001, 16384, 2000000, 1000000, 2, 0, false));
    EXPECT_FALSE(policy.sample(7000001, 16384, 2000000, 1000000, 2, 1, false));
    EXPECT_TRUE(policy.sample(9000001, 16384, 2000000, 1000000, 2, 2, false));
    policy.reset();
    for (std::uint64_t t = 1; t < 30000000; t += 2000000)
        EXPECT_FALSE(policy.sample(t, 128, 2000000, 1000000, 1, t, false));
}

TEST(HlsAuto, PauseAndMissingMetadataDiscardEvidence)
{
    iptv::HlsAutoReduction policy;
    policy.sample(1, 100000, 2000000, 2000000, 1, 0, false);
    policy.sample(5000001, 100000, 2000000, 2000000, 1, 0, false);
    policy.sample(7000001, 100000, 2000000, 2000000, 1, 1, false);
    EXPECT_FALSE(policy.sample(9000001, 100000, 2000000, 2000000, 1, 2, true));
    EXPECT_FALSE(policy.sample(11000001, 100000, 2000000, 2000000, 1, 2, false));
    EXPECT_FALSE(policy.sample(17000001, 100000, 2000000, 0, 1, 2, false));
    EXPECT_FALSE(policy.sample(19000001, 100000, 2000000, 2000000, 1, 2, false));
}

TEST(HlsAuto, CooldownAndFailedCeilingSurviveSampleReset)
{
    iptv::HlsAutoReduction policy;
    policy.reduced(10000000, 1000000);
    policy.reset_samples();
    EXPECT_EQ(policy.ceiling, 1000000u);
    for (std::uint64_t t = 10000001; t < 30000000; t += 2000000)
        EXPECT_FALSE(policy.sample(t, 100000, 2000000, 1000000, 1, t, false));
    policy.failed(500000);
    EXPECT_EQ(policy.ceiling, 499999u);
    policy.failed(2000000);
    EXPECT_EQ(policy.ceiling, 499999u);
    policy.reset();
    EXPECT_EQ(policy.ceiling, UINT64_MAX);
}
