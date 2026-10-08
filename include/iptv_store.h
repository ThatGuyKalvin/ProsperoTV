/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_STORE_H
#define IPTV_STORE_H

#include "iptv_catalog.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace iptv {

// A quarter of a million channels are saved in about 70 MiB.
inline constexpr std::size_t kDefaultMaxStoreBytes = 256u * 1024u * 1024u;
inline constexpr std::size_t kDefaultMaxStoredRecordBytes = 32u * 1024u;
inline constexpr char kDefaultPlaybackHistoryPath[] =
    "/download0/prosperotv-playback-history.sqlite3";

inline constexpr std::size_t kLibraryMaxStoreBytes = kDefaultMaxStoreBytes;
inline constexpr std::size_t kMaxResumeEntries = 512u;
// Positions at or below this are treated as "not started".
inline constexpr std::uint32_t kResumeMinimumSecs = 10u;
// Positions inside this tail (or the last 5%) are treated as "finished".
inline constexpr std::uint32_t kResumeFinishedMarginSecs = 30u;

struct StoreLimits {
    std::size_t max_file_bytes = kDefaultMaxStoreBytes;
    std::size_t max_record_bytes = kDefaultMaxStoredRecordBytes;
    std::size_t max_string_bytes = kDefaultMaxFieldBytes;
    std::size_t max_url_bytes = kDefaultMaxUrlBytes;
    std::size_t max_channels = kDefaultMaxChannels;
    std::size_t max_alternate_urls = kDefaultMaxAlternateUrls;
    std::size_t max_alternate_groups = kDefaultMaxAlternateGroups;
};

// Limits for Xtream movie and series catalogs.
inline StoreLimits LibraryStoreLimits() {
    StoreLimits limits;
    limits.max_file_bytes = kLibraryMaxStoreBytes;
    limits.max_channels = kDefaultMaxLibraryEntries;
    return limits;
}

enum class StoreStatus : std::uint8_t {
    ok,
    invalid_argument,
    not_found,
    too_large,
    io_error,
    corrupt,
    unsupported_version,
};

struct StoreReport {
    StoreStatus status = StoreStatus::ok;
    std::size_t records = 0;
    std::size_t bytes = 0;
    std::uint64_t saved_unix = 0;
};

StoreStatus SaveCatalog(const std::string& path,
                        const Catalog& catalog,
                        const StoreLimits& limits = StoreLimits{},
                        StoreReport* report = nullptr);

StoreStatus LoadCatalog(const std::string& path,
                        Catalog* catalog,
                        const StoreLimits& limits = StoreLimits{},
                        StoreReport* report = nullptr);

StoreStatus RecordPlaybackResult(const std::string& path,
                                 std::uint64_t source_id,
                                 const std::string& channel_id,
                                 bool playable,
                                 int result);

StoreStatus LoadPlaybackResults(const std::string& path,
                                std::uint64_t source_id,
                                Catalog* catalog,
                                const StoreLimits& limits = StoreLimits{});

struct ResumeEntry {
    std::string channel_id;
    std::uint32_t position_secs = 0;
    std::uint32_t duration_secs = 0;
    std::uint64_t updated_unix = 0;
};

// Stores where playback stopped. Positions that are barely started or already
// finished remove the entry instead, so only titles worth resuming are kept.
// duration_secs may be 0 when the provider did not report one.
StoreStatus SaveResumePosition(const std::string& path,
                               std::uint64_t source_id,
                               const std::string& channel_id,
                               std::uint32_t position_secs,
                               std::uint32_t duration_secs);

StoreStatus LoadResumePosition(const std::string& path,
                               std::uint64_t source_id,
                               const std::string& channel_id,
                               ResumeEntry* entry);

// Newest first, at most `limit` entries.
StoreStatus LoadResumePositions(const std::string& path,
                                std::uint64_t source_id,
                                std::size_t limit,
                                std::vector<ResumeEntry>* entries);

}  // namespace iptv

#endif
