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

// The scanlines of a PNG written by encodePngRgba8 (stored zlib blocks):
// each row is one filter byte and width RGBA pixels. Empty when the bytes
// are not such a PNG.
std::vector<std::uint8_t> storedPngScanlines(const std::vector<std::uint8_t>& png)
{
    std::size_t idat = 0;
    for ( std::size_t i = 0; i + 4 < png.size(); ++i )
    {
        if ( png[i] == 'I' && png[i + 1] == 'D' && png[i + 2] == 'A' && png[i + 3] == 'T' )
        {
            idat = i + 4;
            break;
        }
    }
    std::vector<std::uint8_t> raw{};
    if ( idat == 0 )
    {
        return raw;
    }
    std::size_t p = idat + 2;
    while ( p + 5 <= png.size() )
    {
        const bool last = (png[p] & 1u) != 0;
        const std::size_t len = png[p + 1] | (static_cast<std::size_t>(png[p + 2]) << 8);
        if ( p + 5 + len > png.size() )
        {
            return {};
        }
        raw.insert(raw.end(), png.begin() + static_cast<std::ptrdiff_t>(p + 5),
            png.begin() + static_cast<std::ptrdiff_t>(p + 5 + len));
        p += 5 + len;
        if ( last )
        {
            break;
        }
    }
    return raw;
}

using Rgb = std::array<std::uint8_t, 3>;

// A wall on the plane y = 2 that faces the room on its -Y side, seen from
// that room by a camera looking along +Y: +X is to the viewer's right and +Z
// is up. The 2x2 texture has one colour per texel, and the snapshot is 2x2
// pixels, so each pixel shows one quadrant of the wall.
std::array<Rgb, 4> wallQuadrantColours(const bool uTowardViewerRight)
{
    using namespace sed::render;
    Level level{};
    level.uvCoordinateSpace = UvCoordinateSpace::Normalized;
    Sector sector{};
    sector.ambient = ColorF{ 1.0f, 1.0f, 1.0f };
    sector.vertices = { Vertex{ -1, 2, -1 }, Vertex{ -1, 2, 1 }, Vertex{ 1, 2, 1 }, Vertex{ 1, 2, -1 } };
    Surface wall{};
    wall.geo = 4;
    wall.material = "quadrants.mat";
    wall.normal = TVector{ 0, -1, 0 };
    wall.vertices = { 0, 1, 2, 3 };
    // V runs down the wall in both cases; only the sense of U differs.
    const float left = uTowardViewerRight ? 0.0f : 1.0f;
    const float right = 1.0f - left;
    wall.texVertices = { TexVertex{ left, 1 }, TexVertex{ left, 0 }, TexVertex{ right, 0 }, TexVertex{ right, 1 } };
    sector.surfaces = { wall };
    level.sectors = { sector };

    SnapshotMaterial quadrants{};
    quadrants.image.width = 2;
    quadrants.image.height = 2;
    quadrants.image.rgba = {
        255, 0, 0, 255, /* row 0, column 0 */ 0, 255, 0, 255, /* row 0, column 1 */
        0, 0, 255, 255, /* row 1, column 0 */ 255, 255, 255, 255 /* row 1, column 1 */ };
    quadrants.trueColor = true;
    quadrants.alphaBpp = 8;
    const SnapshotMaterialResolver resolver =
        [&](std::string_view name) -> const SnapshotMaterial* {
        return name == "quadrants.mat" ? &quadrants : nullptr;
    };

    SoftwareSnapshotOptions options{};
    options.width = 2;
    options.height = 2;
    options.fullbright = true;
    options.camera.position = TVector{ 0, -3, 0 };
    options.camera.target = TVector{ 0, 2, 0 };
    options.camera.orthographic = true;
    options.camera.orthographicHeight = 2.0;
    const std::vector<std::uint8_t> raw = storedPngScanlines(renderSoftwareSnapshot(level, resolver, options).png);
    std::array<Rgb, 4> seen{};
    if ( raw.size() != 2u * (1u + 2u * 4u) )
    {
        return seen;
    }
    for ( std::size_t y = 0; y < 2; ++y )
    {
        for ( std::size_t x = 0; x < 2; ++x )
        {
            const std::size_t at = y * 9u + 1u + x * 4u;
            seen[y * 2u + x] = Rgb{ raw[at], raw[at + 1], raw[at + 2] };
        }
    }
    return seen;
}

using namespace sed::render;

// A white wall on the plane y = 2 facing the camera at y = -3, drawn 2x1
// pixels wide in an orthographic view: the left pixel shows the corners at
// x = -1 and the right pixel the corners at x = 1.
struct LitWall
{
    Level level{};
    SnapshotMaterial white{};
};

LitWall litWall(const float leftLight, const float rightLight)
{
    LitWall wall{};
    wall.level.uvCoordinateSpace = UvCoordinateSpace::Normalized;
    Sector sector{};
    sector.ambient = ColorF{ 0.5f, 0.5f, 0.5f };
    sector.vertices = { Vertex{ -1, 2, -1 }, Vertex{ -1, 2, 1 }, Vertex{ 1, 2, 1 }, Vertex{ 1, 2, -1 } };
    Surface face{};
    face.geo = 4;
    face.light = 3;
    face.material = "white.mat";
    face.normal = TVector{ 0, -1, 0 };
    face.vertices = { 0, 1, 2, 3 };
    const auto corner = [](const float u, const float v, const float light)
    {
        TexVertex vertex{ u, v };
        vertex.color = ColorF{ light, light, light, light };
        return vertex;
    };
    face.texVertices = { corner(0, 1, leftLight), corner(0, 0, leftLight), corner(1, 0, rightLight),
        corner(1, 1, rightLight) };
    sector.surfaces = { face };
    wall.level.sectors = { sector };
    wall.white.image.width = 1;
    wall.white.image.height = 1;
    wall.white.image.rgba = { 200, 200, 200, 255 };
    wall.white.trueColor = true;
    wall.white.alphaBpp = 8;
    return wall;
}

// Red channel of the left and right pixels.
std::array<int, 2> litWallReds(const LitWall& wall, const SoftwareSnapshotOptions& base)
{
    const SnapshotMaterial white = wall.white;
    const SnapshotMaterialResolver resolver =
        [&](std::string_view name) -> const SnapshotMaterial* {
        return name == "white.mat" ? &white : nullptr;
    };
    SoftwareSnapshotOptions options = base;
    options.width = 2;
    options.height = 1;
    options.camera.position = TVector{ 0, -3, 0 };
    options.camera.target = TVector{ 0, 2, 0 };
    options.camera.orthographic = true;
    options.camera.orthographicHeight = 1.0;
    const std::vector<std::uint8_t> raw = storedPngScanlines(renderSoftwareSnapshot(wall.level, resolver, options).png);
    if ( raw.size() != 1u + 2u * 4u )
    {
        return { -1, -1 };
    }
    return { raw[1], raw[5] };
}

bool near(const int value, const double expected)
{
    return std::abs(value - expected) <= 2.0;
}

void runIndexedLightingTests()
{
    LitWall wall = litWall(0.5f, 0.5f);
    auto& sector = wall.level.sectors[0];
    sector.colormapIndex = 1;
    wall.white.indexed = assets::IndexedMatCel{2, 1, 37, false, true, {1, 2}};
    SoftwareSnapshotOptions options{};
    options.indexed8bit = true;
    options.lighting = SnapshotLighting::baked;
    options.indexedColormaps.resize(2);
    auto& palette = options.indexedColormaps[0].palette;
    palette[0].r = 9;
    palette[1].r = palette[2].r = 55;
    palette[17].r = 170;
    palette[88].r = 188;
    auto& row = options.indexedColormaps[1].lightLevels[31];
    row[1] = 17;
    row[2] = 0;
    row[37] = 88;
    options.colormapTints = {{}, {0, 0, 0}};
    check(litWallReds(wall, options) == std::array<int, 2>{170, 9},
        "indexed snapshot distinguishes equal-RGB source indices using the owner CMP then master palette");
    wall.white.indexed->indices[0] = 0;
    row[0] = 17;
    check(litWallReds(wall, options) == std::array<int, 2>{0, 9},
        "indexed snapshot discards only original keyed zero before translation");
    wall.white.indexed->keyed = false;
    check(litWallReds(wall, options) == std::array<int, 2>{170, 9},
        "opaque original zero remains visible");
    sector.surfaces[0].geo = 3;
    check(litWallReds(wall, options) == std::array<int, 2>{188, 188},
        "Solid geometry uses the separate MAT Solid color");
    sector.surfaces[0].geo = 4;
    sector.surfaces[0].light = 0;
    check(litWallReds(wall, options) == std::array<int, 2>{9, 55},
        "fully lit bypasses the entire mapping table");
    sector.surfaces[0].light = 3;
    for (auto& vertex : sector.surfaces[0].texVertices) vertex.color.a = 1;
    options.indexedColormaps[1].lightLevels[63][0] = 88;
    options.indexedColormaps[1].lightLevels[63][2] = 2;
    check(litWallReds(wall, options) == std::array<int, 2>{188, 55},
        "row 63 still maps indices; a palette fullbright entry preserves its own index");
    for (auto& vertex : sector.surfaces[0].texVertices) vertex.color.a = 0;
    options.indexedColormaps[1].lightLevels[0][2] = 2;
    check(litWallReds(wall, options)[1] == 55, "fullbright palette entry stays bright at row zero");
    for (auto& v : sector.surfaces[0].texVertices) v.color.a = 1.0f / 3.0f;
    options.indexedColormaps[1].lightLevels[21][2] = 17;
    options.indexedColormaps[1].lightLevels[20][2] = 88;
    check(litWallReds(wall, options)[1] == 170, "light just above 21/63 selects row 21");
    for (auto& v : sector.surfaces[0].texVertices) v.color.a = std::nextafter(v.color.a, 0.0f);
    check(litWallReds(wall, options)[1] == 188, "preceding representable light selects row 20");
    for (auto& v : sector.surfaces[0].texVertices) v.color.a = 0;
    sector.colormap = "alternate.cmp";
    sector.colormapIndex = -1;
    options.indexedColormapNames = {"master.cmp", "ALTERNATE.CMP"};
    check(litWallReds(wall, options)[1] == 55, "name-only sector resolves its own CMP");
    sector.colormapIndex = 0;
    checkThrowsWithMessage([&] { (void)litWallReds(wall, options); },
        "indexed8bit sector colormap index disagrees with alternate.cmp", "inconsistent sector colormap refuses");
    sector.colormapIndex = 1;
    sector.surfaces[0].surfaceFlags |= SF_SkyHorizon;
    checkThrowsWithMessage([&] { (void)litWallReds(wall, options); },
        "indexed8bit surface 'white.mat': sky projection is unsupported", "sky projection refuses");
    sector.surfaces[0].surfaceFlags = 0;
    sector.surfaces[0].texVertices[0].color.a = std::numeric_limits<float>::quiet_NaN();
    checkThrowsWithMessage([&] { (void)litWallReds(wall, options); },
        "indexed8bit surface 'white.mat': non-finite scalar corner light", "non-finite scalar light is rejected");

}

void runBakedLightingTests()
{
    SoftwareSnapshotOptions ambient{};
    SoftwareSnapshotOptions baked{};
    baked.lighting = SnapshotLighting::baked;

    // The default keeps shading every surface by its sector's ambient light.
    const LitWall ramp = litWall(0.25f, 1.0f);
    const auto unlit = litWallReds(ramp, ambient);
    check(near(unlit[0], 100) && near(unlit[1], 100),
        "snapshot ambient shading ignores stored corner light");

    // Baked shading uses each corner's stored light, interpolated across the face.
    const auto lit = litWallReds(ramp, baked);
    check(lit[0] < lit[1] && lit[0] > 50 && lit[0] < 150 && lit[1] > 150,
        "snapshot baked shading follows the stored corner light across a face");

    // A fully lit face (light mode 0) ignores its corners.
    LitWall full = litWall(0.0f, 0.0f);
    full.level.sectors[0].surfaces[0].light = 0;
    const auto fullLit = litWallReds(full, baked);
    check(near(fullLit[0], 200) && near(fullLit[1], 200), "snapshot baked shading draws a fully lit face at full light");

    // Corner light plus the face's extra light, never below the sector's
    // extra light, never above full: the game's own combination.
    LitWall extra = litWall(0.25f, 0.25f);
    extra.level.sectors[0].surfaces[0].extraLight = ColorF{ 0.5f, 0.5f, 0.5f, 0.5f };
    const auto withFaceExtra = litWallReds(extra, baked);
    check(near(withFaceExtra[0], 200 * 0.75) && near(withFaceExtra[1], 200 * 0.75),
        "snapshot baked shading adds the face's extra light to its corners");
    LitWall floor = litWall(0.1f, 0.1f);
    floor.level.sectors[0].extraLight = ColorF{ 0.6f, 0.6f, 0.6f, 0.0f };
    const auto withSectorFloor = litWallReds(floor, baked);
    check(near(withSectorFloor[0], 200 * 0.6) && near(withSectorFloor[1], 200 * 0.6),
        "snapshot baked shading never goes below the sector's extra light");
    LitWall over = litWall(0.9f, 0.9f);
    over.level.sectors[0].surfaces[0].extraLight = ColorF{ 0.5f, 0.5f, 0.5f, 0.5f };
    const auto capped = litWallReds(over, baked);
    check(near(capped[0], 200) && near(capped[1], 200), "snapshot baked shading caps light at full");

    // A sector on a colormap other than the master one is multiplied by that
    // colormap's tint, as the hardware renderer does; the master is never tinted.
    LitWall tinted = litWall(1.0f, 1.0f);
    tinted.level.sectors[0].colormapIndex = 1;
    SoftwareSnapshotOptions withTints = baked;
    withTints.colormapTints = { ColorF{ 0.5f, 0.5f, 0.5f }, ColorF{ 0.25f, 0.5f, 1.0f } };
    const auto tintedReds = litWallReds(tinted, withTints);
    check(near(tintedReds[0], 50) && near(tintedReds[1], 50),
        "snapshot baked shading multiplies by the sector colormap's tint");
    tinted.level.sectors[0].colormapIndex = 0;
    const auto master = litWallReds(tinted, withTints);
    check(near(master[0], 200) && near(master[1], 200), "snapshot baked shading never tints the master colormap");

    // JK stores one light value per corner: in its `a` channel.
    LitWall mono = litWall(1.0f, 1.0f);
    for ( TexVertex& corner : mono.level.sectors[0].surfaces[0].texVertices )
    {
        corner.color = ColorF{ 1.0f, 1.0f, 1.0f, 0.5f };
    }
    const auto monoReds = litWallReds(mono, baked);
    check(near(monoReds[0], 100) && near(monoReds[1], 100), "snapshot baked shading reads a JK corner's light from a");
    mono.level.kind = ProjectType::MOTS;
    const auto rgbReds = litWallReds(mono, baked);
    check(near(rgbReds[0], 200) && near(rgbReds[1], 200), "snapshot baked shading reads a MotS corner's red light from r");
}

// Two walls facing a camera that looks along +Y: one at y = 2 and one at
// y = 3, each a flat colour, in that order in one sector. Returns the centre
// pixel of a 1x1 orthographic snapshot.
Rgb layeredWallsPixel(const bool nearTranslucent, const bool farTranslucent, int* surfacesDrawn = nullptr)
{
    using namespace sed::render;
    Level level{};
    level.uvCoordinateSpace = UvCoordinateSpace::Normalized;
    Sector sector{};
    sector.ambient = ColorF{ 1.0f, 1.0f, 1.0f };
    sector.vertices = { Vertex{ -1, 2, -1 }, Vertex{ -1, 2, 1 }, Vertex{ 1, 2, 1 }, Vertex{ 1, 2, -1 },
                        Vertex{ -1, 3, -1 }, Vertex{ -1, 3, 1 }, Vertex{ 1, 3, 1 }, Vertex{ 1, 3, -1 } };
    Surface nearWall{};
    nearWall.geo = 4;
    nearWall.material = "near.mat";
    nearWall.normal = TVector{ 0, -1, 0 };
    nearWall.vertices = { 0, 1, 2, 3 };
    nearWall.texVertices = { TexVertex{ 0, 1 }, TexVertex{ 0, 0 }, TexVertex{ 1, 0 }, TexVertex{ 1, 1 } };
    nearWall.faceFlags = nearTranslucent ? FF_Transluent : 0u;
    Surface farWall = nearWall;
    farWall.material = "far.mat";
    farWall.vertices = { 4, 5, 6, 7 };
    farWall.faceFlags = farTranslucent ? FF_Transluent : 0u;
    sector.surfaces = { nearWall, farWall };
    level.sectors = { sector };
    const auto flat = [](const std::uint8_t r, const std::uint8_t g, const std::uint8_t b) {
        SnapshotMaterial mat{};
        mat.image.width = 1;
        mat.image.height = 1;
        mat.image.rgba = { r, g, b, 255 };
        mat.trueColor = true;
        mat.alphaBpp = 8;
        return mat;
    };
    const SnapshotMaterial nearMat = flat(200, 100, 0);
    const SnapshotMaterial farMat = flat(0, 100, 200);
    const SnapshotMaterialResolver resolver = [&](std::string_view name) -> const SnapshotMaterial* {
        return name == "near.mat" ? &nearMat : name == "far.mat" ? &farMat : nullptr;
    };
    SoftwareSnapshotOptions options{};
    options.width = 1;
    options.height = 1;
    options.camera.position = TVector{ 0, -3, 0 };
    options.camera.target = TVector{ 0, 2, 0 };
    options.camera.orthographic = true;
    options.camera.orthographicHeight = 1.0;
    const SoftwareSnapshotResult result = renderSoftwareSnapshot(level, resolver, options);
    if ( surfacesDrawn ) *surfacesDrawn = result.surfacesDrawn;
    const std::vector<std::uint8_t> raw = storedPngScanlines(result.png);
    if ( raw.size() != 1u + 4u ) return { 0, 0, 0 };
    return { raw[1], raw[2], raw[3] };
}

void runTranslucentTests()
{
    // The near wall is translucent and listed first: it is still drawn over
    // the far wall, at the engine's fixed 90/255.
    int drawn = 0;
    const Rgb blended = layeredWallsPixel(true, false, &drawn);
    constexpr double a = 90.0 / 255.0;
    check(near(blended[0], a * 200) && near(blended[1], 100) && near(blended[2], (1 - a) * 200),
        "snapshot blends a translucent face over what lies behind it at 90/255");
    check(drawn == 2, "snapshot counts a blended translucent face as drawn");
    // Opaque in front of a translucent face: the translucent one is hidden.
    const Rgb hidden = layeredWallsPixel(false, true);
    check(hidden[0] == 200 && hidden[1] == 100 && hidden[2] == 0,
        "snapshot hides a translucent face behind an opaque one");
    // Both opaque: the near wall alone, as before.
    const Rgb opaque = layeredWallsPixel(false, false);
    check(opaque[0] == 200 && opaque[1] == 100 && opaque[2] == 0, "snapshot draws opaque faces unblended");
    // Two translucent layers blend far first, then near.
    const Rgb twice = layeredWallsPixel(true, true);
    const double farOverBlack = a * 200;
    check(near(twice[0], a * 200) && near(twice[1], a * 100 + (1 - a) * a * 100)
            && near(twice[2], (1 - a) * farOverBlack),
        "snapshot blends stacked translucent faces farthest first");
}

} // namespace

void runSoftwareSnapshotTests()
{
    runIndexedLightingTests();
    runBakedLightingTests();
    runTranslucentTests();
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
    options.uvDiagnostic = true;
    const auto ruler = renderSoftwareSnapshot(level, resolver, options);
    check(ruler.png != ortho.png && ruler.png == renderSoftwareSnapshot(level, resolver, options).png,
        "UV diagnostic is visible and byte-repeatable");
    Level mirrored = level;
    for ( auto& uv : mirrored.sectors.front().surfaces.front().texVertices )
    {
        uv.u = 1.0 - uv.u;
    }
    check(ruler.png != renderSoftwareSnapshot(mirrored, resolver, options).png,
        "oriented UV ruler reveals mirrored mapping even on a solid-color MAT");
    options.indexed8bit = true;
    checkThrowsWithMessage([&] { (void)renderSoftwareSnapshot(level, resolver, options); },
        "uvDiagnostic is not an indexed8bit render", "UV diagnostic cannot masquerade as an indexed8bit render");
    options.indexed8bit = false;
    options.uvDiagnostic = false;
    check(ortho.png == renderSoftwareSnapshot(level, resolver, options).png,
        "disabling UV diagnostic exactly restores the original snapshot");
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

    // Texture handedness, as OpenJKDF2 draws it (measured with an asymmetric
    // texture on walls mapped both ways): image row 0 is at V = 0, and the
    // image appears as stored where U cross V points away from the viewer.
    // Where it points at the viewer, as it does on new faces and on most
    // retail walls, the image appears mirrored left to right.
    {
        const Rgb red{ 255, 0, 0 }, green{ 0, 255, 0 }, blue{ 0, 0, 255 }, white{ 255, 255, 255 };
        // Pixels in reading order: top left, top right, bottom left, bottom right.
        const std::array<Rgb, 4> asStored = wallQuadrantColours(true);
        check(asStored == std::array<Rgb, 4>{ red, green, blue, white },
            "snapshot shows a wall texture as stored where U x V points away from the viewer");
        const std::array<Rgb, 4> mirrored = wallQuadrantColours(false);
        check(mirrored == std::array<Rgb, 4>{ green, red, white, blue },
            "snapshot mirrors a wall texture left to right where U x V points at the viewer");
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
