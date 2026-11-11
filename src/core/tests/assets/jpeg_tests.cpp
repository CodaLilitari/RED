#include "test_common.hpp"

#include "sed/assets/jpeg.hpp"
#include "sed/assets/mat_write.hpp"
#include "sed/assets/png.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sed::tests
{
namespace
{

// The fixtures were written by Pillow (libjpeg-turbo), or by ImageMagick for
// 4:4:0 and 4:1:1, and each .png beside a .jpg is Pillow's decode of it.
std::string readFixture(const std::string& name)
{
    std::ifstream in("src/core/tests/assets/fixtures/" + name, std::ios::binary);
    check(in.good(), "jpeg: fixture file opens");
    std::ostringstream buffer{};
    buffer << in.rdbuf();
    return buffer.str();
}

std::span<const std::byte> bytesOf(const std::string& s)
{
    return std::as_bytes(std::span(s.data(), s.size()));
}

// Throws std::runtime_error with `needle` in its message.
bool decodeRefuses(const std::string& bytes, const std::string_view needle)
{
    try
    {
        (void)sed::assets::decodeJpeg(bytesOf(bytes));
    }
    catch ( const std::runtime_error& error )
    {
        const bool matches = std::string_view(error.what()).find(needle) != std::string_view::npos;
        if ( !matches )
        {
            std::cerr << "  got: " << error.what() << "\n";
        }
        return matches;
    }
    std::cerr << "  the JPEG decoded\n";
    return false;
}

// Our decode against the reference within IDCT rounding: every channel
// within 2 levels and the mean difference under half a level.
bool matchesReference(const std::string& name)
{
    sed::assets::RgbaImage ours{};
    sed::assets::RgbaImage reference{};
    try
    {
        ours = sed::assets::decodeJpeg(bytesOf(readFixture(name + ".jpg")));
        reference = sed::assets::decodePng(bytesOf(readFixture(name + ".png")));
    }
    catch ( const std::exception& error )
    {
        std::cerr << "  " << name << ": " << error.what() << "\n";
        return false;
    }
    if ( ours.width != reference.width || ours.height != reference.height || ours.rgba.size() != reference.rgba.size() )
    {
        std::cerr << "  " << name << ": sizes differ\n";
        return false;
    }
    int worst = 0;
    double total = 0.0;
    for ( std::size_t i = 0; i < ours.rgba.size(); ++i )
    {
        const int difference = std::abs(static_cast<int>(ours.rgba[i]) - static_cast<int>(reference.rgba[i]));
        worst = std::max(worst, difference);
        total += difference;
    }
    const double mean = total / static_cast<double>(ours.rgba.size());
    if ( worst > 2 || mean > 0.5 )
    {
        std::cerr << "  " << name << ": largest difference " << worst << ", mean " << mean << "\n";
        return false;
    }
    return true;
}

std::size_t findMarker(const std::string& bytes, const unsigned char code)
{
    for ( std::size_t i = 0; i + 1 < bytes.size(); ++i )
    {
        if ( static_cast<unsigned char>(bytes[i]) == 0xFF && static_cast<unsigned char>(bytes[i + 1]) == code )
        {
            return i;
        }
    }
    return std::string::npos;
}

sed::assets::RgbaImage solid(const std::int32_t width, const std::int32_t height, const std::vector<std::uint8_t>& rgba)
{
    sed::assets::RgbaImage image{};
    image.width = width;
    image.height = height;
    image.rgba = rgba;
    return image;
}

void runDecodeTests()
{
    check(matchesReference("jpeg_420"), "jpeg: baseline 4:2:0 matches the reference decode");
    check(matchesReference("jpeg_444"), "jpeg: baseline 4:4:4 with optimized Huffman tables matches");
    check(matchesReference("jpeg_422"), "jpeg: baseline 4:2:2 matches");
    check(matchesReference("jpeg_440"), "jpeg: baseline 4:4:0 matches");
    check(matchesReference("jpeg_411"), "jpeg: baseline 4:1:1 matches");
    check(matchesReference("jpeg_grey"), "jpeg: a greyscale JPEG matches");
    check(matchesReference("jpeg_restart"), "jpeg: a JPEG with restart markers (wrapping past RST7) matches");

    const std::string baseline = readFixture("jpeg_420.jpg");
    check(sed::assets::isJpeg(bytesOf(baseline)) && !sed::assets::isJpeg(bytesOf(readFixture("jpeg_420.png"))),
        "jpeg: a JPEG is recognised by its start-of-image marker");
    {
        const sed::assets::JpegInfo info = sed::assets::readJpegInfo(bytesOf(baseline));
        check(info.width == 37 && info.height == 29 && info.components == 3 && info.precision == 8
                && !info.progressive && !info.arithmetic && !info.lossless,
            "jpeg: the frame header reads without decoding");
        const sed::assets::JpegInfo progressive = sed::assets::readJpegInfo(bytesOf(readFixture("jpeg_progressive.jpg")));
        check(progressive.progressive && progressive.width == 37, "jpeg: the frame header names a progressive JPEG");
    }
    {
        // A file that ends without its end-of-image marker but holds every
        // scan decodes; one that ends inside the scan does not.
        const sed::assets::RgbaImage whole = sed::assets::decodeJpeg(bytesOf(baseline));
        const sed::assets::RgbaImage noEnd = sed::assets::decodeJpeg(bytesOf(baseline.substr(0, baseline.size() - 2)));
        check(noEnd.rgba == whole.rgba, "jpeg: a missing end-of-image marker alone is tolerated");
    }
}

void runRefusalTests()
{
    const std::string baseline = readFixture("jpeg_420.jpg");
    check(decodeRefuses(readFixture("jpeg_progressive.jpg"), "progressive"),
        "jpeg: a progressive JPEG is refused by name");
    check(decodeRefuses(baseline.substr(0, baseline.size() / 2), "truncated"),
        "jpeg: a file cut inside its image data is refused as truncated");
    check(decodeRefuses(baseline.substr(0, 120), "truncated"), "jpeg: a file cut inside its headers is refused");
    check(decodeRefuses(readFixture("jpeg_cmyk.jpg"), "4-component"), "jpeg: a CMYK JPEG is refused");
    check(decodeRefuses("not a jpeg", "not a JPEG"), "jpeg: a file that is not a JPEG is refused");
    const std::size_t sof = findMarker(baseline, 0xC0);
    check(sof != std::string::npos, "jpeg: the baseline fixture has a SOF0 marker");
    if ( sof != std::string::npos )
    {
        std::string arithmetic = baseline;
        arithmetic[sof + 1] = static_cast<char>(0xC9);
        check(decodeRefuses(arithmetic, "arithmetic"), "jpeg: an arithmetic-coded frame is refused by name");
        std::string lossless = baseline;
        lossless[sof + 1] = static_cast<char>(0xC3);
        check(decodeRefuses(lossless, "lossless"), "jpeg: a lossless frame is refused by name");
        std::string twelveBit = baseline;
        twelveBit[sof + 4] = 12; // FF C0, length (2), precision
        check(decodeRefuses(twelveBit, "12-bit"), "jpeg: 12-bit samples are refused by name");
        std::string huge = baseline;
        huge[sof + 7] = static_cast<char>(0x40); // width 0x4025 = 16421
        check(decodeRefuses(huge, "outside 1..8192"), "jpeg: dimensions beyond the bound are refused");
    }
    {
        std::string restart = readFixture("jpeg_restart.jpg");
        const std::size_t first = findMarker(restart, 0xD0);
        check(first != std::string::npos, "jpeg: the restart fixture has an RST0 marker");
        if ( first != std::string::npos )
        {
            restart[first + 1] = static_cast<char>(0xD5);
            check(decodeRefuses(restart, "restart marker"), "jpeg: a restart marker out of order is refused");
        }
    }
}

void runResizeTests()
{
    using sed::assets::resizeRgbaArea;
    {
        // 4x2 -> 2x1: each output pixel is the mean of a 2x2 block.
        const auto source = solid(4, 2, { 0, 0, 0, 255, 100, 0, 0, 255, 10, 10, 10, 255, 30, 30, 30, 255,
                                          200, 0, 0, 255, 0, 0, 0, 255, 50, 50, 50, 255, 70, 70, 70, 255 });
        const auto out = resizeRgbaArea(source, 2, 1);
        check(out.width == 2 && out.height == 1
                && out.rgba == std::vector<std::uint8_t>{ 75, 0, 0, 255, 40, 40, 40, 255 },
            "resize: shrinking by a whole factor averages whole blocks");
    }
    {
        // 3 -> 2: the middle pixel is shared, half to each side.
        const auto out = resizeRgbaArea(solid(3, 1, { 0, 0, 0, 255, 90, 90, 90, 255, 180, 180, 180, 255 }), 2, 1);
        check(out.rgba == std::vector<std::uint8_t>{ 30, 30, 30, 255, 150, 150, 150, 255 },
            "resize: a partly covered pixel counts by the part it contributes");
    }
    {
        const auto out = resizeRgbaArea(solid(2, 1, { 10, 20, 30, 255, 40, 50, 60, 255 }), 4, 2);
        check(out.width == 4 && out.height == 2 && out.rgba[0] == 10 && out.rgba[4] == 10 && out.rgba[8] == 40
                && out.rgba[16] == 10 && out.rgba[28] == 40,
            "resize: growing repeats pixels");
    }
    {
        std::vector<std::uint8_t> rgba(64u * 64u * 4u);
        for ( std::size_t i = 0; i < rgba.size(); ++i )
        {
            rgba[i] = static_cast<std::uint8_t>((i * 2654435761u) >> 24);
        }
        const auto out = resizeRgbaArea(solid(64, 64, rgba), 4, 4);
        bool means = true;
        for ( std::size_t oy = 0; oy < 4; ++oy )
        {
            for ( std::size_t ox = 0; ox < 4; ++ox )
            {
                for ( std::size_t c = 0; c < 4; ++c )
                {
                    std::uint32_t sum = 0;
                    for ( std::size_t y = oy * 16; y < oy * 16 + 16; ++y )
                    {
                        for ( std::size_t x = ox * 16; x < ox * 16 + 16; ++x )
                        {
                            sum += rgba[(y * 64 + x) * 4 + c];
                        }
                    }
                    const auto expected = static_cast<std::uint8_t>((sum + 128) / 256);
                    means = means && out.rgba[(oy * 4 + ox) * 4 + c] == expected;
                }
            }
        }
        check(means, "resize: a 16x reduction is the rounded mean of each 16x16 block");
    }
    bool refused = false;
    try
    {
        (void)resizeRgbaArea(solid(2, 2, std::vector<std::uint8_t>(16, 0)), 0, 4);
    }
    catch ( const std::invalid_argument& )
    {
        refused = true;
    }
    check(refused, "resize: a zero size is refused");
    refused = false;
    try
    {
        (void)resizeRgbaArea(solid(2, 2, std::vector<std::uint8_t>(15, 0)), 1, 1);
    }
    catch ( const std::invalid_argument& )
    {
        refused = true;
    }
    check(refused, "resize: pixel data that does not match the size is refused");
}

} // namespace

void runJpegTests()
{
    runDecodeTests();
    runRefusalTests();
    runResizeTests();
}

} // namespace sed::tests
