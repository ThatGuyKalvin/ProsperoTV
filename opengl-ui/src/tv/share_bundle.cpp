// ProsperoTV - Bounded sharing format. Credentials never enter diagnostics.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/share_bundle.hpp"
#include "tv/usb_playlist.hpp"
#include "iptv_catalog.h"
#include <algorithm>
#include <set>

namespace ptv
{
namespace
{
constexpr std::string_view magic = "PTVSHARE1";
bool text(std::string_view value, std::size_t limit, bool empty = false)
{
    return (empty || !value.empty()) && value.size() <= limit &&
           std::none_of(value.begin(), value.end(),
                        [](unsigned char c) { return c < 32 || c == 127; });
}
struct Writer
{
    std::string bytes{magic};
    void number(std::uint32_t n)
    {
        for (unsigned shift : {24u, 16u, 8u, 0u})
            bytes += static_cast<char>(n >> shift);
    }
    void string(std::string_view s)
    {
        number(static_cast<std::uint32_t>(s.size()));
        bytes += s;
    }
};
struct Reader
{
    std::string_view bytes;
    bool ok = true;
    std::uint32_t number(std::uint32_t limit)
    {
        if (!ok || bytes.size() < 4)
        {
            ok = false;
            return 0;
        }
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i)
            value = (value << 8) | static_cast<unsigned char>(bytes[i]);
        bytes.remove_prefix(4);
        if (value > limit)
            ok = false;
        return ok ? value : 0;
    }
    std::string string(std::uint32_t limit)
    {
        const auto size = number(limit);
        if (!ok || bytes.size() < size)
        {
            ok = false;
            return {};
        }
        std::string result(bytes.substr(0, size));
        bytes.remove_prefix(size);
        return result;
    }
};
bool valid(const SharedLibrary &library)
{
    if (library.sources.size() > 128 || library.favorites.size() > 65536 ||
        library.folders.size() > 256 || library.skipped_local > 65536)
        return false;
    std::size_t total = 0, bytes = 32;
    for (const auto &item : library.sources)
    {
        std::string canonical;
        if (!portable_source(item.source) || item.mapping.size() > 4096 ||
            (!item.guide.empty() &&
             (!text(item.guide, 4096) || !iptv::CanonicalizeStreamUrl(item.guide, &canonical))))
            return false;
        const auto &s = item.source;
        for (const auto *value : {&s.name, &s.url, &s.username, &s.password, &s.mac, &s.user_agent,
                                  &s.referrer, &s.extra_headers, &item.guide})
            bytes += 4 + value->size();
        bytes += 12;
        for (const auto &[channel, guide] : item.mapping)
        {
            if (!text(channel, 1024) || !text(guide, 256))
                return false;
            bytes += 8 + channel.size() + guide.size();
        }
        if (bytes > share_byte_limit)
            return false;
    }
    for (const auto &id : library.favorites)
    {
        if (!text(id, 256))
            return false;
        bytes += 4 + id.size();
    }
    for (const auto &[folder, ids] : library.folders)
    {
        total += ids.size();
        if (!text(folder, 128) || total > 65536)
            return false;
        bytes += 8 + folder.size();
        for (const auto &id : ids)
        {
            if (!text(id, 256))
                return false;
            bytes += 4 + id.size();
        }
    }
    return bytes <= share_byte_limit;
}
} // namespace

bool portable_source(const SavedSource &source)
{
    return source.kind >= 1 && source.kind <= 5 && !source.url.empty() &&
           !usb_playlist_path(source.url) && valid_source(source);
}
bool same_shared_source(const SavedSource &a, const SavedSource &b)
{
    return a.kind == b.kind && a.url == b.url && a.username == b.username &&
           a.password == b.password && a.mac == b.mac;
}
bool encode_shared_library(const SharedLibrary &library, std::string &bytes)
{
    bytes.clear();
    if (!valid(library))
        return false;
    Writer out;
    out.number(library.skipped_local);
    out.number(static_cast<std::uint32_t>(library.sources.size()));
    for (const auto &item : library.sources)
    {
        const auto &s = item.source;
        out.number(s.kind);
        out.number(static_cast<unsigned>(s.schedule));
        for (const auto *value : {&s.name, &s.url, &s.username, &s.password, &s.mac, &s.user_agent,
                                  &s.referrer, &s.extra_headers, &item.guide})
            out.string(*value);
        out.number(static_cast<std::uint32_t>(item.mapping.size()));
        for (const auto &[channel, guide] : item.mapping)
        {
            out.string(channel);
            out.string(guide);
        }
    }
    out.number(static_cast<std::uint32_t>(library.favorites.size()));
    for (const auto &id : library.favorites)
        out.string(id);
    out.number(static_cast<std::uint32_t>(library.folders.size()));
    for (const auto &[name, ids] : library.folders)
    {
        out.string(name);
        out.number(static_cast<std::uint32_t>(ids.size()));
        for (const auto &id : ids)
            out.string(id);
    }
    if (out.bytes.size() > share_byte_limit)
        return false;
    bytes = std::move(out.bytes);
    return true;
}
bool decode_shared_library(std::string_view bytes, SharedLibrary &library)
{
    library = {};
    if (bytes.size() > share_byte_limit || !bytes.starts_with(magic))
        return false;
    Reader in{bytes.substr(magic.size())};
    SharedLibrary next;
    next.skipped_local = in.number(65536);
    auto count = in.number(128);
    for (unsigned i = 0; i < count && in.ok; ++i)
    {
        SharedSource item;
        auto &s = item.source;
        s.kind = static_cast<int>(in.number(5));
        s.schedule = static_cast<RefreshSchedule>(in.number(2));
        for (auto *value : {&s.name, &s.url, &s.username, &s.password, &s.mac, &s.user_agent,
                            &s.referrer, &s.extra_headers, &item.guide})
            *value = in.string(16384);
        const auto mappings = in.number(4096);
        for (unsigned j = 0; j < mappings && in.ok; ++j)
        {
            auto channel = in.string(1024);
            auto guide = in.string(256);
            if (!item.mapping.emplace(std::move(channel), std::move(guide)).second)
                in.ok = false;
        }
        next.sources.push_back(std::move(item));
    }
    count = in.number(65536);
    for (unsigned i = 0; i < count && in.ok; ++i)
        next.favorites.push_back(in.string(256));
    count = in.number(256);
    unsigned total = 0;
    for (unsigned i = 0; i < count && in.ok; ++i)
    {
        auto name = in.string(128);
        const auto ids = in.number(65536 - total);
        total += ids;
        auto [at, inserted] = next.folders.emplace(std::move(name), std::vector<std::string>{});
        if (!inserted)
            in.ok = false;
        for (unsigned j = 0; j < ids && in.ok; ++j)
            at->second.push_back(in.string(256));
    }
    if (!in.ok || !in.bytes.empty() || !valid(next))
        return false;
    library = std::move(next);
    return true;
}
} // namespace ptv
