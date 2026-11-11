#include "sed/assets/jpeg.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// A baseline / extended sequential Huffman JPEG decoder. The inverse DCT,
// the chroma upsampling and the YCbCr conversion reproduce libjpeg's default
// integer arithmetic (jidctint.c, jdsample.c, jdcolor.c), so images decode
// as libjpeg-based tools decode them.

namespace sed::assets
{
namespace
{

[[noreturn]] void bad(const std::string& what)
{
    throw std::runtime_error("JPEG: " + what);
}

// jpeg_natural_order: the block position of the k-th coefficient in zigzag order.
constexpr std::array<std::uint8_t, 64> kNaturalOrder{ 0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12,
    19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30,
    37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

constexpr int kFastBits = 9;

struct HuffmanTable
{
    bool defined{};
    std::size_t total{};
    std::array<std::uint8_t, 256> values{};
    std::array<std::int32_t, 17> maxCode{};     // largest code of each length, -1 when there is none
    std::array<std::int32_t, 17> valueOffset{}; // values index of a code of each length, minus the code
    // Codes of at most kFastBits bits by their first kFastBits bits:
    // (length << 8) | value, or 0 for a longer code.
    std::array<std::uint16_t, 1u << kFastBits> fast{};
};

// Canonical Huffman codes from the code counts per length (JPEG Annex C),
// refusing tables whose counts do not fit their lengths as libjpeg does.
void buildHuffman(HuffmanTable& table, const std::array<std::uint8_t, 17>& counts, const std::uint8_t* values)
{
    table = HuffmanTable{};
    table.defined = true;
    std::size_t total = 0;
    for ( int length = 1; length <= 16; ++length )
    {
        total += counts[static_cast<std::size_t>(length)];
    }
    if ( total > 256 )
    {
        bad("a Huffman table lists more than 256 codes");
    }
    table.total = total;
    std::copy(values, values + total, table.values.begin());
    std::int32_t code = 0;
    std::int32_t index = 0;
    for ( int length = 1; length <= 16; ++length )
    {
        const auto count = static_cast<std::int32_t>(counts[static_cast<std::size_t>(length)]);
        table.valueOffset[static_cast<std::size_t>(length)] = index - code;
        if ( count > 0 )
        {
            for ( std::int32_t i = 0; i < count; ++i )
            {
                if ( length <= kFastBits )
                {
                    const std::uint32_t first = static_cast<std::uint32_t>(code + i) << (kFastBits - length);
                    const std::uint32_t span = 1u << (kFastBits - length);
                    for ( std::uint32_t j = 0; j < span; ++j )
                    {
                        table.fast[first + j] = static_cast<std::uint16_t>(
                            (length << 8) | table.values[static_cast<std::size_t>(index + i)]);
                    }
                }
            }
            code += count;
            index += count;
            table.maxCode[static_cast<std::size_t>(length)] = code - 1;
        }
        else
        {
            table.maxCode[static_cast<std::size_t>(length)] = -1;
        }
        // No code may be all ones, so the next code must still fit the length.
        if ( code >= (std::int32_t{ 1 } << length) )
        {
            bad("a Huffman table is invalid (its code counts overflow their lengths)");
        }
        code <<= 1;
    }
}

// Entropy-coded data: bits most significant first, 0xFF 0x00 stands for
// 0xFF, and any other 0xFF xx is a marker that ends the data. Bits read
// past the data are zeros and mark the stream as overrun.
class BitReader
{
public:
    BitReader(const std::uint8_t* data, const std::size_t size, const std::size_t position)
        : data_(data), size_(size), pos_(position)
    {
    }

    std::uint32_t peek(const int count)
    {
        if ( bits_ < count )
        {
            fill();
        }
        return static_cast<std::uint32_t>(buffer_ >> (64 - count));
    }

    void skip(const int count) noexcept
    {
        buffer_ <<= count;
        bits_ -= count;
        if ( bits_ < padding_ )
        {
            overrun_ = true;
        }
    }

    std::uint32_t get(const int count)
    {
        if ( count == 0 )
        {
            return 0;
        }
        const std::uint32_t value = peek(count);
        skip(count);
        return value;
    }

    bool overrun() const noexcept
    {
        return overrun_;
    }

    // Moves to the marker that ends the data (skipping any data the decoder
    // did not need) and returns its position: the 0xFF before its code.
    std::size_t markerPosition()
    {
        if ( !atMarker_ )
        {
            while ( pos_ < size_ )
            {
                if ( data_[pos_] == 0xFF && pos_ + 1 < size_ && data_[pos_ + 1] != 0x00 )
                {
                    break;
                }
                pos_ += data_[pos_] == 0xFF ? 2 : 1;
            }
        }
        while ( pos_ + 1 < size_ && data_[pos_ + 1] == 0xFF )
        {
            ++pos_; // fill bytes before the marker code
        }
        return std::min(pos_, size_);
    }

    // At the end of a restart interval: the next marker must be RSTn.
    void restart(const int expected)
    {
        const std::size_t at = markerPosition();
        if ( at + 1 >= size_ || data_[at + 1] != 0xD0 + expected )
        {
            bad("restart marker RST" + std::to_string(expected) + " is missing or out of order (corrupt data)");
        }
        pos_ = at + 2;
        buffer_ = 0;
        bits_ = 0;
        padding_ = 0;
        atMarker_ = false;
    }

private:
    void fill()
    {
        while ( bits_ <= 56 )
        {
            std::uint64_t byte = 0;
            if ( !atMarker_ && pos_ < size_ )
            {
                byte = data_[pos_];
                if ( byte == 0xFF )
                {
                    if ( pos_ + 1 < size_ && data_[pos_ + 1] == 0x00 )
                    {
                        pos_ += 2;
                    }
                    else
                    {
                        atMarker_ = true;
                        byte = 0;
                        padding_ += 8;
                    }
                }
                else
                {
                    ++pos_;
                }
            }
            else
            {
                padding_ += 8;
            }
            buffer_ |= byte << (56 - bits_);
            bits_ += 8;
        }
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_;
    std::uint64_t buffer_{};
    int bits_{};
    int padding_{};
    bool atMarker_{};
    bool overrun_{};
};

int decodeSymbol(BitReader& in, const HuffmanTable& table)
{
    const std::uint16_t entry = table.fast[in.peek(kFastBits)];
    if ( entry != 0 )
    {
        in.skip(entry >> 8);
        return entry & 0xFF;
    }
    const auto code = static_cast<std::int32_t>(in.peek(16));
    for ( int length = kFastBits + 1; length <= 16; ++length )
    {
        const std::int32_t prefix = code >> (16 - length);
        if ( prefix <= table.maxCode[static_cast<std::size_t>(length)] )
        {
            const std::int32_t index = table.valueOffset[static_cast<std::size_t>(length)] + prefix;
            if ( index < 0 || static_cast<std::size_t>(index) >= table.total )
            {
                break;
            }
            in.skip(length);
            return table.values[static_cast<std::size_t>(index)];
        }
    }
    bad("corrupt entropy-coded data (a Huffman code that is not in its table)");
}

std::int32_t extend(const std::uint32_t value, const int size) noexcept
{
    if ( size == 0 )
    {
        return 0;
    }
    return value < (1u << (size - 1)) ? static_cast<std::int32_t>(value) - (std::int32_t{ 1 } << size) + 1
                                      : static_cast<std::int32_t>(value);
}

// jpeg_idct_islow: the accurate integer inverse DCT (8-bit samples,
// CONST_BITS 13, PASS1_BITS 2), in 64-bit arithmetic so no input overflows.
constexpr std::int64_t kFix0_298631336 = 2446;
constexpr std::int64_t kFix0_390180644 = 3196;
constexpr std::int64_t kFix0_541196100 = 4433;
constexpr std::int64_t kFix0_765366865 = 6270;
constexpr std::int64_t kFix0_899976223 = 7373;
constexpr std::int64_t kFix1_175875602 = 9633;
constexpr std::int64_t kFix1_501321110 = 12299;
constexpr std::int64_t kFix1_847759065 = 15137;
constexpr std::int64_t kFix1_961570560 = 16069;
constexpr std::int64_t kFix2_053119869 = 16819;
constexpr std::int64_t kFix2_562915447 = 20995;
constexpr std::int64_t kFix3_072711026 = 25172;
constexpr int kConstBits = 13;
constexpr int kPass1Bits = 2;

constexpr std::int64_t descale(const std::int64_t x, const int n) noexcept
{
    return (x + (std::int64_t{ 1 } << (n - 1))) >> n;
}

std::uint8_t rangeLimit(const std::int64_t value) noexcept
{
    return static_cast<std::uint8_t>(std::clamp<std::int64_t>(value + 128, 0, 255));
}

// `in`: dequantized coefficients in natural order. Writes 8x8 samples.
void inverseDct(const std::array<std::int64_t, 64>& in, std::uint8_t* out, const std::size_t stride)
{
    std::array<std::int64_t, 64> work{};
    for ( int column = 0; column < 8; ++column )
    {
        const auto at = [&](const int row) { return in[static_cast<std::size_t>(row * 8 + column)]; };
        if ( at(1) == 0 && at(2) == 0 && at(3) == 0 && at(4) == 0 && at(5) == 0 && at(6) == 0 && at(7) == 0 )
        {
            const std::int64_t dc = at(0) * (std::int64_t{ 1 } << kPass1Bits);
            for ( int row = 0; row < 8; ++row )
            {
                work[static_cast<std::size_t>(row * 8 + column)] = dc;
            }
            continue;
        }
        std::int64_t z2 = at(2);
        std::int64_t z3 = at(6);
        std::int64_t z1 = (z2 + z3) * kFix0_541196100;
        const std::int64_t tmp2e = z1 + z3 * -kFix1_847759065;
        const std::int64_t tmp3e = z1 + z2 * kFix0_765366865;
        z2 = at(0);
        z3 = at(4);
        const std::int64_t tmp0e = (z2 + z3) * (std::int64_t{ 1 } << kConstBits);
        const std::int64_t tmp1e = (z2 - z3) * (std::int64_t{ 1 } << kConstBits);
        const std::int64_t tmp10 = tmp0e + tmp3e;
        const std::int64_t tmp13 = tmp0e - tmp3e;
        const std::int64_t tmp11 = tmp1e + tmp2e;
        const std::int64_t tmp12 = tmp1e - tmp2e;

        std::int64_t tmp0 = at(7);
        std::int64_t tmp1 = at(5);
        std::int64_t tmp2 = at(3);
        std::int64_t tmp3 = at(1);
        z1 = tmp0 + tmp3;
        z2 = tmp1 + tmp2;
        z3 = tmp0 + tmp2;
        std::int64_t z4 = tmp1 + tmp3;
        const std::int64_t z5 = (z3 + z4) * kFix1_175875602;
        tmp0 = tmp0 * kFix0_298631336;
        tmp1 = tmp1 * kFix2_053119869;
        tmp2 = tmp2 * kFix3_072711026;
        tmp3 = tmp3 * kFix1_501321110;
        z1 = z1 * -kFix0_899976223;
        z2 = z2 * -kFix2_562915447;
        z3 = z3 * -kFix1_961570560;
        z4 = z4 * -kFix0_390180644;
        z3 += z5;
        z4 += z5;
        tmp0 += z1 + z3;
        tmp1 += z2 + z4;
        tmp2 += z2 + z3;
        tmp3 += z1 + z4;

        constexpr int shift = kConstBits - kPass1Bits;
        const auto put = [&](const int row, const std::int64_t value) {
            work[static_cast<std::size_t>(row * 8 + column)] = descale(value, shift);
        };
        put(0, tmp10 + tmp3);
        put(7, tmp10 - tmp3);
        put(1, tmp11 + tmp2);
        put(6, tmp11 - tmp2);
        put(2, tmp12 + tmp1);
        put(5, tmp12 - tmp1);
        put(3, tmp13 + tmp0);
        put(4, tmp13 - tmp0);
    }
    for ( int row = 0; row < 8; ++row )
    {
        const std::int64_t* w = work.data() + row * 8;
        std::uint8_t* o = out + static_cast<std::size_t>(row) * stride;
        if ( w[1] == 0 && w[2] == 0 && w[3] == 0 && w[4] == 0 && w[5] == 0 && w[6] == 0 && w[7] == 0 )
        {
            const std::uint8_t dc = rangeLimit(descale(w[0], kPass1Bits + 3));
            std::fill(o, o + 8, dc);
            continue;
        }
        std::int64_t z2 = w[2];
        std::int64_t z3 = w[6];
        std::int64_t z1 = (z2 + z3) * kFix0_541196100;
        const std::int64_t tmp2e = z1 + z3 * -kFix1_847759065;
        const std::int64_t tmp3e = z1 + z2 * kFix0_765366865;
        const std::int64_t tmp0e = (w[0] + w[4]) * (std::int64_t{ 1 } << kConstBits);
        const std::int64_t tmp1e = (w[0] - w[4]) * (std::int64_t{ 1 } << kConstBits);
        const std::int64_t tmp10 = tmp0e + tmp3e;
        const std::int64_t tmp13 = tmp0e - tmp3e;
        const std::int64_t tmp11 = tmp1e + tmp2e;
        const std::int64_t tmp12 = tmp1e - tmp2e;

        std::int64_t tmp0 = w[7];
        std::int64_t tmp1 = w[5];
        std::int64_t tmp2 = w[3];
        std::int64_t tmp3 = w[1];
        z1 = tmp0 + tmp3;
        z2 = tmp1 + tmp2;
        z3 = tmp0 + tmp2;
        std::int64_t z4 = tmp1 + tmp3;
        const std::int64_t z5 = (z3 + z4) * kFix1_175875602;
        tmp0 = tmp0 * kFix0_298631336;
        tmp1 = tmp1 * kFix2_053119869;
        tmp2 = tmp2 * kFix3_072711026;
        tmp3 = tmp3 * kFix1_501321110;
        z1 = z1 * -kFix0_899976223;
        z2 = z2 * -kFix2_562915447;
        z3 = z3 * -kFix1_961570560;
        z4 = z4 * -kFix0_390180644;
        z3 += z5;
        z4 += z5;
        tmp0 += z1 + z3;
        tmp1 += z2 + z4;
        tmp2 += z2 + z3;
        tmp3 += z1 + z4;

        constexpr int shift = kConstBits + kPass1Bits + 3;
        o[0] = rangeLimit(descale(tmp10 + tmp3, shift));
        o[7] = rangeLimit(descale(tmp10 - tmp3, shift));
        o[1] = rangeLimit(descale(tmp11 + tmp2, shift));
        o[6] = rangeLimit(descale(tmp11 - tmp2, shift));
        o[2] = rangeLimit(descale(tmp12 + tmp1, shift));
        o[5] = rangeLimit(descale(tmp12 - tmp1, shift));
        o[3] = rangeLimit(descale(tmp13 + tmp0, shift));
        o[4] = rangeLimit(descale(tmp13 - tmp0, shift));
    }
}

struct Component
{
    std::uint8_t id{};
    int h{ 1 };
    int v{ 1 };
    std::size_t quantTable{};
    // Samples the image needs (libjpeg's downsampled size) and the block
    // grid holding them, padded to whole MCUs.
    std::size_t width{};
    std::size_t height{};
    std::size_t stride{};
    std::size_t rows{};
    std::vector<std::uint8_t> samples{};
    bool decoded{};
};

struct Frame
{
    bool present{};
    std::uint8_t marker{};
    int precision{};
    std::size_t width{};
    std::size_t height{};
    int hMax{ 1 };
    int vMax{ 1 };
    std::size_t mcusX{};
    std::size_t mcusY{};
    std::vector<Component> components{};
};

std::uint16_t readBe16(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

bool isFrameMarker(const std::uint8_t code) noexcept
{
    return code >= 0xC0 && code <= 0xCF && code != 0xC4 && code != 0xC8 && code != 0xCC;
}

// The frame types this decoder does not read, with the reason.
const char* unsupportedFrame(const std::uint8_t code) noexcept
{
    switch ( code )
    {
    case 0xC2:
        return "progressive JPEGs are not supported; save it as a baseline (non-progressive) JPEG";
    case 0xC3:
        return "lossless JPEGs are not supported; save it as a baseline JPEG";
    case 0xC5:
    case 0xC6:
    case 0xC7:
        return "hierarchical (differential) JPEGs are not supported; save it as a baseline JPEG";
    case 0xC9:
    case 0xCA:
    case 0xCB:
    case 0xCD:
    case 0xCE:
    case 0xCF:
        return "arithmetic-coded JPEGs are not supported; save it with Huffman coding (a baseline JPEG)";
    default:
        return nullptr;
    }
}

class JpegDecoder
{
public:
    explicit JpegDecoder(const std::span<const std::byte> bytes)
        : data_(reinterpret_cast<const std::uint8_t*>(bytes.data())), size_(bytes.size())
    {
    }

    // Walks the markers. With infoOnly it stops at the frame header.
    void run(const bool infoOnly)
    {
        if ( size_ < 3 || data_[0] != 0xFF || data_[1] != 0xD8 || data_[2] != 0xFF )
        {
            bad("not a JPEG file (no start-of-image marker)");
        }
        std::size_t pos = 2;
        for ( ;; )
        {
            if ( pos >= size_ )
            {
                if ( infoOnly || !frame_.present )
                {
                    bad("the file ends before the image data (truncated)");
                }
                break; // no end-of-image marker; every scan was complete
            }
            if ( data_[pos] != 0xFF )
            {
                bad("expected a marker at byte " + std::to_string(pos) + " (corrupt data)");
            }
            while ( pos < size_ && data_[pos] == 0xFF )
            {
                ++pos;
            }
            if ( pos >= size_ )
            {
                bad("the file ends inside a marker (truncated)");
            }
            const std::uint8_t code = data_[pos++];
            if ( code == 0xD9 )
            {
                break; // end of image
            }
            if ( (code >= 0xD0 && code <= 0xD7) || code == 0x01 )
            {
                continue; // markers without a segment
            }
            if ( code == 0xD8 )
            {
                bad("a second start-of-image marker (corrupt data)");
            }
            if ( pos + 2 > size_ )
            {
                bad("the file ends inside a marker segment (truncated)");
            }
            const std::size_t length = readBe16(data_ + pos);
            if ( length < 2 || pos + length > size_ )
            {
                bad("a marker segment runs past the end of the file (truncated)");
            }
            const std::uint8_t* segment = data_ + pos + 2;
            const std::size_t segmentSize = length - 2;
            pos += length;
            if ( isFrameMarker(code) )
            {
                readFrame(code, segment, segmentSize);
                if ( infoOnly )
                {
                    return;
                }
                if ( const char* reason = unsupportedFrame(code) )
                {
                    bad(reason);
                }
                if ( frame_.precision != 8 )
                {
                    bad(std::to_string(frame_.precision) + "-bit samples are not supported; save it with 8-bit samples");
                }
                checkFrame();
                continue;
            }
            switch ( code )
            {
            case 0xC4:
                readHuffmanTables(segment, segmentSize);
                break;
            case 0xDB:
                readQuantTables(segment, segmentSize);
                break;
            case 0xDD:
                if ( segmentSize != 2 )
                {
                    bad("invalid restart interval segment");
                }
                restartInterval_ = readBe16(segment);
                break;
            case 0xDA:
                if ( infoOnly )
                {
                    bad("image data before the frame header");
                }
                pos = readScan(segment, segmentSize, pos);
                break;
            case 0xDC:
                bad("an image height given by a DNL marker is not supported");
            case 0xC8:
            case 0xCC:
                bad("arithmetic-coded or reserved JPEG markers are not supported; save it as a baseline JPEG");
            case 0xDE:
            case 0xDF:
                bad("hierarchical JPEGs are not supported; save it as a baseline JPEG");
            default:
                if ( code == 0xE0 && segmentSize >= 5 && std::equal(segment, segment + 5, "JFIF") )
                {
                    sawJfif_ = true;
                }
                if ( code == 0xEE && segmentSize >= 12 && std::equal(segment, segment + 5, "Adobe") )
                {
                    sawAdobe_ = true;
                    adobeTransform_ = segment[11];
                }
                if ( code < 0xE0 && code != 0xFE && !(code >= 0xF0 && code <= 0xFD) )
                {
                    constexpr char kHex[] = "0123456789ABCDEF";
                    bad(std::string("unknown marker 0xFF") + kHex[code >> 4] + kHex[code & 15] + " (corrupt data)");
                }
                break; // APPn, COM, JPGn
            }
        }
        if ( !frame_.present )
        {
            bad("no frame header (not an image)");
        }
        for ( const Component& component : frame_.components )
        {
            if ( !component.decoded )
            {
                bad("the image data for component " + std::to_string(component.id) + " is missing (truncated)");
            }
        }
    }

    JpegInfo info() const
    {
        JpegInfo info{};
        info.width = static_cast<std::int32_t>(frame_.width);
        info.height = static_cast<std::int32_t>(frame_.height);
        info.components = static_cast<std::int32_t>(frame_.components.size());
        info.precision = frame_.precision;
        const std::uint8_t m = frame_.marker;
        info.progressive = m == 0xC2 || m == 0xC6 || m == 0xCA || m == 0xCE;
        info.lossless = m == 0xC3 || m == 0xC7 || m == 0xCB || m == 0xCF;
        info.arithmetic = m >= 0xC9;
        return info;
    }

    RgbaImage image() const
    {
        RgbaImage out{};
        out.width = static_cast<std::int32_t>(frame_.width);
        out.height = static_cast<std::int32_t>(frame_.height);
        out.rgba.resize(frame_.width * frame_.height * 4u);
        const std::size_t count = frame_.components.size();
        std::vector<std::vector<std::uint8_t>> rows(count, std::vector<std::uint8_t>(frame_.width + 2));
        std::vector<std::int32_t> colsum(frame_.width + 2);
        const bool rgb = count == 3 && isRgb();
        const ColourTables& tables = colourTables();
        for ( std::size_t y = 0; y < frame_.height; ++y )
        {
            for ( std::size_t c = 0; c < count; ++c )
            {
                upsampleRow(frame_.components[c], y, rows[c], colsum);
            }
            std::uint8_t* px = out.rgba.data() + y * frame_.width * 4u;
            for ( std::size_t x = 0; x < frame_.width; ++x, px += 4 )
            {
                if ( count == 1 )
                {
                    px[0] = px[1] = px[2] = rows[0][x];
                }
                else if ( rgb )
                {
                    px[0] = rows[0][x];
                    px[1] = rows[1][x];
                    px[2] = rows[2][x];
                }
                else
                {
                    const int luma = rows[0][x];
                    const std::uint8_t cb = rows[1][x];
                    const std::uint8_t cr = rows[2][x];
                    px[0] = clampSample(luma + tables.crR[cr]);
                    px[1] = clampSample(luma + static_cast<int>((tables.cbG[cb] + tables.crG[cr]) >> 16));
                    px[2] = clampSample(luma + tables.cbB[cb]);
                }
                px[3] = 255;
            }
        }
        return out;
    }

private:
    struct ColourTables
    {
        std::array<int, 256> crR{};
        std::array<int, 256> cbB{};
        std::array<std::int64_t, 256> crG{};
        std::array<std::int64_t, 256> cbG{};
    };

    // jdcolor.c build_ycc_rgb_table: 16-bit fixed point, rounded.
    static const ColourTables& colourTables()
    {
        static const ColourTables tables = []
        {
            constexpr int kScaleBits = 16;
            constexpr std::int64_t kHalf = std::int64_t{ 1 } << (kScaleBits - 1);
            const auto fix = [](const double x) { return static_cast<std::int64_t>(x * 65536.0 + 0.5); };
            ColourTables t{};
            for ( int i = 0; i < 256; ++i )
            {
                const std::int64_t x = i - 128;
                t.crR[static_cast<std::size_t>(i)] = static_cast<int>((fix(1.40200) * x + kHalf) >> kScaleBits);
                t.cbB[static_cast<std::size_t>(i)] = static_cast<int>((fix(1.77200) * x + kHalf) >> kScaleBits);
                t.crG[static_cast<std::size_t>(i)] = -fix(0.71414) * x;
                t.cbG[static_cast<std::size_t>(i)] = -fix(0.34414) * x + kHalf;
            }
            return t;
        }();
        return tables;
    }

    static std::uint8_t clampSample(const int value) noexcept
    {
        return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
    }

    // libjpeg's choice: JFIF means YCbCr; otherwise an Adobe marker's
    // transform 0, or component ids 'R', 'G', 'B', mean RGB.
    bool isRgb() const noexcept
    {
        if ( sawJfif_ )
        {
            return false;
        }
        if ( sawAdobe_ )
        {
            return adobeTransform_ == 0;
        }
        return frame_.components[0].id == 'R' && frame_.components[1].id == 'G' && frame_.components[2].id == 'B';
    }

    void readFrame(const std::uint8_t code, const std::uint8_t* s, const std::size_t size)
    {
        if ( frame_.present )
        {
            bad("a second frame header (corrupt data)");
        }
        if ( size < 6 )
        {
            bad("the frame header is cut short");
        }
        frame_.present = true;
        frame_.marker = code;
        frame_.precision = s[0];
        frame_.height = readBe16(s + 1);
        frame_.width = readBe16(s + 3);
        const std::size_t count = s[5];
        if ( size != 6 + 3 * count )
        {
            bad("the frame header has the wrong length");
        }
        for ( std::size_t i = 0; i < count; ++i )
        {
            Component component{};
            component.id = s[6 + 3 * i];
            component.h = s[7 + 3 * i] >> 4;
            component.v = s[7 + 3 * i] & 15;
            component.quantTable = s[8 + 3 * i];
            frame_.components.push_back(component);
        }
    }

    void checkFrame()
    {
        if ( frame_.height == 0 )
        {
            bad("an image height given by a DNL marker is not supported");
        }
        if ( frame_.width == 0 || frame_.width > static_cast<std::size_t>(kMaxDecodedJpegDimension)
            || frame_.height > static_cast<std::size_t>(kMaxDecodedJpegDimension) )
        {
            bad("image dimensions are outside 1.." + std::to_string(kMaxDecodedJpegDimension));
        }
        const std::size_t count = frame_.components.size();
        if ( count != 1 && count != 3 )
        {
            bad(std::to_string(count) + "-component JPEGs (such as CMYK) are not supported; save it as RGB or greyscale");
        }
        for ( std::size_t i = 0; i < count; ++i )
        {
            const Component& c = frame_.components[i];
            if ( c.h < 1 || c.h > 4 || c.v < 1 || c.v > 4 || c.quantTable > 3 )
            {
                bad("invalid sampling factors or quantization table in the frame header");
            }
            for ( std::size_t j = 0; j < i; ++j )
            {
                if ( frame_.components[j].id == c.id )
                {
                    bad("two components share one id (corrupt data)");
                }
            }
            frame_.hMax = std::max(frame_.hMax, c.h);
            frame_.vMax = std::max(frame_.vMax, c.v);
        }
        for ( const Component& c : frame_.components )
        {
            if ( count > 1 && (frame_.hMax % c.h != 0 || frame_.vMax % c.v != 0) )
            {
                bad("chroma sampling factors whose ratios are not whole numbers are not supported");
            }
        }
        frame_.mcusX = (frame_.width + 8u * static_cast<std::size_t>(frame_.hMax) - 1u)
            / (8u * static_cast<std::size_t>(frame_.hMax));
        frame_.mcusY = (frame_.height + 8u * static_cast<std::size_t>(frame_.vMax) - 1u)
            / (8u * static_cast<std::size_t>(frame_.vMax));
        for ( Component& c : frame_.components )
        {
            if ( count == 1 )
            {
                c.h = c.v = 1;
                frame_.hMax = frame_.vMax = 1;
                c.width = frame_.width;
                c.height = frame_.height;
                c.stride = (frame_.width + 7u) / 8u * 8u;
                c.rows = (frame_.height + 7u) / 8u * 8u;
            }
            else
            {
                c.width = (frame_.width * static_cast<std::size_t>(c.h) + static_cast<std::size_t>(frame_.hMax) - 1u)
                    / static_cast<std::size_t>(frame_.hMax);
                c.height = (frame_.height * static_cast<std::size_t>(c.v) + static_cast<std::size_t>(frame_.vMax) - 1u)
                    / static_cast<std::size_t>(frame_.vMax);
                c.stride = frame_.mcusX * static_cast<std::size_t>(c.h) * 8u;
                c.rows = frame_.mcusY * static_cast<std::size_t>(c.v) * 8u;
            }
            c.samples.assign(c.stride * c.rows, 0);
        }
    }

    void readHuffmanTables(const std::uint8_t* s, const std::size_t size)
    {
        std::size_t at = 0;
        while ( at < size )
        {
            if ( size - at < 17 )
            {
                bad("a Huffman table segment is cut short");
            }
            const std::uint8_t kind = s[at] >> 4;
            const std::uint8_t id = s[at] & 15;
            if ( kind > 1 || id > 3 )
            {
                bad("invalid Huffman table class or id");
            }
            std::array<std::uint8_t, 17> counts{};
            std::size_t total = 0;
            for ( std::size_t length = 1; length <= 16; ++length )
            {
                counts[length] = s[at + length];
                total += counts[length];
            }
            at += 17;
            if ( total > 256 || size - at < total )
            {
                bad("a Huffman table segment is cut short");
            }
            buildHuffman(kind == 0 ? dcTables_[id] : acTables_[id], counts, s + at);
            at += total;
        }
    }

    void readQuantTables(const std::uint8_t* s, const std::size_t size)
    {
        std::size_t at = 0;
        while ( at < size )
        {
            const std::uint8_t precision = s[at] >> 4;
            const std::uint8_t id = s[at] & 15;
            if ( precision > 1 || id > 3 )
            {
                bad("invalid quantization table precision or id");
            }
            const std::size_t bytes = precision == 0 ? 64u : 128u;
            if ( size - at - 1 < bytes )
            {
                bad("a quantization table segment is cut short");
            }
            for ( std::size_t k = 0; k < 64; ++k )
            {
                quant_[id][k] = precision == 0 ? s[at + 1 + k] : readBe16(s + at + 1 + 2 * k);
            }
            quantDefined_[id] = true;
            at += 1 + bytes;
        }
    }

    struct ScanComponent
    {
        Component* component{};
        const HuffmanTable* dc{};
        const HuffmanTable* ac{};
        const std::array<std::int32_t, 64>* quant{};
        std::int64_t predictor{};
    };

    // Decodes one scan's entropy-coded data starting at `pos`; returns the
    // position of the marker after it.
    std::size_t readScan(const std::uint8_t* s, const std::size_t size, const std::size_t pos)
    {
        if ( !frame_.present )
        {
            bad("image data before the frame header");
        }
        if ( size < 1 )
        {
            bad("the scan header is cut short");
        }
        const std::size_t count = s[0];
        if ( count < 1 || count > frame_.components.size() || size != 4 + 2 * count )
        {
            bad("the scan header is invalid");
        }
        std::vector<ScanComponent> scan{};
        int blocksPerMcu = 0;
        for ( std::size_t i = 0; i < count; ++i )
        {
            const std::uint8_t id = s[1 + 2 * i];
            const std::uint8_t tables = s[2 + 2 * i];
            const auto found = std::find_if(frame_.components.begin(), frame_.components.end(),
                [id](const Component& c) { return c.id == id; });
            if ( found == frame_.components.end() )
            {
                bad("a scan names a component the frame does not have");
            }
            if ( found->decoded )
            {
                bad("a component is coded in two scans (corrupt data)");
            }
            const std::size_t dc = tables >> 4;
            const std::size_t ac = tables & 15;
            if ( dc > 3 || ac > 3 || !dcTables_[dc].defined || !acTables_[ac].defined )
            {
                bad("a scan uses a Huffman table that was not defined");
            }
            if ( !quantDefined_[found->quantTable] )
            {
                bad("a component uses a quantization table that was not defined");
            }
            scan.push_back({ &*found, &dcTables_[dc], &acTables_[ac], &quant_[found->quantTable], 0 });
            blocksPerMcu += found->h * found->v;
        }
        const std::uint8_t ss = s[1 + 2 * count];
        const std::uint8_t se = s[2 + 2 * count];
        const std::uint8_t a = s[3 + 2 * count];
        if ( ss != 0 || se != 63 || a != 0 )
        {
            bad("the scan is not sequential (spectral selection or approximation is set)");
        }
        if ( count > 1 && blocksPerMcu > 10 )
        {
            bad("an MCU holds more than 10 blocks (corrupt data)");
        }

        BitReader in(data_, size_, pos);
        std::array<std::int64_t, 64> coefficients{};
        std::size_t blocksX = frame_.mcusX;
        std::size_t blocksY = frame_.mcusY;
        if ( count == 1 )
        {
            blocksX = (scan[0].component->width + 7u) / 8u;
            blocksY = (scan[0].component->height + 7u) / 8u;
        }
        const std::size_t mcus = blocksX * blocksY;
        int nextRestart = 0;
        for ( std::size_t mcu = 0; mcu < mcus; ++mcu )
        {
            if ( restartInterval_ != 0 && mcu != 0 && mcu % restartInterval_ == 0 )
            {
                in.restart(nextRestart);
                nextRestart = (nextRestart + 1) & 7;
                for ( ScanComponent& sc : scan )
                {
                    sc.predictor = 0;
                }
            }
            const std::size_t mx = mcu % blocksX;
            const std::size_t my = mcu / blocksX;
            for ( ScanComponent& sc : scan )
            {
                Component& c = *sc.component;
                const int bh = count == 1 ? 1 : c.h;
                const int bv = count == 1 ? 1 : c.v;
                for ( int by = 0; by < bv; ++by )
                {
                    for ( int bx = 0; bx < bh; ++bx )
                    {
                        decodeBlock(in, sc, coefficients);
                        const std::size_t x = (mx * static_cast<std::size_t>(bh) + static_cast<std::size_t>(bx)) * 8u;
                        const std::size_t y = (my * static_cast<std::size_t>(bv) + static_cast<std::size_t>(by)) * 8u;
                        inverseDct(coefficients, c.samples.data() + y * c.stride + x, c.stride);
                    }
                }
            }
            if ( in.overrun() )
            {
                bad("the image data ends early (the file is truncated or corrupt)");
            }
        }
        for ( ScanComponent& sc : scan )
        {
            sc.component->decoded = true;
        }
        return in.markerPosition();
    }

    static void decodeBlock(BitReader& in, ScanComponent& sc, std::array<std::int64_t, 64>& coefficients)
    {
        coefficients.fill(0);
        const std::array<std::int32_t, 64>& quant = *sc.quant;
        const int size = decodeSymbol(in, *sc.dc);
        if ( size > 16 )
        {
            bad("corrupt entropy-coded data (a DC difference that is too large)");
        }
        sc.predictor += extend(in.get(size), size);
        coefficients[0] = sc.predictor * quant[0];
        for ( int k = 1; k < 64; )
        {
            const int rs = decodeSymbol(in, *sc.ac);
            const int run = rs >> 4;
            const int bits = rs & 15;
            if ( bits == 0 )
            {
                if ( run != 15 )
                {
                    break; // end of block
                }
                k += 16;
                continue;
            }
            k += run;
            if ( k > 63 )
            {
                bad("corrupt entropy-coded data (coefficients past the end of a block)");
            }
            coefficients[kNaturalOrder[static_cast<std::size_t>(k)]] =
                static_cast<std::int64_t>(extend(in.get(bits), bits)) * quant[static_cast<std::size_t>(k)];
            ++k;
        }
    }

    // One output row of a component at full size (jdsample.c with fancy
    // upsampling): h2v1, h1v2 and h2v2 use the triangle filter with
    // libjpeg's rounding, other whole ratios replicate samples. Rows and
    // columns past the component's samples repeat its edge.
    void upsampleRow(const Component& c, const std::size_t y, std::vector<std::uint8_t>& out,
        std::vector<std::int32_t>& colsum) const
    {
        const std::size_t width = frame_.width;
        const int rh = frame_.hMax / c.h;
        const int rv = frame_.vMax / c.v;
        const auto row = [&c](const std::size_t r) { return c.samples.data() + std::min(r, c.height - 1) * c.stride; };
        if ( rh == 1 && rv == 1 )
        {
            std::copy(row(y), row(y) + width, out.begin());
            return;
        }
        const bool fancyWide = c.width > 2;
        if ( rh == 2 && rv == 1 && fancyWide )
        {
            const std::uint8_t* in = row(y);
            for ( std::size_t col = 0; col < c.width; ++col )
            {
                const int here = in[col] * 3;
                const int left = in[col == 0 ? 0 : col - 1];
                const int right = in[col + 1 < c.width ? col + 1 : col];
                if ( 2 * col < width )
                {
                    out[2 * col] = static_cast<std::uint8_t>((here + left + 1) >> 2);
                }
                if ( 2 * col + 1 < width )
                {
                    out[2 * col + 1] = static_cast<std::uint8_t>((here + right + 2) >> 2);
                }
            }
            return;
        }
        if ( rh == 1 && rv == 2 )
        {
            const std::size_t r = y / 2;
            const bool below = y % 2 == 1;
            const std::uint8_t* near = row(r);
            const std::uint8_t* far = below ? row(r + 1) : row(r == 0 ? 0 : r - 1);
            const int bias = below ? 2 : 1;
            for ( std::size_t x = 0; x < width; ++x )
            {
                out[x] = static_cast<std::uint8_t>((near[x] * 3 + far[x] + bias) >> 2);
            }
            return;
        }
        if ( rh == 2 && rv == 2 && fancyWide )
        {
            const std::size_t r = y / 2;
            const bool below = y % 2 == 1;
            const std::uint8_t* near = row(r);
            const std::uint8_t* far = below ? row(r + 1) : row(r == 0 ? 0 : r - 1);
            for ( std::size_t col = 0; col < c.width; ++col )
            {
                colsum[col] = near[col] * 3 + far[col];
            }
            for ( std::size_t col = 0; col < c.width; ++col )
            {
                const std::int32_t here = colsum[col] * 3;
                const std::int32_t left = colsum[col == 0 ? 0 : col - 1];
                const std::int32_t right = colsum[col + 1 < c.width ? col + 1 : col];
                if ( 2 * col < width )
                {
                    out[2 * col] = static_cast<std::uint8_t>((here + left + 8) >> 4);
                }
                if ( 2 * col + 1 < width )
                {
                    out[2 * col + 1] = static_cast<std::uint8_t>((here + right + 7) >> 4);
                }
            }
            return;
        }
        const std::uint8_t* in = row(y / static_cast<std::size_t>(rv));
        for ( std::size_t x = 0; x < width; ++x )
        {
            out[x] = in[x / static_cast<std::size_t>(rh)];
        }
    }

    const std::uint8_t* data_;
    std::size_t size_;
    Frame frame_{};
    std::array<HuffmanTable, 4> dcTables_{};
    std::array<HuffmanTable, 4> acTables_{};
    std::array<std::array<std::int32_t, 64>, 4> quant_{};
    std::array<bool, 4> quantDefined_{};
    std::size_t restartInterval_{};
    bool sawJfif_{};
    bool sawAdobe_{};
    std::uint8_t adobeTransform_{};
};

} // namespace

bool isJpeg(const std::span<const std::byte> bytes) noexcept
{
    return bytes.size() >= 3 && bytes[0] == std::byte{ 0xFF } && bytes[1] == std::byte{ 0xD8 }
        && bytes[2] == std::byte{ 0xFF };
}

JpegInfo readJpegInfo(const std::span<const std::byte> bytes)
{
    JpegDecoder decoder(bytes);
    decoder.run(true);
    return decoder.info();
}

RgbaImage decodeJpeg(const std::span<const std::byte> bytes)
{
    JpegDecoder decoder(bytes);
    decoder.run(false);
    return decoder.image();
}

} // namespace sed::assets
