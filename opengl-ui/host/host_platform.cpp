// ProsperoTV - The PC's stand-ins for the console: keyboard, network, clock.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "host_platform.hpp"

#include "iptv_ime.h"
#include "tv/platform.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <pthread.h>
#include <thread>

namespace
{

struct Keyboard
{
    bool available = true;
    bool ready = false;
    bool pending = false;
    bool has_answer = false;
    std::string answer;
    std::string title;
    int requests = 0;
    iptv_ime_result_fn callback = nullptr;
    void *user_data = nullptr;
};

struct Network
{
    std::mutex mutex;
    bool reachable = false;
    std::string playlist_path;
    unsigned delay_ms = 0;
    std::atomic<bool> cancelled{false};
    std::atomic<int> fetches{0};
};

struct Update
{
    bool offered = false;
    ptv::platform::UpdateOffer offer;
    ptv::platform::UpdateProgress progress;
    bool refuse_begin = false;
    bool refuse_apply = false;
    bool begun = false;
    host::UpdateCalls calls;
};

Keyboard g_keyboard;
Network g_network;
Update g_update;
// 0: the PC's own clock. The catalog store stamps its files with that clock,
// so a test moves this one forward from it.
std::atomic<std::uint64_t> g_unix_time{0};

void request(const char *title, iptv_ime_result_fn callback, void *user_data)
{
    if (!g_keyboard.ready || g_keyboard.pending)
        return;
    g_keyboard.pending = true;
    g_keyboard.title = title != nullptr ? title : "";
    g_keyboard.callback = callback;
    g_keyboard.user_data = user_data;
    ++g_keyboard.requests;
}

} // namespace

namespace host
{

void set_keyboard_text(const std::string &text)
{
    g_keyboard.answer = text;
    g_keyboard.has_answer = true;
}

void cancel_keyboard()
{
    g_keyboard.pending = false;
    g_keyboard.has_answer = false;
}

int keyboard_requests()
{
    return g_keyboard.requests;
}

const std::string &keyboard_title()
{
    return g_keyboard.title;
}

void set_keyboard_available(bool available)
{
    g_keyboard.available = available;
}

void set_network(bool reachable, const std::string &playlist_path, unsigned delay_ms)
{
    const std::lock_guard<std::mutex> lock(g_network.mutex);
    g_network.reachable = reachable;
    g_network.playlist_path = playlist_path;
    g_network.delay_ms = delay_ms;
}

int fetch_count()
{
    return g_network.fetches.load();
}

void set_unix_time(std::uint64_t seconds)
{
    g_unix_time.store(seconds);
}

void offer_update(const ptv::platform::UpdateOffer &offer)
{
    g_update.offer = offer;
    g_update.offered = true;
}

void set_update_progress(const ptv::platform::UpdateProgress &progress)
{
    g_update.progress = progress;
}

void refuse_update(bool begin, bool apply)
{
    g_update.refuse_begin = begin;
    g_update.refuse_apply = apply;
}

UpdateCalls update_calls()
{
    return g_update.calls;
}

void reset()
{
    g_keyboard = {};
    g_update = {};
    set_network(false, "", 0);
    g_network.fetches.store(0);
    g_network.cancelled.store(false);
    g_unix_time.store(0);
}

} // namespace host

// ---- the console keyboard ----------------------------------------------------

extern "C" bool iptv_ime_init(void)
{
    g_keyboard.ready = g_keyboard.available;
    return g_keyboard.ready;
}

extern "C" void iptv_ime_request(const char *, iptv_ime_result_fn callback, void *user_data)
{
    request("Search ProsperoTV", callback, user_data);
}

extern "C" void iptv_ime_request_prompt(const char *, const char *title, const char *, unsigned,
                                        iptv_ime_result_fn callback, void *user_data)
{
    request(title, callback, user_data);
}

extern "C" void iptv_ime_request_password(const char *title, const char *, unsigned,
                                          iptv_ime_result_fn callback, void *user_data)
{
    request(title, callback, user_data);
}

extern "C" void iptv_ime_poll(void)
{
    if (!g_keyboard.pending || !g_keyboard.has_answer)
        return;
    g_keyboard.pending = false;
    g_keyboard.has_answer = false;
    const std::string answer = g_keyboard.answer;
    if (g_keyboard.callback != nullptr)
        g_keyboard.callback(answer.c_str(), g_keyboard.user_data);
}

extern "C" void iptv_ime_cancel(void)
{
    g_keyboard.pending = false;
}

extern "C" void iptv_ime_shutdown(void)
{
    g_keyboard.pending = false;
    g_keyboard.ready = false;
}

// ---- threads, clock and network -------------------------------------------------

namespace ptv::platform
{

void *thread_start(void *(*entry)(void *), void *argument, std::size_t stack_bytes, const char *)
{
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0)
        return nullptr;
    pthread_attr_setstacksize(&attributes, stack_bytes);
    auto *thread = new pthread_t;
    const int result = pthread_create(thread, &attributes, entry, argument);
    pthread_attr_destroy(&attributes);
    if (result != 0)
    {
        delete thread;
        return nullptr;
    }
    return thread;
}

int thread_join(void *thread)
{
    auto *handle = static_cast<pthread_t *>(thread);
    const int result = pthread_join(*handle, nullptr);
    if (result == 0)
        delete handle;
    return result;
}

int thread_detach(void *thread)
{
    auto *handle = static_cast<pthread_t *>(thread);
    const int result = pthread_detach(*handle);
    delete handle;
    return result;
}

void sleep_ms(unsigned milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

std::uint64_t unix_time()
{
    const std::uint64_t set = g_unix_time.load();
    return set != 0 ? set : static_cast<std::uint64_t>(std::time(nullptr));
}

iptv::http::Status network_init()
{
    g_network.cancelled.store(false);
    return iptv::http::Status::ok;
}

void network_shutdown()
{
}

void network_cancel()
{
    g_network.cancelled.store(true);
}

iptv::http::FetchResult fetch(const char *, char *buffer, std::size_t capacity,
                              std::size_t max_bytes, const iptv::http::RequestControl *control)
{
    g_network.fetches.fetch_add(1);
    bool reachable = false;
    std::string playlist_path;
    unsigned delay_ms = 0;
    {
        const std::lock_guard<std::mutex> lock(g_network.mutex);
        reachable = g_network.reachable;
        playlist_path = g_network.playlist_path;
        delay_ms = g_network.delay_ms;
    }
    if (capacity != 0)
        buffer[0] = '\0';
    for (unsigned waited = 0; waited < delay_ms; waited += 5)
    {
        if (g_network.cancelled.load() || (control != nullptr && control->cancelled != nullptr &&
                                           control->cancelled(control->context)))
            return {iptv::http::Status::cancelled, 0, 0, 0};
        sleep_ms(5);
    }
    if (!reachable)
        return {iptv::http::Status::request_failed, 0, 0, -1};
    std::FILE *file = std::fopen(playlist_path.c_str(), "rb");
    if (file == nullptr)
        return {iptv::http::Status::http_status_error, 0, 404, 0};
    const std::size_t room = capacity != 0 ? capacity - 1u : 0u;
    const std::size_t bytes = std::fread(buffer, 1, room < max_bytes ? room : max_bytes, file);
    const bool more = std::fgetc(file) != EOF;
    std::fclose(file);
    if (capacity != 0)
        buffer[bytes] = '\0';
    if (more)
        return {iptv::http::Status::response_too_large, bytes, 200, 0};
    return {iptv::http::Status::ok, bytes, 200, 0};
}

} // namespace ptv::platform

// ---- the update ---------------------------------------------------------------

namespace ptv::platform
{

bool update_take(UpdateOffer *offer)
{
    if (!g_update.offered)
        return false;
    g_update.offered = false;
    if (offer != nullptr)
        *offer = g_update.offer;
    return true;
}

bool update_begin()
{
    ++g_update.calls.begin;
    g_update.begun = !g_update.refuse_begin;
    return g_update.begun;
}

UpdateProgress update_poll()
{
    return g_update.begun ? g_update.progress : UpdateProgress{};
}

void update_cancel()
{
    ++g_update.calls.cancel;
}

bool update_apply()
{
    ++g_update.calls.apply;
    return !g_update.refuse_apply;
}

void update_finish()
{
    ++g_update.calls.finish;
    g_update.begun = false;
}

} // namespace ptv::platform
