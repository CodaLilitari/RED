#include "test_common.hpp"

#include "sed/level/overview.hpp"
#include "sed/render/software_snapshot.hpp"

#include <array>

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
    floor.geo = 1;
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
