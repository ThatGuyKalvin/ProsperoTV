/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_INFLATE_H
#define IPTV_INFLATE_H

#include <cstddef>
#include <cstdint>

// A small streaming gzip (RFC 1952) / deflate (RFC 1951) decoder for compressed TV guides.
// The console build has no zlib, and guides are far larger than any buffer, so input is
// pulled from a callback and output is pushed to one as it is produced.
namespace iptv::gzip
{

enum class Status : std::uint8_t
{
    ok,
    read_failed, // the source reported an error
    truncated,   // the source ended inside the stream
    corrupt,     // not gzip, a bad deflate block, or a CRC or length mismatch
    too_large,   // more than max_output bytes
    stopped,     // the sink asked to stop
};

// Reads up to `capacity` bytes; returns the count, 0 at the end, or a negative error.
using ReadFunction = long (*)(void *context, unsigned char *buffer, std::size_t capacity);
// Takes decompressed bytes; returns false to stop.
using WriteFunction = bool (*)(void *context, const char *data, std::size_t size);

// True when the data starts with the gzip magic bytes.
bool LooksGzipped(const unsigned char *data, std::size_t size);

// Decodes one or more concatenated gzip members.
Status Inflate(ReadFunction read, WriteFunction write, void *context, std::uint64_t max_output);

const char *StatusName(Status status);

} // namespace iptv::gzip

#endif
