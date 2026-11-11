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
    // pixels and their palette colours at full size, over all cels.
    double meanError{};
};

struct IndexedMatResult
{
    std::string bytes;
    IndexedMatReport report;
};

/**
 * Builds an 8-bit indexed texture MAT (version 0x32, one or more cels of the
 * same size) from RGBA images mapped to `palette`. Every cel's width and
 * height must be a power of two from 8 to 1024 and all cels must share them.
 * Alpha is ignored (texture MATs here are opaque). Each mipmap is a box
 * filter of the source, mapped separately. Throws std::invalid_argument for
 * anything else; the bytes always load with the MAT reader.
 */
IndexedMatResult buildIndexedMat(const std::vector<RgbaImage>& cels, const CmpPalette& palette,
    const IndexedMatOptions& options = {});

} // namespace sed::assets
