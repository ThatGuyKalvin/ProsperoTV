/* ProsperoTV - Pitch-preserving audio speed with source timestamps.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "iptv_audio_tempo.h"
extern "C"
{
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <new>

struct iptv_audio_tempo
{
    AVFilterGraph *graph = nullptr;
    AVFilterContext *source = nullptr, *sink = nullptr;
    AVFrame *input = nullptr, *output = nullptr;
    unsigned rate = 0, channels = 0, speed = 100;
    std::int64_t anchor = AV_NOPTS_VALUE, next_ticks = 0;
    bool finished = false;
    ~iptv_audio_tempo()
    {
        av_frame_free(&input);
        av_frame_free(&output);
        avfilter_graph_free(&graph);
    }
};

void iptv_audio_tempo_free(iptv_audio_tempo_t **tempo)
{
    if (tempo)
    {
        delete *tempo;
        *tempo = nullptr;
    }
}

iptv_audio_tempo_t *iptv_audio_tempo_create(unsigned rate, unsigned channels, unsigned speed)
{
    if (rate < 8000 || rate > 192000 || channels < 1 || channels > 2 || speed < 50 || speed > 200)
        return nullptr;
    auto *t = new (std::nothrow) iptv_audio_tempo;
    if (!t)
        return nullptr;
    t->rate = rate;
    t->channels = channels;
    t->speed = speed;
    t->graph = avfilter_graph_alloc();
    t->input = av_frame_alloc();
    t->output = av_frame_alloc();
    if (!t->graph || !t->input || !t->output)
        goto fail;
    t->graph->nb_threads = 1;
    {
        AVFilterContext *filter = nullptr;
        char args[160];
        std::snprintf(args, sizeof(args),
                      "time_base=1/%u:sample_rate=%u:sample_fmt=s16:channel_layout=%s", rate, rate,
                      channels == 2 ? "stereo" : "mono");
        if (avfilter_graph_create_filter(&t->source, avfilter_get_by_name("abuffer"), "input", args,
                                         nullptr, t->graph) < 0)
            goto fail;
        std::snprintf(args, sizeof(args), "tempo=%u.%02u", speed / 100, speed % 100);
        if (avfilter_graph_create_filter(&filter, avfilter_get_by_name("atempo"), "speed", args,
                                         nullptr, t->graph) < 0 ||
            avfilter_graph_create_filter(&t->sink, avfilter_get_by_name("abuffersink"), "output",
                                         nullptr, nullptr, t->graph) < 0 ||
            avfilter_link(t->source, 0, filter, 0) < 0 ||
            avfilter_link(filter, 0, t->sink, 0) < 0 ||
            avfilter_graph_config(t->graph, nullptr) < 0)
            goto fail;
    }
    return t;
fail:
    delete t;
    return nullptr;
}

static int receive(iptv_audio_tempo *t, iptv_audio_tempo_output_t output, void *context)
{
    int result;
    while ((result = av_buffersink_get_frame(t->sink, t->output)) >= 0)
    {
        auto *f = t->output;
        if (f->format != AV_SAMPLE_FMT_S16 || f->ch_layout.nb_channels != (int)t->channels ||
            f->nb_samples < 0 || f->nb_samples > 131072)
        {
            av_frame_unref(f);
            return AVERROR_INVALIDDATA;
        }
        std::uint64_t pts = UINT64_MAX;
        if (t->anchor != AV_NOPTS_VALUE && f->pts >= 0 && f->pts < INT64_MAX / 4)
        {
            const auto elapsed = av_rescale_q(f->pts, AVRational{(int)t->speed, (int)t->rate},
                                              AVRational{100, 1000000});
            if (elapsed >= 0 && elapsed <= INT64_MAX / 4)
                pts = static_cast<std::uint64_t>(t->anchor + elapsed);
        }
        const int accepted = output(context, reinterpret_cast<const int16_t *>(f->data[0]),
                                    static_cast<std::size_t>(f->nb_samples), pts);
        av_frame_unref(f);
        if (accepted < 0)
            return accepted;
    }
    return result == AVERROR(EAGAIN) || result == AVERROR_EOF ? 0 : result;
}

int iptv_audio_tempo_push(iptv_audio_tempo_t *t, const int16_t *samples, size_t frames,
                          uint64_t pts, iptv_audio_tempo_output_t output, void *context)
{
    if (!t || !samples || !frames || frames > 65536 || !output || t->finished ||
        (pts != UINT64_MAX && pts > INT64_MAX / 4) ||
        (t && t->next_ticks > INT64_MAX - static_cast<std::int64_t>(frames)))
        return AVERROR(EINVAL);
    auto *f = t->input;
    av_frame_unref(f);
    f->format = AV_SAMPLE_FMT_S16;
    f->sample_rate = static_cast<int>(t->rate);
    f->nb_samples = static_cast<int>(frames);
    av_channel_layout_default(&f->ch_layout, static_cast<int>(t->channels));
    if (pts != UINT64_MAX && t->anchor == AV_NOPTS_VALUE)
    {
        const auto elapsed =
            av_rescale_q(t->next_ticks, AVRational{1, (int)t->rate}, AVRational{1, 1000000});
        if (pts >= static_cast<std::uint64_t>(elapsed))
            t->anchor = static_cast<std::int64_t>(pts) - elapsed;
    }
    f->pts = t->next_ticks;
    t->next_ticks += static_cast<std::int64_t>(frames);
    const int allocated = av_frame_get_buffer(f, 0);
    if (allocated < 0)
        return allocated;
    std::memcpy(f->data[0], samples, frames * t->channels * sizeof(*samples));
    const int added = av_buffersrc_add_frame_flags(t->source, f, AV_BUFFERSRC_FLAG_KEEP_REF);
    if (added < 0)
        return added;
    return receive(t, output, context);
}

int iptv_audio_tempo_finish(iptv_audio_tempo_t *t, iptv_audio_tempo_output_t output, void *context)
{
    if (!t || !output || t->finished)
        return AVERROR(EINVAL);
    t->finished = true;
    const int ended = av_buffersrc_add_frame_flags(t->source, nullptr, 0);
    return ended < 0 ? ended : receive(t, output, context);
}
