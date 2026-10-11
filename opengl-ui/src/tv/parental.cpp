// ProsperoTV - Persistent parental PIN and category policy.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/parental.hpp"
#include "tv/platform.hpp"
#include "tv/category_path.hpp"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <limits>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ptv
{
namespace
{
std::string lower(std::string_view text)
{
    std::string out(text);
    for (auto &c : out)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return out;
}
bool category_has(std::string_view category, bool adult)
{
    for (category = category_trim(category); !category.empty();
         category = category_parent(category))
    {
        const auto leaf = lower(category_leaf(category));
        if (adult ? leaf == "adult" || leaf == "adults" || leaf == "xxx" || leaf == "18+" ||
                        leaf == "+18"
                  : leaf == "kids" || leaf == "children" || leaf == "children's")
            return true;
    }
    return false;
}
std::string hex(const auto &bytes)
{
    std::string text;
    for (auto byte : bytes)
    {
        text += "0123456789abcdef"[byte >> 4];
        text += "0123456789abcdef"[byte & 15];
    }
    return text;
}
bool unhex(std::string_view text, auto &bytes)
{
    if (text.size() != bytes.size() * 2)
        return false;
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        unsigned value = 0;
        const auto parsed =
            std::from_chars(text.data() + i * 2, text.data() + i * 2 + 2, value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + i * 2 + 2)
            return false;
        bytes[i] = static_cast<unsigned char>(value);
    }
    return true;
}
bool derive(std::string_view pin, const auto &salt, auto &hash)
{
    return PKCS5_PBKDF2_HMAC(pin.data(), static_cast<int>(pin.size()), salt.data(),
                             static_cast<int>(salt.size()), 200000, EVP_sha256(),
                             static_cast<int>(hash.size()), hash.data()) == 1;
}
} // namespace
bool adult_category(std::string_view category)
{
    return category_has(category, true);
}
bool kids_category(std::string_view category)
{
    return category_has(category, false);
}

bool Parental::valid_pin(std::string_view pin)
{
    return pin.size() >= 4 && pin.size() <= 8 &&
           std::all_of(pin.begin(), pin.end(), [](char c) { return c >= '0' && c <= '9'; });
}
bool Parental::load(std::string file)
{
    *this = {};
    file_ = std::move(file);
    const int fd = ::open(file_.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return valid_ = errno == ENOENT;
    struct stat st
    {
    };
    char data[320]{};
    bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size < 320;
    if (ok)
        ok = read(fd, data, static_cast<std::size_t>(st.st_size)) == st.st_size;
    close(fd);
    char salt[33]{}, hash[65]{};
    unsigned kids = 0, failures = 0;
    unsigned long long until = 0;
    int used = 0;
    const bool version2 = std::string_view(data).starts_with("PTV_PARENTAL_2\n");
    unsigned allowance = 0, wake = 360, charged = 0;
    int bedtime = -1;
    long long day = -1, bedtime_day = -1;
    unsigned long long wall = 0;
    const int fields =
        version2 ? std::sscanf(data,
                               "PTV_PARENTAL_2\n%32[0-9a-f]\n%64[0-9a-f]\n%u %u %llu\n%u %d %u "
                               "%lld %u %llu %lld\n%n",
                               salt, hash, &kids, &failures, &until, &allowance, &bedtime, &wake,
                               &day, &charged, &wall, &bedtime_day, &used)
                 : std::sscanf(data, "PTV_PARENTAL_1\n%32[0-9a-f]\n%64[0-9a-f]\n%u %u %llu\n%n",
                               salt, hash, &kids, &failures, &until, &used);
    ok = ok && fields == (version2 ? 12 : 5) && used == st.st_size && kids <= 1 && failures < 5 &&
         until <= UINT64_C(253402300799) && unhex(salt, salt_) && unhex(hash, hash_) &&
         allowance <= 1440 && bedtime >= -1 && bedtime < 1440 && wake < 1440 &&
         (bedtime < 0 || unsigned(bedtime) != wake) && day >= -1 && day <= 2932896 &&
         charged <= 86400 && wall <= UINT64_C(253402300799) && bedtime_day >= -1 &&
         bedtime_day <= 2932896;
    allowance_ = allowance;
    bedtime_ = bedtime;
    wake_ = wake;
    day_ = day;
    bedtime_day_ = bedtime_day;
    used_ = charged;
    last_wall_ = wall;
    enabled_ = true; // A malformed existing file never turns protection off.
    valid_ = ok;
    kids_ = kids != 0;
    failures_ = failures;
    until_ = until;
    return ok;
}
bool Parental::save()
{
    const std::string text = "PTV_PARENTAL_2\n" + hex(salt_) + '\n' + hex(hash_) + '\n' +
                             std::to_string(kids_ ? 1 : 0) + ' ' + std::to_string(failures_) + ' ' +
                             std::to_string(until_) + '\n' + std::to_string(allowance_) + ' ' +
                             std::to_string(bedtime_) + ' ' + std::to_string(wake_) + ' ' +
                             std::to_string(day_) + ' ' + std::to_string(used_) + ' ' +
                             std::to_string(last_wall_) + ' ' + std::to_string(bedtime_day_) + '\n';
    const auto temporary = file_ + ".tmp";
    // Profile storage is private. Refuse links and remove only our prior temporary file.
    if (unlink(temporary.c_str()) != 0 && errno != ENOENT)
        return false;
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0)
        return false;
    bool ok =
        write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size()) && fsync(fd) == 0;
    if (close(fd) != 0)
        ok = false;
    if (ok)
        ok = rename(temporary.c_str(), file_.c_str()) == 0;
    if (!ok)
        unlink(temporary.c_str());
    return ok;
}
bool Parental::set_pin(std::string_view pin)
{
    if (!unlocked() || !valid_pin(pin))
        return false;
    auto next = *this;
    if (RAND_bytes(next.salt_.data(), static_cast<int>(next.salt_.size())) != 1 ||
        !derive(pin, next.salt_, next.hash_))
        return false;
    next.enabled_ = true;
    next.unlocked_ = false;
    next.failures_ = 0;
    next.until_ = 0;
    if (!next.save())
        return false;
    *this = std::move(next);
    return true;
}
bool Parental::unlock(std::string_view pin, std::uint64_t now)
{
    if (!valid_ || !enabled_ || now == 0 || now < until_)
        return false;
    std::array<unsigned char, 32> hash{};
    const bool match = valid_pin(pin) && derive(pin, salt_, hash) &&
                       CRYPTO_memcmp(hash.data(), hash_.data(), hash.size()) == 0;
    OPENSSL_cleanse(hash.data(), hash.size());
    auto next = *this;
    next.unlocked_ = false;
    if (match)
    {
        next.failures_ = 0;
        next.until_ = 0;
    }
    else if (++next.failures_ >= 5)
    {
        next.failures_ = 0;
        next.until_ = now + 30;
    }
    // A storage failure cannot reset the retry count by reopening the app.
    if (!next.save())
    {
        valid_ = false;
        unlocked_ = false;
        return false;
    }
    next.unlocked_ = match;
    *this = std::move(next);
    return match;
}
bool Parental::set_kids(bool enabled)
{
    if (!enabled_ || !unlocked())
        return false;
    auto next = *this;
    next.kids_ = enabled;
    if (!next.save())
        return false;
    if (enabled)
        next.lock();
    *this = std::move(next);
    return true;
}
bool Parental::set_limits(unsigned minutes, int bedtime, unsigned wake)
{
    if (!enabled_ || !unlocked() || minutes > 1440 || bedtime < -1 || bedtime >= 1440 ||
        wake >= 1440 || (bedtime >= 0 && unsigned(bedtime) == wake) || viewing_)
        return false;
    auto next = *this;
    next.allowance_ = minutes;
    if (next.bedtime_ != bedtime || next.wake_ != wake)
        next.bedtime_day_ = -1;
    next.bedtime_ = bedtime;
    next.wake_ = wake;
    // Preserve charged usage when changing limits.
    if (!next.save())
        return false;
    *this = std::move(next);
    return true;
}
bool Parental::clock_day(std::uint64_t wall, std::int64_t &day, unsigned &minute) const
{
    if (!wall || wall > UINT64_C(253402300799) ||
        wall > std::uint64_t(std::numeric_limits<std::time_t>::max()))
        return false;
    std::tm copy{};
    if (!platform::calendar_time(wall, true, copy))
        return false;
    const std::chrono::year_month_day date{std::chrono::year(copy.tm_year + 1900),
                                           std::chrono::month(unsigned(copy.tm_mon + 1)),
                                           std::chrono::day(unsigned(copy.tm_mday))};
    if (!date.ok() || copy.tm_hour < 0 || copy.tm_hour > 23 || copy.tm_min < 0 || copy.tm_min > 59)
        return false;
    minute = unsigned(copy.tm_hour * 60 + copy.tm_min);
    day = std::chrono::sys_days(date).time_since_epoch().count() - (minute < wake_ ? 1 : 0);
    return day >= 0;
}
unsigned Parental::remaining_seconds(std::uint64_t wall) const
{
    std::int64_t day;
    unsigned minute;
    if (clock_day(wall, day, minute) && day > day_)
        return allowance_ * 60;
    return remaining_seconds();
}
unsigned Parental::remaining_seconds() const
{
    const auto reserved = std::min(used_, unsigned(credit_us_ / 1000000));
    return allowance_ * 60 > used_ - reserved ? allowance_ * 60 - used_ + reserved : 0;
}
const char *Parental::viewing_reason(std::uint64_t wall) const
{
    if (!valid_)
        return "Parental settings could not be read";
    if (!limits_enabled())
        return nullptr;
    std::int64_t day;
    unsigned minute;
    if (!clock_day(wall, day, minute) || (last_wall_ > wall && last_wall_ - wall > 120) ||
        day < day_ || day < bedtime_day_)
        return "Check the console clock before watching";
    const bool blocked = bedtime_ >= 0 && (unsigned(bedtime_) > wake_
                                               ? minute >= unsigned(bedtime_) || minute < wake_
                                               : minute >= unsigned(bedtime_) && minute < wake_);
    if (blocked || (bedtime_ >= 0 && day == bedtime_day_))
        return "It is bedtime";
    if (allowance_ && day <= day_ && !remaining_seconds())
        return "Today's viewing time is finished";
    return nullptr;
}
bool Parental::viewing_allowed(std::uint64_t wall) const
{
    return !viewing_reason(wall);
}
bool Parental::save_usage()
{
    if (save())
        return true;
    valid_ = false;
    unlocked_ = false;
    viewing_ = false;
    return false;
}
bool Parental::viewing_tick(std::uint64_t wall, std::uint64_t mono, bool active)
{
    if (viewing_)
    {
        if (!mono || mono < last_mono_)
        {
            valid_ = false;
            return false;
        }
        const auto elapsed = mono - last_mono_;
        if (allowance_ && elapsed > credit_us_)
            used_ = unsigned(
                std::min<std::uint64_t>(86400, used_ + (elapsed - credit_us_ + 999999) / 1000000));
        credit_us_ = elapsed >= credit_us_ ? 0 : credit_us_ - elapsed;
    }
    last_mono_ = mono;
    if (valid_ && limits_enabled())
    {
        std::int64_t day;
        unsigned minute;
        if (clock_day(wall, day, minute))
            last_wall_ = std::max(last_wall_, wall);
    }
    if (valid_ && limits_enabled() && bedtime_ >= 0)
    {
        std::int64_t day;
        unsigned minute;
        if (clock_day(wall, day, minute) && day > bedtime_day_ &&
            (unsigned(bedtime_) > wake_ ? minute >= unsigned(bedtime_) || minute < wake_
                                        : minute >= unsigned(bedtime_) && minute < wake_))
        {
            bedtime_day_ = day;
            last_wall_ = std::max(last_wall_, wall);
            if (!save_usage())
                return false;
        }
    }
    if (!active || !limits_enabled())
    {
        if (viewing_)
        {
            used_ -= std::min(used_, unsigned(credit_us_ / 1000000));
            credit_us_ = 0;
            viewing_ = false;
            if (!save_usage())
                return false;
        }
        return viewing_allowed(wall);
    }
    if (!viewing_allowed(wall) || !mono)
    {
        if (viewing_)
        {
            std::int64_t day;
            unsigned minute;
            // A normal bedtime stop refunds unused time; uncertain clocks remain conservative.
            if (mono && valid_ && clock_day(wall, day, minute) && day == bedtime_day_ &&
                wall >= last_wall_)
                used_ -= std::min(used_, unsigned(credit_us_ / 1000000));
            viewing_ = false;
            credit_us_ = 0;
            (void)save_usage();
        }
        return false;
    }
    std::int64_t day;
    unsigned minute;
    if (!clock_day(wall, day, minute))
        return false;
    bool dirty = false;
    if (day > day_)
    {
        day_ = day;
        used_ = 0;
        credit_us_ = 0;
        dirty = true;
    }
    last_wall_ = std::max(last_wall_, wall);
    if (!viewing_ || credit_us_ == 0)
    {
        const unsigned block =
            allowance_ ? std::min(30u, allowance_ * 60 - std::min(used_, allowance_ * 60)) : 30u;
        if (!block)
            return false;
        if (allowance_)
            used_ += block;
        credit_us_ = std::uint64_t(block) * 1000000;
        dirty = true;
    }
    if (dirty && !save_usage())
        return false;
    viewing_ = true;
    return true;
}

bool Parental::remove()
{
    if (!unlocked() || unlink(file_.c_str()) != 0)
        return false;
    const auto file = file_;
    *this = {};
    file_ = file;
    return true;
}
} // namespace ptv
