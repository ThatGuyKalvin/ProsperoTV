// ProsperoTV - The tuning screen: from the menu's last picture to the channel's first.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv_tuning.h"

#include "tv_plate.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace
{

enum class Phase
{
    running,    // the channel opens: the bar creeps between its floor and ceiling
    completing, // the first picture is ready: the bar runs to its end
    holding,    // a breath at 100 %
    fading,     // the screen goes to black
    done,
};

struct State
{
    tuning::Plate plate;
    std::atomic<bool> active{false};
    std::atomic<bool> finishing{false};
    std::atomic<std::uint32_t> floor_x1000{0};
    std::atomic<std::uint32_t> ceiling_x1000{0};
    std::atomic<bool> fresh_surface{true};
    // Only the loading thread touches these while a picture is up.
    Phase phase = Phase::running;
    float progress = 0.0f;
    float from = 0.0f;
    std::uint64_t last_us = 0;
    std::uint64_t phase_us = 0;
    std::uint64_t begun_us = 0;
    std::string dump_dir;
    bool dumped_start = false;
    bool dumped_middle = false;
    bool dumped_end = false;
};

State &state()
{
    static State instance;
    return instance;
}

std::uint64_t now_us()
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * 1000000u +
           static_cast<std::uint64_t>(now.tv_nsec) / 1000u;
}

float load(const std::atomic<std::uint32_t> &value)
{
    return static_cast<float>(value.load(std::memory_order_relaxed)) / 1000.0f;
}

// The surface, half size, as a 24-bit BMP: what the television showed.
void dump(const void *surface, const std::string &path)
{
    constexpr int kOut = 960;
    constexpr int kOutHeight = 540;
    const auto *luma = static_cast<const std::uint8_t *>(surface);
    const std::uint8_t *chroma = luma + static_cast<std::size_t>(tuning::kWidth) *
                                            tuning::kSurfaceHeight;
    const std::uint32_t row = kOut * 3;
    const std::uint32_t size = 54 + row * kOutHeight;
    unsigned char header[54] = {'B', 'M'};
    const auto put = [&](int at, std::uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            header[at + i] = static_cast<unsigned char>(value >> (8 * i));
    };
    put(2, size);
    put(10, 54);
    put(14, 40);
    put(18, kOut);
    put(22, kOutHeight);
    header[26] = 1;
    header[28] = 24;
    put(34, size - 54);
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr)
        return;
    std::fwrite(header, 1, sizeof(header), file);
    std::vector<unsigned char> line(row);
    for (int y = kOutHeight - 1; y >= 0; --y)
    {
        const int sy = y * 2;
        for (int x = 0; x < kOut; ++x)
        {
            const int sx = x * 2;
            const float l = (static_cast<float>(luma[sy * tuning::kWidth + sx]) - 16.0f) / 219.0f;
            const std::uint8_t *pair = chroma + (sy / 2) * tuning::kWidth + (sx & ~1);
            const float u = (static_cast<float>(pair[0]) - 128.0f) / 224.0f;
            const float v = (static_cast<float>(pair[1]) - 128.0f) / 224.0f;
            const float r = l + 1.5748f * v;
            const float g = l - 0.1873f * u - 0.4681f * v;
            const float b = l + 1.8556f * u;
            const auto byte = [](float c)
            { return static_cast<unsigned char>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f); };
            line[static_cast<std::size_t>(x) * 3 + 0] = byte(b);
            line[static_cast<std::size_t>(x) * 3 + 1] = byte(g);
            line[static_cast<std::size_t>(x) * 3 + 2] = byte(r);
        }
        std::fwrite(line.data(), 1, line.size(), file);
    }
    std::fclose(file);
}

} // namespace

extern "C" void tv_tuning_set_picture(const uint8_t *rgba, float bar_x, float bar_y,
                                      float bar_width, float bar_height, const uint8_t fill[3],
                                      float progress)
{
    State &s = state();
    s.active.store(false, std::memory_order_release);
    tuning::Bar bar;
    bar.x = bar_x;
    bar.y = bar_y;
    bar.width = bar_width;
    bar.height = bar_height;
    bar.fill[0] = fill[0];
    bar.fill[1] = fill[1];
    bar.fill[2] = fill[2];
    s.plate.build(rgba, bar, false);
    s.phase = Phase::running;
    s.progress = std::clamp(progress, 0.0f, 1.0f);
    s.last_us = 0;
    s.begun_us = now_us();
    s.dumped_start = s.dumped_middle = s.dumped_end = false;
    s.finishing.store(false, std::memory_order_relaxed);
    s.fresh_surface.store(true, std::memory_order_relaxed);
    tv_tuning_stage(s.progress, 0.45f);
    s.active.store(rgba != nullptr, std::memory_order_release);
}

extern "C" void tv_tuning_clear(void)
{
    State &s = state();
    s.active.store(false, std::memory_order_release);
    s.plate.clear();
}

extern "C" int tv_tuning_active(void)
{
    return state().active.load(std::memory_order_acquire) ? 1 : 0;
}

extern "C" void tv_tuning_stage(float floor, float ceiling)
{
    State &s = state();
    s.floor_x1000.store(static_cast<std::uint32_t>(std::clamp(floor, 0.0f, 1.0f) * 1000.0f),
                        std::memory_order_relaxed);
    s.ceiling_x1000.store(static_cast<std::uint32_t>(std::clamp(ceiling, 0.0f, 1.0f) * 1000.0f),
                          std::memory_order_relaxed);
}

extern "C" void tv_tuning_surface_changed(void)
{
    state().fresh_surface.store(true, std::memory_order_relaxed);
}

extern "C" void tv_tuning_finishing(void)
{
    state().finishing.store(true, std::memory_order_release);
}

extern "C" void tv_tuning_set_dump_dir(const char *directory)
{
    state().dump_dir = directory != nullptr ? directory : "";
}

extern "C" int tv_tuning_compose(void *surface, size_t surface_bytes)
{
    State &s = state();
    if (!s.active.load(std::memory_order_acquire) || surface == nullptr ||
        surface_bytes < tuning::surface_bytes(false) || s.phase == Phase::done)
        return 0;
    const std::uint64_t now = now_us();
    const float dt = s.last_us == 0 ? 0.0f
                                    : std::min(static_cast<float>(now - s.last_us) / 1e6f, 0.1f);
    s.last_us = now;
    if (s.phase == Phase::running && s.finishing.load(std::memory_order_acquire))
    {
        s.phase = Phase::completing;
        s.from = s.progress;
        s.phase_us = now;
        std::fprintf(stderr, "[TV] tuning: first picture after %llu ms, the bar at %.0f %%\n",
                     static_cast<unsigned long long>((now - s.begun_us) / 1000u),
                     static_cast<double>(s.progress * 100.0f));
    }

    float brightness = 1.0f;
    switch (s.phase)
    {
    case Phase::running:
    {
        // Quickly up to the floor; slowly towards the ceiling, never quite
        // reaching it while the step it stands for runs.
        const float floor = load(s.floor_x1000);
        const float ceiling = load(s.ceiling_x1000);
        if (s.progress < floor)
            s.progress += (floor - s.progress) * (1.0f - std::exp(-dt / 0.12f));
        if (s.progress < ceiling)
            s.progress += (ceiling - s.progress) * (1.0f - std::exp(-dt / 2.5f));
        break;
    }
    case Phase::completing:
    {
        const float t = static_cast<float>(now - s.phase_us) / 260000.0f;
        const float eased = t >= 1.0f ? 1.0f : 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        s.progress = s.from + (1.0f - s.from) * eased;
        if (t >= 1.0f)
        {
            s.phase = Phase::holding;
            s.phase_us = now;
        }
        break;
    }
    case Phase::holding:
        s.progress = 1.0f;
        if (now - s.phase_us >= 90000u)
        {
            s.phase = Phase::fading;
            s.phase_us = now;
        }
        break;
    case Phase::fading:
    {
        const float t = static_cast<float>(now - s.phase_us) / 280000.0f;
        brightness = t >= 1.0f ? 0.0f : 1.0f - t * t * (3.0f - 2.0f * t);
        if (t >= 1.0f)
            s.phase = Phase::done; // this frame is black; the next call ends it
        break;
    }
    case Phase::done:
        return 0;
    }

    // The whole picture once per surface and while it fades; otherwise only
    // the rows of the bar change.
    if (s.fresh_surface.exchange(false, std::memory_order_relaxed) || s.phase == Phase::fading ||
        s.phase == Phase::done)
    {
        s.plate.compose(surface, s.progress, brightness);
    }
    else
    {
        tuning::Range written[2];
        s.plate.compose_bar(surface, s.progress, written);
    }

    if (!s.dump_dir.empty())
    {
        if (!s.dumped_start)
        {
            s.dumped_start = true;
            dump(surface, s.dump_dir + "/tuning-1-start.bmp");
        }
        else if (!s.dumped_middle && s.progress >= 0.40f)
        {
            s.dumped_middle = true;
            dump(surface, s.dump_dir + "/tuning-2-middle.bmp");
        }
        else if (!s.dumped_end && s.phase == Phase::holding)
        {
            s.dumped_end = true;
            dump(surface, s.dump_dir + "/tuning-3-complete.bmp");
        }
    }
    return 1;
}
