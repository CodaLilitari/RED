#include "sed/assets/png.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace sed::assets
{
namespace
{

[[noreturn]] void bad(const std::string& what)
{
    throw std::runtime_error("PNG: " + what);
}

class BitReader
{
public:
    explicit BitReader(std::span<const std::uint8_t> data) : data_(data) {}

    std::uint32_t bits(const int count)
    {
        while (bitCount_ < count)
        {
            if (pos_ >= data_.size()) bad("compressed data is truncated");
            buffer_ |= static_cast<std::uint32_t>(data_[pos_++]) << bitCount_;
            bitCount_ += 8;
        }
        const std::uint32_t value = count == 0 ? 0u : (buffer_ & ((1u << count) - 1u));
        buffer_ >>= count;
        bitCount_ -= count;
        return value;
    }

    void alignToByte()
    {
        buffer_ = 0;
        bitCount_ = 0;
    }

    std::size_t position() const noexcept { return pos_; }
    std::span<const std::uint8_t> data() const noexcept { return data_; }
    void skip(const std::size_t count) { pos_ += count; }

private:
    std::span<const std::uint8_t> data_;
    std::size_t pos_{};
    std::uint32_t buffer_{};
    int bitCount_{};
};

// Canonical Huffman table: counts per length and symbols sorted by code.
struct Huffman
{
    std::array<std::uint16_t, 16> counts{};
    std::vector<std::uint16_t> symbols;
};

Huffman buildHuffman(const std::uint8_t* lengths, const std::size_t count)
{
    Huffman h;
    h.symbols.resize(count);
    for (std::size_t i = 0; i < count; ++i) ++h.counts[lengths[i]];
    h.counts[0] = 0;
    int left = 1;
    for (int len = 1; len < 16; ++len)
    {
        left <<= 1;
        left -= h.counts[static_cast<std::size_t>(len)];
        if (left < 0) bad("over-subscribed Huffman code");
    }
    std::array<std::uint16_t, 16> offsets{};
    for (int len = 1; len < 15; ++len)
        offsets[static_cast<std::size_t>(len + 1)] = static_cast<std::uint16_t>(
            offsets[static_cast<std::size_t>(len)] + h.counts[static_cast<std::size_t>(len)]);
    for (std::size_t i = 0; i < count; ++i)
        if (lengths[i] != 0) h.symbols[offsets[lengths[i]]++] = static_cast<std::uint16_t>(i);
    return h;
}

int decodeSymbol(BitReader& in, const Huffman& h)
{
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len < 16; ++len)
    {
        code |= static_cast<int>(in.bits(1));
        const int count = h.counts[static_cast<std::size_t>(len)];
        if (code - count < first) return h.symbols[static_cast<std::size_t>(index + (code - first))];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    bad("invalid Huffman code");
}

constexpr std::array<std::uint16_t, 29> kLengthBase{ 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
constexpr std::array<std::uint8_t, 29> kLengthExtra{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
constexpr std::array<std::uint16_t, 30> kDistBase{ 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
constexpr std::array<std::uint8_t, 30> kDistExtra{ 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

void inflateBlock(BitReader& in, std::vector<std::uint8_t>& out, const Huffman& lit, const Huffman& dist,
    const std::size_t maxOutput)
{
    for (;;)
    {
        const int symbol = decodeSymbol(in, lit);
        if (symbol < 256)
        {
            if (out.size() >= maxOutput) bad("decompressed data is larger than the image");
            out.push_back(static_cast<std::uint8_t>(symbol));
        }
        else if (symbol == 256)
        {
            return;
        }
        else
        {
            const int li = symbol - 257;
            if (li >= 29) bad("invalid length symbol");
            const std::size_t length = kLengthBase[static_cast<std::size_t>(li)]
                + in.bits(kLengthExtra[static_cast<std::size_t>(li)]);
            const int di = decodeSymbol(in, dist);
            if (di >= 30) bad("invalid distance symbol");
            const std::size_t distance = kDistBase[static_cast<std::size_t>(di)]
                + in.bits(kDistExtra[static_cast<std::size_t>(di)]);
            if (distance > out.size()) bad("distance reaches before the start of the data");
            if (out.size() + length > maxOutput) bad("decompressed data is larger than the image");
            const std::size_t from = out.size() - distance;
            for (std::size_t i = 0; i < length; ++i) out.push_back(out[from + i]);
        }
    }
}

std::uint32_t readBe32(const std::uint8_t* p) noexcept
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
        | (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::uint32_t crc32(const std::uint8_t* data, const std::size_t size) noexcept
{
    static const auto table = []
    {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n)
        {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::uint8_t paeth(const int a, const int b, const int c) noexcept
{
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return static_cast<std::uint8_t>(a);
    if (pb <= pc) return static_cast<std::uint8_t>(b);
    return static_cast<std::uint8_t>(c);
}

} // namespace

std::vector<std::uint8_t> inflateZlib(const std::span<const std::uint8_t> data, const std::size_t maxOutput)
{
    if (data.size() < 6) bad("zlib stream is truncated");
    const std::uint8_t cmf = data[0];
    const std::uint8_t flg = data[1];
    if ((cmf & 0x0Fu) != 8 || (cmf >> 4) > 7) bad("zlib stream is not DEFLATE");
    if (((static_cast<unsigned>(cmf) << 8) | flg) % 31u != 0) bad("zlib header check failed");
    if (flg & 0x20u) bad("zlib preset dictionaries are not used by PNG");
    BitReader in(data.subspan(2));
    std::vector<std::uint8_t> out;
    out.reserve(std::min<std::size_t>(maxOutput, data.size() * 4));
    bool last = false;
    while (!last)
    {
        last = in.bits(1) != 0;
        const std::uint32_t type = in.bits(2);
        if (type == 0)
        {
            in.alignToByte();
            const auto bytes = in.data();
            std::size_t pos = in.position();
            if (pos + 4 > bytes.size()) bad("stored block is truncated");
            const std::uint16_t len = static_cast<std::uint16_t>(bytes[pos] | (bytes[pos + 1] << 8));
            const std::uint16_t nlen = static_cast<std::uint16_t>(bytes[pos + 2] | (bytes[pos + 3] << 8));
            if (static_cast<std::uint16_t>(~nlen) != len) bad("stored block length check failed");
            pos += 4;
            if (pos + len > bytes.size()) bad("stored block is truncated");
            if (out.size() + len > maxOutput) bad("decompressed data is larger than the image");
            out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                bytes.begin() + static_cast<std::ptrdiff_t>(pos + len));
            in.skip(4u + len);
        }
        else if (type == 1)
        {
            static const auto fixed = []
            {
                std::array<std::uint8_t, 288> l{};
                for (int i = 0; i < 144; ++i) l[static_cast<std::size_t>(i)] = 8;
                for (int i = 144; i < 256; ++i) l[static_cast<std::size_t>(i)] = 9;
                for (int i = 256; i < 280; ++i) l[static_cast<std::size_t>(i)] = 7;
                for (int i = 280; i < 288; ++i) l[static_cast<std::size_t>(i)] = 8;
                std::array<std::uint8_t, 30> d{};
                d.fill(5);
                return std::make_pair(buildHuffman(l.data(), l.size()), buildHuffman(d.data(), d.size()));
            }();
            inflateBlock(in, out, fixed.first, fixed.second, maxOutput);
        }
        else if (type == 2)
        {
            const std::size_t hlit = in.bits(5) + 257u;
            const std::size_t hdist = in.bits(5) + 1u;
            const std::size_t hclen = in.bits(4) + 4u;
            if (hlit > 286 || hdist > 30) bad("dynamic block declares too many codes");
            constexpr std::array<std::uint8_t, 19> order{ 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            std::array<std::uint8_t, 19> codeLengths{};
            for (std::size_t i = 0; i < hclen; ++i) codeLengths[order[i]] = static_cast<std::uint8_t>(in.bits(3));
            const Huffman lengthCode = buildHuffman(codeLengths.data(), codeLengths.size());
            std::array<std::uint8_t, 316> lengths{};
            std::size_t n = 0;
            while (n < hlit + hdist)
            {
                const int symbol = decodeSymbol(in, lengthCode);
                if (symbol < 16)
                {
                    lengths[n++] = static_cast<std::uint8_t>(symbol);
                    continue;
                }
                std::uint8_t value = 0;
                std::size_t repeat = 0;
                if (symbol == 16)
                {
                    if (n == 0) bad("length repeat with no previous length");
                    value = lengths[n - 1];
                    repeat = 3 + in.bits(2);
                }
                else if (symbol == 17) repeat = 3 + in.bits(3);
                else repeat = 11 + in.bits(7);
                if (n + repeat > hlit + hdist) bad("code lengths overflow the declared count");
                for (std::size_t i = 0; i < repeat; ++i) lengths[n++] = value;
            }
            if (lengths[256] == 0) bad("dynamic block has no end-of-block code");
            const Huffman lit = buildHuffman(lengths.data(), hlit);
            const Huffman dist = buildHuffman(lengths.data() + hlit, hdist);
            inflateBlock(in, out, lit, dist, maxOutput);
        }
        else
        {
            bad("invalid DEFLATE block type");
        }
    }
    // Adler-32 trailer, big-endian, after the last block's byte boundary.
    in.alignToByte();
    const std::size_t at = 2 + in.position();
    if (at + 4 > data.size()) bad("zlib checksum is missing");
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : out)
    {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    if (readBe32(data.data() + at) != ((b << 16) | a)) bad("zlib checksum does not match");
    return out;
}

RgbaImage decodePng(const std::span<const std::byte> bytes)
{
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    const std::size_t size = bytes.size();
    static constexpr std::uint8_t signature[8]{ 137, 80, 78, 71, 13, 10, 26, 10 };
    if (size < 8 || std::memcmp(data, signature, 8) != 0) bad("not a PNG file");
    std::size_t pos = 8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    int depth = 0;
    int colorType = -1;
    bool haveHeader = false;
    std::vector<std::uint8_t> idat;
    std::array<std::array<std::uint8_t, 4>, 256> palette{};
    std::size_t paletteSize = 0;
    std::array<std::uint8_t, 256> paletteAlpha{};
    paletteAlpha.fill(255);
    bool done = false;
    while (!done)
    {
        if (pos + 12 > size) bad("chunk is truncated");
        const std::uint32_t length = readBe32(data + pos);
        if (length > size - pos - 12) bad("chunk is truncated");
        const std::uint8_t* type = data + pos + 4;
        const std::uint8_t* body = data + pos + 8;
        if (crc32(type, length + 4u) != readBe32(body + length)) bad("chunk CRC does not match");
        const std::string name(reinterpret_cast<const char*>(type), 4);
        if (name == "IHDR")
        {
            if (length != 13 || haveHeader) bad("invalid IHDR");
            width = readBe32(body);
            height = readBe32(body + 4);
            depth = body[8];
            colorType = body[9];
            if (body[10] != 0 || body[11] != 0) bad("unknown compression or filter method");
            if (body[12] != 0) bad("interlaced images are not supported; save the PNG without interlacing");
            if (width == 0 || height == 0 || width > static_cast<std::uint32_t>(kMaxDecodedPngDimension)
                || height > static_cast<std::uint32_t>(kMaxDecodedPngDimension))
                bad("image dimensions are outside 1.." + std::to_string(kMaxDecodedPngDimension));
            const bool valid = (colorType == 0 && (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16))
                || (colorType == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8))
                || ((colorType == 2 || colorType == 4 || colorType == 6) && (depth == 8 || depth == 16));
            if (!valid) bad("invalid colour type / bit depth combination");
            haveHeader = true;
        }
        else if (name == "PLTE")
        {
            if (length % 3 != 0 || length / 3 > 256) bad("invalid palette");
            paletteSize = length / 3;
            for (std::size_t i = 0; i < paletteSize; ++i)
                palette[i] = { body[i * 3], body[i * 3 + 1], body[i * 3 + 2], 255 };
        }
        else if (name == "tRNS")
        {
            if (colorType == 3)
            {
                if (length > 256) bad("invalid palette transparency");
                for (std::size_t i = 0; i < length; ++i) paletteAlpha[i] = body[i];
            }
        }
        else if (name == "IDAT")
        {
            if (!haveHeader) bad("image data before the header");
            idat.insert(idat.end(), body, body + length);
        }
        else if (name == "IEND")
        {
            done = true;
        }
        else if ((type[0] & 0x20u) == 0)
        {
            bad("unknown critical chunk " + name);
        }
        pos += 12u + length;
    }
    if (!haveHeader || idat.empty()) bad("missing header or image data");
    if (colorType == 3 && paletteSize == 0) bad("palette image without a palette");

    const int channels = colorType == 0 ? 1 : colorType == 2 ? 3 : colorType == 3 ? 1 : colorType == 4 ? 2 : 4;
    const std::size_t bitsPerPixel = static_cast<std::size_t>(channels) * static_cast<std::size_t>(depth);
    const std::size_t stride = (static_cast<std::size_t>(width) * bitsPerPixel + 7u) / 8u;
    const std::size_t bpp = std::max<std::size_t>(1u, bitsPerPixel / 8u);
    const std::size_t expected = (stride + 1u) * height;
    const std::vector<std::uint8_t> raw = inflateZlib(idat, expected);
    if (raw.size() != expected) bad("image data has the wrong size");

    std::vector<std::uint8_t> rows(stride * height);
    std::vector<std::uint8_t> zero(stride, 0);
    for (std::uint32_t y = 0; y < height; ++y)
    {
        const std::uint8_t filter = raw[y * (stride + 1u)];
        const std::uint8_t* src = raw.data() + y * (stride + 1u) + 1u;
        std::uint8_t* dst = rows.data() + y * stride;
        const std::uint8_t* prev = y == 0 ? zero.data() : rows.data() + (y - 1u) * stride;
        for (std::size_t x = 0; x < stride; ++x)
        {
            const int a = x >= bpp ? dst[x - bpp] : 0;
            const int b = prev[x];
            const int c = x >= bpp ? prev[x - bpp] : 0;
            int value = src[x];
            switch (filter)
            {
            case 0: break;
            case 1: value += a; break;
            case 2: value += b; break;
            case 3: value += (a + b) / 2; break;
            case 4: value += paeth(a, b, c); break;
            default: bad("invalid row filter");
            }
            dst[x] = static_cast<std::uint8_t>(value & 0xFF);
        }
    }

    RgbaImage image;
    image.width = static_cast<std::int32_t>(width);
    image.height = static_cast<std::int32_t>(height);
    image.rgba.resize(static_cast<std::size_t>(width) * height * 4u);
    const auto sample = [&](const std::uint8_t* row, const std::size_t index) -> std::uint8_t
    {
        // index counts samples along the row
        if (depth == 8) return row[index];
        if (depth == 16) return row[index * 2u];
        const std::size_t bit = index * static_cast<std::size_t>(depth);
        const std::uint8_t byte = row[bit / 8u];
        const int shift = 8 - depth - static_cast<int>(bit % 8u);
        return static_cast<std::uint8_t>((byte >> shift) & ((1u << depth) - 1u));
    };
    for (std::uint32_t y = 0; y < height; ++y)
    {
        const std::uint8_t* row = rows.data() + y * stride;
        for (std::uint32_t x = 0; x < width; ++x)
        {
            std::uint8_t* px = image.rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4u;
            if (colorType == 3)
            {
                const std::uint8_t index = sample(row, x);
                if (index >= paletteSize) bad("pixel uses a colour outside the palette");
                px[0] = palette[index][0];
                px[1] = palette[index][1];
                px[2] = palette[index][2];
                px[3] = paletteAlpha[index];
            }
            else if (colorType == 0 || colorType == 4)
            {
                std::uint8_t g = sample(row, x * static_cast<std::size_t>(channels));
                if (depth < 8) g = static_cast<std::uint8_t>(g * 255 / ((1 << depth) - 1));
                px[0] = px[1] = px[2] = g;
                px[3] = colorType == 4 ? sample(row, x * 2u + 1u) : 255;
            }
            else
            {
                px[0] = sample(row, x * static_cast<std::size_t>(channels));
                px[1] = sample(row, x * static_cast<std::size_t>(channels) + 1u);
                px[2] = sample(row, x * static_cast<std::size_t>(channels) + 2u);
                px[3] = colorType == 6 ? sample(row, x * 4u + 3u) : 255;
            }
        }
    }
    return image;
}

} // namespace sed::assets
