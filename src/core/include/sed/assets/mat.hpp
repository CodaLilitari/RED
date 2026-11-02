#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "sed/image.hpp"
#include "sed/assets/cmp.hpp"
#include "sed/geometry.hpp"

namespace sed::assets
{

/** How a MAT/surface combination handles palette index zero. */
enum class MatTransparencyMode : std::uint8_t
{
    Opaque,
    Masked,
    Translucent,
};

std::string_view matTransparencyModeName(MatTransparencyMode mode) noexcept;

/**
 * Resolves the transparency mode for one MAT use. A translucent face flag
 * makes the use translucent; otherwise a nonzero MAT header alpha field
 * (the classic 1-bit alpha-reference marker) makes it masked, matching the
 * decoded-indexed-image alpha convention in `mat.cpp`. An opaque use has
 * neither signal.
 */
MatTransparencyMode resolveMatTransparencyMode(
    const sed::ColorInfo& colorInfo,
    std::uint32_t faceFlags) noexcept;

/**
 * Index-zero discard rule, stated on the palette index before any colormap
 * (`translation`) lookup. Masked and translucent uses discard palette index
 * zero; an opaque use keeps it. Solid-colour uses (`isSolidColor`, the MAT
 * colour-MAT / solid colour index path) always keep it, including index zero.
 * The value after colormap mapping is irrelevant: discard when true for the
 * pre-mapping index even if the colormap would map it to a nonzero entry.
 */
bool matDiscardsPaletteIndex(
    MatTransparencyMode mode,
    std::uint8_t paletteIndex,
    bool isSolidColor) noexcept;

/** Convenience: resolve the mode, then apply the pre-colormap discard rule. */
bool matDiscardsPaletteIndex(
    const sed::ColorInfo& colorInfo,
    std::uint32_t faceFlags,
    std::uint8_t paletteIndex,
    bool isSolidColor) noexcept;

class MatFile
{
public:
    using MipMap = sed::Image8;

    struct Cel
    {
        std::int32_t width{};
        std::int32_t height{};
        std::uint8_t solidColorIndex{};
        CmpColor solidColor{};
        std::vector<MipMap> mipmaps{};
    };

    static MatFile parse(std::vector<std::byte> data, const CmpPalette* palette = nullptr);

    bool isColor() const noexcept;
    bool isAnimated() const noexcept;
    std::int32_t celCount() const noexcept;
    const sed::ColorInfo& colorInfo() const noexcept;

    const Cel& cel(std::int32_t celIndex) const;
    const std::vector<Cel>& cels() const noexcept;

    sed::Rgba8Image decodeCelRgba8(std::int32_t celIndex) const;

private:
    sed::ColorInfo colorInfo_{};
    bool isColor_{};
    bool isAnimated_{};
    std::uint8_t colorIndex_{};
    std::vector<Cel> cels_{};
};

} // namespace sed::assets
