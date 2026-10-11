// ProsperoTV - Native recording transport and real container remux with file-backed HTTP.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "iptv_http.h"
#include "iptv_stream.h"
#include "tv/recording.hpp"
#include <gtest/gtest.h>
#include <map>
#include <fstream>
#include <iterator>
#include <cstring>
#include <algorithm>
namespace
{
struct Download
{
    std::vector<char> bytes;
    std::size_t at = 0;
};
std::map<int, Download> downloads;
std::vector<std::string> authorizations, custom_headers;
int next_request = 1;
std::string fixture_prefix = "build/media-tests/fixtures/";
} // namespace
namespace iptv::http
{
Status RecordingNetworkInit()
{
    return Status::ok;
}
void RecordingNetworkShutdown()
{
}
Status RecordingOpenStream(const char *url, const char *, StreamRequest *stream,
                           const RequestHeaders *headers)
{
    std::string_view address(url);
    const auto path = address.find('/', address.find("://") + 3);
    if (path == address.npos || address.find("..") != address.npos)
        return Status::unsupported_url;
    std::ifstream file(fixture_prefix + std::string(address.substr(path + 1)), std::ios::binary);
    if (!file)
        return Status::read_failed;
    Download download;
    download.bytes.assign(std::istreambuf_iterator<char>(file), {});
    download.at = headers && headers->byte_offset > 0 ? headers->byte_offset : 0;
    if (download.at > download.bytes.size())
        return Status::read_failed;
    stream->size = download.bytes.size();
    stream->open = true;
    stream->request = next_request++;
    std::strncpy(stream->effective_url, url, sizeof(stream->effective_url) - 1);
    downloads.emplace(stream->request, std::move(download));
    authorizations.emplace_back(headers && headers->authorization ? headers->authorization : "");
    custom_headers.emplace_back(headers && headers->extra_headers ? headers->extra_headers : "");
    return Status::ok;
}
int RecordingReadStream(StreamRequest *stream, void *buffer, std::size_t count)
{
    auto &d = downloads.at(stream->request);
    count = std::min({count, d.bytes.size() - d.at, std::size_t(37)});
    if (count)
        std::memcpy(buffer, d.bytes.data() + d.at, count);
    d.at += count;
    return static_cast<int>(count);
}
void RecordingCloseStream(StreamRequest *stream)
{
    if (stream->open)
    {
        downloads.erase(stream->request);
        stream->open = false;
    }
}
} // namespace iptv::http
#define NetworkInit RecordingNetworkInit
#define NetworkShutdown RecordingNetworkShutdown
#define OpenStream RecordingOpenStream
#define ReadStream RecordingReadStream
#define CloseStream RecordingCloseStream
#include "../opengl-ui/ps5/src/tv_recording.cpp"
#undef NetworkInit
#undef NetworkShutdown
#undef OpenStream
#undef ReadStream
#undef CloseStream
namespace
{
TEST(RecordingTransport, RemuxesContainersAndHlsIntoPlayableTransportWithoutLeakingConnections)
{
    for (const char *file :
         {"two-audio.mp4", "two-audio.mkv", "hls-mpegts/master.m3u8", "hls-fmp4/master.m3u8"})
    {
        ptv::RecordingInput input;
        input.url = std::string("https://fixture.test/") + file;
        std::vector<char> output;
        std::string error;
        const iptv::http::ListSink sink{[](void *p, const char *bytes, std::size_t count)
                                        {
                                            auto &v = *static_cast<std::vector<char> *>(p);
                                            v.insert(v.end(), bytes, bytes + count);
                                            return true;
                                        },
                                        &output};
        EXPECT_LT(ptv::platform::record_stream(input, sink, {}, error), 0) << file;
        EXPECT_EQ(error, "The stream ended before the recording finished") << file;
        ASSERT_GT(output.size(), 188u) << file;
        EXPECT_EQ(static_cast<unsigned char>(output[0]), 0x47) << file;
        EXPECT_EQ(output.size() % 188, 0u) << file;
        unsigned frames = 0;
        iptv_stream_backend_t backend{};
        backend.context = &frames;
        backend.open = [](void *, const iptv_stream_format_t *) { return 0; };
        backend.submit_video = [](void *p, const std::uint8_t *, std::size_t, std::uint64_t)
        {
            ++*static_cast<unsigned *>(p);
            return 0;
        };
        backend.submit_audio = [](void *, const std::uint8_t *, std::size_t, std::uint64_t)
        { return 0; };
        backend.disable_audio = [](void *) { return 0; };
        backend.discontinuity = [](void *) { return 0; };
        backend.drain = [](void *) { return 0; };
        backend.close = [](void *) {};
        iptv_stream_session_t session{};
        iptv_stream_init(&session);
        ASSERT_EQ(iptv_stream_open(&session, nullptr, &backend), 0);
        ASSERT_EQ(iptv_stream_start(&session), 0);
        EXPECT_EQ(iptv_stream_push(&session, output.data(), output.size()), 0);
        EXPECT_EQ(iptv_stream_stop(&session), 0);
        EXPECT_EQ(frames, 25u) << file;
        EXPECT_GT(session.telemetry.audio_frames, 40u) << file;
        EXPECT_EQ(iptv_stream_cleanup(&session), 0);
        EXPECT_TRUE(downloads.empty()) << file;
    }
}
TEST(RecordingTransport, CopiesDirectTransportAndReportsEarlyEof)
{
    ptv::RecordingInput input;
    input.url = "https://fixture.test/hls-mpegts/video0.ts";
    std::ifstream file(fixture_prefix + "hls-mpegts/video0.ts", std::ios::binary);
    const std::vector<char> expected{std::istreambuf_iterator<char>(file), {}};
    ASSERT_FALSE(expected.empty());
    std::vector<char> output;
    std::string error;
    const iptv::http::ListSink sink{[](void *p, const char *bytes, std::size_t count)
                                    {
                                        auto &out = *static_cast<std::vector<char> *>(p);
                                        out.insert(out.end(), bytes, bytes + count);
                                        return true;
                                    },
                                    &output};
    EXPECT_LT(ptv::platform::record_stream(input, sink, {}, error), 0);
    EXPECT_EQ(output, expected);
    EXPECT_EQ(error, "The stream ended before the recording finished");
    EXPECT_TRUE(downloads.empty());
}

TEST(RecordingTransport, SinkFailureAndCancellationReleaseEveryRequest)
{
    ptv::RecordingInput input;
    input.url = "https://fixture.test/two-audio.mp4";
    std::string error;
    bool stop = false;
    const iptv::http::RequestControl control{[](void *p) { return *static_cast<bool *>(p); },
                                             &stop};
    const iptv::http::ListSink sink{[](void *p, const char *, std::size_t)
                                    {
                                        *static_cast<bool *>(p) = true;
                                        return false;
                                    },
                                    &stop};
    (void)ptv::platform::record_stream(input, sink, control, error);
    EXPECT_TRUE(stop);
    EXPECT_TRUE(downloads.empty());
    stop = false;
    const iptv::http::ListSink rejecting{[](void *, const char *, std::size_t) { return false; },
                                         nullptr};
    EXPECT_LT(ptv::platform::record_stream(input, rejecting, control, error), 0);
    EXPECT_TRUE(downloads.empty());
}
TEST(RecordingTransport, CredentialsStayOnTheirConfiguredOrigin)
{
    authorizations.clear();
    custom_headers.clear();
    ptv::RecordingInput input;
    input.url = "https://fixture.test/two-audio.mp4";
    input.credential_origin = "https://provider.test";
    input.authorization = "Bearer private";
    input.extra_headers = "X-Key: private";
    std::string error;
    const iptv::http::ListSink sink{[](void *, const char *, std::size_t) { return true; },
                                    nullptr};
    (void)ptv::platform::record_stream(input, sink, {}, error);
    ASSERT_FALSE(authorizations.empty());
    for (const auto &value : authorizations)
        EXPECT_TRUE(value.empty());
    for (const auto &value : custom_headers)
        EXPECT_TRUE(value.empty());
    EXPECT_TRUE(downloads.empty());
    custom_headers.clear();
    input.credential_origin = "https://fixture.test/account";
    (void)ptv::platform::record_stream(input, sink, {}, error);
    ASSERT_FALSE(custom_headers.empty());
    for (const auto &value : custom_headers)
        EXPECT_EQ(value, "X-Key: private");
}
} // namespace
