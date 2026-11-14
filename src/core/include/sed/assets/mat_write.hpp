#pragma once

#include "sed/assets/cmp.hpp"
#include "sed/assets/png.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sed::assets
{

struct IndexedMatOptions
{
    // Mipmap levels to store, each half the size of the last (JK uses 4).
    // Clamped to what the smaller dimension allows.
    std::int32_t mipmaps{ 4 };
    // Floyd-Steinberg error diffusion while mapping to the palette.
    bool dither{ true };
    // Palette indices the mapping may use. JK's 8-bit renderer reserves
    // none for opaque textures, but a caller can keep, e.g., index 0 free.
    std::array<bool, 256> allowed{ allTrue() };

    static constexpr std::array<bool, 256> allTrue() noexcept
    {
        std::array<bool, 256> a{};
        a.fill(true);
        return a;
    }
};

struct IndexedMatReport
{
    std::int32_t width{};
    std::int32_t height{};
    std::int32_t mipmaps{};
    std::int32_t cels{};
    std::int32_t coloursUsed{};
    // Mean per-channel absolute difference (0..255) between the source
    // pixels and their decoded output colours at full size, over all cels.
    double meanError{};
};

struct IndexedMatResult
{
    std::string bytes;
    IndexedMatReport report;
};

/**
 * Builds an 8-bit indexed texture MAT (version 0x32, 1 to 16 cels of the
 * same size, the engine's texinfo limit) from RGBA images mapped to `palette`. Every cel's width and
 * height must be a power of two from 8 to 1024 and all cels must share them.
 * Alpha is ignored (texture MATs here are opaque). Each mipmap is a box
 * filter of the source, mapped separately. Throws std::invalid_argument for
 * anything else; the bytes always load with the MAT reader.
 */
IndexedMatResult buildIndexedMat(const std::vector<RgbaImage>& cels, const CmpPalette& palette,
    const IndexedMatOptions& options = {});

struct Rgb565MatOptions
{
    // Same 1..4 level/dimension clamping as indexed MAT creation.
    std::int32_t mipmaps{ 4 };
};

// Both encodings report stored colour count and decoded-channel error.
using Rgb565MatResult = IndexedMatResult;

/**
 * Builds an opaque 16-bit RGB565 texture MAT with explicit 5:6:5 channel
 * widths and little-endian pixels. No palette is read or written. Cel and
 * dimension limits match buildIndexedMat; alpha is ignored. Each channel
 * is rounded to its nearest 5/6-bit value, with half values rounded up.
 * Mips box-filter the unquantized source before independent quantization.
 * The MAT reader supports this format; target-engine support must be
 * qualified separately (this is not a retail JKDF2 compatibility claim).
 */
Rgb565MatResult buildRgb565Mat(const std::vector<RgbaImage>& cels, const Rgb565MatOptions& options = {});

inline constexpr std::int32_t kMaxResizeDimension = 8192;

/**
 * Scales an RGBA image to width x height with an area (box) filter: each
 * output pixel is the mean of the source area it covers, a source pixel that
 * is only partly covered counting by the part it contributes. Shrinking by a
 * whole factor averages whole blocks; growing repeats pixels. Channels are
 * averaged as stored (no gamma) and rounded to nearest. Throws
 * std::invalid_argument for an empty or inconsistent source or a size
 * outside 1..kMaxResizeDimension each way.
 */
RgbaImage resizeRgbaArea(const RgbaImage& image, std::int32_t width, std::int32_t height);

} // namespace sed::assets
