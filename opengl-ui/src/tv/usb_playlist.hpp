// ProsperoTV - Bounded playlists on normal USB mounts.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "iptv_http.h"
#include <string>
#include <string_view>
#include <vector>
namespace ptv
{
bool usb_playlist_path(std::string_view path);
std::vector<std::string> usb_playlists(const std::string &mount_root = "/mnt");
iptv::http::FetchResult read_usb_playlist(const std::string &path, iptv::http::ListSink sink,
                                          const iptv::http::RequestControl *control,
                                          const std::string &mount_root = "/mnt");
} // namespace ptv
