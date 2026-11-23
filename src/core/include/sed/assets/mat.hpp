#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sed/image.hpp"
#include "sed/assets/cmp.hpp"
#include "sed/geometry.hpp"

namespace sed::assets
{

/** Mip levels the retail engine keeps for one texture of a MAT.
 *
 * Its texture record has four slots, and its loader fills one per stored
 * level without checking the count: a fifth level and its surface entries
 * are written past the record, over whatever the heap holds next. What
 * breaks therefore depends on the heap. In JK.EXE 1.01 one nine-level
 * 256x256 MAT drew through several builds of a level and then, once the
 * level's rooms changed, crashed the hardware renderer whenever its model
 * came into view. A stock texture record made no difference and four levels
 * cured it. A seven-level 64x64 MAT in the same level drew throughout, and
 * the software renderer never crashed. OpenJKDF2's record has the same four
 * slots; the MotS-Fixes fork's main refuses such a MAT when it loads, and
 * Coda's surplus-mips branch loads it with four resident levels. */
inline constexpr std::int32_t kRetailMatMipLevels = 4;

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

/** Cel zero in source palette space. Color MATs carry only their Solid index. */
struct IndexedMatCel
{
    int width{};
    int height{};
    std::uint8_t solidColorIndex{};
    bool colorMaterial{};
    bool keyed{};
    std::vector<std::uint8_t> indices{};
};

enum class MatPreviewKind : std::uint8_t
{
    SolidIndexed,
    Indexed,
    TrueColor,
};

/** Source cel zero, mip zero. Solids have no texture extent. */
struct MatPreviewCel
{
    MatPreviewKind kind{MatPreviewKind::SolidIndexed};
    std::int32_t width{};
    std::int32_t height{};
    std::uint8_t solidColorIndex{};
    bool keyed{};
    std::vector<std::uint8_t> indices{};
    sed::Rgba8Image rgba{};
    /// The most mip levels any texture of the MAT declares; compare with kRetailMatMipLevels.
    std::int32_t storedMipLevels{};
};

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

    static IndexedMatCel parseIndexedSnapshotCel(std::vector<std::byte> data);

    static MatPreviewCel inspectPreviewCel(std::span<const std::byte> data,
        std::size_t maxPixels = 4'194'304);

    bool isColor() const noexcept;
    bool isAnimated() const noexcept;
    /** True for a 16/32-bpp texture MAT decoded to RGBA8. Colour and 8-bpp indexed MATs are false. */
    bool isTrueColor() const noexcept;
    /** True when any texture cel carries the classic color-key enable flag. */
    bool hasColorKey() const noexcept;
    /** Stored bits per pixel from the MAT header (8, 16 or 32 for accepted texture MATs). */
    std::uint32_t bitsPerPixel() const noexcept;
    /** Retail mip fallback note: non-empty when mip 0 was complete but a later mip of the final texture was short, so the complete earlier mips were kept. Empty otherwise. */
    const std::string& mipFallbackNote() const noexcept;
    /** The most mip levels any texture declares (0 for a colour MAT). A short final chain still counts as declared. */
    std::int32_t storedMipLevels() const noexcept;
    /** Retail mip limit note: non-empty when a texture declares more than kRetailMatMipLevels levels. Empty otherwise. */
    const std::string& mipLimitNote() const noexcept;
    std::int32_t celCount() const noexcept;
    const sed::ColorInfo& colorInfo() const noexcept;

    const Cel& cel(std::int32_t celIndex) const;
    const std::vector<Cel>& cels() const noexcept;

    sed::Rgba8Image decodeCelRgba8(std::int32_t celIndex) const;

private:
    static MatFile parseImpl(std::span<const std::byte> data, const CmpPalette* palette,
        MatPreviewCel* snapshot, bool indexedOnly, std::size_t maxPixels);
    sed::ColorInfo colorInfo_{};
    bool isColor_{};
    bool isTrueColor_{};
    bool isAnimated_{};
    std::uint8_t colorIndex_{};
    bool hasColorKey_{};
    std::string mipFallbackNote_{};
    std::string mipLimitNote_{};
    std::int32_t storedMipLevels_{};
    std::vector<Cel> cels_{};
};

/**
 * The palette indices a MAT can draw, read from its raw bytes: every texel of
 * every cel and mip of an 8-bit texture MAT, or each colour number of a colour
 * MAT. A 16/32-bit MAT draws no palette entry, so every flag is false.
 * Throws std::runtime_error for bytes that are not a complete MAT.
 */
std::array<bool, 256> matPaletteIndices(std::span<const std::byte> mat);

} // namespace sed::assets
