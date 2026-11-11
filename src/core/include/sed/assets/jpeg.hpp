#pragma once

#include "sed/assets/png.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace sed::assets
{

inline constexpr std::int32_t kMaxDecodedJpegDimension = 8192;

/** What a JPEG's frame header says, read without decoding the image. */
struct JpegInfo
{
    std::int32_t width{};
    std::int32_t height{};
    std::int32_t components{};
    std::int32_t precision{};
    // The frame type: baseline and extended sequential Huffman decode;
    // progressive, lossless and arithmetic-coded frames do not.
    bool progressive{};
    bool lossless{};
    bool arithmetic{};
};

/** True when `bytes` start with a JPEG start-of-image marker (FF D8 FF). */
bool isJpeg(std::span<const std::byte> bytes) noexcept;

/**
 * Reads the frame header of a JPEG up to its first SOF marker. Throws
 * std::runtime_error when the markers before it are malformed or there is
 * no frame header.
 */
JpegInfo readJpegInfo(std::span<const std::byte> bytes);

/**
 * Decodes a baseline or extended sequential Huffman JPEG with 8-bit samples
 * and 1 (grey) or 3 (YCbCr, or RGB when an Adobe marker or the component
 * ids say so) components, at any sampling factors whose ratios are whole,
 * with restart markers and interleaved or separate scans. The inverse DCT,
 * chroma upsampling and colour conversion follow libjpeg's defaults (the
 * accurate integer IDCT, "fancy" triangle upsampling for 2x1, 1x2 and 2x2
 * chroma), so results match libjpeg-based decoders to within rounding.
 *
 * Progressive, lossless, hierarchical and arithmetic-coded JPEGs, 12-bit
 * samples, 2 or 4 components (CMYK), dimensions above
 * kMaxDecodedJpegDimension, missing tables and entropy data that is
 * truncated or corrupt throw std::runtime_error naming the problem; nothing
 * is guessed. The result is opaque RGBA, rows top to bottom.
 */
RgbaImage decodeJpeg(std::span<const std::byte> bytes);

} // namespace sed::assets
