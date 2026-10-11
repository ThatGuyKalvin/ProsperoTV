// ProsperoTV - Bounded records of fatal signals, never inferred from an unclean exit.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>

namespace ptv::crash
{
enum class Phase : unsigned
{
    startup,
    menu,
    playback
};
struct Record
{
    int signal = 0, code = 0;
    Phase phase = Phase::startup;
    std::uint64_t timestamp = 0;
    std::string receipt() const;
};
enum class Read
{
    none,
    record,
    invalid,
    error
};
Read read(const std::string &path, Record &record);
// Call before starting workers. Recover a previous record before installing again.
bool install(const std::string &path);
void phase(Phase value);
// Only after all workers have joined; no signal handler may race descriptor closure.
void stop();
} // namespace ptv::crash
