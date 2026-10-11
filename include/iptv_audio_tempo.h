/* ProsperoTV - Pitch-preserving audio speed with source timestamps.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IPTV_AUDIO_TEMPO_H
#define IPTV_AUDIO_TEMPO_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct iptv_audio_tempo iptv_audio_tempo_t;
    typedef int (*iptv_audio_tempo_output_t)(void *context, const int16_t *samples, size_t frames,
                                             uint64_t source_pts_us);
    iptv_audio_tempo_t *iptv_audio_tempo_create(unsigned rate, unsigned channels,
                                                unsigned speed_percent);
    void iptv_audio_tempo_free(iptv_audio_tempo_t **tempo);
    /* The decoder's owner calls these serially. Output borrows filter storage. */
    int iptv_audio_tempo_push(iptv_audio_tempo_t *tempo, const int16_t *samples, size_t frames,
                              uint64_t source_pts_us, iptv_audio_tempo_output_t output,
                              void *context);
    int iptv_audio_tempo_finish(iptv_audio_tempo_t *tempo, iptv_audio_tempo_output_t output,
                                void *context);
#ifdef __cplusplus
}
#endif
#endif
