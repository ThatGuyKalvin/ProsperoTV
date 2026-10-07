// ProsperoTV - What an address and the first bytes of its answer say, for the debug trace.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace ptv
{

// An address with what identifies its owner taken out, safe to send to
// someone else: the scheme, the host and the port stay; a user name and
// password, the path and the query are replaced by their shape
// ("http://192.168.1.20:8080/<3 parts>.m3u8?<24 bytes>").
std::string redact_address(std::string_view address);

// What the first bytes of an answer look like ("MPEG-TS", "HLS playlist",
// "fragmented MP4", "JPEG picture", ...), followed by the first sixteen in hex.
std::string describe_bytes(const unsigned char *data, std::size_t size);

} // namespace ptv
