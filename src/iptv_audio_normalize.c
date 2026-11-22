/* ProsperoTV - optional bounded loudness control without allocations.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "iptv_audio_normalize.h"
#include <math.h>
#include <string.h>

#define WINDOW_FRAMES 4800u /* 100 ms at the fixed 48 kHz output rate. */
#define START_WINDOWS 5u
#define GATE_ENERGY 1.1724653e-5 /* -50 LUFS, accounting for -0.691 offset. */
#define PEAK_CEILING 29203.0     /* Conservative -1 dBFS sample-peak ceiling. */

static double filter(double x, double z[2], double b0, double b1, double b2, double a1, double a2)
{
    const double y = b0 * x + z[0];
    z[0] = b1 * x - a1 * y + z[1];
    z[1] = b2 * x - a2 * y;
    return y;
}

void iptv_audio_normalizer_reset(iptv_audio_normalizer_t *state)
{
    if (!state)
        return;
    memset(state, 0, sizeof(*state));
    state->limiter_gain = 1.0;
    state->loudness_lufs = -100.0;
    for (unsigned i = 0; i <= IPTV_NORMALIZE_LOOKAHEAD; ++i)
        state->required[i] = 1.0;
}

void iptv_audio_normalizer_process(iptv_audio_normalizer_t *state, int16_t *pcm, size_t frames,
                                   int preset)
{
    iptv_audio_normalizer_process_channels(state, pcm, frames, 2, preset);
}

void iptv_audio_normalizer_process_channels(iptv_audio_normalizer_t *state, int16_t *pcm,
                                            size_t frames, unsigned channels, int preset)
{
    if (!state || !pcm || !frames || (channels != 2 && channels != 6 && channels != 8) ||
        frames > SIZE_MAX / channels)
        return;
    int enabled = preset >= IPTV_NORMALIZE_OFF && preset <= IPTV_NORMALIZE_SPEAKER
                      ? preset
                      : IPTV_NORMALIZE_STANDARD;
    if (state->enabled != enabled || state->channels != channels)
    {
        iptv_audio_normalizer_reset(state);
        state->enabled = enabled;
        state->channels = channels;
    }
    if (enabled == IPTV_NORMALIZE_OFF)
        return;

    double block_energy = 0.0, peak = 0.0;
    for (size_t i = 0; i < frames; ++i)
    {
        double energy = 0.0;
        for (unsigned channel = 0; channel < channels; ++channel)
        {
            const double sample = pcm[i * channels + channel];
            if (fabs(sample) > peak)
                peak = fabs(sample);
            if (channels > 2 && channel == 3)
                continue; /* LFE contributes to peak protection, not loudness. */
            /* BS.1770 K weighting, fixed coefficients for 48 kHz. */
            double weighted =
                filter(sample / 32768.0, state->shelf[channel], 1.53512485958697, -2.69169618940638,
                       1.19839281085285, -1.69065929318241, 0.73248077421585);
            weighted = filter(weighted, state->highpass[channel], 1.0, -2.0, 1.0, -1.99004745483398,
                              0.99007225036621);
            energy += weighted * weighted * (channel >= 4 ? 1.41 : 1.0);
        }
        block_energy += energy;
        state->window_sum += energy;
        if (++state->window_frames == WINDOW_FRAMES)
        {
            const double average = state->window_sum / WINDOW_FRAMES;
            /* Do not let quiet gaps dilute the program-level estimate. */
            if (average >= GATE_ENERGY)
            {
                state->energy_sum -= state->windows[state->window_position];
                state->windows[state->window_position] = average;
                state->energy_sum += average;
                state->window_position = (state->window_position + 1u) % IPTV_NORMALIZE_WINDOWS;
                if (state->window_count < IPTV_NORMALIZE_WINDOWS)
                    ++state->window_count;
                state->meter_valid = state->window_count >= START_WINDOWS;
                state->loudness_lufs =
                    -0.691 + 10.0 * log10(state->energy_sum / state->window_count);
            }
            state->window_sum = 0.0;
            state->window_frames = 0;
        }
    }

    const double seconds = (double)frames / IPTV_NORMALIZE_RATE;
    const double previous_db = state->gain_db;
    double target_db = 0.0;
    if (state->meter_valid && block_energy / frames >= GATE_ENERGY)
    {
        const double target = enabled == IPTV_NORMALIZE_SPEAKER  ? -14.0
                              : enabled == IPTV_NORMALIZE_LOUDER ? -18.0
                                                                 : -23.0;
        target_db = target - state->loudness_lufs;
        if (target_db > 18.0)
            target_db = 18.0;
        if (target_db < -18.0)
            target_db = -18.0;
    }
    const double step = (target_db > previous_db              ? 3.0
                         : enabled == IPTV_NORMALIZE_STANDARD ? 12.0
                                                              : 24.0) *
                        seconds;
    if (fabs(target_db - previous_db) <= step)
        state->gain_db = target_db;
    else
        state->gain_db += target_db > previous_db ? step : -step;

    const double start_gain = pow(10.0, previous_db / 20.0);
    const double end_gain = pow(10.0, state->gain_db / 20.0);
    if (enabled >= IPTV_NORMALIZE_LOUDER)
    {
        int limited = 0;
        for (size_t i = 0; i < frames; ++i)
        {
            const unsigned first = state->delay_position;
            const unsigned last =
                (first + IPTV_NORMALIZE_LOOKAHEAD) % (IPTV_NORMALIZE_LOOKAHEAD + 1);
            const double gain = start_gain + (end_gain - start_gain) * (double)(i + 1) / frames;
            double frame_peak = 0.0;
            for (unsigned c = 0; c < channels; ++c)
            {
                const double sample = pcm[i * channels + c] * gain;
                state->delayed[last][c] = (int32_t)lrint(sample);
                frame_peak = fmax(frame_peak, fabs(sample));
            }
            state->required[last] = frame_peak > PEAK_CEILING ? PEAK_CEILING / frame_peak : 1.0;
            /* Every future peak imposes an attack ramp. A newly arrived peak is
             * 5 ms away and cannot introduce a gain step at the current sample.
             * The minimum envelope falls by at most 1/lookahead per sample;
             * release is linked across channels and takes at least 250 ms.
             * Persistent history makes the envelope continuous across blocks. */
            double limit = fmin(1.0, state->limiter_gain + 1.0 / (0.25 * IPTV_NORMALIZE_RATE));
            for (unsigned j = 0; j <= IPTV_NORMALIZE_LOOKAHEAD; ++j)
            {
                const unsigned at = (first + j) % (IPTV_NORMALIZE_LOOKAHEAD + 1);
                limit = fmin(limit, state->required[at] + (double)j / IPTV_NORMALIZE_LOOKAHEAD);
            }
            state->limiter_gain = limit;
            limited |= limit < 0.999999;
            for (unsigned c = 0; c < channels; ++c)
            {
                const double sample = state->delayed[first][c] * limit;
                pcm[i * channels + c] = (int16_t)fmax(-PEAK_CEILING, fmin(PEAK_CEILING, sample));
            }
            state->delay_position = (first + 1) % (IPTV_NORMALIZE_LOOKAHEAD + 1);
        }
        state->limited_blocks += limited;
        return;
    }
    const double maximum_gain = start_gain > end_gain ? start_gain : end_gain;
    double limit = peak > 0.0 ? PEAK_CEILING / (peak * maximum_gain) : 1.0;
    if (limit > 1.0)
        limit = 1.0;
    if (limit < state->limiter_gain)
        state->limiter_gain = limit; /* Current block provides bounded lookahead. */
    else
        state->limiter_gain = fmin(limit, state->limiter_gain + seconds / 0.25);
    if (state->limiter_gain < 0.999999)
        ++state->limited_blocks;
    for (size_t i = 0; i < frames; ++i)
    {
        const double gain =
            (start_gain + (end_gain - start_gain) * (double)(i + 1) / frames) * state->limiter_gain;
        for (unsigned channel = 0; channel < channels; ++channel)
        {
            double sample = pcm[i * channels + channel] * gain;
            if (sample > PEAK_CEILING)
                sample = PEAK_CEILING;
            if (sample < -PEAK_CEILING)
                sample = -PEAK_CEILING;
            pcm[i * channels + channel] = (int16_t)sample;
        }
    }
}
