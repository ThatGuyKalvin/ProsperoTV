// ProsperoTV - Standalone, unsanitized fatal-signal recorder checks.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/crash_report.hpp"
#include <array>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
namespace
{
unsigned checks = 0;
void require(bool value, const char *label)
{
    ++checks;
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}
volatile sig_atomic_t called = 0;
void custom(int)
{
    called = 1;
}
void handler(int signal, void (*function)(int))
{
    struct sigaction action
    {
    };
    action.sa_handler = function;
    sigemptyset(&action.sa_mask);
    require(sigaction(signal, &action, nullptr) == 0, "set test handler");
}
void (*handler_of(int signal))(int)
{
    struct sigaction action
    {
    };
    require(sigaction(signal, nullptr, &action) == 0, "read handler");
    return action.sa_handler;
}
void bytes(const std::string &path, const void *data, std::size_t size)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
    require(stream.good(), "write test record");
}
} // namespace
int main()
{
    using namespace ptv::crash;
    // SIGALRM prevents a broken handler or FIFO operation from hanging this check.
    alarm(30);
    char directory[] = "/tmp/prosperotv-crash-check-XXXXXX";
    require(mkdtemp(directory) != nullptr, "temporary directory");
    const std::string root = directory;
    const auto path = root + "/fatal.bin";
    Record record;
    require(read(path, record) == Read::none, "absent record");
    constexpr int signals[] = {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV};
    for (int signal : signals)
    {
        const pid_t child = fork();
        require(child >= 0, "fork");
        if (!child)
        {
            const rlimit limit{0, 0};
            if (setrlimit(RLIMIT_CORE, &limit) != 0)
                _exit(20);
            alarm(5);
            for (int value : signals)
                handler(value, SIG_DFL);
            if (!install(path))
                _exit(21);
            phase(Phase::playback);
            raise(signal);
            _exit(22);
        }
        int status = 0;
        require(waitpid(child, &status, 0) == child, "wait fatal child");
        require(WIFSIGNALED(status) && WTERMSIG(status) == signal,
                "original fatal signal preserved");
        require(std::filesystem::file_size(path) == 32, "fixed 32-byte record");
        require(read(path, record) == Read::record, "fatal record accepted");
        require(record.signal == signal && record.phase == Phase::playback && record.timestamp > 0,
                "fatal metadata");
        require(record.receipt().find("crash_signal=" + std::to_string(signal) + "\n") !=
                    std::string::npos,
                "receipt metadata");
    }
    std::array<std::uint32_t, 8> valid{};
    {
        std::ifstream stream(path, std::ios::binary);
        stream.read(reinterpret_cast<char *>(valid.data()), sizeof(valid));
        require(stream.good(), "read fixture");
    }
    for (const auto mutation : {std::pair<unsigned, std::uint32_t>{0, 0},
                                {1, 2},
                                {2, SIGTERM},
                                {4, 3},
                                {6, 0xffffffffu},
                                {7, 0}})
    {
        auto malformed = valid;
        malformed[mutation.first] = mutation.second;
        bytes(path, malformed.data(), sizeof(malformed));
        require(read(path, record) == Read::invalid, "malformed record rejected");
    }
    bytes(path, valid.data(), 31);
    require(read(path, record) == Read::invalid, "short record rejected");
    std::array<char, 33> oversized{};
    bytes(path, oversized.data(), oversized.size());
    require(read(path, record) == Read::invalid, "long record rejected");
    require(install(path), "ordinary install");
    phase(Phase::menu);
    stop();
    stop();
    require(read(path, record) == Read::none, "ordinary close creates no crash");
    for (int signal : signals)
        require(handler_of(signal) == SIG_DFL, "stop restores defaults");

    handler(SIGABRT, custom);
    require(install(path), "install alongside custom handler");
    require(handler_of(SIGABRT) == custom, "custom handler retained during install");
    called = 0;
    raise(SIGABRT);
    require(called == 1, "custom handler invoked");
    stop();
    require(handler_of(SIGABRT) == custom, "custom handler retained after stop");
    require(read(path, record) == Read::none, "custom signal not misreported");
    handler(SIGABRT, SIG_IGN);
    require(install(path), "install alongside ignored signal");
    require(handler_of(SIGABRT) == SIG_IGN, "ignored signal retained");
    raise(SIGABRT);
    stop();
    require(handler_of(SIGABRT) == SIG_IGN, "ignored signal retained after stop");
    handler(SIGABRT, SIG_DFL);
    require(install(path), "install before replacement");
    handler(SIGABRT, custom);
    stop();
    require(handler_of(SIGABRT) == custom, "later replacement retained");
    handler(SIGABRT, SIG_DFL);
    for (int signal : signals)
        handler(signal, SIG_IGN);
    require(!install(path), "no handlers available reported");
    for (int signal : signals)
    {
        require(handler_of(signal) == SIG_IGN, "all ignored handlers preserved");
        handler(signal, SIG_DFL);
    }

    const auto link = root + "/link", fifo = root + "/fifo";
    bytes(path, valid.data(), sizeof(valid));
    require(symlink(path.c_str(), link.c_str()) == 0, "make symlink");
    require(read(link, record) == Read::error && !install(link), "symlink rejected");
    require(read(path, record) == Read::record, "symlink target unchanged");
    require(mkfifo(fifo.c_str(), 0600) == 0, "make FIFO");
    require(read(fifo, record) == Read::error && !install(fifo), "FIFO rejected without blocking");
    require(read(root, record) == Read::error && !install(root), "directory rejected");
    stop();
    std::filesystem::remove_all(root);
    alarm(0);
    std::printf("PASS: %u checks; five fatal signals preserve termination and bounded records. "
                "Host only.\n",
                checks);
}
