/* ps5-native-app-boilerplate - Audio fallback and downmix regression check.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "iptv_audio_decode.h"
#include "iptv_audio_frame.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum
{
    CEILING = 29205
}; /* -1 dBFS in 16 bits, rounded up. */

static void check_limiter(void)
{
    enum
    {
        FRAMES = 48000
    };
    float *in = malloc(sizeof(float) * 2 * FRAMES);
    int16_t *out = malloc(sizeof(int16_t) * 2 * FRAMES);
    assert(in && out);
    /* A quiet tone passes untouched. */
    for (int i = 0; i < FRAMES; ++i)
        in[2 * i] = in[2 * i + 1] = 0.25f * sinf(2.0f * 3.14159265f * 997.0f * (float)i / 48000.0f);
    float gain = 1.0f;
    iptv_audio_limit_to_s16(&gain, 48000, in, FRAMES, out);
    assert(gain == 1.0f);
    for (int i = 0; i < FRAMES; ++i)
        assert(out[2 * i] == (int16_t)lrintf(in[2 * i] * 32767.0f) && out[2 * i] == out[2 * i + 1]);
    /* A tone over full scale on one side is held under the ceiling on both, in step. */
    for (int i = 0; i < FRAMES; ++i)
    {
        in[2 * i] = 2.4f * sinf(2.0f * 3.14159265f * 997.0f * (float)i / 48000.0f);
        in[2 * i + 1] = 0.5f * in[2 * i];
    }
    in[10] = NAN;
    iptv_audio_limit_to_s16(&gain, 48000, in, FRAMES, out);
    int peak = 0;
    for (int i = 0; i < FRAMES; ++i)
    {
        peak = abs(out[2 * i]) > peak ? abs(out[2 * i]) : peak;
        assert(abs(out[2 * i]) <= CEILING && abs(out[2 * i + 1]) <= CEILING / 2 + 1);
    }
    assert(peak > CEILING - 300 && gain < 0.5f && gain > 0.3f);
    /* Silence afterwards lets the gain come back within a quarter of a second. */
    for (int i = 0; i < 2 * FRAMES; ++i)
        in[i] = 0.0f;
    iptv_audio_limit_to_s16(&gain, 48000, in, FRAMES / 4, out);
    assert(gain == 1.0f);
    float surround[6] = {0.4f, 0.2f, 0.1f, 2.0f, -0.2f, 0.0f};
    int16_t limited[6];
    gain = 1.0f;
    iptv_audio_limit_to_s16_channels(&gain, 48000, surround, 1, 6, limited);
    assert(abs(limited[3]) <= CEILING && limited[5] == 0);
    assert(abs(limited[0] - 2 * limited[1]) <= 1 && limited[1] == -limited[4]);
    free(in);
    free(out);
}

int main(int argc, char **argv)
{
    assert(argc == 3 || argc == 4 || argc == 6);
    const unsigned channels = argc == 6 ? 6 : 2;
    const unsigned mask = argc == 6 ? (unsigned)atoi(argv[5]) : 3;
    assert(!iptv_audio_decoder_create_channels(15, 3));
    check_limiter();
    /* The loudest sample a surround fixture must reach: its tone is in the centre only. */
    const int least_peak = argc >= 4 ? atoi(argv[3]) : 0;
    const unsigned type = (unsigned)strtoul(argv[1], NULL, 0);
    FILE *input = fopen(argv[2], "rb");
    assert(input);
    uint8_t data[65536];
    const size_t bytes = fread(data, 1, sizeof(data), input);
    fclose(input);
    assert(bytes > 7 && bytes < sizeof(data));
    iptv_audio_decoder_t *decoder = iptv_audio_decoder_create_channels(type, channels);
    assert(decoder);

    for (unsigned pass = 0; pass < 2; ++pass)
    {
        size_t at = 0;
        unsigned output_frames = 0;
        long long energy[6] = {0};
        int peak = 0;
        while (at + 7 <= bytes)
        {
            uint32_t rate = 0, samples = 0;
            size_t frame =
                type == 15 ? ((data[at + 3] & 3u) << 11) + (data[at + 4] << 3) + (data[at + 5] >> 5)
                           : iptv_audio_frame_info(type, data + at, bytes - at, &rate, &samples);
            assert(frame && at + frame <= bytes);
            int16_t pcm[24576];
            const int count = iptv_audio_decode(decoder, data + at, frame, pcm, sizeof(pcm), &rate);
            assert(count >= 0 && count % (2 * channels) == 0);
            if (count)
                assert(rate >= 8000 && rate <= 192000);
            for (int i = 0; i < count / 2; ++i)
            {
                peak = abs(pcm[i]) > peak ? abs(pcm[i]) : peak;
                energy[i % channels] += (long long)pcm[i] * pcm[i];
            }
            output_frames += count / (2 * channels);
            at += frame;
        }
        assert(at == bytes && output_frames >= 6000);
        long long total = 0;
        for (unsigned c = 0; c < channels; ++c)
            total += energy[c];
        assert(total > 1000000);
        for (unsigned c = 0; c < channels; ++c)
            if (mask & (1u << c))
                assert(energy[c] > 1000000);
            else
                assert(energy[c] < total / 1000 + 1); /* No speaker crossfeed beyond codec noise. */
        if (getenv("AUDIO_TEST_PRINT"))
            printf("type %u pass %u peak %d\n", type, pass, peak);
        assert(peak >= least_peak && peak <= CEILING);
        iptv_audio_decoder_reset(decoder);
    }
    uint32_t rate = 0, samples = 0;
    size_t first_frame = type == 15 ? ((data[3] & 3u) << 11) + (data[4] << 3) + (data[5] >> 5)
                                    : iptv_audio_frame_info(type, data, bytes, &rate, &samples);
    int16_t tiny[2] = {123, 456};
    assert(iptv_audio_decode(decoder, data, first_frame, tiny, 0, &rate) < 0);
    assert(tiny[0] == 123 && tiny[1] == 456);
    iptv_audio_decoder_free(&decoder);
    assert(!decoder);
    return 0;
}
