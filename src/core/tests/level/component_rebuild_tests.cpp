#include "test_common.hpp"

#include "sed/level/component_rebuild.hpp"
#include "sed/level/geometry.hpp"

#include <array>
#include <cstdint>
#include <set>
#include <vector>

namespace sed::tests
{
namespace
{

using level::ComponentRebuildError;

constexpr std::size_t kCubeTopFace = 4;

Level openTopCubeLevel()
{
    Level level{};
    (void)level.newSector();
    Sector cube = makeTestCubeSector(0.0);
    cube.surfaces.erase(cube.surfaces.begin() + static_cast<std::ptrdiff_t>(kCubeTopFace));
    for ( std::size_t i = 0; i < cube.surfaces.size(); ++i )
    {
        cube.surfaces[i].num = static_cast<std::int32_t>(i);
        cube.surfaces[i].texVertices.resize(cube.surfaces[i].vertices.size());
    }
    cube.id = level.sectors[0].id;
    level.sectors[0] = std::move(cube);
    return level;
}

void addSurfaceCog(Level& level, const SurfaceRef ref)
{
    Cog& cog = level.newCog();
    CogValue value{};
    value.type = ValueType::ptr;
    value.cogType = CogType::surface;
    value.intValue = level.getGlobalSurfaceNumber(ref.sector, ref.surface);
    cog.values.push_back(value);
}

std::set<std::int32_t> vertexSet(const Surface& surface)
{
    return { surface.vertices.begin(), surface.vertices.end() };
}

void testBuildSurfaceClosesAnOpenSector()
{
    Level level = openTopCubeLevel();
    (void)level.newSector();
    level.sectors[1] = makeTestCubeSector(5.0);
    level.sectors[1].num = 1;
    addSurfaceCog(level, SurfaceRef{ 1, 2 });

    // Unordered selection, as the editor's vertex selection set provides.
    const std::array<std::int32_t, 4> top{ 6, 4, 5, 7 };
    const level::BuildSurfaceResult result = level::levelBuildSurfaceFromVertices(level, 0, top);
    check(result.error == ComponentRebuildError::None && result.surface == SurfaceRef{ 0, 5 },
        "Build Surface appends one surface to the sector");
    const Sector& sector = level.sectors[0];
    check(sector.surfaces.size() == 6u, "Build Surface closes the open cube");
    if ( sector.surfaces.size() != 6u )
    {
        return;
    }
    const Surface& built = sector.surfaces[5];
    check(vertexSet(built) == std::set<std::int32_t>{ 4, 5, 6, 7 } && built.texVertices.size() == 4u,
        "Build Surface uses every selected vertex once with matching UV corners");
    check(closeVec(built.normal, TVector::down, 1e-9), "Build Surface winds the face so its normal points into the sector");
    check(closeVec(level::surfaceCalcNormalNewell(sector, built), TVector::down, 1e-9),
        "Build Surface orders the corners into a simple polygon");
    check(built.num == 5 && built.material == "dflt.mat" && built.geo == static_cast<std::int32_t>(TGeoMode::Texture)
            && (built.surfaceFlags & SF_Collision) && !built.adjoin,
        "Build Surface uses the default new-surface properties");
    check(level.cogs[0].values[0].intValue == level.getGlobalSurfaceNumber(1, 2),
        "Build Surface keeps COG surface references on the same face");

    // The best-fit plane's initial orientation follows selection order; both
    // orientations must end up wound into the sector.
    Level reversed = openTopCubeLevel();
    const std::array<std::int32_t, 4> otherOrder{ 6, 7, 5, 4 };
    check(level::levelBuildSurfaceFromVertices(reversed, 0, otherOrder).error == ComponentRebuildError::None
            && reversed.sectors[0].surfaces.size() == 6u
            && closeVec(level::surfaceCalcNormalNewell(reversed.sectors[0], reversed.sectors[0].surfaces[5]),
                TVector::down, 1e-9),
        "Build Surface winds the face into the sector for any selection order");
}

void testBuildSurfaceRejectsInvalidSelections()
{
    const Level original = openTopCubeLevel();
    const auto rejects = [&original](const std::int32_t sector, const std::vector<std::int32_t>& vertices,
                             const ComponentRebuildError expected, const char* const message)
    {
        Level level = original;
        const level::BuildSurfaceResult result = level::levelBuildSurfaceFromVertices(level, sector, vertices);
        check(result.error == expected && level.sectors[0].surfaces.size() == original.sectors[0].surfaces.size(),
            message);
    };
    rejects(0, { 4, 5 }, ComponentRebuildError::TooFewElements, "Build Surface needs three vertices");
    rejects(0, { 4, 5, 5 }, ComponentRebuildError::TooFewElements, "Build Surface counts distinct vertices");
    rejects(0, { 4, 5, 99 }, ComponentRebuildError::InvalidReference, "Build Surface rejects a bad vertex index");
    rejects(3, { 0, 1, 2 }, ComponentRebuildError::InvalidReference, "Build Surface rejects a bad sector");

    Level collinear = original;
    collinear.sectors[0].vertices.push_back(Vertex{ 0.0, -1.0, -1.0, 0 });
    const std::array<std::int32_t, 3> line{ 0, 1, static_cast<std::int32_t>(collinear.sectors[0].vertices.size() - 1) };
    check(level::levelBuildSurfaceFromVertices(collinear, 0, line).error == ComponentRebuildError::Degenerate,
        "Build Surface rejects collinear vertices");

    rejects(0, { 0, 1, 2, 3 }, ComponentRebuildError::DuplicateSurface,
        "Build Surface refuses to duplicate an existing face");
}

// Sector 0 holds two disjoint cubes; sector 1 is adjoined to a face of the
// second cube, the way a level looks after a Build Sector candidate is merged.
Level twoCubeSectorLevel()
{
    Level level{};
    (void)level.newSector();
    (void)level.newSector();
    Sector merged = makeTestCubeSector(0.0);
    const Sector second = makeTestCubeSector(10.0);
    for ( const Surface& face : second.surfaces )
    {
        Surface copy = face;
        for ( std::int32_t& index : copy.vertices )
        {
            index += 8;
        }
        merged.surfaces.push_back(copy);
    }
    merged.vertices.insert(merged.vertices.end(), second.vertices.begin(), second.vertices.end());
    for ( std::size_t i = 0; i < merged.surfaces.size(); ++i )
    {
        merged.surfaces[i].num = static_cast<std::int32_t>(i);
        merged.surfaces[i].texVertices.resize(merged.surfaces[i].vertices.size());
        merged.surfaces[i].material = "face" + std::to_string(i) + ".mat";
    }
    merged.id = level.sectors[0].id;
    merged.ambient = ColorF{ 0.25f, 0.5f, 0.75f, 1.0f };
    merged.colormap = "dflt.cmp";
    level.sectors[0] = std::move(merged);
    const std::int32_t sector1Id = level.sectors[1].id;
    level.sectors[1] = makeTestCubeSector(12.0);
    level.sectors[1].id = sector1Id;
    level.sectors[1].num = 1;
    // Second cube's +X face (index 6) is adjoined to sector 1's -X face (index 1).
    level.sectors[0].surfaces[6].adjoin = SurfaceRef{ 1, 1 };
    level.sectors[1].surfaces[1].adjoin = SurfaceRef{ 0, 6 };
    Thing thing{};
    thing.sector = 0;
    level.things.push_back(thing);
    return level;
}

void testBuildSectorSplitsSelectedFaces()
{
    Level level = twoCubeSectorLevel();
    addSurfaceCog(level, SurfaceRef{ 0, 7 });
    addSurfaceCog(level, SurfaceRef{ 0, 2 });
    addSurfaceCog(level, SurfaceRef{ 1, 3 });
    const std::array<SurfaceRef, 6> faces{ SurfaceRef{ 0, 11 }, SurfaceRef{ 0, 6 }, SurfaceRef{ 0, 7 },
        SurfaceRef{ 0, 8 }, SurfaceRef{ 0, 9 }, SurfaceRef{ 0, 10 } };
    const level::BuildSectorResult result = level::levelBuildSectorFromSurfaces(level, faces);
    check(result.error == ComponentRebuildError::None && result.sector == 2 && !result.sourceSectorDeleted,
        "Build Sector appends a new sector");
    if ( level.sectors.size() != 3u )
    {
        check(false, "Build Sector leaves three sectors");
        return;
    }
    const Sector& source = level.sectors[0];
    const Sector& built = level.sectors[2];
    check(source.surfaces.size() == 6u && source.vertices.size() == 8u,
        "Build Sector keeps the unselected faces and their vertices in the source");
    check(built.surfaces.size() == 6u && built.vertices.size() == 8u,
        "Build Sector moves the selected faces with only the vertices they use");
    check(built.num == 2 && built.id != 0 && built.id != source.id, "Build Sector gives the new sector its own identity");
    check(built.ambient.r == source.ambient.r && built.colormap == source.colormap,
        "Build Sector copies the source sector's properties");
    bool facesPreserved = true;
    for ( std::size_t i = 0; i < built.surfaces.size(); ++i )
    {
        const Surface& face = built.surfaces[i];
        facesPreserved = facesPreserved && face.num == static_cast<std::int32_t>(i)
            && face.material == "face" + std::to_string(6 + i) + ".mat";
        for ( const std::int32_t index : face.vertices )
        {
            facesPreserved = facesPreserved && index >= 0 && static_cast<std::size_t>(index) < built.vertices.size()
                && built.vertices[static_cast<std::size_t>(index)].x > 5.0;
        }
        facesPreserved = facesPreserved && closeVec(face.normal, level::surfaceCalcNormalNewell(built, face), 1e-9);
    }
    check(facesPreserved, "Build Sector keeps each moved face's properties and geometry in surface order");
    check(built.surfaces[0].adjoin == SurfaceRef{ 1, 1 } && level.sectors[1].surfaces[1].adjoin == SurfaceRef{ 2, 0 },
        "Build Sector redirects both sides of a moved adjoin");
    check(level.cogs[0].values[0].intValue == level.getGlobalSurfaceNumber(2, 1)
            && level.cogs[1].values[0].intValue == level.getGlobalSurfaceNumber(0, 2)
            && level.cogs[2].values[0].intValue == level.getGlobalSurfaceNumber(1, 3),
        "Build Sector keeps COG surface references on the same faces");
    check(level.things[0].sector == 0, "Build Sector leaves Things in their sector, like the legacy command");
}

void testBuildSectorFromEveryFaceReplacesTheSource()
{
    Level level = openTopCubeLevel();
    (void)level.newSector();
    level.sectors[1] = makeTestCubeSector(5.0);
    level.sectors[1].num = 1;
    std::vector<SurfaceRef> faces{};
    for ( std::int32_t i = 0; i < 5; ++i )
    {
        faces.push_back(SurfaceRef{ 0, i });
    }
    const level::BuildSectorResult result = level::levelBuildSectorFromSurfaces(level, faces);
    check(result.error == ComponentRebuildError::None && result.sourceSectorDeleted && result.sector == 1
            && level.sectors.size() == 2u && level.sectors[1].surfaces.size() == 5u && level.sectors[1].num == 1,
        "Build Sector deletes an emptied source sector and reports the shifted index");
}

void testBuildSectorRejectsInvalidSelections()
{
    const Level original = twoCubeSectorLevel();
    const auto rejects = [&original](const std::vector<SurfaceRef>& faces, const ComponentRebuildError expected,
                             const char* const message)
    {
        Level level = original;
        const level::BuildSectorResult result = level::levelBuildSectorFromSurfaces(level, faces);
        check(result.error == expected && level.sectors.size() == original.sectors.size()
                && level.sectors[0].surfaces.size() == original.sectors[0].surfaces.size(),
            message);
    };
    rejects({ SurfaceRef{ 0, 6 }, SurfaceRef{ 0, 7 }, SurfaceRef{ 0, 8 } }, ComponentRebuildError::TooFewElements,
        "Build Sector needs four surfaces");
    rejects({ SurfaceRef{ 0, 6 }, SurfaceRef{ 0, 6 }, SurfaceRef{ 0, 7 }, SurfaceRef{ 0, 8 } },
        ComponentRebuildError::TooFewElements, "Build Sector counts distinct surfaces");
    rejects({ SurfaceRef{ 0, 6 }, SurfaceRef{ 0, 7 }, SurfaceRef{ 0, 8 }, SurfaceRef{ 1, 0 } },
        ComponentRebuildError::MixedSectors, "Build Sector needs surfaces from one sector");
    rejects({ SurfaceRef{ 0, 6 }, SurfaceRef{ 0, 7 }, SurfaceRef{ 0, 8 }, SurfaceRef{ 0, 40 } },
        ComponentRebuildError::InvalidReference, "Build Sector rejects a bad surface reference");
}

} // namespace

void runComponentRebuildTests()
{
    testBuildSurfaceClosesAnOpenSector();
    testBuildSurfaceRejectsInvalidSelections();
    testBuildSectorSplitsSelectedFaces();
    testBuildSectorFromEveryFaceReplacesTheSource();
    testBuildSectorRejectsInvalidSelections();
}

} // namespace sed::tests
