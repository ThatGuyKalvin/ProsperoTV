// ProsperoTV - Recording uses the same scoped HTTP and container remux as playback.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/recording.hpp"
#include "iptv_media.h"
#include "iptv_catalog.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <memory>
namespace ptv::platform
{
namespace
{
struct Source
{
    const RecordingInput &input;
    const iptv::http::RequestControl &control;
    bool cancelled() const
    {
        return control.cancelled && control.cancelled(control.context);
    }
};
struct Resource
{
    Source &owner;
    iptv::http::StreamRequest stream{};
    std::string url;
    std::int64_t offset = 0, end = -1;
    std::array<std::uint8_t, 4096> prefix{};
    int prefix_size = 0, prefix_at = 0;
    explicit Resource(Source &s) : owner(s)
    {
    }
    ~Resource()
    {
        iptv::http::CloseStream(&stream);
    }
    bool open(const char *address, std::int64_t begin = 0, std::int64_t until = -1)
    {
        std::string canonical;
        if (owner.cancelled() || !iptv::CanonicalizeStreamUrl(address, &canonical))
            return false;
        iptv::http::RequestHeaders headers{};
        headers.user_agent = owner.input.user_agent.c_str();
        headers.referrer = owner.input.referrer.c_str();
        headers.authorization = owner.input.authorization.c_str();
        headers.credential_origin = owner.input.credential_origin.empty()
                                        ? owner.input.url.c_str()
                                        : owner.input.credential_origin.c_str();
        headers.byte_offset = begin > 0 ? begin : -1;
        const auto scoped =
            iptv::http::HeadersForUrl(owner.input.url.c_str(), canonical.c_str(), headers);
        if (iptv::http::OpenStream(canonical.c_str(), "*/*", &stream, &scoped) !=
            iptv::http::Status::ok)
            return false;
        url = stream.effective_url[0] ? stream.effective_url : canonical;
        offset = begin;
        end = until;
        return true;
    }
    static int read(void *p, std::uint8_t *out, int count)
    {
        auto &r = *static_cast<Resource *>(p);
        if (r.owner.cancelled())
            return -1;
        if (r.prefix_at < r.prefix_size)
        {
            const int take = std::min(count, r.prefix_size - r.prefix_at);
            std::memcpy(out, r.prefix.data() + r.prefix_at, take);
            r.prefix_at += take;
            return take;
        }
        if (r.end >= 0)
        {
            if (r.offset >= r.end)
                return 0;
            count = static_cast<int>(std::min<std::int64_t>(count, r.end - r.offset));
        }
        const int got = iptv::http::ReadStream(&r.stream, out, static_cast<std::size_t>(count));
        if (got > 0)
            r.offset += got;
        return got;
    }
    static std::int64_t seek(void *p, std::int64_t at)
    {
        auto &r = *static_cast<Resource *>(p);
        const std::string url = r.url;
        iptv::http::CloseStream(&r.stream);
        r.prefix_at = r.prefix_size = 0;
        return at >= 0 && r.open(url.c_str(), at, r.end) ? at : -1;
    }
    iptv::MediaInput media(bool hls = false)
    {
        iptv::MediaInput result{};
        result.context = this;
        result.read = read;
        result.seek = seek;
        result.size = [](void *p) { return static_cast<Resource *>(p)->stream.size; };
        result.cancelled = [](void *p) { return static_cast<Resource *>(p)->owner.cancelled(); };
        if (hls)
        {
            result.url = url.c_str();
            result.open_resource = [](void *p, const char *url, std::int64_t begin,
                                      std::int64_t end, iptv::MediaInput *output)
            {
                auto *r = new (std::nothrow) Resource(static_cast<Resource *>(p)->owner);
                if (!r)
                    return false;
                if (!r->open(url, begin, end))
                {
                    delete r;
                    return false;
                }
                *output = r->media();
                output->url = r->url.c_str();
                output->close = [](void *v) { delete static_cast<Resource *>(v); };
                return true;
            };
        }
        return result;
    }
};
} // namespace
int record_stream(const RecordingInput &input, const iptv::http::ListSink &sink,
                  const iptv::http::RequestControl &control, std::string &error)
{
    if (iptv::http::NetworkInit() != iptv::http::Status::ok)
    {
        error = "Recording network initialization failed";
        return -1;
    }
    int result = -1;
    {
        Source source{input, control};
        Resource resource(source);
        if (!resource.open(input.url.c_str()))
            error = "The recording stream could not be opened";
        else
        {
            while (resource.prefix_size < 188 && !source.cancelled())
            {
                const int got = iptv::http::ReadStream(
                    &resource.stream, resource.prefix.data() + resource.prefix_size,
                    resource.prefix.size() - resource.prefix_size);
                if (got <= 0)
                    break;
                resource.prefix_size += got;
            }
            resource.offset = std::max(0, resource.prefix_size);
            if (resource.prefix_size <= 0)
                error = "The recording stream returned no media";
            else
            {
                const bool hls = resource.prefix_size >= 7 &&
                                 std::memcmp(resource.prefix.data(), "#EXTM3U", 7) == 0;
                if (hls || iptv::LooksLikeMedia(resource.prefix.data(), resource.prefix_size))
                {
                    const iptv::MediaOutput output{
                        const_cast<iptv::http::ListSink *>(&sink),
                        [](void *p, const std::uint8_t *data, std::size_t bytes)
                        {
                            const auto &s = *static_cast<iptv::http::ListSink *>(p);
                            return s.write(s.context, reinterpret_cast<const char *>(data), bytes);
                        }};
                    result = iptv::ReadMedia(resource.media(hls), output, &error);
                }
                else if (resource.prefix[0] == 0x47)
                {
                    std::array<std::uint8_t, 64u * 1024u> buffer{};
                    result = 0;
                    while (!source.cancelled())
                    {
                        const int got = Resource::read(&resource, buffer.data(), buffer.size());
                        if (got == 0)
                            break;
                        if (got < 0 ||
                            !sink.write(sink.context, reinterpret_cast<const char *>(buffer.data()),
                                        static_cast<std::size_t>(got)))
                        {
                            result = -1;
                            error = "The stream or USB recording write failed";
                            break;
                        }
                    }
                }
                else
                    error = "This stream format cannot be recorded";
            }
        }
    }
    if (result == 0 && !(control.cancelled && control.cancelled(control.context)))
    {
        error = "The stream ended before the recording finished";
        result = -1;
    }
    iptv::http::NetworkShutdown();
    return result;
}
} // namespace ptv::platform
