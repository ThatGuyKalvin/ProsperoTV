/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_inflate.h"

#include "gzip_fixtures.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace
{

using iptv::gzip::Status;

// Feeds the input in small, uneven pieces to exercise every buffer boundary.
struct Pipe
{
    std::vector<unsigned char> input;
    std::size_t position = 0;
    std::size_t piece = 7;
    bool fail_after_input = false;
    std::string output;
    std::size_t stop_after = 0; // stop once this much output arrived (0: never)

    static long Read(void *context, unsigned char *buffer, std::size_t capacity)
    {
        auto *pipe = static_cast<Pipe *>(context);
        if (pipe->position == pipe->input.size())
            return pipe->fail_after_input ? -1 : 0;
        const std::size_t count =
            std::min({capacity, pipe->piece, pipe->input.size() - pipe->position});
        std::copy_n(pipe->input.data() + pipe->position, count, buffer);
        pipe->position += count;
        pipe->piece = pipe->piece % 61u + 13u;
        return static_cast<long>(count);
    }
    static bool Write(void *context, const char *data, std::size_t size)
    {
        auto *pipe = static_cast<Pipe *>(context);
        pipe->output.append(data, size);
        return pipe->stop_after == 0 || pipe->output.size() < pipe->stop_after;
    }
    template <std::size_t N> explicit Pipe(const unsigned char (&data)[N]) : input(data, data + N)
    {
    }
    Status Run(std::uint64_t limit = 1u << 30)
    {
        return iptv::gzip::Inflate(&Pipe::Read, &Pipe::Write, this, limit);
    }
};

std::string SmallText()
{
    std::string text;
    for (int index = 0; index < 3; ++index)
        text +=
            "<tv><programme channel=\"a\"><title>Hello &amp; welcome</title></programme></tv>\n";
    return text;
}

// The same generator the fixture script used.
std::string Generated(int count)
{
    std::uint32_t state = 12345u;
    std::string text;
    char line[160];
    for (int index = 0; index < count; ++index)
    {
        state = (state * 1103515245u + 12345u) & 0x7fffffffu;
        std::snprintf(line, sizeof(line),
                      "<programme start=\"2026100%u%06u +0000\" channel=\"ch%u\"><title>Show "
                      "%u</title></programme>\n",
                      state % 9u, state % 1000000u, state % 97u, state % 5000u);
        text += line;
    }
    return text;
}

TEST(IptvInflateTest, DecodesEveryBlockTypeAndHeaderField)
{
    for (Pipe pipe : {Pipe(kDynamic), Pipe(kStored), Pipe(kFixed), Pipe(kHeaderFields)})
    {
        ASSERT_EQ(pipe.Run(), Status::ok);
        EXPECT_EQ(pipe.output, SmallText());
    }
    EXPECT_EQ(SmallText().size(), kSmallSize);
}

TEST(IptvInflateTest, DecodesALargeStreamWithLongDistances)
{
    Pipe pipe(kLarge);
    ASSERT_EQ(pipe.Run(), Status::ok);
    EXPECT_EQ(pipe.output.size(), kLargeSize);
    EXPECT_EQ(pipe.output, Generated(4000));
}

TEST(IptvInflateTest, HandlesMembersPaddingAndEmptyStreams)
{
    Pipe two(kTwoMembers);
    ASSERT_EQ(two.Run(), Status::ok);
    EXPECT_EQ(two.output, "first,second");
    Pipe padded(kPadded);
    ASSERT_EQ(padded.Run(), Status::ok);
    EXPECT_EQ(padded.output, "padded");
    Pipe empty(kEmpty);
    ASSERT_EQ(empty.Run(), Status::ok);
    EXPECT_TRUE(empty.output.empty());
}

TEST(IptvInflateTest, RejectsDamagedTruncatedAndForeignInput)
{
    Pipe crc(kDynamic);
    crc.input[crc.input.size() - 6u] ^= 0x01u;
    EXPECT_EQ(crc.Run(), Status::corrupt);

    Pipe length(kDynamic);
    length.input[length.input.size() - 1u] ^= 0x01u;
    EXPECT_EQ(length.Run(), Status::corrupt);

    Pipe truncated(kLarge);
    truncated.input.resize(truncated.input.size() / 2u);
    EXPECT_EQ(truncated.Run(), Status::truncated);

    Pipe failing(kLarge);
    failing.input.resize(failing.input.size() / 2u);
    failing.fail_after_input = true;
    EXPECT_EQ(failing.Run(), Status::read_failed);

    const unsigned char xml[] = {'<', 't', 'v', '>'};
    Pipe foreign(xml);
    EXPECT_EQ(foreign.Run(), Status::corrupt);
    EXPECT_FALSE(iptv::gzip::LooksGzipped(xml, sizeof(xml)));
    EXPECT_TRUE(iptv::gzip::LooksGzipped(kDynamic, sizeof(kDynamic)));

    const unsigned char nothing[] = {0};
    Pipe none(nothing);
    none.input.clear();
    EXPECT_EQ(none.Run(), Status::truncated);

    // A reserved block type.
    Pipe reserved(kStored);
    reserved.input[10] = 0x07u; // final block, type 3
    EXPECT_EQ(reserved.Run(), Status::corrupt);
}

TEST(IptvInflateTest, StopsAtTheOutputLimitOrWhenTheSinkAsks)
{
    Pipe limited(kLarge);
    EXPECT_EQ(limited.Run(1000u), Status::too_large);
    EXPECT_LE(limited.output.size(), 1000u);

    Pipe stopped(kLarge);
    stopped.stop_after = 20000u;
    EXPECT_EQ(stopped.Run(), Status::stopped);
    EXPECT_LT(stopped.output.size(), kLargeSize);
    EXPECT_EQ(stopped.output, Generated(4000).substr(0, stopped.output.size()));
}

} // namespace
