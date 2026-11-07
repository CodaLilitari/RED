#include "test_common.hpp"

#include "sed/assets/dxf.hpp"
#include "sed/level/build.hpp"
#include "sed/level/dxf_import.hpp"
#include "sed/level/geometry.hpp"

#include <algorithm>
#include <array>

#include <cmath>
#include <sstream>

namespace sed::tests
{
namespace
{

bool near(const TVector& a, const TVector& b)
{
    return std::abs(a.x - b.x) < 1e-9 && std::abs(a.y - b.y) < 1e-9 && std::abs(a.z - b.z) < 1e-9;
}

void testDxfExportImport()
{
    Level level{};
    (void)level::levelCreateSectorPrimitiveBox(
        level, level::SectorPrimitive::Cube,
        TVector::Create(0, 0, 0), TVector::Create(1, 1, 1));

    const std::string text = sed::assets::saveLevelToDxfText(level);
    check(text.find("3DFACE") != std::string::npos
            && text.find("SECTOR_0") != std::string::npos,
        "DXF export writes 3DFACE entities on a sector layer");

    std::istringstream input(text);
    const std::vector<sed::assets::DxfFace> faces = sed::assets::loadDxfFaces(input);
    check(faces.size() == 12u, "DXF export fans six quads into twelve triangles");
    bool onLayer = !faces.empty();
    bool unitCube = !faces.empty();
    for (const sed::assets::DxfFace& face : faces)
    {
        onLayer = onLayer && face.layer == "SECTOR_0" && face.cornerCount == 3u;
        for (std::size_t i = 0; i < face.cornerCount; ++i)
        {
            const TVector& corner = face.corners[i];
            unitCube = unitCube && corner.x >= -1e-9 && corner.x <= 1.0 + 1e-9
                && corner.y >= -1e-9 && corner.y <= 1.0 + 1e-9
                && corner.z >= -1e-9 && corner.z <= 1.0 + 1e-9;
        }
    }
    check(onLayer, "DXF faces keep their layer and triangle shape");
    check(unitCube, "DXF faces stay on the exported cube");
}

void testDxfSkipsOtherEntities()
{
    const char* const dxf =
        "0\nSECTION\n2\nENTITIES\n"
        "0\nLINE\n8\nWALLS\n10\n0\n20\n0\n30\n0\n11\n1\n21\n1\n31\n1\n"
        "0\n3DFACE\n8\nWALLS\n"
        "10\n0\n20\n0\n30\n0\n11\n1\n21\n0\n31\n0\n12\n1\n22\n1\n31\n0\n13\n0\n23\n1\n31\n0\n"
        "0\nENDSEC\n0\nEOF\n";
    std::istringstream input(dxf);
    const std::vector<sed::assets::DxfFace> faces = sed::assets::loadDxfFaces(input);
    check(faces.size() == 1u, "DXF import keeps 3DFACE and skips LINE entities");
    if (faces.size() == 1u)
    {
        check(faces[0].layer == "WALLS" && faces[0].cornerCount == 4u
                && near(faces[0].corners[0], TVector::Create(0, 0, 0))
                && near(faces[0].corners[3], TVector::Create(0, 1, 0)),
            "DXF import reads a quad face with its layer");
    }
}

} // namespace

void testDxfImportSectors()
{
    const auto face = [](const std::array<TVector, 4>& corners, const std::size_t count)
    {
        assets::DxfFace face{};
        face.corners = corners;
        face.cornerCount = count;
        face.layer = "ROOMS";
        return face;
    };
    std::vector<assets::DxfFace> faces;
    // Two unit quads sharing the edge x=1, one horizontal triangle, and
    // one vertical quad that must be skipped.
    faces.push_back(face({ TVector::Create(0, 0, 0), TVector::Create(1, 0, 0),
        TVector::Create(1, 1, 0), TVector::Create(0, 1, 0) }, 4));
    faces.push_back(face({ TVector::Create(1, 0, 0), TVector::Create(2, 0, 0),
        TVector::Create(2, 1, 0), TVector::Create(1, 1, 0) }, 4));
    faces.push_back(face({ TVector::Create(3, 0, 0), TVector::Create(4, 0, 0),
        TVector::Create(3, 1, 0), TVector::Create(3, 1, 0) }, 3));
    faces.push_back(face({ TVector::Create(5, 0, 0), TVector::Create(6, 0, 0),
        TVector::Create(6, 0, 1), TVector::Create(5, 0, 1) }, 4));

    Level level{};
    const auto result = level::levelImportDxfFaces(level, faces, 1.0);
    check(result && result->sectors.size() == 3u && result->skippedFaces == 1u
            && level.sectors.size() == 3u,
        "DXF import extrudes horizontal faces and skips the vertical one");
    if ( !result || result->sectors.size() != 3u )
    {
        return;
    }
    bool convex = true;
    bool heights = true;
    for ( const std::int32_t index : result->sectors )
    {
        const Sector& sector = level.sectors[static_cast<std::size_t>(index)];
        convex = convex && level::sectorIsConvex(sector);
        double minZ = sector.vertices[0].z;
        double maxZ = sector.vertices[0].z;
        for ( const Vertex& vertex : sector.vertices )
        {
            minZ = std::min(minZ, vertex.z);
            maxZ = std::max(maxZ, vertex.z);
        }
        heights = heights && std::abs(minZ) < 1e-9 && std::abs(maxZ - 1.0) < 1e-9;
    }
    check(convex, "DXF import sectors stay convex");
    check(heights, "DXF import extrudes from the face plane by the height");
    check(level.sectors[2].surfaces.size() == 5u,
        "DXF import triangle becomes a five-surface prism");
    std::size_t adjoined = 0;
    for ( const Surface& surface : level.sectors[0].surfaces )
    {
        if ( surface.adjoin )
        {
            ++adjoined;
        }
    }
    check(adjoined >= 1u, "DXF import adjoins sectors sharing a wall");

    Level refused{};
    check(!level::levelImportDxfFaces(refused, faces, 0.0) && refused.sectors.empty(),
        "DXF import refuses a zero height without adding sectors");
    const std::vector<assets::DxfFace> none{};
    const auto empty = level::levelImportDxfFaces(refused, none, 1.0);
    check(empty && empty->sectors.empty() && empty->skippedFaces == 0u,
        "DXF import of no faces builds nothing and skips nothing");
}

void runDxfTests()
{
    testDxfExportImport();
    testDxfSkipsOtherEntities();
    testDxfImportSectors();
}

} // namespace sed::tests
