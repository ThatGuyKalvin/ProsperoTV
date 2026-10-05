// ProsperoTV - Updates on the console: the catalog check and the app replacing itself.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Both are the PS5 Native App Boilerplate's self-update kit (update_kit/, see
// its README.md). The check fetches the catalog's signed manifest and the
// app's own entry from homebrew.page, verifies the Ed25519 signature and the
// hashes, and compares the listed content version with this build's. The
// update downloads the release ZIP from GitHub over HTTPS and streams it to
// the helper (self-updater.elf in the app's folder), which the app sends to
// the console's payload loader: the helper checks and unpacks the release
// beside the app, and once the app has closed it replaces the app's files and
// posts a notification.

#include "tv_update.hpp"

#include "platform/ps5/system.hpp"
#include "tv/platform.hpp"
#include "tv_build_options.h"
#include "tv_paths.h"
#include "tv_storage.hpp"
#include "tv_update_paths.h"
#include "update_kit/self_update.h"
#include "update_kit/update_check.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <string>

extern "C" const char *tv_self_update_path(int which)
{
    static const std::string helper = tv::storage::app_file("self-updater.elf");
    static const std::string param = tv::storage::app_file("sce_sys/param.json");
    return which == 0 ? helper.c_str() : which == 1 ? param.c_str()
                                                    : tv_data_file("self-update-sequence");
}

namespace
{

using hui::sys::log;

// libcurl and OpenSSL want more stack than a default thread of the console has.
constexpr std::size_t kStackBytes = 1024u * 1024u;

std::mutex g_lock;
bool g_started = false;
bool g_found = false; // an offer nobody has been told about yet
self_update_check_result g_answer = SELF_UPDATE_UNKNOWN;
self_update_offer g_offer{};
self_update_job g_job{}; // zero until the first begin, as the kit asks
bool g_begun = false;

const char *result_name(self_update_check_result result)
{
    static const char *const names[] = {"available", "up-to-date", "unknown", "untrusted",
                                        "not-installable"};
    return static_cast<unsigned>(result) < 5 ? names[result] : "?";
}

bool read_lines(const std::string &path, std::string *lines, int count)
{
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;
    char line[700];
    int read = 0;
    while (read < count && std::fgets(line, sizeof(line), file) != nullptr)
    {
        std::size_t length = std::strlen(line);
        while (length != 0 && (line[length - 1] == '\n' || line[length - 1] == '\r' ||
                               line[length - 1] == ' '))
            line[--length] = '\0';
        lines[read++] = line;
    }
    std::fclose(file);
    return read == count;
}

// Test builds: dev/update-offer.txt beside the app replaces the catalog's
// answer (and so skips its signature), so the update can be tried before the
// catalog lists a newer release. Five lines: the new content version, the
// release's name, its ZIP on GitHub, its SHA-256, its size in bytes.
bool test_offer(self_update_offer *out)
{
    std::string lines[5];
    if (TV_DEV_SCRIPTS == 0 || !read_lines(tv::storage::app_file("dev/update-offer.txt"), lines, 5))
        return false;
    self_update_offer filled{};
    if (!update_check_read_param(tv_self_update_path(1), filled.title, filled.installed))
        return false;
    std::snprintf(filled.name, sizeof(filled.name), "ProsperoTV");
    std::snprintf(filled.available, sizeof(filled.available), "%s", lines[0].c_str());
    std::snprintf(filled.version, sizeof(filled.version), "%s", lines[1].c_str());
    std::snprintf(filled.artifact, sizeof(filled.artifact), "%s", lines[2].c_str());
    std::snprintf(filled.sha256, sizeof(filled.sha256), "%s", lines[3].c_str());
    filled.size = std::strtoull(lines[4].c_str(), nullptr, 10);
    // Like the catalog, only a newer version is offered (content versions
    // compare as text).
    if (std::strcmp(filled.available, filled.installed) <= 0)
        return false;
    *out = filled;
    return true;
}

void *check(void *)
{
    self_update_offer result{};
    self_update_check_result state = SELF_UPDATE_UNKNOWN;
    // A test build can ask as another title and version (dev/update-as.txt:
    // "PPSA99003 01.000.000"), since the test title is not in the catalog.
    // That release is another app's: it is only announced.
    std::string as[1];
    char title[10] = {};
    char installed[12] = {};
    if (TV_DEV_SCRIPTS != 0 && read_lines(tv::storage::app_file("dev/update-as.txt"), as, 1) &&
        std::sscanf(as[0].c_str(), "%9s %11s", title, installed) == 2)
    {
        state = self_update_check(self_update_console(), title, installed, &result);
        if (state == SELF_UPDATE_AVAILABLE)
            state = SELF_UPDATE_NOT_INSTALLABLE;
    }
    else
    {
        state = self_update_check_self(&result);
    }
    if (test_offer(&result))
    {
        log("[TV] update check: dev/update-offer.txt replaces the catalog's answer");
        state = SELF_UPDATE_AVAILABLE;
    }
    log("[TV] update check: result=%s installed=%s available=%s version=%s size=%llu",
        result_name(state), result.installed[0] != '\0' ? result.installed : "-",
        result.available[0] != '\0' ? result.available : "-",
        result.version[0] != '\0' ? result.version : "-",
        static_cast<unsigned long long>(result.size));
    if (state == SELF_UPDATE_AVAILABLE || state == SELF_UPDATE_NOT_INSTALLABLE)
    {
        const std::lock_guard guard(g_lock);
        g_answer = state;
        g_offer = result;
        g_found = true;
    }
    return nullptr;
}

ptv::platform::UpdatePhase from_kit(self_update_phase phase)
{
    using ptv::platform::UpdatePhase;
    switch (phase)
    {
    case SELF_UPDATE_STARTING:
        return UpdatePhase::starting;
    case SELF_UPDATE_DOWNLOADING:
        return UpdatePhase::downloading;
    case SELF_UPDATE_UNPACKING:
        return UpdatePhase::unpacking;
    case SELF_UPDATE_READY:
        return UpdatePhase::ready;
    case SELF_UPDATE_APPLYING:
        return UpdatePhase::applying;
    case SELF_UPDATE_CANCELLED:
        return UpdatePhase::cancelled;
    case SELF_UPDATE_FAILED:
        return UpdatePhase::failed;
    default:
        break;
    }
    return UpdatePhase::idle;
}

} // namespace

namespace tv
{

void start_update_check()
{
    {
        const std::lock_guard guard(g_lock);
        if (g_started)
            return;
        g_started = true;
    }
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0)
        return;
    pthread_attr_setstacksize(&attributes, kStackBytes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    if (pthread_create(&thread, &attributes, check, nullptr) != 0)
        log("[TV] update check: the thread could not start");
    pthread_attr_destroy(&attributes);
}

} // namespace tv

namespace ptv::platform
{

bool update_take(UpdateOffer *out)
{
    const std::lock_guard guard(g_lock);
    if (!g_found)
        return false;
    g_found = false;
    if (out != nullptr)
    {
        out->installable = g_answer == SELF_UPDATE_AVAILABLE;
        out->version = g_offer.version[0] != '\0' ? g_offer.version : g_offer.available;
        out->installed = g_offer.installed;
        out->available = g_offer.available;
        out->size = g_offer.size;
    }
    return true;
}

bool update_begin()
{
    const std::lock_guard guard(g_lock);
    if (g_answer != SELF_UPDATE_AVAILABLE)
        return false;
    if (g_begun)
        self_update_finish(&g_job);
    g_begun = self_update_start(&g_job, self_update_console(), &g_offer) == 1;
    log("[TV] update: %s %s", g_begun ? "downloading" : "could not begin", g_offer.available);
    return g_begun;
}

UpdateProgress update_poll()
{
    UpdateProgress progress;
    const std::lock_guard guard(g_lock);
    if (!g_begun)
        return progress;
    self_update_status status{};
    self_update_poll(&g_job, &status);
    progress.phase = from_kit(status.phase);
    progress.done = status.done;
    progress.total = status.total;
    progress.time_left = status.time_left;
    progress.error = status.error;
    return progress;
}

void update_cancel()
{
    const std::lock_guard guard(g_lock);
    if (g_begun)
        self_update_cancel(&g_job);
}

bool update_apply()
{
    const std::lock_guard guard(g_lock);
    if (!g_begun)
        return false;
    const bool going = self_update_apply(&g_job) == 1;
    log("[TV] update: %s", going ? "staged; the helper replaces the files once the app has closed"
                                 : "the helper did not take the go-ahead");
    return going;
}

void update_finish()
{
    const std::lock_guard guard(g_lock);
    if (!g_begun)
        return;
    self_update_finish(&g_job);
    g_begun = false;
}

} // namespace ptv::platform
