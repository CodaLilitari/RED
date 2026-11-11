#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sed::assets
{

/** An 8-bit RGBA image, rows top to bottom. */
struct RgbaImage
{
    std::int32_t width{};
    std::int32_t height{};
    std::vector<std::uint8_t> rgba;
};

inline constexpr std::int32_t kMaxDecodedPngDimension = 8192;

/**
 * Decodes a PNG file: colour types 0, 2, 3, 4 and 6 at every bit depth the
 * format allows, palette transparency (tRNS), non-interlaced. 16-bit samples
 * keep their high byte. Adam7 interlacing, dimensions above
 * `maxDimension` (checked from the header, before anything is inflated; at
 * most kMaxDecodedPngDimension), a bad zlib stream or checksum, and truncated
 * data throw std::runtime_error naming the problem; nothing is guessed.
 */
RgbaImage decodePng(std::span<const std::byte> bytes,
    std::int32_t maxDimension = kMaxDecodedPngDimension);

/** Raw DEFLATE/zlib inflate used by decodePng (RFC 1950/1951). Throws on
 * malformed input or when the output would exceed `maxOutput` bytes. */
std::vector<std::uint8_t> inflateZlib(std::span<const std::uint8_t> data, std::size_t maxOutput);

} // namespace sed::assets
