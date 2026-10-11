// ProsperoTV - Persistent parental PIN and category policy.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace ptv
{
bool adult_category(std::string_view category);
bool kids_category(std::string_view category);

// The session unlock is deliberately never written to disk.
class Parental
{
  public:
    bool load(std::string file);
    bool enabled() const
    {
        return enabled_;
    }
    bool valid() const
    {
        return valid_;
    }
    bool unlocked() const
    {
        return valid_ && (!enabled_ || unlocked_);
    }
    bool kids_only() const
    {
        return kids_;
    }
    bool set_pin(std::string_view pin);
    bool unlock(std::string_view pin, std::uint64_t now);
    void lock()
    {
        unlocked_ = false;
    }
    bool set_kids(bool enabled);
    bool remove();
    bool set_limits(unsigned minutes, int bedtime, unsigned wake);
    // Session time includes pauses and buffering; one meter covers all visible video.
    bool viewing_tick(std::uint64_t wall, std::uint64_t mono, bool active);
    bool viewing_allowed(std::uint64_t wall) const;
    unsigned allowance() const
    {
        return allowance_;
    }
    int bedtime() const
    {
        return bedtime_;
    }
    unsigned wake() const
    {
        return wake_;
    }
    unsigned remaining_seconds() const;
    unsigned remaining_seconds(std::uint64_t wall) const;
    bool limits_enabled() const
    {
        return enabled_ && kids_ && (allowance_ || bedtime_ >= 0);
    }
    const char *viewing_reason(std::uint64_t wall) const;

    std::uint64_t wait_until() const
    {
        return until_;
    }
    static bool valid_pin(std::string_view pin);

  private:
    bool save();
    bool clock_day(std::uint64_t wall, std::int64_t &day, unsigned &minute) const;
    bool save_usage();
    unsigned allowance_ = 0, wake_ = 360, used_ = 0;
    int bedtime_ = -1;
    std::int64_t day_ = -1, bedtime_day_ = -1;
    std::uint64_t last_wall_ = 0, last_mono_ = 0, credit_us_ = 0;
    bool viewing_ = false;

    std::string file_;
    std::array<unsigned char, 16> salt_{};
    std::array<unsigned char, 32> hash_{};
    bool valid_ = true, enabled_ = false, unlocked_ = false, kids_ = false;
    unsigned failures_ = 0;
    std::uint64_t until_ = 0;
};
} // namespace ptv
