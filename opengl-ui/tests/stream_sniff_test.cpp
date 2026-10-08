// ProsperoTV - The debug trace's reading of addresses and first bytes.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/stream_sniff.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{

std::string kind(const std::string &bytes)
{
    return ptv::describe_bytes(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size());
}

bool begins(const std::string &text, const std::string &with)
{
    return text.compare(0, with.size(), with) == 0;
}

TEST(StreamSniff, AnAddressKeepsItsHostAndLosesWhatIdentifiesItsOwner)
{
    EXPECT_EQ(ptv::redact_address("http://provider.example:8080/live/john/secret/1234.ts"),
              "http://provider.example:8080/<4 parts>.ts");
    EXPECT_EQ(ptv::redact_address("https://cdn.example/a/master.m3u8?token=abcdef&u=john"),
              "https://cdn.example/<2 parts>.m3u8?<19 bytes>");
    EXPECT_EQ(ptv::redact_address("rtsp://admin:hunter2@192.168.1.20:554/Streaming/Channels/101"),
              "rtsp://<user>@192.168.1.20:554/<3 parts>");
    EXPECT_EQ(ptv::redact_address("http://192.168.1.20"), "http://192.168.1.20");
    EXPECT_EQ(ptv::redact_address("http://192.168.1.20/video"), "http://192.168.1.20/<1 part>");
    // Nothing of it is repeated when it is not an address at all.
    const std::string odd = ptv::redact_address("john:secret");
    EXPECT_EQ(odd.find("john"), std::string::npos);
    EXPECT_EQ(odd.find("secret"), std::string::npos);
    for (const char *address :
         {"http://provider.example:8080/live/john/secret/1234.ts",
          "rtsp://admin:hunter2@192.168.1.20:554/Streaming/Channels/101"})
    {
        const std::string shown = ptv::redact_address(address);
        for (const char *secret : {"john", "secret", "hunter2", "admin", "1234", "Streaming"})
            EXPECT_EQ(shown.find(secret), std::string::npos) << shown;
    }
}

TEST(StreamSniff, TheFirstBytesSayWhatAnAnswerIs)
{
    std::string ts(376, '\0');
    ts[0] = ts[188] = 0x47;
    EXPECT_TRUE(begins(kind(ts), "MPEG-TS ["));
    EXPECT_TRUE(begins(kind("\xef\xbb\xbf#EXTM3U\n#EXT-X-VERSION:3\n"), "HLS or M3U playlist"));
    EXPECT_TRUE(begins(kind(std::string("\x1a\x45\xdf\xa3\x01\x00\x00\x00", 8)), "WebM or Matroska"));
    EXPECT_TRUE(begins(kind(std::string("FLV\x01\x05\x00\x00\x00\x09", 9)), "FLV"));
    EXPECT_TRUE(begins(kind(std::string("\x00\x00\x00\x1c" "ftypisom", 12)), "MP4 or fragmented MP4"));
    EXPECT_TRUE(begins(kind("\xff\xd8\xff\xe0JFIF"), "JPEG picture"));
    EXPECT_TRUE(begins(kind("--myboundary\r\nContent-Type: image/jpeg\r\n"), "multipart answer"));
    EXPECT_TRUE(begins(kind(std::string("\x00\x00\x00\x01\x67\x64", 6)), "raw H.264 or HEVC"));
    EXPECT_TRUE(begins(kind("<!DOCTYPE html><html>"), "a web page"));
    EXPECT_TRUE(begins(kind("<?xml version=\"1.0\"?><MPD xmlns=\"urn:mpeg:dash\">"), "DASH manifest"));
    EXPECT_TRUE(begins(kind("{\"error\":\"auth\"}"), "JSON"));
    EXPECT_EQ(kind(""), "nothing");
    EXPECT_TRUE(begins(kind("\x01\x02\x03"), "not recognised [01 02 03]"));
    // One byte, sixteen at most in the hex.
    EXPECT_EQ(kind(std::string(40, 'A')).size(), std::string("not recognised [").size() + 16 * 3);
}

} // namespace
