/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_inflate.h"

#include <array>
#include <initializer_list>

// The decoder follows the structure of Mark Adler's reference "puff": canonical Huffman codes
// decoded a bit at a time. It favours being small and easy to check over speed.
namespace iptv::gzip
{
namespace
{

constexpr unsigned kMaxBits = 15u;
constexpr unsigned kMaxLiteralCodes = 286u;
constexpr unsigned kMaxDistanceCodes = 30u;
constexpr unsigned kFixedLiteralCodes = 288u;
constexpr std::size_t kWindowBytes = 32768u;
constexpr std::size_t kInputBytes = 16384u;
constexpr std::size_t kOutputBytes = 16384u;

std::array<std::uint32_t, 256> MakeCrcTable()
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t index = 0; index < 256u; ++index)
    {
        std::uint32_t value = index;
        for (unsigned bit = 0; bit < 8u; ++bit)
            value = value & 1u ? 0xedb88320u ^ (value >> 1u) : value >> 1u;
        table[index] = value;
    }
    return table;
}

const std::array<std::uint32_t, 256> kCrcTable = MakeCrcTable();

struct Huffman
{
    std::uint16_t count[kMaxBits + 1u]{};
    std::uint16_t symbol[kFixedLiteralCodes]{};
};

class Decoder
{
  public:
    Decoder(ReadFunction read, WriteFunction write, void *context, std::uint64_t max_output)
        : read_(read), write_(write), context_(context), max_output_(max_output)
    {
    }

    Status Run()
    {
        bool first = true;
        for (;;)
        {
            const int magic = Byte();
            if (magic < 0)
                return first ? Status::truncated : status_ == Status::ok ? Status::ok : status_;
            // Bytes after a complete member that do not start another one are padding.
            if (!first && magic != 0x1f)
                return Status::ok;
            first = false;
            if (Status header = Header(magic); header != Status::ok)
                return header;
            crc_ = 0xffffffffu;
            member_bytes_ = 0;
            int last = 0;
            do
            {
                last = static_cast<int>(Bits(1));
                const unsigned type = Bits(2);
                Status block = Status::corrupt;
                if (failed())
                    return status_;
                if (type == 0u)
                    block = Stored();
                else if (type == 1u)
                    block = Fixed();
                else if (type == 2u)
                    block = Dynamic();
                if (block != Status::ok)
                    return block;
            } while (!last);
            if (!Flush())
                return status_;
            // The trailer starts on a byte boundary.
            bit_count_ = 0;
            bit_buffer_ = 0;
            const std::uint32_t crc = Word32();
            const std::uint32_t size = Word32();
            if (failed())
                return status_;
            if (crc != (crc_ ^ 0xffffffffu) ||
                size != static_cast<std::uint32_t>(member_bytes_ & 0xffffffffu))
                return Status::corrupt;
        }
    }

  private:
    bool failed() const
    {
        return status_ != Status::ok;
    }

    int Byte()
    {
        if (input_position_ == input_size_)
        {
            if (input_end_)
                return -1;
            const long read = read_(context_, input_, sizeof(input_));
            if (read < 0)
            {
                status_ = Status::read_failed;
                input_end_ = true;
                return -1;
            }
            if (read == 0)
            {
                input_end_ = true;
                return -1;
            }
            input_size_ = static_cast<std::size_t>(read);
            input_position_ = 0;
        }
        return input_[input_position_++];
    }

    // Reads a byte that must exist; a missing one marks the stream truncated.
    unsigned Required()
    {
        const int value = Byte();
        if (value < 0)
        {
            if (status_ == Status::ok)
                status_ = Status::truncated;
            return 0;
        }
        return static_cast<unsigned>(value);
    }

    unsigned Bits(unsigned needed)
    {
        std::uint32_t value = bit_buffer_;
        while (bit_count_ < needed)
        {
            value |= static_cast<std::uint32_t>(Required()) << bit_count_;
            bit_count_ += 8u;
            if (failed())
                return 0;
        }
        bit_buffer_ = value >> needed;
        bit_count_ -= needed;
        return value & ((1u << needed) - 1u);
    }

    std::uint32_t Word32()
    {
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < 32u; shift += 8u)
            value |= static_cast<std::uint32_t>(Required()) << shift;
        return value;
    }

    Status Header(int magic)
    {
        constexpr unsigned kText = 1u, kHeaderCrc = 2u, kExtra = 4u, kName = 8u, kComment = 16u;
        if (magic != 0x1f || Required() != 0x8bu || Required() != 8u)
            return failed() ? status_ : Status::corrupt;
        const unsigned flags = Required();
        for (unsigned skip = 0; skip < 6u; ++skip) // mtime, extra flags, OS
            Required();
        if (flags & ~(kText | kHeaderCrc | kExtra | kName | kComment))
            return Status::corrupt;
        if (flags & kExtra)
        {
            unsigned length = Required();
            length |= Required() << 8u;
            while (length-- && !failed())
                Required();
        }
        for (const unsigned field : {kName, kComment})
            if (flags & field)
                while (!failed() && Required() != 0u)
                {
                }
        if (flags & kHeaderCrc)
        {
            Required();
            Required();
        }
        return status_;
    }

    bool Emit(unsigned char value)
    {
        if (total_ >= max_output_)
        {
            status_ = Status::too_large;
            return false;
        }
        window_[window_position_] = value;
        window_position_ = (window_position_ + 1u) & (kWindowBytes - 1u);
        output_[output_size_++] = static_cast<char>(value);
        crc_ = kCrcTable[(crc_ ^ value) & 0xffu] ^ (crc_ >> 8u);
        ++total_;
        ++member_bytes_;
        return output_size_ < sizeof(output_) || Flush();
    }

    bool Flush()
    {
        if (output_size_ == 0)
            return true;
        const bool more = write_(context_, output_, output_size_);
        output_size_ = 0;
        if (!more)
            status_ = Status::stopped;
        return more;
    }

    Status Stored()
    {
        bit_buffer_ = 0;
        bit_count_ = 0;
        unsigned length = Required();
        length |= Required() << 8u;
        unsigned complement = Required();
        complement |= Required() << 8u;
        if (failed())
            return status_;
        if (length != (~complement & 0xffffu))
            return Status::corrupt;
        while (length--)
        {
            const unsigned value = Required();
            if (failed() || !Emit(static_cast<unsigned char>(value)))
                return status_;
        }
        return Status::ok;
    }

    int Decode(const Huffman &huffman)
    {
        int code = 0, first = 0, index = 0;
        for (unsigned length = 1; length <= kMaxBits; ++length)
        {
            code |= static_cast<int>(Bits(1));
            if (failed())
                return -1;
            const int count = huffman.count[length];
            if (code - count < first)
                return huffman.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        status_ = Status::corrupt;
        return -1;
    }

    // Builds a canonical code; returns 0 for complete, > 0 for incomplete, < 0 for
    // over-subscribed.
    static int Construct(Huffman *huffman, const std::uint16_t *lengths, unsigned count)
    {
        for (std::uint16_t &value : huffman->count)
            value = 0;
        for (unsigned symbol = 0; symbol < count; ++symbol)
            ++huffman->count[lengths[symbol]];
        if (huffman->count[0] == count)
            return 0;
        int left = 1;
        for (unsigned length = 1; length <= kMaxBits; ++length)
        {
            left <<= 1;
            left -= huffman->count[length];
            if (left < 0)
                return left;
        }
        std::uint16_t offsets[kMaxBits + 1u]{};
        for (unsigned length = 1; length < kMaxBits; ++length)
            offsets[length + 1u] =
                static_cast<std::uint16_t>(offsets[length] + huffman->count[length]);
        for (unsigned symbol = 0; symbol < count; ++symbol)
            if (lengths[symbol])
                huffman->symbol[offsets[lengths[symbol]]++] = static_cast<std::uint16_t>(symbol);
        return left;
    }

    Status Codes(const Huffman &literals, const Huffman &distances)
    {
        static constexpr std::uint16_t kLengthBase[29] = {
            3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
            31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static constexpr std::uint8_t kLengthExtra[29] = {
            0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static constexpr std::uint16_t kDistanceBase[30] = {
            1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
            193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static constexpr std::uint8_t kDistanceExtra[30] = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,
                                                            4, 4, 5,  5,  6,  6,  7,  7,  8,  8,
                                                            9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;)
        {
            int symbol = Decode(literals);
            if (symbol < 0)
                return status_;
            if (symbol < 256)
            {
                if (!Emit(static_cast<unsigned char>(symbol)))
                    return status_;
                continue;
            }
            if (symbol == 256)
                return Status::ok;
            symbol -= 257;
            if (symbol >= 29)
                return Status::corrupt;
            const unsigned length = kLengthBase[symbol] + Bits(kLengthExtra[symbol]);
            if (failed())
                return status_;
            const int distance_symbol = Decode(distances);
            if (distance_symbol < 0)
                return status_;
            if (distance_symbol >= 30)
                return Status::corrupt;
            const unsigned distance =
                kDistanceBase[distance_symbol] + Bits(kDistanceExtra[distance_symbol]);
            if (failed())
                return status_;
            if (distance > member_bytes_ || distance > kWindowBytes)
                return Status::corrupt;
            for (unsigned copied = 0; copied < length; ++copied)
            {
                const unsigned char value =
                    window_[(window_position_ - distance) & (kWindowBytes - 1u)];
                if (!Emit(value))
                    return status_;
            }
        }
    }

    Status Fixed()
    {
        // Rebuilt per block: cheap next to the block itself, and no shared state.
        std::uint16_t lengths[kFixedLiteralCodes]{};
        unsigned symbol = 0;
        for (; symbol < 144u; ++symbol)
            lengths[symbol] = 8;
        for (; symbol < 256u; ++symbol)
            lengths[symbol] = 9;
        for (; symbol < 280u; ++symbol)
            lengths[symbol] = 7;
        for (; symbol < kFixedLiteralCodes; ++symbol)
            lengths[symbol] = 8;
        Huffman literals;
        Huffman distances;
        Construct(&literals, lengths, kFixedLiteralCodes);
        for (symbol = 0; symbol < kMaxDistanceCodes; ++symbol)
            lengths[symbol] = 5;
        Construct(&distances, lengths, kMaxDistanceCodes);
        return Codes(literals, distances);
    }

    Status Dynamic()
    {
        static constexpr std::uint8_t kOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                    11, 4,  12, 3, 13, 2, 14, 1, 15};
        const unsigned literal_count = Bits(5) + 257u;
        const unsigned distance_count = Bits(5) + 1u;
        const unsigned code_count = Bits(4) + 4u;
        if (failed())
            return status_;
        if (literal_count > kMaxLiteralCodes || distance_count > kMaxDistanceCodes)
            return Status::corrupt;
        std::uint16_t lengths[kMaxLiteralCodes + kMaxDistanceCodes]{};
        for (unsigned index = 0; index < code_count; ++index)
            lengths[kOrder[index]] = static_cast<std::uint16_t>(Bits(3));
        if (failed())
            return status_;
        Huffman literals;
        Huffman distances;
        if (Construct(&literals, lengths, 19) != 0)
            return Status::corrupt;
        for (std::uint16_t &length : lengths)
            length = 0;
        unsigned index = 0;
        while (index < literal_count + distance_count)
        {
            int symbol = Decode(literals);
            if (symbol < 0)
                return status_;
            if (symbol < 16)
            {
                lengths[index++] = static_cast<std::uint16_t>(symbol);
                continue;
            }
            std::uint16_t value = 0;
            unsigned repeat = 0;
            if (symbol == 16)
            {
                if (index == 0)
                    return Status::corrupt;
                value = lengths[index - 1u];
                repeat = 3u + Bits(2);
            }
            else if (symbol == 17)
                repeat = 3u + Bits(3);
            else
                repeat = 11u + Bits(7);
            if (failed())
                return status_;
            if (index + repeat > literal_count + distance_count)
                return Status::corrupt;
            while (repeat--)
                lengths[index++] = value;
        }
        if (lengths[256] == 0)
            return Status::corrupt;
        // An incomplete code is only allowed when it has a single one-bit code.
        int result = Construct(&literals, lengths, literal_count);
        if (result < 0 || (result > 0 && literal_count - literals.count[0] != 1u))
            return Status::corrupt;
        result = Construct(&distances, lengths + literal_count, distance_count);
        if (result < 0 || (result > 0 && distance_count - distances.count[0] != 1u))
            return Status::corrupt;
        return Codes(literals, distances);
    }

    ReadFunction read_;
    WriteFunction write_;
    void *context_;
    std::uint64_t max_output_;
    Status status_ = Status::ok;
    unsigned char input_[kInputBytes]{};
    std::size_t input_size_ = 0;
    std::size_t input_position_ = 0;
    bool input_end_ = false;
    std::uint32_t bit_buffer_ = 0;
    unsigned bit_count_ = 0;
    unsigned char window_[kWindowBytes]{};
    std::size_t window_position_ = 0;
    char output_[kOutputBytes]{};
    std::size_t output_size_ = 0;
    std::uint32_t crc_ = 0xffffffffu;
    std::uint64_t total_ = 0;
    std::uint64_t member_bytes_ = 0;
};

} // namespace

bool LooksGzipped(const unsigned char *data, std::size_t size)
{
    return data && size >= 2u && data[0] == 0x1fu && data[1] == 0x8bu;
}

Status Inflate(ReadFunction read, WriteFunction write, void *context, std::uint64_t max_output)
{
    if (!read || !write)
        return Status::corrupt;
    // About 100 KiB of state: kept off the caller's stack.
    auto *decoder = new Decoder(read, write, context, max_output);
    const Status status = decoder->Run();
    delete decoder;
    return status;
}

const char *StatusName(Status status)
{
    switch (status)
    {
    case Status::ok:
        return "ok";
    case Status::read_failed:
        return "download failed";
    case Status::truncated:
        return "compressed guide ended early";
    case Status::corrupt:
        return "compressed guide is damaged";
    case Status::too_large:
        return "guide is too large";
    case Status::stopped:
        return "stopped";
    }
    return "compressed guide is damaged";
}

} // namespace iptv::gzip
