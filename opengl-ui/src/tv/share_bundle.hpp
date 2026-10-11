// ProsperoTV - Portable sources and favorites, without installation-specific state.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tv/library.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace ptv
{
inline constexpr std::size_t share_byte_limit = 8u * 1024u * 1024u;
struct SharedSource
{
    SavedSource source;
    std::string guide;
    std::map<std::string, std::string> mapping;
};
struct SharedLibrary
{
    std::vector<SharedSource> sources;
    std::vector<std::string> favorites;
    std::map<std::string, std::vector<std::string>> folders;
    unsigned skipped_local = 0;
};
bool portable_source(const SavedSource &source);
bool same_shared_source(const SavedSource &a, const SavedSource &b);
bool encode_shared_library(const SharedLibrary &library, std::string &bytes);
bool decode_shared_library(std::string_view bytes, SharedLibrary &library);
// Call with the destination model closed; preserves every unrelated profile file.
bool import_shared_library(const std::string &directory, const SharedLibrary &library,
                           std::string &error);
} // namespace ptv
