// ProsperoTV - What the app's logic asks of the machine it runs on.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The console answers these with its own threads and its HTTP stack
// (ps5/src/tv_platform.cpp); a PC answers them with pthreads and a stand-in
// network (host/platform_host.cpp), so the same logic runs in tests and in
// the PC renderer.

#pragma once

#include "iptv_http.h"

#include <cstddef>
#include <cstdint>

namespace ptv::platform
{

// A worker thread with a stack of its own. Returns null when it cannot start.
void *thread_start(void *(*entry)(void *), void *argument, std::size_t stack_bytes,
                   const char *name);
// 0 once the thread has ended and was joined.
int thread_join(void *thread);
int thread_detach(void *thread);
void sleep_ms(unsigned milliseconds);
// Seconds since 1970, or 0 when the clock is not set.
std::uint64_t unix_time();

// The network, as the catalog worker uses it. Always called from that thread,
// except network_cancel(), which interrupts it from the frame loop.
iptv::http::Status network_init();
void network_shutdown();
void network_cancel();
iptv::http::FetchResult fetch(const char *url, char *buffer, std::size_t capacity,
                              std::size_t max_bytes, const iptv::http::RequestControl *control);

} // namespace ptv::platform
