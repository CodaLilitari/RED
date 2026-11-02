#include "test_common.hpp"

#include "sed/assets/mat.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sed::tests
{

namespace
{

void appendU32(std::vector<std::byte>& out, const std::uint32_t value)
{
    out.push_back(static_cast<std::byte>(value & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 16u) & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 24u) & 0xFFu));
}

void appendI32(std::vector<std::byte>& out, const std::int32_t value)
{
    appendU32(out, static_cast<std::uint32_t>(value));
}

void appendColorInfo(std::vector<std::byte>& out, const sed::ColorInfo& colorInfo)
{
    appendU32(out, static_cast<std::uint32_t>(colorInfo.mode));
    appendU32(out, colorInfo.bpp);
    appendU32(out, colorInfo.redBPP);
    appendU32(out, colorInfo.greenBPP);
    appendU32(out, colorInfo.blueBPP);
    appendU32(out, colorInfo.redShl);
    appendU32(out, colorInfo.greenShl);
    appendU32(out, colorInfo.blueShl);
    appendU32(out, colorInfo.redShr);
    appendU32(out, colorInfo.greenShr);
    appendU32(out, colorInfo.blueShr);
    appendU32(out, colorInfo.alphaBPP);
    appendU32(out, colorInfo.alphaShl);
    appendU32(out, colorInfo.alphaShr);
}

// One-cel 2x2 indexed texture; pixels are palette indices 0,1,0,2.
sed::assets::MatFile parseTextureMat(const std::uint32_t alphaBpp)
{
    std::vector<std::byte> bytes{};
    bytes.push_back(std::byte{ 'M' });
    bytes.push_back(std::byte{ 'A' });
    bytes.push_back(std::byte{ 'T' });
    bytes.push_back(std::byte{ ' ' });
    appendI32(bytes, 0x32);
    appendI32(bytes, 2);
    appendI32(bytes, 1);
    appendI32(bytes, 1);
    sed::ColorInfo colorInfo{};
    colorInfo.mode = sed::ColorMode::Indexed;
    colorInfo.bpp = 8;
    colorInfo.alphaBPP = alphaBpp;
    appendColorInfo(bytes, colorInfo);
    appendI32(bytes, 8);
    appendI32(bytes, 37);
    for (int i = 0; i < 8; ++i)
    {
        appendI32(bytes, 0);
    }
    appendI32(bytes, 2);
    appendI32(bytes, 2);
    appendI32(bytes, 0);
    appendI32(bytes, 0);
    appendI32(bytes, 0);
    appendI32(bytes, 1);
    bytes.push_back(std::byte{ 0 });
    bytes.push_back(std::byte{ 1 });
    bytes.push_back(std::byte{ 0 });
    bytes.push_back(std::byte{ 2 });
    const sed::assets::CmpPalette palette = sed::assets::makeGrayscalePalette();
    return sed::assets::MatFile::parse(bytes, &palette);
}

// Colour MAT (matType 0) whose colour number is the supplied index.
sed::assets::MatFile parseColourMat(const std::int32_t colourIndex)
{
    std::vector<std::byte> bytes{};
    bytes.push_back(std::byte{ 'M' });
    bytes.push_back(std::byte{ 'A' });
    bytes.push_back(std::byte{ 'T' });
    bytes.push_back(std::byte{ ' ' });
    appendI32(bytes, 0x32);
    appendI32(bytes, 0);
    appendI32(bytes, 1);
    appendI32(bytes, 0);
    sed::ColorInfo colorInfo{};
    colorInfo.mode = sed::ColorMode::Indexed;
    colorInfo.bpp = 8;
    appendColorInfo(bytes, colorInfo);
    appendI32(bytes, 0);
    appendI32(bytes, colourIndex);
    for (int i = 0; i < 4; ++i)
    {
        appendI32(bytes, 0);
    }
    const sed::assets::CmpPalette palette = sed::assets::makeGrayscalePalette();
    return sed::assets::MatFile::parse(bytes, &palette);
}

} // namespace

void runMatTransparencyTests()
{
    using sed::assets::MatTransparencyMode;

    check(sed::assets::matTransparencyModeName(MatTransparencyMode::Opaque) == "opaque"
            && sed::assets::matTransparencyModeName(MatTransparencyMode::Masked) == "masked"
            && sed::assets::matTransparencyModeName(MatTransparencyMode::Translucent) == "translucent",
        "MAT.transparency: mode names are stable");

    {
        sed::ColorInfo colorInfo{};
        colorInfo.mode = sed::ColorMode::Indexed;
        colorInfo.bpp = 8;
        check(sed::assets::resolveMatTransparencyMode(colorInfo, 0u) == MatTransparencyMode::Opaque,
            "MAT.transparency: no alpha marker and no translucent flag resolves opaque");
        colorInfo.alphaBPP = 1;
        check(sed::assets::resolveMatTransparencyMode(colorInfo, 0u) == MatTransparencyMode::Masked,
            "MAT.transparency: nonzero header alpha marker resolves masked");
        check(sed::assets::resolveMatTransparencyMode(colorInfo, sed::FF_Transluent)
                == MatTransparencyMode::Translucent,
            "MAT.transparency: translucent face flag resolves translucent over masked");
        sed::ColorInfo opaqueInfo{};
        check(sed::assets::resolveMatTransparencyMode(opaqueInfo, sed::FF_Transluent)
                == MatTransparencyMode::Translucent,
            "MAT.transparency: translucent face flag alone resolves translucent");
    }

    {
        check(sed::assets::matDiscardsPaletteIndex(MatTransparencyMode::Masked, 0, false)
                && sed::assets::matDiscardsPaletteIndex(MatTransparencyMode::Translucent, 0, false),
            "MAT.discard: masked and translucent discard palette index zero");
        check(!sed::assets::matDiscardsPaletteIndex(MatTransparencyMode::Opaque, 0, false),
            "MAT.discard: opaque index zero stays visible");
        check(!sed::assets::matDiscardsPaletteIndex(MatTransparencyMode::Masked, 1, false)
                && !sed::assets::matDiscardsPaletteIndex(MatTransparencyMode::Translucent, 2, false),
            "MAT.discard: nonzero indices are never discarded");
        check(!sed::assets::matDiscardsPaletteIndex(MatTransparencyMode::Masked, 0, true)
                && !sed::assets::matDiscardsPaletteIndex(MatTransparencyMode::Translucent, 0, true),
            "MAT.discard: solid colour index zero stays visible");
    }

    {
        // The check is on the pre-colormap index. Even though this synthetic
        // translation maps 0 -> 80, the masked/translucent sample is still
        // discarded and the opaque sample still survives as index 80.
        sed::assets::CmpTranslation translation{};
        translation.fill(7);
        translation[0] = 80;
        translation[1] = 81;
        const sed::assets::CmpPalette master = sed::assets::makeGrayscalePalette();
        const sed::assets::CmpPalette mapped =
            sed::assets::applyCmpTranslation(master, translation);
        check(mapped[0].r == 80 && mapped[1].r == 81,
            "MAT.discard: synthetic colormap maps index zero to a nonzero entry");
        check(sed::assets::matDiscardsPaletteIndex(MatTransparencyMode::Masked, 0, false)
                && !sed::assets::matDiscardsPaletteIndex(MatTransparencyMode::Opaque, 0, false),
            "MAT.discard: pre-colormap index zero decides discard, not the mapped entry");
    }

    {
        const sed::assets::MatFile opaque = parseTextureMat(0);
        const sed::assets::MatFile masked = parseTextureMat(1);
        check(!opaque.isColor() && opaque.celCount() == 1 && !masked.isColor(),
            "MAT.discard: synthetic indexed MATs parse as one texture cel");
        const sed::Rgba8Image opaqueImage = opaque.decodeCelRgba8(0);
        const sed::Rgba8Image maskedImage = masked.decodeCelRgba8(0);
        check(opaqueImage.rgba.size() == 16 && opaqueImage.rgba[3] == 255
                && opaqueImage.rgba[7] == 255,
            "MAT.discard: opaque decode keeps index zero opaque");
        check(maskedImage.rgba.size() == 16 && maskedImage.rgba[3] == 0
                && maskedImage.rgba[7] == 255 && maskedImage.rgba[11] == 0
                && maskedImage.rgba[15] == 255,
            "MAT.discard: masked decode makes exactly the index-zero pixels transparent");
        check(sed::assets::matDiscardsPaletteIndex(
                  masked.colorInfo(), 0u, 0, false)
                && !sed::assets::matDiscardsPaletteIndex(
                    opaque.colorInfo(), 0u, 0, false)
                && sed::assets::matDiscardsPaletteIndex(
                    opaque.colorInfo(), sed::FF_Transluent, 0, false),
            "MAT.discard: parsed colour info resolves through the core rule");
    }

    {
        const sed::assets::MatFile solidZero = parseColourMat(0);
        check(solidZero.isColor() && solidZero.celCount() == 1
                && solidZero.cel(0).solidColorIndex == 0,
            "MAT.discard: colour MAT with colour index zero parses");
        check(!sed::assets::matDiscardsPaletteIndex(
                  solidZero.colorInfo(), sed::FF_Transluent,
                  solidZero.cel(0).solidColorIndex, true),
            "MAT.discard: solid colour zero is not discarded as a texture sample");
        const sed::Rgba8Image image = solidZero.decodeCelRgba8(0);
        check(image.rgba.size() == 64u * 64u * 4u && image.rgba[3] == 255,
            "MAT.discard: solid colour zero decodes opaque");
    }
}

} // namespace sed::tests
