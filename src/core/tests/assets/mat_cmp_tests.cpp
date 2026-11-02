#include "test_common.hpp"

#include "sed/assets/cmp.hpp"
#include "sed/assets/mat.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sed::tests
{

void runMatCmpTests()
{
    const sed::assets::CmpPalette pal = sed::assets::makeGrayscalePalette();

    auto pushU32LE = [](std::vector<std::byte>& buf, const std::uint32_t v)
    {
        buf.push_back(static_cast<std::byte>(v & 0xFFu));
        buf.push_back(static_cast<std::byte>((v >> 8) & 0xFFu));
        buf.push_back(static_cast<std::byte>((v >> 16) & 0xFFu));
        buf.push_back(static_cast<std::byte>((v >> 24) & 0xFFu));
    };

    auto pushI32LE = [&](std::vector<std::byte>& buf, const std::int32_t v)
    {
        pushU32LE(buf, static_cast<std::uint32_t>(v));
    };

    auto pushColorInfo = [&](std::vector<std::byte>& buf, const sed::ColorInfo& ci)
    {
        pushU32LE(buf, static_cast<std::uint32_t>(ci.mode));
        pushU32LE(buf, ci.bpp);

        pushU32LE(buf, ci.redBPP);
        pushU32LE(buf, ci.greenBPP);
        pushU32LE(buf, ci.blueBPP);

        pushU32LE(buf, ci.redShl);
        pushU32LE(buf, ci.greenShl);
        pushU32LE(buf, ci.blueShl);

        pushU32LE(buf, ci.redShr);
        pushU32LE(buf, ci.greenShr);
        pushU32LE(buf, ci.blueShr);

        pushU32LE(buf, ci.alphaBPP);
        pushU32LE(buf, ci.alphaShl);
        pushU32LE(buf, ci.alphaShr);
    };

    {
        std::vector<std::byte> cmpBytes{};
        cmpBytes.resize(64);
        cmpBytes[0] = std::byte{ 'C' };
        cmpBytes[1] = std::byte{ 'M' };
        cmpBytes[2] = std::byte{ 'P' };
        cmpBytes[3] = std::byte{ ' ' };
        for (std::uint32_t i = 0; i < 256; ++i)
        {
            cmpBytes.push_back(static_cast<std::byte>(i));
            cmpBytes.push_back(static_cast<std::byte>(0));
            cmpBytes.push_back(static_cast<std::byte>(0));
        }

        const auto parsedPal = sed::assets::parseCmpPalette(cmpBytes);
        check(parsedPal.has_value(), "CMP.parseCmpPalette");
        if (parsedPal)
        {
            check((*parsedPal)[10].r == 10, "CMP.palette.r");
            check((*parsedPal)[10].g == 0, "CMP.palette.g");
            check((*parsedPal)[10].b == 0, "CMP.palette.b");
        }
    }

    {
        std::vector<std::byte> cmpBytes(64 + 256 * 3 + 64 * 256);
        cmpBytes[0] = std::byte{ 'C' };
        cmpBytes[1] = std::byte{ 'M' };
        cmpBytes[2] = std::byte{ 'P' };
        cmpBytes[3] = std::byte{ ' ' };
        for (std::uint32_t i = 0; i < 256; ++i)
        {
            cmpBytes[64 + i * 3 + 0] = static_cast<std::byte>(i);
            cmpBytes[64 + i * 3 + 1] = static_cast<std::byte>(255u - i);
            cmpBytes[64 + i * 3 + 2] = static_cast<std::byte>(i / 2u);
            for (std::uint32_t lightLevel = 0; lightLevel < 64; ++lightLevel)
            {
                cmpBytes[64 + 256 * 3 + lightLevel * 256 + i] =
                    static_cast<std::byte>((i + lightLevel + 17u) & 0xffu);
            }
        }

        const auto parsed = sed::assets::parseCmp(cmpBytes);
        check(parsed.has_value(),
            "CMP.parse retains the classic palette translation table");
        if (parsed)
        {
            check(parsed->lightLevels[0][0] == 17
                    && parsed->lightLevels[31][208] == 0
                    && parsed->lightLevels[63][176] == 0,
                "CMP.parse retains every classic 256-entry light-level table");
            check(parsed->translation[0] == 80
                    && parsed->translation[176] == 0,
                "CMP.parse reads the final classic 256-entry translation table");

            const sed::assets::CmpLightRamp solidRamp =
                sed::assets::makeCmpLightRamp(*parsed, 6);
            check(solidRamp[0].r == 23
                    && solidRamp[31].r == 54
                    && solidRamp[63].r == 86,
                "CMP.lightRamp resolves a Solid MAT palette index through all lighting tables");

            const sed::assets::CmpPalette translated =
                sed::assets::applyCmpTranslation(parsed->palette, parsed->translation);
            check(translated[0].r == 80
                    && translated[0].g == 175
                    && translated[176].r == 0
                    && translated[176].g == 255,
                "CMP.applyTranslation maps every output index through the retained table");
        }

        cmpBytes.resize(64 + 256 * 3 + 63 * 256 + 255);
        check(!sed::assets::parseCmp(cmpBytes).has_value(),
            "CMP.parse rejects a truncated classic translation table");
    }

    {
        std::vector<std::byte> mat{};
        mat.push_back(std::byte{ 'M' });
        mat.push_back(std::byte{ 'A' });
        mat.push_back(std::byte{ 'T' });
        mat.push_back(std::byte{ ' ' });
        pushI32LE(mat, 0x32);
        pushI32LE(mat, 2); // texture MAT
        pushI32LE(mat, 1);
        pushI32LE(mat, 1);

        sed::ColorInfo ci{};
        ci.mode = sed::ColorMode::Indexed;
        ci.bpp  = 8;
        pushColorInfo(mat, ci);

        pushI32LE(mat, 8);  // texture_type: sampled texture
        pushI32LE(mat, 37); // solidColor palette index
        for (int i = 0; i < 8; ++i)
        {
            pushI32LE(mat, 0);
        }

        pushI32LE(mat, 2);
        pushI32LE(mat, 2);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 1);

        mat.push_back(std::byte{ 0 });
        mat.push_back(std::byte{ 1 });
        mat.push_back(std::byte{ 2 });
        mat.push_back(std::byte{ 3 });

        const sed::assets::MatFile mf = sed::assets::MatFile::parse(std::move(mat), &pal);
        check(!mf.isColor(), "MAT.texture.isColor");
        check(mf.celCount() == 1, "MAT.texture.celCount");
        check(mf.cel(0).solidColorIndex == 37,
            "MAT.texture retains the per-cel solidColor palette index");
        check(mf.cel(0).solidColor.r == 37
                && mf.cel(0).solidColor.g == 37
                && mf.cel(0).solidColor.b == 37,
            "MAT.texture resolves the per-cel solidColor through its CMP palette");

        check(mf.cel(0).mipmaps.size() == 1, "MAT.texture.mipmaps.count");
        check(mf.cel(0).mipmaps[0].width == 2 && mf.cel(0).mipmaps[0].height == 2, "MAT.texture.mip0.size");
        check(mf.cel(0).mipmaps[0].format == sed::PixelFormat::Rgb8, "MAT.texture.mip0.format");
        check(mf.cel(0).mipmaps[0].pixels.size() == 12, "MAT.texture.mip0.pixels.size");
        check(mf.cel(0).mipmaps[0].pixels[9] == 3, "MAT.texture.mip0.pixel3.r");
        check(mf.cel(0).mipmaps[0].pixels[10] == 3, "MAT.texture.mip0.pixel3.g");
        check(mf.cel(0).mipmaps[0].pixels[11] == 3, "MAT.texture.mip0.pixel3.b");

        const sed::Rgba8Image img = mf.decodeCelRgba8(0);
        check(img.width == 2 && img.height == 2, "MAT.texture.size");
        check(img.rgba.size() == 16, "MAT.texture.rgba.size");
        check(img.rgba.size() >= 4 && img.rgba[0] == 0 && img.rgba[1] == 0 && img.rgba[2] == 0 && img.rgba[3] == 255,
              "MAT.texture.pixel0");
    }

    {
        std::vector<std::byte> mat{};
        mat.push_back(std::byte{ 'M' });
        mat.push_back(std::byte{ 'A' });
        mat.push_back(std::byte{ 'T' });
        mat.push_back(std::byte{ ' ' });
        pushI32LE(mat, 0x32);
        pushI32LE(mat, 2);
        pushI32LE(mat, 1);
        pushI32LE(mat, 1);

        sed::ColorInfo ci{};
        ci.mode = sed::ColorMode::Indexed;
        ci.bpp = 8;
        pushColorInfo(mat, ci);

        pushI32LE(mat, 8);
        pushI32LE(mat, 256);
        for (int i = 0; i < 8; ++i)
        {
            pushI32LE(mat, 0);
        }

        checkThrowsWithMessage(
            [&mat, &pal]()
            {
                (void)sed::assets::MatFile::parse(mat, &pal);
            },
            "MAT solid color index out of range",
            "MAT.texture rejects an out-of-range solidColor palette index before publishing a cel");
    }

    {
        std::vector<std::byte> mat{};
        mat.push_back(std::byte{ 'M' });
        mat.push_back(std::byte{ 'A' });
        mat.push_back(std::byte{ 'T' });
        mat.push_back(std::byte{ ' ' });
        pushI32LE(mat, 0x32);
        pushI32LE(mat, 2);
        pushI32LE(mat, 1);
        pushI32LE(mat, 1);

        sed::ColorInfo ci{};
        ci.mode = sed::ColorMode::Indexed;
        ci.bpp = 8;
        // Shipped JKDF2/MotS indexed MATs use only mode/bpp reliably. These
        // deliberately nonsensical unused channel fields model the observed
        // legacy bytes and must not control indexed transparency semantics.
        ci.redBPP = 0x7f7f7f7fu;
        ci.greenShl = 0xfefefefeu;
        ci.alphaBPP = 0;
        ci.alphaShl = 0xababababu;
        pushColorInfo(mat, ci);

        mat.resize(mat.size() + 40);
        pushI32LE(mat, 2);
        pushI32LE(mat, 1);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 1);
        mat.push_back(std::byte{ 0 });
        mat.push_back(std::byte{ 7 });

        const sed::assets::MatFile mf =
            sed::assets::MatFile::parse(std::move(mat), &pal);
        const sed::Rgba8Image image = mf.decodeCelRgba8(0);
        check(image.rgba.size() == 8
                && image.rgba[3] == 255
                && image.rgba[7] == 255,
            "MAT.indexed zero alpha marker keeps palette-index-zero opaque despite unrelated ColorInfo garbage");
    }

    {
        std::vector<std::byte> mat{};
        mat.push_back(std::byte{ 'M' });
        mat.push_back(std::byte{ 'A' });
        mat.push_back(std::byte{ 'T' });
        mat.push_back(std::byte{ ' ' });
        pushI32LE(mat, 0x32);
        pushI32LE(mat, 2);
        pushI32LE(mat, 1);
        pushI32LE(mat, 1);

        sed::ColorInfo ci{};
        ci.mode = sed::ColorMode::Indexed;
        ci.bpp = 8;
        ci.redBPP = 0xbabababbu;
        ci.alphaBPP = 0xbabababbu;
        ci.alphaShl = 0x804080ebu;
        ci.alphaShr = 0xffffffffu;
        pushColorInfo(mat, ci);

        mat.resize(mat.size() + 40);
        pushI32LE(mat, 2);
        pushI32LE(mat, 1);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 1);
        mat.push_back(std::byte{ 0 });
        mat.push_back(std::byte{ 7 });

        const sed::assets::MatFile mf =
            sed::assets::MatFile::parse(std::move(mat), &pal);
        const sed::Rgba8Image image = mf.decodeCelRgba8(0);
        check(image.rgba.size() == 8
                && image.rgba[3] == 0
                && image.rgba[7] == 255,
            "MAT.indexed nonzero legacy alpha marker treats only palette index zero as transparent");
    }

    {
        std::vector<std::byte> mat{};
        mat.push_back(std::byte{ 'M' });
        mat.push_back(std::byte{ 'A' });
        mat.push_back(std::byte{ 'T' });
        mat.push_back(std::byte{ ' ' });
        pushI32LE(mat, 0x32);
        pushI32LE(mat, 0); // color MAT
        pushI32LE(mat, 1);
        pushI32LE(mat, 0);

        sed::ColorInfo ci{};
        ci.mode = sed::ColorMode::Indexed;
        ci.bpp  = 8;
        pushColorInfo(mat, ci);

        pushI32LE(mat, 0); // textype
        pushI32LE(mat, 5); // colornum
        for (int i = 0; i < 4; ++i)
        {
            pushI32LE(mat, 0);
        }

        const sed::assets::MatFile mf = sed::assets::MatFile::parse(std::move(mat), &pal);
        check(mf.isColor(), "MAT.color.isColor");
        check(mf.celCount() == 1, "MAT.color.celCount");
        check(mf.cel(0).solidColorIndex == 5,
            "MAT.color retains its solid color palette index");
        check(mf.cel(0).solidColor.r == 5
                && mf.cel(0).solidColor.g == 5
                && mf.cel(0).solidColor.b == 5,
            "MAT.color resolves its solid color through its CMP palette");

        check(mf.cel(0).mipmaps.size() == 1, "MAT.color.mipmaps.count");
        check(mf.cel(0).mipmaps[0].width == 64 && mf.cel(0).mipmaps[0].height == 64, "MAT.color.mip0.size");
        check(mf.cel(0).mipmaps[0].format == sed::PixelFormat::Rgb8, "MAT.color.mip0.format");
        check(mf.cel(0).mipmaps[0].pixels.size() == 64 * 64 * 3, "MAT.color.mip0.pixels.size");
        check(mf.cel(0).mipmaps[0].pixels[0] == 5 && mf.cel(0).mipmaps[0].pixels[1] == 5 && mf.cel(0).mipmaps[0].pixels[2] == 5,
              "MAT.color.mip0.pixel0.rgb");
        check(mf.cel(0).mipmaps[0].pixels.back() == 5, "MAT.color.mip0.pixels[last]");

        const sed::Rgba8Image img = mf.decodeCelRgba8(0);
        check(img.width == 64 && img.height == 64, "MAT.color.size");
        check(img.rgba.size() == 64 * 64 * 4, "MAT.color.rgba.size");
        check(img.rgba.size() >= 4 && img.rgba[0] == 5 && img.rgba[1] == 5 && img.rgba[2] == 5 && img.rgba[3] == 255,
              "MAT.color.pixel0");
    }

    {
        std::vector<std::byte> mat{};
        mat.push_back(std::byte{ 'M' });
        mat.push_back(std::byte{ 'A' });
        mat.push_back(std::byte{ 'T' });
        mat.push_back(std::byte{ ' ' });
        pushI32LE(mat, 0x32);
        pushI32LE(mat, 2); // texture MAT
        pushI32LE(mat, 1);
        pushI32LE(mat, 1);

        sed::ColorInfo ci{};
        ci.mode     = sed::ColorMode::RGB;
        ci.bpp      = 16;
        ci.redBPP   = 5;
        ci.greenBPP = 6;
        ci.blueBPP  = 5;
        ci.redShl   = 11;
        ci.greenShl = 5;
        ci.blueShl  = 0;
        ci.redShr   = 3;
        ci.greenShr = 2;
        ci.blueShr  = 3;
        pushColorInfo(mat, ci);

        mat.resize(mat.size() + 40); // texture headers

        pushI32LE(mat, 2);
        pushI32LE(mat, 2);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 1);

        const std::uint16_t pixels[]{
            0xF800, // red
            0x07E0, // green
            0x001F, // blue
            0xFFFF, // white
        };

        for (const std::uint16_t p : pixels)
        {
            mat.push_back(static_cast<std::byte>(p & 0xFFu));
            mat.push_back(static_cast<std::byte>((p >> 8) & 0xFFu));
        }

        const sed::assets::MatFile mf = sed::assets::MatFile::parse(std::move(mat));
        check(mf.isTrueColor() && mf.bitsPerPixel() == 16u, "MAT.trueColor.16bpp declared layout is true colour");
        check(mf.cel(0).mipmaps[0].format == sed::PixelFormat::Rgba8, "MAT.16bpp.mip0.format");
        check(mf.cel(0).mipmaps[0].pixels.size() == 16, "MAT.16bpp.mip0.pixels.size");

        const sed::Rgba8Image img = mf.decodeCelRgba8(0);
        check(img.width == 2 && img.height == 2, "MAT.16bpp.size");
        check(img.rgba.size() >= 4 && img.rgba[0] == 255 && img.rgba[1] == 0 && img.rgba[2] == 0 && img.rgba[3] == 255, "MAT.16bpp.pixel0");
    }

    {
        std::vector<std::byte> mat{};
        mat.push_back(std::byte{ 'M' });
        mat.push_back(std::byte{ 'A' });
        mat.push_back(std::byte{ 'T' });
        mat.push_back(std::byte{ ' ' });
        pushI32LE(mat, 0x32);
        pushI32LE(mat, 2); // texture MAT
        pushI32LE(mat, 1);
        pushI32LE(mat, 1);

        sed::ColorInfo ci{};
        ci.mode     = sed::ColorMode::RGB;
        ci.bpp      = 24;
        ci.redBPP   = 8;
        ci.greenBPP = 8;
        ci.blueBPP  = 8;
        ci.redShl   = 16;
        ci.greenShl = 8;
        ci.blueShl  = 0;
        pushColorInfo(mat, ci);

        mat.resize(mat.size() + 40); // texture headers

        pushI32LE(mat, 2);
        pushI32LE(mat, 2);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 1);

        const std::uint8_t rgb[][3]{
            { 0x11, 0x22, 0x33 },
            { 0x44, 0x55, 0x66 },
            { 0x77, 0x88, 0x99 },
            { 0xAA, 0xBB, 0xCC },
        };

        for (const auto& p : rgb)
        {
            mat.push_back(static_cast<std::byte>(p[2])); // B
            mat.push_back(static_cast<std::byte>(p[1])); // G
            mat.push_back(static_cast<std::byte>(p[0])); // R
        }

        bool refused24 = false;
        try { (void)sed::assets::MatFile::parse(std::move(mat)); } catch (const std::runtime_error&) { refused24 = true; }
        check(refused24, "MAT.trueColor.24bpp is refused as unsupported bpp");
        // 24-bpp layout above is built only to be refused.
    }

    {
        std::vector<std::byte> mat{};
        mat.push_back(std::byte{ 'M' });
        mat.push_back(std::byte{ 'A' });
        mat.push_back(std::byte{ 'T' });
        mat.push_back(std::byte{ ' ' });
        pushI32LE(mat, 0x32);
        pushI32LE(mat, 2); // texture MAT
        pushI32LE(mat, 1);
        pushI32LE(mat, 1);

        sed::ColorInfo ci{};
        ci.mode     = sed::ColorMode::RGBA;
        ci.bpp      = 32;
        ci.redBPP   = 8;
        ci.greenBPP = 8;
        ci.blueBPP  = 8;
        ci.redShl   = 16;
        ci.greenShl = 8;
        ci.blueShl  = 0;
        ci.alphaBPP = 8;
        ci.alphaShl = 24;
        pushColorInfo(mat, ci);

        mat.resize(mat.size() + 40); // texture headers

        pushI32LE(mat, 2);
        pushI32LE(mat, 2);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 1);

        const std::uint8_t rgba[][4]{
            { 1, 2, 3, 4 },
            { 5, 6, 7, 8 },
            { 9, 10, 11, 12 },
            { 13, 14, 15, 16 },
        };

        for (const auto& p : rgba)
        {
            mat.push_back(static_cast<std::byte>(p[2])); // B
            mat.push_back(static_cast<std::byte>(p[1])); // G
            mat.push_back(static_cast<std::byte>(p[0])); // R
            mat.push_back(static_cast<std::byte>(p[3])); // A
        }

        const sed::assets::MatFile mf = sed::assets::MatFile::parse(std::move(mat));
        check(mf.cel(0).mipmaps[0].format == sed::PixelFormat::Rgba8, "MAT.32bpp.mip0.format");
        check(mf.cel(0).mipmaps[0].pixels.size() == 16, "MAT.32bpp.mip0.pixels.size");

        const sed::Rgba8Image img = mf.decodeCelRgba8(0);
        check(img.width == 2 && img.height == 2, "MAT.32bpp.size");
        check(img.rgba.size() >= 4 && img.rgba[0] == 1 && img.rgba[1] == 2 && img.rgba[2] == 3 && img.rgba[3] == 4, "MAT.32bpp.pixel0");
    }

    {
        std::vector<std::byte> mat{};
        mat.push_back(std::byte{ 'M' });
        mat.push_back(std::byte{ 'A' });
        mat.push_back(std::byte{ 'T' });
        mat.push_back(std::byte{ ' ' });
        pushI32LE(mat, 0x32);
        pushI32LE(mat, 2); // texture MAT
        pushI32LE(mat, 1);
        pushI32LE(mat, 1);

        sed::ColorInfo ci{};
        ci.mode = sed::ColorMode::Indexed;
        ci.bpp  = 8;
        pushColorInfo(mat, ci);

        mat.resize(mat.size() + 40); // texture headers

        pushI32LE(mat, 4);
        pushI32LE(mat, 4);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 3);

        for (std::uint8_t i = 0; i < 16; ++i)
        {
            mat.push_back(static_cast<std::byte>(i));
        }
        mat.push_back(static_cast<std::byte>(20));
        mat.push_back(static_cast<std::byte>(21));
        mat.push_back(static_cast<std::byte>(22));
        mat.push_back(static_cast<std::byte>(23));
        mat.push_back(static_cast<std::byte>(42));

        const sed::assets::MatFile mf = sed::assets::MatFile::parse(std::move(mat), &pal);
        check(mf.cel(0).mipmaps.size() == 3, "MAT.mipmaps.count");
        check(mf.cel(0).mipmaps[0].width == 4 && mf.cel(0).mipmaps[0].height == 4, "MAT.mipmaps.m0.size");
        check(mf.cel(0).mipmaps[1].width == 2 && mf.cel(0).mipmaps[1].height == 2, "MAT.mipmaps.m1.size");
        check(mf.cel(0).mipmaps[2].width == 1 && mf.cel(0).mipmaps[2].height == 1, "MAT.mipmaps.m2.size");
        check(mf.cel(0).mipmaps[2].format == sed::PixelFormat::Rgb8, "MAT.mipmaps.m2.format");
        check(mf.cel(0).mipmaps[2].pixels.size() == 3, "MAT.mipmaps.m2.pixels.size");
        check(mf.cel(0).mipmaps[2].pixels[0] == 42 && mf.cel(0).mipmaps[2].pixels[1] == 42 && mf.cel(0).mipmaps[2].pixels[2] == 42,
              "MAT.mipmaps.m2.pixel0.rgb");
    }

    {
        std::vector<std::byte> mat{};
        mat.push_back(std::byte{ 'M' });
        mat.push_back(std::byte{ 'A' });
        mat.push_back(std::byte{ 'T' });
        mat.push_back(std::byte{ ' ' });
        pushI32LE(mat, 0x32);
        pushI32LE(mat, 2); // texture MAT
        pushI32LE(mat, 2);
        pushI32LE(mat, 2);

        sed::ColorInfo ci{};
        ci.mode = sed::ColorMode::Indexed;
        ci.bpp  = 8;
        pushColorInfo(mat, ci);

        pushI32LE(mat, 8);
        pushI32LE(mat, 11);
        for (int i = 0; i < 8; ++i)
        {
            pushI32LE(mat, 0);
        }
        pushI32LE(mat, 8);
        pushI32LE(mat, 29);
        for (int i = 0; i < 8; ++i)
        {
            pushI32LE(mat, 0);
        }

        pushI32LE(mat, 2);
        pushI32LE(mat, 2);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 1);
        mat.push_back(static_cast<std::byte>(1));
        mat.push_back(static_cast<std::byte>(2));
        mat.push_back(static_cast<std::byte>(3));
        mat.push_back(static_cast<std::byte>(4));

        pushI32LE(mat, 2);
        pushI32LE(mat, 2);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 0);
        pushI32LE(mat, 1);
        mat.push_back(static_cast<std::byte>(5));
        mat.push_back(static_cast<std::byte>(6));
        mat.push_back(static_cast<std::byte>(7));
        mat.push_back(static_cast<std::byte>(8));

        const sed::assets::MatFile mf = sed::assets::MatFile::parse(std::move(mat), &pal);
        check(mf.celCount() == 2, "MAT.cels.count");
        check(mf.cel(0).solidColorIndex == 11
                && mf.cel(1).solidColorIndex == 29,
            "MAT.cels retain their independent solidColor palette indices");
        check(mf.cel(0).mipmaps[0].pixels[0] == 1, "MAT.cels.0.pixel0.r");
        check(mf.cel(1).mipmaps[0].pixels[0] == 5, "MAT.cels.1.pixel0.r");
    }

    {
        auto makeTcMat = [&](const sed::ColorInfo& ci, const std::int32_t w, const std::int32_t h, const std::int32_t alphaFlags, const std::vector<std::byte>& pixels) {
            std::vector<std::byte> mat{};
            mat.push_back(std::byte{ 'M' }); mat.push_back(std::byte{ 'A' }); mat.push_back(std::byte{ 'T' }); mat.push_back(std::byte{ ' ' });
            pushI32LE(mat, 0x32); pushI32LE(mat, 2); pushI32LE(mat, 1); pushI32LE(mat, 1);
            pushColorInfo(mat, ci);
            mat.resize(mat.size() + 40);
            pushI32LE(mat, w); pushI32LE(mat, h); pushI32LE(mat, alphaFlags); pushI32LE(mat, 0); pushI32LE(mat, 0); pushI32LE(mat, 1);
            mat.insert(mat.end(), pixels.begin(), pixels.end());
            return mat;
        };
        auto u16Bytes = [](const std::initializer_list<std::uint16_t> values) {
            std::vector<std::byte> bytes{};
            for (const std::uint16_t v : values) { bytes.push_back(static_cast<std::byte>(v & 0xFFu)); bytes.push_back(static_cast<std::byte>((v >> 8) & 0xFFu)); }
            return bytes;
        };
        auto u32Bytes = [](const std::initializer_list<std::uint32_t> values) {
            std::vector<std::byte> bytes{};
            for (const std::uint32_t v : values) { for (int s = 0; s < 32; s += 8) bytes.push_back(static_cast<std::byte>((v >> s) & 0xFFu)); }
            return bytes;
        };
        auto ciFor = [](const sed::ColorMode mode, const std::uint32_t bpp, const std::uint32_t rB, const std::uint32_t gB, const std::uint32_t bB, const std::uint32_t rS, const std::uint32_t gS, const std::uint32_t bS, const std::uint32_t aB = 0, const std::uint32_t aS = 0) {
            sed::ColorInfo ci{}; ci.mode = mode; ci.bpp = bpp; ci.redBPP = rB; ci.greenBPP = gB; ci.blueBPP = bB; ci.redShl = rS; ci.greenShl = gS; ci.blueShl = bS; ci.alphaBPP = aB; ci.alphaShl = aS; return ci;
        };
        auto expectRefusal = [&](std::vector<std::byte> bytes, const char* msg) {
            bool threw = false;
            try { (void)sed::assets::MatFile::parse(std::move(bytes)); } catch (const std::runtime_error&) { threw = true; }
            check(threw, msg);
        };
        const sed::ColorInfo rgb565 = ciFor(sed::ColorMode::RGB, 16, 5, 6, 5, 11, 5, 0);
        {
            const auto mf = sed::assets::MatFile::parse(makeTcMat(rgb565, 2, 2, 0, u16Bytes({ 0xF800, 0x07E0, 0x001F, 0x8410 })));
            check(mf.isTrueColor() && mf.cel(0).mipmaps[0].format == sed::PixelFormat::Rgba8, "MAT.trueColor.declared16 decodes to RGBA8");
            check(mf.decodeCelRgba8(0).rgba == std::vector<std::uint8_t>{ 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 132, 130, 132, 255 }, "MAT.trueColor.declared16 uses (v*255+max/2)/max rounding");
        }
        {
            const sed::ColorInfo legacy = ciFor(sed::ColorMode::RGB, 16, 0, 0, 0, 0, 0, 0);
            const auto mf = sed::assets::MatFile::parse(makeTcMat(legacy, 2, 1, 0, u16Bytes({ 0xF800, 0x001F })));
            check(mf.decodeCelRgba8(0).rgba == std::vector<std::uint8_t>{ 255, 0, 0, 255, 0, 0, 255, 255 }, "MAT.trueColor.legacy RGB565 fallback when no RGB declared and alpha flag clear");
        }
        {
            const sed::ColorInfo legacy = ciFor(sed::ColorMode::RGB, 16, 0, 0, 0, 0, 0, 0);
            const auto mf = sed::assets::MatFile::parse(makeTcMat(legacy, 2, 1, 1, u16Bytes({ 0xFC00, 0x001F })));
            check(mf.decodeCelRgba8(0).rgba == std::vector<std::uint8_t>{ 255, 0, 0, 255, 0, 0, 255, 0 }, "MAT.trueColor.legacy ARGB1555 fallback when no RGB declared and alpha flag set");
        }
        {
            const sed::ColorInfo rgb555 = ciFor(sed::ColorMode::RGB, 16, 5, 5, 5, 10, 5, 0);
            const auto masked = sed::assets::MatFile::parse(makeTcMat(rgb555, 2, 1, 1, u16Bytes({ 0x7C00, 0x83E0 })));
            check(masked.decodeCelRgba8(0).rgba == std::vector<std::uint8_t>{ 255, 0, 0, 0, 0, 255, 0, 255 }, "MAT.trueColor.free bit15 synthesizes a mask when alpha flag set");
            const auto unmasked = sed::assets::MatFile::parse(makeTcMat(rgb555, 2, 1, 0, u16Bytes({ 0x7C00, 0x83E0 })));
            check(unmasked.decodeCelRgba8(0).rgba == std::vector<std::uint8_t>{ 255, 0, 0, 255, 0, 255, 0, 255 }, "MAT.trueColor.bit15 is not a mask when alpha flag clear");
            const auto full = sed::assets::MatFile::parse(makeTcMat(rgb565, 1, 1, 1, u16Bytes({ 0xFFFF })));
            check(full.decodeCelRgba8(0).rgba == std::vector<std::uint8_t>{ 255, 255, 255, 255 }, "MAT.trueColor.masked RGB565 with no free bit stays opaque");
        }
        {
            const sed::ColorInfo argb8888 = ciFor(sed::ColorMode::RGBA, 32, 8, 8, 8, 16, 8, 0, 8, 24);
            const auto mf = sed::assets::MatFile::parse(makeTcMat(argb8888, 1, 1, 0, u32Bytes({ 0x80112233u })));
            check(mf.isTrueColor() && mf.bitsPerPixel() == 32u && mf.decodeCelRgba8(0).rgba == std::vector<std::uint8_t>{ 0x11, 0x22, 0x33, 0x80 }, "MAT.trueColor.declared32 decodes declared alpha");
            const sed::ColorInfo opaque8888 = ciFor(sed::ColorMode::RGBA, 32, 8, 8, 8, 16, 8, 0, 0, 0);
            const auto opaque = sed::assets::MatFile::parse(makeTcMat(opaque8888, 1, 1, 0, u32Bytes({ 0x00112233u })));
            check(opaque.decodeCelRgba8(0).rgba == std::vector<std::uint8_t>{ 0x11, 0x22, 0x33, 255 }, "MAT.trueColor.declared32 alpha defaults to 255 when absent");
        }
        {
            const sed::ColorInfo argb1555 = ciFor(sed::ColorMode::RGBA, 16, 5, 5, 5, 10, 5, 0, 1, 15);
            const auto mf = sed::assets::MatFile::parse(makeTcMat(argb1555, 2, 1, 0, u16Bytes({ 0xFC00, 0x03E0 })));
            check(mf.decodeCelRgba8(0).rgba == std::vector<std::uint8_t>{ 255, 0, 0, 255, 0, 255, 0, 0 }, "MAT.trueColor.declared alpha takes precedence over mask flag");
            const auto four = sed::assets::MatFile::parse(makeTcMat(ciFor(sed::ColorMode::RGBA, 16, 4, 4, 4, 8, 4, 0, 4, 12), 1, 1, 1, u16Bytes({ 0x8F71 })));
            check(four.decodeCelRgba8(0).rgba == std::vector<std::uint8_t>{ 0xFF, 0x77, 0x11, 0x88 }, "MAT.trueColor.declared ARGB4444 expands alpha with rounding");
        }
        expectRefusal(makeTcMat(ciFor(sed::ColorMode::RGB, 16, 5, 6, 0, 11, 5, 0), 1, 1, 0, u16Bytes({ 0 })), "MAT.trueColor.refusal partial layout with blue omitted");
        expectRefusal(makeTcMat(ciFor(sed::ColorMode::RGB, 16, 5, 0, 0, 11, 0, 0), 1, 1, 0, u16Bytes({ 0 })), "MAT.trueColor.refusal partial layout with only red declared");
        expectRefusal(makeTcMat(ciFor(sed::ColorMode::RGB, 16, 5, 6, 5, 11, 8, 0), 1, 1, 0, u16Bytes({ 0 })), "MAT.trueColor.refusal overlapping RGB channels");
        expectRefusal(makeTcMat(ciFor(sed::ColorMode::RGB, 16, 5, 6, 5, 12, 5, 0), 1, 1, 0, u16Bytes({ 0 })), "MAT.trueColor.refusal channel running past bit depth");
        expectRefusal(makeTcMat(ciFor(sed::ColorMode::RGB, 16, 9, 4, 3, 7, 3, 0), 1, 1, 0, u16Bytes({ 0 })), "MAT.trueColor.refusal channel wider than eight bits");
        expectRefusal(makeTcMat(ciFor(sed::ColorMode::RGBA, 32, 8, 8, 8, 16, 8, 0, 8, 16), 1, 1, 0, u32Bytes({ 0u })), "MAT.trueColor.refusal overlapping alpha channel");
        expectRefusal(makeTcMat(ciFor(sed::ColorMode::RGBA, 32, 0, 0, 0, 0, 0, 0), 1, 1, 0, u32Bytes({ 0u })), "MAT.trueColor.refusal 32bpp with no RGB layout");
        expectRefusal(makeTcMat(ciFor(sed::ColorMode::Indexed, 16, 5, 6, 5, 11, 5, 0), 1, 1, 0, u16Bytes({ 0 })), "MAT.trueColor.refusal 16bpp with indexed colour mode");
        expectRefusal(makeTcMat(ciFor(sed::ColorMode::IndexedCols, 32, 8, 8, 8, 16, 8, 0, 8, 24), 1, 1, 0, u32Bytes({ 0u })), "MAT.trueColor.refusal 32bpp with indexed-cols colour mode");
        expectRefusal(makeTcMat(ciFor(sed::ColorMode::RGB, 15, 5, 5, 5, 10, 5, 0), 1, 1, 0, u16Bytes({ 0 })), "MAT.trueColor.refusal unsupported 15bpp");
        expectRefusal(makeTcMat(rgb565, 2, 2, 0, u16Bytes({ 0xF800, 0x07E0, 0x001F })), "MAT.trueColor.refusal truncated 16bpp pixels");
        expectRefusal(makeTcMat(ciFor(sed::ColorMode::RGBA, 32, 8, 8, 8, 16, 8, 0, 8, 24), 2, 1, 0, u32Bytes({ 0xFF102030u })), "MAT.trueColor.refusal truncated 32bpp pixels");
    }

    {
        struct MipTexSpec
        {
            std::int32_t width{};
            std::int32_t height{};
            std::int32_t mipCount{};
            std::vector<std::byte> pixels{};
        };
        auto buildMipMat = [&](const std::vector<MipTexSpec>& texs) {
            std::vector<std::byte> mat{};
            mat.push_back(std::byte{ 'M' }); mat.push_back(std::byte{ 'A' }); mat.push_back(std::byte{ 'T' }); mat.push_back(std::byte{ ' ' });
            pushI32LE(mat, 0x32); pushI32LE(mat, 2);
            pushI32LE(mat, static_cast<std::int32_t>(texs.size())); pushI32LE(mat, static_cast<std::int32_t>(texs.size()));
            sed::ColorInfo ci{}; ci.mode = sed::ColorMode::Indexed; ci.bpp = 8; pushColorInfo(mat, ci);
            for (std::size_t t = 0; t < texs.size(); ++t) { pushI32LE(mat, 8); pushI32LE(mat, 37); for (int i = 0; i < 8; ++i) pushI32LE(mat, 0); }
            for (const MipTexSpec& tex : texs) { pushI32LE(mat, tex.width); pushI32LE(mat, tex.height); pushI32LE(mat, 0); pushI32LE(mat, 0); pushI32LE(mat, 0); pushI32LE(mat, tex.mipCount); mat.insert(mat.end(), tex.pixels.begin(), tex.pixels.end()); }
            return mat;
        };
        auto byteRange = [](const std::uint8_t first, const std::size_t count) {
            std::vector<std::byte> bytes{}; for (std::size_t i = 0; i < count; ++i) bytes.push_back(static_cast<std::byte>(first + static_cast<std::uint8_t>(i))); return bytes;
        };
        auto expectMipThrow = [&](std::vector<std::byte> bytes, const char* msg) {
            bool threw = false; try { (void)sed::assets::MatFile::parse(std::move(bytes), &pal); } catch (const std::runtime_error&) { threw = true; } check(threw, msg);
        };
        {
            std::vector<std::byte> pixels = byteRange(0, 16); pixels.push_back(std::byte{ 20 }); pixels.push_back(std::byte{ 21 });
            const auto mf = sed::assets::MatFile::parse(buildMipMat({ { 4, 4, 3, pixels } }), &pal);
            check(mf.mipFallbackNote() == "Retail mip fallback: mip level 1 of 3 levels (2x2) needs 4 bytes but 2 remain; showing complete mip level 0, as retail JK does on faces nearer than the level's MipMap Distances; retail faces far enough to use mip level 1 or later show data the file never filled.", "MAT.mipFallback.shortMip1 records the retail fallback note");
            check(mf.cel(0).mipmaps.size() == 1, "MAT.mipFallback.shortMip1 keeps only the complete mip 0");
            check(mf.cel(0).mipmaps[0].width == 4 && mf.cel(0).mipmaps[0].height == 4 && mf.cel(0).mipmaps[0].pixels.size() == 48, "MAT.mipFallback.shortMip1 mip0 size");
            check(mf.cel(0).mipmaps[0].pixels[9] == 3 && mf.cel(0).mipmaps[0].pixels[10] == 3 && mf.cel(0).mipmaps[0].pixels[11] == 3, "MAT.mipFallback.shortMip1 mip0 pixel3");
            const auto img = mf.decodeCelRgba8(0);
            check(img.rgba.size() == 64 && img.rgba[12] == 3 && img.rgba[13] == 3 && img.rgba[14] == 3 && img.rgba[15] == 255, "MAT.mipFallback.shortMip1 decodeCelRgba8 uses mip 0");
        }
        {
            std::vector<std::byte> pixels = byteRange(0, 16); const auto mip1 = byteRange(20, 4); pixels.insert(pixels.end(), mip1.begin(), mip1.end());
            const auto mf = sed::assets::MatFile::parse(buildMipMat({ { 4, 4, 3, pixels } }), &pal);
            check(mf.cel(0).mipmaps.size() == 2, "MAT.mipFallback.shortMip2 keeps complete mips 0 and 1");
            check(mf.mipFallbackNote().find("mip level 2 of 3 levels (1x1) needs 1 bytes but 0 remain") != std::string::npos, "MAT.mipFallback.shortMip2 note names the short mip");
        }
        {
            std::vector<std::byte> pixels = byteRange(0, 21);
            const auto mf = sed::assets::MatFile::parse(buildMipMat({ { 4, 4, 3, pixels } }), &pal);
            check(mf.mipFallbackNote().empty(), "MAT.mipFallback.complete chain has no fallback note");
            check(mf.cel(0).mipmaps.size() == 3, "MAT.mipFallback.complete chain keeps every mip");
        }
        expectMipThrow(buildMipMat({ { 4, 4, 3, byteRange(0, 15) } }), "MAT.mipFallback.short mip 0 still throws");
        expectMipThrow(buildMipMat({ { 4, 4, 4, byteRange(0, 16) } }), "MAT.mipFallback.impossible mip count still throws");
        expectMipThrow(buildMipMat({ { 1, 1, 2, byteRange(0, 1) } }), "MAT.mipFallback.mip count beyond a 1x1 texture still throws");
        {
            auto mat = buildMipMat({ { 4, 4, 3, byteRange(0, 21) }, { 2, 2, 1, byteRange(30, 4) } });
            mat.resize(76 + 2 * 40 + 24 + 16 + 4);
            expectMipThrow(std::move(mat), "MAT.mipFallback.short earlier texture still throws");
        }
        {
            const auto mf = sed::assets::MatFile::parse(buildMipMat({ { 2, 2, 1, byteRange(30, 4) }, { 4, 4, 3, byteRange(0, 16) } }), &pal);
            check(mf.celCount() == 2 && mf.cel(1).mipmaps.size() == 1 && !mf.mipFallbackNote().empty(), "MAT.mipFallback.fallback applies to the final texture of a multi-texture file");
            check(mf.cel(1).mipmaps[0].pixels[9] == 3, "MAT.mipFallback.final texture mip0 pixel3");
        }
    }
}

} // namespace sed::tests
