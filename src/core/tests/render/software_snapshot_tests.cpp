#include "test_common.hpp"

#include "sed/level/overview.hpp"
#include "sed/render/software_snapshot.hpp"

#include <array>
#include <limits>

namespace sed::tests
{

namespace
{

Level quadLevel()
{
    Level level{};
    Sector sector{};
    sector.ambient = ColorF{ 1.0f, 1.0f, 1.0f };
    sector.vertices = { Vertex{ -1, -1, 0 }, Vertex{ 1, -1, 0 }, Vertex{ 1, 1, 0 }, Vertex{ -1, 1, 0 } };
    Surface floor{};
    floor.geo = 4;
    floor.material = "wall.mat";
    floor.normal = TVector{ 0, 0, 1 };
    floor.vertices = { 0, 1, 2, 3 };
    floor.texVertices = { TexVertex{ 0, 0 }, TexVertex{ 1, 0 }, TexVertex{ 1, 1 }, TexVertex{ 0, 1 } };
    sector.surfaces = { floor };
    level.sectors = { sector };
    return level;
}

render::SnapshotMaterial redMaterial()
{
    render::SnapshotMaterial mat{};
    mat.image.width = 1;
    mat.image.height = 1;
    mat.image.rgba = { 200, 30, 40, 255 };
    mat.trueColor = true;
    mat.alphaBpp = 8;
    return mat;
}

bool isPng(const std::vector<std::uint8_t>& bytes)
{
    return bytes.size() > 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G';
}

} // namespace

void runSoftwareSnapshotTests()
{
    using namespace sed::render;
    const Level level = quadLevel();
    const SnapshotMaterial red = redMaterial();
    const SnapshotMaterialResolver resolver =
        [&](std::string_view name) -> const SnapshotMaterial* {
        return name == "wall.mat" ? &red : nullptr;
    };

    SoftwareSnapshotOptions options{};
    options.width = 64;
    options.height = 64;
    options.fullbright = true;
    options.camera.position = TVector{ 0, 0, 5 };
    options.camera.target = TVector{ 0, 0, 0 };
    options.camera.orthographic = true;
    options.camera.orthographicHeight = 4.0;

    const SoftwareSnapshotResult ortho = renderSoftwareSnapshot(level, resolver, options);
    check(isPng(ortho.png), "snapshot.png has PNG signature");
    check(ortho.surfacesDrawn == 1 && ortho.trianglesDrawn == 2, "snapshot draws the quad as two triangles");
    check(ortho.materialsUsed == 1 && ortho.materialsMissing == 0, "snapshot resolves the material");

    // Decode the stored PNG back: centre pixel is the texel, corners
    // are background. The encoder is stored-block zlib, so inflate
    // via a minimal check on the raw scanlines is unnecessary here;
    // instead re-render through encodePngRgba8 round-trip sizes.
    check(ortho.png.size() > 64u * 64u, "snapshot.png carries pixel data");

    // Missing material still renders (flat) and reports missing.
    const SnapshotMaterialResolver none =
        [](std::string_view) -> const SnapshotMaterial* { return nullptr; };
    const SoftwareSnapshotResult flat = renderSoftwareSnapshot(level, none, options);
    check(flat.materialsMissing == 1 && isPng(flat.png), "snapshot flat-shades missing materials");

    // A portal (an adjoin with no material) is not drawn and is not a
    // missing material: from above, a portal over the floor hides nothing.
    {
        Level portal = level;
        Sector& s = portal.sectors[0];
        const std::int32_t base = static_cast<std::int32_t>(s.vertices.size());
        s.vertices.push_back(Vertex{ -1, -1, 1 });
        s.vertices.push_back(Vertex{ 1, -1, 1 });
        s.vertices.push_back(Vertex{ 1, 1, 1 });
        s.vertices.push_back(Vertex{ -1, 1, 1 });
        Surface door{};
        door.normal = TVector{ 0, 0, 1 };
        door.adjoin = SurfaceRef{};
        door.vertices = { base, base + 1, base + 2, base + 3 };
        s.surfaces.push_back(door);
        const SoftwareSnapshotResult seen = renderSoftwareSnapshot(portal, resolver, options);
        check(seen.portalsSkipped == 1 && seen.surfacesDrawn == 1 && seen.materialsMissing == 0
                  && seen.png == ortho.png,
              "snapshot skips portals: the floor under one renders unchanged");
    }

    // Core adjoin creation retains the source material while setting geo=0.
    // Hidden geometry must neither occlude nor ask for that retained material.
    {
        SnapshotMaterial blue = red;
        blue.image.rgba = { 20, 40, 220, 255 };
        int portalLookups = 0;
        const SnapshotMaterialResolver layers = [&](const std::string_view name) -> const SnapshotMaterial*
        {
            if ( name == "portal.mat" )
            {
                ++portalLookups;
                return &blue;
            }
            return name == "wall.mat" ? &red : nullptr;
        };
        Level portal = level;
        auto& sector = portal.sectors[0];
        const auto base = static_cast<std::int32_t>(sector.vertices.size());
        sector.vertices.insert(sector.vertices.end(), {
            Vertex{ -1, -1, 1 }, Vertex{ 1, -1, 1 }, Vertex{ 1, 1, 1 }, Vertex{ -1, 1, 1 } });
        Surface door{};
        door.geo = 0;
        door.material = "portal.mat";
        door.normal = { 0, 0, 1 };
        door.adjoin = SurfaceRef{};
        door.vertices = { base, base + 1, base + 2, base + 3 };
        door.texVertices = level.sectors[0].surfaces[0].texVertices;
        sector.surfaces.push_back(door);
        for ( const bool orthographic : { false, true } )
        {
            SoftwareSnapshotOptions view = options;
            view.camera.orthographic = orthographic;
            const auto expected = renderSoftwareSnapshot(level, layers, view);
            portalLookups = 0;
            const auto hidden = renderSoftwareSnapshot(portal, layers, view);
            check(hidden.png == expected.png && hidden.surfacesDrawn == 1
                    && hidden.trianglesDrawn == 2 && hidden.portalsSkipped == 1,
                "snapshot geo0 material-bearing portal leaves the wall beyond unchanged");
            check(portalLookups == 0 && hidden.materialsUsed == 1 && hidden.materialsMissing == 0,
                "snapshot hidden material-bearing portal never resolves or reports its material");
        }
        portal.sectors[0].surfaces.back().adjoin.reset();
        const auto hiddenSolid = renderSoftwareSnapshot(portal, layers, options);
        check(hiddenSolid.png == ortho.png && hiddenSolid.surfacesDrawn == 1
                && hiddenSolid.portalsSkipped == 0,
            "snapshot geo0 also hides non-adjoined material-bearing geometry");

        // Solid/Textured controls prove that hiding portals does not suppress
        // an intentionally visible adjoined boundary that has a material.
        SoftwareSnapshotOptions perspective = options;
        perspective.camera.orthographic = false;
        const auto wall = renderSoftwareSnapshot(level, layers, perspective);
        portal.sectors[0].surfaces.back().adjoin = SurfaceRef{};
        for ( const std::int32_t geo : { 3, 4 } )
        {
            portal.sectors[0].surfaces.back().geo = geo;
            portalLookups = 0;
            const auto visible = renderSoftwareSnapshot(portal, layers, perspective);
            check(visible.png != wall.png && visible.surfacesDrawn == 2 && visible.portalsSkipped == 0
                    && visible.materialsUsed == 2 && portalLookups == 1,
                "snapshot retains visible geo3/4 material-bearing adjoined boundaries");
        }
    }

    // Orthographic depth must use view-space distance rather than invZ=1.
    // Both quads fill the same 4x4 footprint, so every pixel must be the
    // nearer blue texel regardless of surface submission order.
    {
        Level layers = level;
        auto& sector = layers.sectors[0];
        const auto base = static_cast<std::int32_t>(sector.vertices.size());
        sector.vertices.insert(sector.vertices.end(), {
            Vertex{ -1, -1, 1 }, Vertex{ 1, -1, 1 }, Vertex{ 1, 1, 1 }, Vertex{ -1, 1, 1 } });
        Surface near = sector.surfaces[0];
        near.material = "near.mat";
        near.vertices = { base, base + 1, base + 2, base + 3 };
        sector.surfaces.push_back(near);
        SnapshotMaterial blue = red;
        blue.image.rgba = { 20, 40, 220, 255 };
        const SnapshotMaterialResolver resolveLayers = [&](const std::string_view name) -> const SnapshotMaterial*
        {
            return name == "near.mat" ? &blue : &red;
        };
        SoftwareSnapshotOptions tiny = options;
        tiny.width = 4;
        tiny.height = 4;
        tiny.camera.orthographicHeight = 2;
        std::vector<std::uint8_t> expectedPixels;
        for ( int pixel = 0; pixel < 16; ++pixel )
        {
            expectedPixels.insert(expectedPixels.end(), { 20, 40, 220, 255 });
        }
        const auto expectedPng = encodePngRgba8(4, 4, expectedPixels);
        const auto farFirst = renderSoftwareSnapshot(layers, resolveLayers, tiny);
        std::swap(sector.surfaces[0], sector.surfaces[1]);
        const auto nearFirst = renderSoftwareSnapshot(layers, resolveLayers, tiny);
        check(farFirst.png == expectedPng && nearFirst.png == expectedPng,
            "snapshot orthographic depth selects the nearer blue texel at every pixel in either order");
        check(farFirst.surfacesDrawn == 2 && nearFirst.surfacesDrawn == 2,
            "snapshot orthographic depth test submits both visible surfaces");
    }

    // Finite accepted geometry can project far beyond the integer pixel
    // range. Clip the raster bounds before narrowing, including thin images.
    {
        Level wide = level;
        for ( auto& vertex : wide.sectors[0].vertices )
        {
            vertex.x *= 1000000.0;
            vertex.y *= 1000000.0;
        }
        const auto solidPng = [](const int width, const int height, const std::array<std::uint8_t, 4> color)
        {
            std::vector<std::uint8_t> pixels;
            for ( int pixel = 0; pixel < width * height; ++pixel )
            {
                pixels.insert(pixels.end(), color.begin(), color.end());
            }
            return encodePngRgba8(width, height, pixels);
        };
        for ( const std::array<int, 2> size : { std::array{ 64, 64 }, std::array{ 1, 1 },
                 std::array{ 1, 3 }, std::array{ 3, 1 } } )
        {
            SoftwareSnapshotOptions view = options;
            view.width = size[0];
            view.height = size[1];
            view.camera.orthographicHeight = 0.001;
            const auto result = renderSoftwareSnapshot(wide, resolver, view);
            check(result.png == solidPng(size[0], size[1], { 200, 30, 40, 255 }),
                "snapshot clamps oversized finite screen bounds before integer conversion");
        }
        SoftwareSnapshotOptions view = options;
        view.camera.orthographicHeight = 0.001;
        SoftwareSnapshotOptions perspective = view;
        perspective.camera.orthographic = false;
        perspective.camera.fieldOfViewDegrees = 0.001;
        check(renderSoftwareSnapshot(wide, resolver, perspective).png
                == solidPng(64, 64, { 200, 30, 40, 255 }),
            "snapshot safely clips oversized finite perspective projections too");
        Level outside = wide;
        for ( auto& vertex : outside.sectors[0].vertices )
        {
            vertex.x = vertex.x < 0 ? 500000.0 : 1000000.0;
        }
        const auto outsideResult = renderSoftwareSnapshot(outside, resolver, view);
        check(outsideResult.png == solidPng(64, 64, { 0, 0, 0, 255 })
                && outsideResult.trianglesDrawn == 2,
            "snapshot clamping does not draw geometry wholly outside the viewport");

        const SnapshotMaterialResolver noMaterial = [](std::string_view) -> const SnapshotMaterial*
        {
            return nullptr;
        };
        for ( const double extent : { 1e150, std::numeric_limits<double>::max() / 2.0 } )
        {
            Level overflowing = level;
            for ( auto& vertex : overflowing.sectors[0].vertices )
            {
                vertex.x *= extent;
                vertex.y *= extent;
            }
            checkThrowsWithMessage(
                [&]() { (void)renderSoftwareSnapshot(overflowing, noMaterial, view); },
                "snapshot: non-finite projected geometry",
                "snapshot refuses non-finite projection or raster area from finite geometry");
        }
        for ( const std::array<int, 2> size : { std::array{ 0, 1 }, std::array{ 1, 0 },
                 std::array{ -1, 1 }, std::array{ 1, -1 } } )
        {
            view.width = size[0];
            view.height = size[1];
            checkThrowsWithMessage(
                [&]() { (void)renderSoftwareSnapshot(level, resolver, view); },
                "snapshot dimensions must be positive",
                "snapshot preserves invalid width and height refusal at the pixel boundary");
        }
    }

    // Cull back faces: same camera under the floor sees nothing.
    SoftwareSnapshotOptions below = options;
    below.camera.position = TVector{ 0, 0, -5 };
    below.cullBackFaces = true;
    const SoftwareSnapshotResult culled = renderSoftwareSnapshot(level, resolver, below);
    check(culled.surfacesDrawn == 0, "snapshot cullBackFaces discards the floor from below");
    below.cullBackFaces = false;
    const SoftwareSnapshotResult kept = renderSoftwareSnapshot(level, resolver, below);
    check(kept.surfacesDrawn == 1, "snapshot without culling draws the floor from below");

    // Perspective renders too.
    SoftwareSnapshotOptions persp = options;
    persp.camera.orthographic = false;
    persp.camera.fieldOfViewDegrees = 60.0;
    const SoftwareSnapshotResult perspResult = renderSoftwareSnapshot(level, resolver, persp);
    check(perspResult.surfacesDrawn == 1 && isPng(perspResult.png), "snapshot perspective renders");

    // Overview framing feeds the renderer (D-09 end to end).
    const auto bounds = level::worldSurfaceBounds(level);
    check(bounds.has_value(), "snapshot overview bounds exist");
    if (bounds)
    {
        const level::OverviewCamera cam =
            level::computeOverviewCamera(*bounds, level::kOverviewOverhead, level::kOverviewWren, 64, 64, 0.025);
        SoftwareSnapshotOptions framed = options;
        framed.camera.position = cam.position;
        framed.camera.target = cam.target;
        framed.camera.orthographicHeight = cam.orthographicHeight;
        framed.camera.nearPlane = cam.nearPlane;
        framed.camera.farPlane = cam.farPlane;
        const SoftwareSnapshotResult framedResult = renderSoftwareSnapshot(level, resolver, framed);
        check(framedResult.surfacesDrawn == 1, "snapshot renders the overview-framed camera");
    }

    // Pixel-level check via a direct rasterization of the same scene
    // at 4x4: decode PNG by inflating stored blocks manually.
    {
        SoftwareSnapshotOptions tiny = options;
        tiny.width = 4;
        tiny.height = 4;
        tiny.camera.orthographicHeight = 4.0;
        const SoftwareSnapshotResult tinyResult = renderSoftwareSnapshot(level, resolver, tiny);
        // Stored-block PNG: scanline data sits after the 8-byte
        // signature, IHDR chunk (25 bytes), IDAT header (8 bytes)
        // and the 5-byte first stored-block header + 2-byte zlib
        // header. Find the IDAT payload by scanning for "IDAT".
        const auto& png = tinyResult.png;
        std::size_t idat = 0;
        for (std::size_t i = 0; i + 4 < png.size(); ++i)
        {
            if (png[i] == 'I' && png[i + 1] == 'D' && png[i + 2] == 'A' && png[i + 3] == 'T')
            {
                idat = i + 4;
                break;
            }
        }
        check(idat > 0, "snapshot tiny PNG has IDAT");
        if (idat)
        {
            // zlib header 2 bytes, then stored blocks.
            std::size_t p = idat + 2;
            std::vector<std::uint8_t> raw{};
            while (p + 5 <= png.size())
            {
                const bool last = (png[p] & 1u) != 0;
                const std::size_t len = png[p + 1] | (static_cast<std::size_t>(png[p + 2]) << 8);
                raw.insert(raw.end(), png.begin() + static_cast<std::ptrdiff_t>(p + 5),
                           png.begin() + static_cast<std::ptrdiff_t>(p + 5 + len));
                p += 5 + len;
                if (last) break;
            }
            check(raw.size() == 4u * (1u + 4u * 4u), "snapshot tiny PNG raw size");
            if (raw.size() == 4u * 17u)
            {
                auto px = [&](const int x, const int y) {
                    const std::size_t at = static_cast<std::size_t>(y) * 17u + 1u
                        + static_cast<std::size_t>(x) * 4u;
                    return std::array<std::uint8_t, 4>{ raw[at], raw[at + 1], raw[at + 2], raw[at + 3] };
                };
                const auto centre = px(2, 2);
                check(centre[0] == 200 && centre[1] == 30 && centre[2] == 40 && centre[3] == 255,
                      "snapshot centre pixel is the material texel");
                const auto corner = px(0, 0);
                check(corner[0] == 0 && corner[1] == 0 && corner[2] == 0,
                      "snapshot corner pixel is background");
            }
        }
    }
}

} // namespace sed::tests
