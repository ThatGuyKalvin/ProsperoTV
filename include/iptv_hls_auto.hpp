// ProsperoTV - Conservative automatic HLS quality reduction.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "iptv_hls.h"
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace iptv
{
// HTTP may split even the first box header across reads. Keep only eight bytes.
class HlsMediaPrefix
{
  public:
    int observe(const std::uint8_t *data, std::size_t size)
    {
        if (kind_ || !size)
            return kind_;
        const auto copied = std::min(size, sizeof(prefix_) - used_);
        std::memcpy(prefix_ + used_, data, copied);
        used_ += copied;
        if (used_ == sizeof(prefix_))
            kind_ = prefix_[0] == 0x47 || std::memcmp(prefix_ + 4, "styp", 4) == 0 ||
                            std::memcmp(prefix_ + 4, "moof", 4) == 0 ||
                            std::memcmp(prefix_ + 4, "mdat", 4) == 0 ||
                            std::memcmp(prefix_ + 4, "sidx", 4) == 0
                        ? 1
                        : -1;
        return kind_;
    }

  private:
    std::uint8_t prefix_[8]{};
    std::size_t used_ = 0;
    int kind_ = 0;
};
// Completed network operations only: decoder/presentation waits are not bandwidth.
class HlsAutoReduction
{
  public:
    void reset_samples()
    {
        first_frame_ = window_ = bytes_ = network_ = frames_ = progress_ = underruns_ = 0;
        slow_ = 0;
    }
    void reset()
    {
        *this = {};
    }
    void failed(std::uint64_t bitrate)
    {
        if (bitrate)
            ceiling = std::min(ceiling, bitrate - 1);
        reset_samples();
    }
    void reduced(std::uint64_t now, std::uint64_t bitrate)
    {
        ceiling = std::min(ceiling, bitrate);
        changed_ = now;
        reset_samples();
    }
    unsigned lower(const iptv_hls_quality_t *variants, unsigned count, unsigned current) const
    {
        if (!variants || current >= count)
            return IPTV_HLS_NO_VARIANT;
        unsigned result = IPTV_HLS_NO_VARIANT;
        std::uint64_t best = 0;
        for (unsigned i = 0; i < count; ++i)
            if (variants[i].bandwidth < variants[current].bandwidth &&
                variants[i].bandwidth > best && variants[i].bandwidth <= ceiling)
            {
                result = i;
                best = variants[i].bandwidth;
            }
        return result;
    }
    bool sample(std::uint64_t now, std::uint64_t bytes, std::uint64_t network_us,
                std::uint64_t bitrate, std::uint64_t frames, std::uint64_t underruns,
                bool suspended)
    {
        if (suspended || !frames || !bitrate)
        {
            reset_samples();
            return false;
        }
        if (!first_frame_)
        {
            first_frame_ = progress_ = now;
            frames_ = frames;
            underruns_ = underruns;
        }
        if (frames != frames_)
            progress_ = now;
        frames_ = frames;
        if (now - first_frame_ < 5000000 || (changed_ && now - changed_ < 20000000))
        {
            underruns_ = underruns;
            return false;
        }
        if (!window_)
            window_ = now;
        bytes_ += bytes;
        network_ += network_us;
        if (now - window_ < 2000000)
            return false;
        const bool deficit = bytes_ >= 4096 && network_ >= 100000 &&
                             static_cast<long double>(bytes_) * 8000000.0L <
                                 static_cast<long double>(network_) * bitrate * 1.15L;
        const bool starving = underruns > underruns_ || now - progress_ >= 1000000;
        slow_ = deficit ? std::min(slow_ + 1, 2u) : 0;
        const bool reduce = slow_ >= 2 && starving;
        window_ = now;
        bytes_ = network_ = 0;
        underruns_ = underruns;
        return reduce;
    }
    std::uint64_t ceiling = UINT64_MAX;

  private:
    std::uint64_t first_frame_ = 0, window_ = 0, bytes_ = 0, network_ = 0, frames_ = 0,
                  progress_ = 0, underruns_ = 0, changed_ = 0;
    unsigned slow_ = 0;
};
} // namespace iptv
