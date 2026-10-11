/* ProsperoTV - Actual pitch-preserving speed and source clock tests.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "iptv_audio_tempo.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
struct Sound
{
    std::vector<std::int16_t> samples;
    std::uint64_t first = UINT64_MAX, last = UINT64_MAX;
    unsigned channels = 2;
    static int write(void *context, const int16_t *samples, size_t frames, uint64_t pts)
    {
        auto &s = *static_cast<Sound *>(context);
        if (s.first == UINT64_MAX)
            s.first = pts;
        if (s.last != UINT64_MAX)
            EXPECT_GE(pts, s.last);
        s.last = pts;
        for (std::size_t i = 0; i < frames; ++i)
            s.samples.push_back(samples[i * s.channels]);
        return 0;
    }
};
} // namespace

TEST(AudioTempo, ChangesDurationWithoutChangingPitchAndKeepsSourceTime)
{
    for (const unsigned rate : {44100u, 48000u})
        for (const unsigned speed : {50u, 75u, 100u, 125u, 150u, 200u})
        {
            SCOPED_TRACE(std::to_string(rate) + " Hz at " + std::to_string(speed));
            auto *tempo = iptv_audio_tempo_create(rate, 2, speed);
            ASSERT_NE(tempo, nullptr);
            Sound sound;
            std::int16_t input[2048];
            for (unsigned at = 0; at < rate * 4;)
            {
                const auto count = std::min(1024u, rate * 4 - at);
                for (unsigned i = 0; i < count; ++i)
                    input[2 * i] = input[2 * i + 1] = static_cast<std::int16_t>(
                        6000 * std::sin(6.283185307179586 * 1000 * (at + i) / rate));
                ASSERT_EQ(iptv_audio_tempo_push(tempo, input, count,
                                                2000000 + UINT64_C(1000000) * at / rate,
                                                Sound::write, &sound),
                          0);
                at += count;
            }
            ASSERT_EQ(iptv_audio_tempo_finish(tempo, Sound::write, &sound), 0);
            EXPECT_NEAR(static_cast<double>(sound.samples.size()) / rate, 400.0 / speed, 0.07);
            ASSERT_GT(sound.samples.size(), rate / 5);
            const auto begin = rate / 10;
            unsigned crossings = 0;
            for (std::size_t i = begin + 1; i < sound.samples.size(); ++i)
                if (sound.samples[i - 1] <= 0 && sound.samples[i] > 0)
                    ++crossings;
            const double frequency =
                static_cast<double>(crossings) * rate / (sound.samples.size() - begin);
            EXPECT_NEAR(frequency, 1000, 20); // Resampling alone would change this with speed.
            EXPECT_NEAR(sound.first, 2000000, 1000);
            EXPECT_GT(sound.last, 5800000u);
            EXPECT_LT(sound.last, 6100000u);
            iptv_audio_tempo_free(&tempo);
            EXPECT_EQ(tempo, nullptr);
        }
}

TEST(AudioTempo, RejectsInvalidInputsAndPropagatesOutputFailure)
{
    EXPECT_EQ(iptv_audio_tempo_create(0, 2, 100), nullptr);
    EXPECT_EQ(iptv_audio_tempo_create(48000, 3, 100), nullptr);
    EXPECT_EQ(iptv_audio_tempo_create(48000, 2, 201), nullptr);
    auto *tempo = iptv_audio_tempo_create(48000, 1, 50);
    ASSERT_NE(tempo, nullptr);
    Sound sound;
    sound.channels = 1;
    std::vector<std::int16_t> samples(8192, 1000);
    EXPECT_LT(iptv_audio_tempo_push(tempo, samples.data(), 65537, 0, Sound::write, &sound), 0);
    auto reject = [](void *, const int16_t *, size_t, uint64_t) { return -77; };
    int result = iptv_audio_tempo_push(tempo, samples.data(), samples.size(), 0, reject, nullptr);
    if (result == 0)
        result = iptv_audio_tempo_finish(tempo, reject, nullptr);
    EXPECT_EQ(result, -77);
    iptv_audio_tempo_free(&tempo);
}

TEST(AudioTempo, SurroundSpeakersStayIsolatedAtEverySpeed)
{
    for (unsigned speed : {50u, 100u, 150u, 200u})
        for (unsigned speaker = 0; speaker < 6; ++speaker)
        {
            auto *tempo = iptv_audio_tempo_create(48000, 6, speed);
            ASSERT_NE(tempo, nullptr);
            struct Check
            {
                unsigned speaker;
                size_t frames = 0;
                long long energy = 0;
            } check{speaker};
            auto output = [](void *context, const int16_t *pcm, size_t frames, uint64_t)
            {
                auto &c = *static_cast<Check *>(context);
                c.frames += frames;
                for (size_t i = 0; i < frames; ++i)
                    for (unsigned channel = 0; channel < 6; ++channel)
                        if (channel == c.speaker)
                            c.energy +=
                                static_cast<long long>(pcm[i * 6 + channel]) * pcm[i * 6 + channel];
                        else
                            EXPECT_EQ(pcm[i * 6 + channel], 0);
                return 0;
            };
            int16_t pcm[1024 * 6]{};
            for (unsigned at = 0; at < 48000; at += 1024)
            {
                unsigned frames = std::min(1024u, 48000 - at);
                for (unsigned i = 0; i < frames; ++i)
                    pcm[i * 6 + speaker] = static_cast<int16_t>(
                        6000 * std::sin(6.283185307179586 * 997 * (at + i) / 48000));
                ASSERT_EQ(iptv_audio_tempo_push(tempo, pcm, frames, UINT64_MAX, output, &check), 0);
            }
            ASSERT_EQ(iptv_audio_tempo_finish(tempo, output, &check), 0);
            EXPECT_NEAR(static_cast<double>(check.frames) / 48000, 100.0 / speed, 0.07);
            EXPECT_GT(check.energy, 1000000);
            iptv_audio_tempo_free(&tempo);
        }
}
