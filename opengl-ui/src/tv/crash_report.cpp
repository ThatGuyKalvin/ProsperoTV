// ProsperoTV - Fatal-signal handler with only lock-free state and POSIX-safe I/O.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/crash_report.hpp"
#include <atomic>
#include <cerrno>
#include <csignal>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ptv::crash
{
namespace
{
constexpr std::uint32_t magic = 0x50545643;
struct Wire
{
    std::uint32_t magic, version;
    std::int32_t signal, code;
    std::uint32_t phase, time_low, time_high, tail;
};
static_assert(sizeof(Wire) == 32);
static_assert(std::atomic<int>::is_always_lock_free);
std::atomic<int> descriptor{-1}, current_phase{0};
std::atomic_flag handling = ATOMIC_FLAG_INIT;
constexpr int signals[] = {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV};
bool installed[5]{};
struct sigaction previous[5]{};

bool fatal(int value)
{
    for (int signal : signals)
        if (signal == value)
            return true;
    return false;
}
void caught(int signal, siginfo_t *info, void *)
{
    // shortcut: no alternate signal stack; stack-exhaustion faults may leave no record.
    if (!handling.test_and_set(std::memory_order_relaxed))
    {
        timespec now{};
        const auto stamp = clock_gettime(CLOCK_REALTIME, &now) == 0 && now.tv_sec > 0
                               ? static_cast<std::uint64_t>(now.tv_sec)
                               : 0;
        const Wire record{magic,
                          1,
                          signal,
                          info ? info->si_code : 0,
                          static_cast<std::uint32_t>(current_phase.load(std::memory_order_relaxed)),
                          static_cast<std::uint32_t>(stamp),
                          static_cast<std::uint32_t>(stamp >> 32),
                          magic};
        const int fd = descriptor.load(std::memory_order_relaxed);
        if (fd >= 0 && ::write(fd, &record, sizeof(record)) == sizeof(record))
            (void)::fsync(fd);
    }
    // SIGILL need not be reset by SA_RESETHAND; restore the fatal action explicitly.
    struct sigaction action
    {
    };
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    if (::sigaction(signal, &action, nullptr) != 0 || ::raise(signal) != 0)
        ::_exit(128 + signal);
}
} // namespace

std::string Record::receipt() const
{
    return "crash_signal=" + std::to_string(signal) + "\ncrash_code=" + std::to_string(code) +
           "\ncrash_phase=" + std::to_string(static_cast<unsigned>(phase)) + '\n';
}
Read read(const std::string &path, Record &record)
{
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        return errno == ENOENT ? Read::none : Read::error;
    struct stat facts
    {
    };
    Wire wire{};
    Read result = Read::error;
    if (::fstat(fd, &facts) == 0 && S_ISREG(facts.st_mode))
    {
        if (!facts.st_size)
            result = Read::none;
        else if (facts.st_size != sizeof(wire))
            result = Read::invalid;
        else if (::read(fd, &wire, sizeof(wire)) == sizeof(wire))
        {
            const auto stamp = (std::uint64_t(wire.time_high) << 32) | wire.time_low;
            result = wire.magic == magic && wire.tail == magic && wire.version == 1 &&
                             fatal(wire.signal) && wire.phase <= unsigned(Phase::playback) &&
                             stamp <= UINT64_C(253402300799)
                         ? Read::record
                         : Read::invalid;
            if (result == Read::record)
                record = {wire.signal, wire.code, static_cast<Phase>(wire.phase), stamp};
        }
    }
    ::close(fd);
    return result;
}
bool install(const std::string &path)
{
    stop();
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0)
        return false;
    struct stat facts
    {
    };
    if (::fstat(fd, &facts) != 0 || !S_ISREG(facts.st_mode) || ::ftruncate(fd, 0) != 0)
    {
        ::close(fd);
        return false;
    }
    handling.clear(std::memory_order_relaxed);
    current_phase.store(0, std::memory_order_relaxed);
    descriptor.store(fd, std::memory_order_relaxed);
    bool any = false;
    for (unsigned i = 0; i < 5; ++i)
    {
        if (::sigaction(signals[i], nullptr, &previous[i]) != 0 ||
            previous[i].sa_handler != SIG_DFL)
            continue; // Keep any handler installed by the platform or another library.
        struct sigaction action
        {
        };
        action.sa_sigaction = caught;
        action.sa_flags = SA_SIGINFO | SA_RESETHAND;
        sigemptyset(&action.sa_mask);
        installed[i] = ::sigaction(signals[i], &action, nullptr) == 0;
        any |= installed[i];
    }
    if (!any)
        stop();
    return any;
}
void phase(Phase value)
{
    current_phase.store(static_cast<int>(value), std::memory_order_relaxed);
}
void stop()
{
    for (unsigned i = 0; i < 5; ++i)
        if (installed[i])
        {
            struct sigaction current
            {
            };
            if (::sigaction(signals[i], nullptr, &current) == 0 &&
                (current.sa_flags & SA_SIGINFO) && current.sa_sigaction == caught)
                (void)::sigaction(signals[i], &previous[i], nullptr);
            installed[i] = false;
        }
    const int fd = descriptor.exchange(-1, std::memory_order_relaxed);
    if (fd >= 0)
        ::close(fd);
}
} // namespace ptv::crash
