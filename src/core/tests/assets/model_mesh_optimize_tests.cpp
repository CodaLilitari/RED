#include "sed/assets/model_mesh_optimize.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace
{
using namespace sed;
using namespace sed::assets;

void require(bool condition, const char* message)
{
    if ( !condition )
    {
        throw std::runtime_error(message);
    }
}

/** One corner as the importer writes it: its own vertex and texture vertex. */
struct Corner
{
    double x, y, z;
    float light;
    float u, v;
    TVector normal{0, 0, 1};
};

/** Triangles with unshared corners, the way the model-scene importer emits them. */
Model3doMesh soup(const std::vector<std::array<Corner, 3>>& triangles, std::int32_t material = 0)
{
    Model3doMesh mesh;
    mesh.geo = 4;
    mesh.light = 3;
    mesh.tex = 1;
    for ( const auto& triangle : triangles )
    {
        Model3doFace face;
        face.material = material;
        face.geo = 4;
        face.light = 3;
        face.tex = 1;
        face.extraLight = ColorBlack;
        for ( const auto& c : triangle )
        {
            face.vertices.push_back(static_cast<std::int32_t>(mesh.vertices.size()));
            face.texVertices.push_back(static_cast<std::int32_t>(mesh.texVertices.size()));
            mesh.vertices.push_back({c.x, c.y, c.z, {c.light, c.light, c.light, 1.0f}});
            mesh.vertexNormals.push_back(c.normal);
            mesh.texVertices.push_back({c.u, c.v});
        }
        mesh.faces.push_back(std::move(face));
    }
    return mesh;
}

/** A unit quad split on its diagonal, light and UV linear in x and y unless bent. */
Model3doMesh quad(float bentLight = 0.0f, float bentU = 0.0f, TVector secondNormal = {0, 0, 1})
{
    const auto at = [&](double x, double y, float extraLight = 0.0f, float extraU = 0.0f, TVector n = {0, 0, 1})
    {
        return Corner{x, y, 0.0, static_cast<float>(0.2 + 0.3 * x + 0.1 * y) + extraLight,
                      static_cast<float>(64 * x) + extraU, static_cast<float>(64 * y), n};
    };
    return soup({{at(0, 0), at(1, 0), at(1, 1)},
                 {at(0, 0, 0, 0, secondNormal), at(1, 1, 0, 0, secondNormal), at(0, 1, bentLight, bentU, secondNormal)}});
}

double area(const Model3doMesh& mesh, const Model3doFace& face)
{
    double sx = 0, sy = 0, sz = 0;
    const auto& p0 = mesh.vertices[static_cast<std::size_t>(face.vertices[0])];
    for ( std::size_t k = 1; k + 1 < face.vertices.size(); ++k )
    {
        const auto& a = mesh.vertices[static_cast<std::size_t>(face.vertices[k])];
        const auto& b = mesh.vertices[static_cast<std::size_t>(face.vertices[k + 1])];
        const double ux = a.x - p0.x, uy = a.y - p0.y, uz = a.z - p0.z;
        const double vx = b.x - p0.x, vy = b.y - p0.y, vz = b.z - p0.z;
        sx += uy * vz - uz * vy;
        sy += uz * vx - ux * vz;
        sz += ux * vy - uy * vx;
    }
    return 0.5 * std::sqrt(sx * sx + sy * sy + sz * sz);
}

using CornerKey = std::tuple<double, double, double, float, float, float>;

std::set<CornerKey> corners(const Model3doMesh& mesh)
{
    std::set<CornerKey> out;
    for ( const auto& face : mesh.faces )
    {
        for ( std::size_t c = 0; c < face.vertices.size(); ++c )
        {
            const auto& v = mesh.vertices[static_cast<std::size_t>(face.vertices[c])];
            const auto& t = mesh.texVertices[static_cast<std::size_t>(face.texVertices[c])];
            out.insert({v.x, v.y, v.z, v.color.r, t.u, t.v});
        }
    }
    return out;
}

double totalArea(const Model3doMesh& mesh)
{
    double sum = 0;
    for ( const auto& face : mesh.faces )
    {
        sum += area(mesh, face);
    }
    return sum;
}

std::size_t fanTriangles(const Model3doMesh& mesh)
{
    std::size_t sum = 0;
    for ( const auto& face : mesh.faces )
    {
        sum += face.vertices.size() - 2;
    }
    return sum;
}

void weldsAndMergesALinearQuad()
{
    const auto before = quad();
    auto mesh = before;
    const auto stats = optimizeModel3doMesh(mesh, meshOptimizationLimits(MeshOptimization::Strict));
    require(stats.verticesIn == 6 && stats.verticesOut == 4 && mesh.vertexNormals.size() == 4,
            "the diagonal's two shared corners weld");
    require(stats.texVerticesOut == 4, "identical texture coordinates share one texture vertex");
    require(stats.facesIn == 2 && stats.facesOut == 1 && mesh.faces[0].vertices.size() == 4,
            "two coplanar triangles with linear light and UVs become one quad");
    require(corners(mesh) == corners(before), "every corner keeps its position, light and texture coordinates");
    require(std::abs(totalArea(mesh) - totalArea(before)) < 1e-12 && fanTriangles(mesh) == 2,
            "the quad covers the same area with the same fan-equivalent triangles");
    auto again = mesh;
    const auto second = optimizeModel3doMesh(again, meshOptimizationLimits(MeshOptimization::Strict));
    require(second.verticesOut == 4 && second.facesOut == 1 && again.faces[0].vertices == mesh.faces[0].vertices,
            "optimizing twice changes nothing more");
}

void handlesNearlyCollinearTextureBasis()
{
    const Corner a{0, 0, 0, 0.5f, 0, 0}, b{1, 0, 0, 0.5f, 1, 0}, d{0, 1, 0, 0.5f, 0, 1};
    const auto thinQuad = [&](float deviation)
    {
        const Corner c{2, 0.00001, 0, 0.5f, 2 + deviation, 0.00001f};
        return soup({{a, b, c}, {a, c, d}});
    };
    const auto limits = meshOptimizationLimits(MeshOptimization::Strict);
    auto before = thinQuad(0.01f);
    auto mesh = before;
    require(optimizeModel3doMesh(mesh, limits).facesOut == 1,
            "a skinny first triangle does not hide a bounded four-corner texture fit");
    require(corners(mesh) == corners(before) && std::abs(totalArea(mesh) - totalArea(before)) < 1e-12,
            "the bounded fit retains the original corners and covered area");
    mesh = thinQuad(0.04f);
    require(optimizeModel3doMesh(mesh, limits).facesOut == 2,
            "a second basis must meet half of the stated error budget");
}

void refusesBentLightAndTexture()
{
    auto light = quad(3.0f / 255.0f);
    auto stats = optimizeModel3doMesh(light, meshOptimizationLimits(MeshOptimization::Strict));
    require(stats.verticesOut == 4 && stats.facesOut == 2,
            "strict welds the diagonal but keeps both faces when light bends by 3/255");
    light = quad(3.0f / 255.0f);
    stats = optimizeModel3doMesh(light, meshOptimizationLimits(MeshOptimization::Loose));
    require(stats.facesOut == 1, "loose accepts light within 4/255 of linear");
    light = quad(6.0f / 255.0f);
    stats = optimizeModel3doMesh(light, meshOptimizationLimits(MeshOptimization::Loose));
    require(stats.facesOut == 2, "loose refuses light 6/255 off linear");

    auto texture = quad(0.0f, 0.25f);
    stats = optimizeModel3doMesh(texture, meshOptimizationLimits(MeshOptimization::Strict));
    require(stats.facesOut == 2, "strict refuses a texture coordinate a quarter texel off linear");
    texture = quad(0.0f, 0.25f);
    stats = optimizeModel3doMesh(texture, meshOptimizationLimits(MeshOptimization::Loose));
    require(stats.facesOut == 1, "loose accepts a quarter texel");
    texture = quad(0.0f, 2.0f);
    stats = optimizeModel3doMesh(texture, meshOptimizationLimits(MeshOptimization::Loose));
    require(stats.facesOut == 2, "loose refuses two texels");
}

void weldsOnlyNearlyEqualNormals()
{
    const double small = 0.3 * 3.14159265358979323846 / 180.0;
    auto near = quad(0.0f, 0.0f, {std::sin(small), 0, std::cos(small)});
    auto stats = optimizeModel3doMesh(near, meshOptimizationLimits(MeshOptimization::Strict));
    require(stats.verticesOut == 4 && stats.worstNormalTurnDegrees > 0.29 && stats.worstNormalTurnDegrees < 0.31,
            "normals 0.3 degrees apart weld and keep the first normal");
    require(near.vertexNormals[0].x == 0 && near.vertexNormals[0].z == 1, "the first corner's normal is the one kept");
    const double large = 2.0 * 3.14159265358979323846 / 180.0;
    auto far = quad(0.0f, 0.0f, {std::sin(large), 0, std::cos(large)});
    stats = optimizeModel3doMesh(far, meshOptimizationLimits(MeshOptimization::Strict));
    require(stats.verticesOut == 6 && stats.facesOut == 2,
            "normals 2 degrees apart stay separate, so the faces share no edge and don't merge");
}

void keepsMaterialsAndCreasesApart()
{
    const auto at = [](double x, double y, double z)
    { return Corner{x, y, z, 0.5f, static_cast<float>(64 * x), static_cast<float>(64 * y)}; };
    auto mixed = soup({{at(0, 0, 0), at(1, 0, 0), at(1, 1, 0)}});
    auto other = soup({{at(0, 0, 0), at(1, 1, 0), at(0, 1, 0)}}, 1);
    for ( auto& face : other.faces )
    {
        for ( auto& v : face.vertices )
        {
            v += 3;
        }
        for ( auto& t : face.texVertices )
        {
            t += 3;
        }
        mixed.faces.push_back(face);
    }
    mixed.vertices.insert(mixed.vertices.end(), other.vertices.begin(), other.vertices.end());
    mixed.vertexNormals.insert(mixed.vertexNormals.end(), other.vertexNormals.begin(), other.vertexNormals.end());
    mixed.texVertices.insert(mixed.texVertices.end(), other.texVertices.begin(), other.texVertices.end());
    auto stats = optimizeModel3doMesh(mixed, meshOptimizationLimits(MeshOptimization::Strict));
    require(stats.verticesOut == 4 && stats.facesOut == 2, "different materials weld corners but never merge");

    auto crease = soup({{at(0, 0, 0), at(1, 0, 0), at(1, 1, 0)}, {at(0, 0, 0), at(1, 1, 0), at(0, 1, 0.5)}});
    stats = optimizeModel3doMesh(crease, meshOptimizationLimits(MeshOptimization::Strict));
    require(stats.facesOut == 2, "faces at an angle to each other never merge");

    // An arrowhead: the merged outline would be concave.
    auto arrow = soup({{at(0, 0, 0), at(1, 0.5, 0), at(0.4, 0.5, 0)}, {at(0, 1, 0), at(0.4, 0.5, 0), at(1, 0.5, 0)}});
    stats = optimizeModel3doMesh(arrow, meshOptimizationLimits(MeshOptimization::Strict));
    require(stats.facesOut == 2, "a merge that would be concave is refused");
}

void capsCorners()
{
    // A fan of 12 triangles round a centre: one 12-corner polygon unless capped.
    std::vector<std::array<Corner, 3>> fan;
    const auto at = [](double angle)
    {
        const double x = std::cos(angle), y = std::sin(angle);
        return Corner{x, y, 0, 0.5f, static_cast<float>(32 * x), static_cast<float>(32 * y)};
    };
    const Corner centre{0, 0, 0, 0.5f, 0, 0};
    for ( int i = 0; i < 12; ++i )
    {
        const double a = i * 2 * 3.14159265358979323846 / 12, b = (i + 1) * 2 * 3.14159265358979323846 / 12;
        fan.push_back({centre, at(a), at(b)});
    }
    auto mesh = soup(fan);
    const auto stats = optimizeModel3doMesh(mesh, meshOptimizationLimits(MeshOptimization::Strict));
    require(stats.facesOut < 12 && fanTriangles(mesh) == 12, "the fan merges without losing coverage");
    require(std::all_of(mesh.faces.begin(), mesh.faces.end(), [](const auto& f) { return f.vertices.size() <= 8; }),
            "no merged face passes the 8-corner cap");
    for ( const auto& face : mesh.faces )
    {
        std::vector<std::int32_t> used = face.vertices;
        std::sort(used.begin(), used.end());
        require(std::adjacent_find(used.begin(), used.end()) == used.end(), "no face repeats a vertex");
    }
}

void refusesInconsistentTables()
{
    auto mesh = quad();
    mesh.vertexNormals.pop_back();
    bool threw = false;
    try
    {
        optimizeModel3doMesh(mesh, meshOptimizationLimits(MeshOptimization::Strict));
    }
    catch ( const std::invalid_argument& )
    {
        threw = true;
    }
    require(threw, "a short normal table is refused");
    auto limits = meshOptimizationLimits(MeshOptimization::Strict);
    limits.maxCorners = 25;
    threw = false;
    mesh = quad();
    try
    {
        optimizeModel3doMesh(mesh, limits);
    }
    catch ( const std::invalid_argument& )
    {
        threw = true;
    }
    require(threw, "a corner cap above the loader's 24 is refused");
    require(std::string(meshOptimizationName(MeshOptimization::Loose)) == "loose", "level names");
}

using Ring = std::vector<std::array<double, 2>>;

/**
 * Indexed polygons in the z = 0 plane, sharing a vertex wherever corners coincide. Light and
 * texture coordinates are linear in the world position; `bend` adds light at chosen positions.
 * The mesh stores positions relative to `origin`, the way the importer centres each model.
 */
Model3doMesh polygons(const std::vector<Ring>& rings, std::map<std::pair<double, double>, float> bend = {},
                      double origin = 0)
{
    Model3doMesh mesh;
    mesh.geo = 4;
    mesh.light = 3;
    mesh.tex = 1;
    std::map<std::pair<double, double>, std::int32_t> ids;
    for ( const auto& ring : rings )
    {
        Model3doFace face;
        face.geo = 4;
        face.light = 3;
        face.tex = 1;
        face.extraLight = ColorBlack;
        for ( const auto& [x, y] : ring )
        {
            const auto [at, added] = ids.try_emplace({x, y}, static_cast<std::int32_t>(mesh.vertices.size()));
            if ( added )
            {
                const float light = static_cast<float>(0.2 + 0.1 * x + 0.05 * y) + bend[{x, y}];
                mesh.vertices.push_back({x - origin, y, 0.0, {light, light, light, 1.0f}});
                mesh.vertexNormals.push_back({0, 0, 1});
                mesh.texVertices.push_back({static_cast<float>(64 * x), static_cast<float>(64 * y)});
            }
            face.vertices.push_back(at->second);
            face.texVertices.push_back(at->second);
        }
        mesh.faces.push_back(std::move(face));
    }
    return mesh;
}

// A 2 x 1 floor with a corner in the middle of its top edge, at (1, 1).
const Ring kFloorWithMidpoint{{0, 0}, {2, 0}, {2, 1}, {1, 1}, {0, 1}};

StraightCornerStats prune(std::vector<Model3doMesh*> meshes, std::vector<TVector> fixed = {},
                          MeshOptimization level = MeshOptimization::Strict, std::vector<double> origins = {})
{
    std::vector<MeshPlacement> placements;
    for ( std::size_t i = 0; i < meshes.size(); ++i )
    {
        placements.push_back({meshes[i], {i < origins.size() ? origins[i] : 0.0, 0, 0}});
    }
    return pruneStraightCorners(placements, fixed, meshOptimizationLimits(level));
}

void prunesAStraightCornerEveryFaceDrops()
{
    // The face above has the same straight corner, so both faces can drop it.
    const auto before = polygons({kFloorWithMidpoint, {{0, 1}, {1, 1}, {2, 1}, {2, 2}, {0, 2}}});
    auto mesh = before;
    const auto stats = prune({&mesh});
    require(stats.cornersRemoved == 2 && stats.fanTrianglesIn == 6 && stats.fanTrianglesOut == 4,
            "a straight corner both faces share leaves both of them");
    require(mesh.faces[0].vertices.size() == 4 && mesh.faces[1].vertices.size() == 4 && mesh.vertices.size() == 6 &&
                mesh.vertexNormals.size() == 6 && mesh.texVertices.size() == 6 && stats.verticesOut == 6,
            "the unused vertex, normal and texture vertex are dropped");
    require(std::abs(totalArea(mesh) - totalArea(before)) < 1e-12, "the faces cover the same area");
    auto remaining = corners(before);
    remaining.erase({1, 1, 0, static_cast<float>(0.2 + 0.1 + 0.05), 64.0f, 64.0f});
    require(corners(mesh) == remaining, "every other corner keeps its position, light and texture coordinates");
    auto again = mesh;
    require(prune({&again}).cornersRemoved == 0 && again.faces[0].vertices == mesh.faces[0].vertices,
            "pruning twice changes nothing more");
}

void keepsCornersAnotherFaceTurnsAt()
{
    // Above the floor, two squares meet at (1, 1). Dropping it from the floor would leave a T-junction.
    const auto before = polygons({kFloorWithMidpoint, {{0, 1}, {1, 1}, {1, 2}, {0, 2}}, {{1, 1}, {2, 1}, {2, 2}, {1, 2}}});
    auto mesh = before;
    require(prune({&mesh}).cornersRemoved == 0 && mesh.faces[0].vertices == before.faces[0].vertices,
            "a corner another face turns at stays");

    auto floor = polygons({kFloorWithMidpoint});
    auto above = polygons({{{0, 1}, {1, 1}, {1, 2}, {0, 2}}, {{1, 1}, {2, 1}, {2, 2}, {1, 2}}}, {}, 10.0);
    require(prune({&floor, &above}, {}, MeshOptimization::Strict, {0.0, 10.0}).cornersRemoved == 0,
            "a face in another mesh, placed at the same world position, keeps the corner too");
    floor = polygons({kFloorWithMidpoint});
    require(prune({&floor}, {{1, 1, 0}}).cornersRemoved == 0, "a fixed position such as a sector vertex is kept");
    floor = polygons({kFloorWithMidpoint});
    require(prune({&floor}, {{1, 1.00002, 0}}).cornersRemoved == 1 && floor.faces[0].vertices.size() == 4,
            "with no other corner there, the straight corner goes");
}

void keepsBentAttributesAndTheFanOrigin()
{
    auto bent = polygons({kFloorWithMidpoint}, {{{1, 1}, 1.0f / 255.0f}});
    require(prune({&bent}).cornersRemoved == 0, "strict keeps a corner whose light is a whole step off the edge");
    require(prune({&bent}, {}, MeshOptimization::Loose).cornersRemoved == 1,
            "loose drops it: half of its 4/255 tolerance is 2/255");
    auto origin = polygons({{{1, 1}, {0, 1}, {0, 0}, {2, 0}, {2, 1}}});
    require(prune({&origin}).cornersRemoved == 0, "a face's first corner is its fan origin and stays");
    auto wedge = polygons({{{0, 0}, {1, 0}, {2, 0}, {1, 1}}});
    require(prune({&wedge}).cornersRemoved == 1 && wedge.faces[0].vertices.size() == 3 &&
                prune({&wedge}).cornersRemoved == 0,
            "a quad with a straight corner becomes a triangle, which keeps all three corners");
}

void prunesARunOfStraightCorners()
{
    const Ring run{{0, 0}, {2, 0}, {2, 1}, {1.5, 1}, {0.5, 1}, {0, 1}};
    auto mesh = polygons({run});
    const auto stats = prune({&mesh});
    require(stats.cornersRemoved == 2 && mesh.faces[0].vertices.size() == 4,
            "neighbouring straight corners go in successive rounds");
    auto shifted = polygons({run}, {{{1.5, 1}, 0.4f / 255.0f}});
    require(prune({&shifted}).cornersRemoved == 1 && shifted.faces[0].vertices.size() == 5,
            "the corner whose light is off the final edge stays");
}

} // namespace

int main()
{
    int failures = 0;
    for ( auto test : {weldsAndMergesALinearQuad, handlesNearlyCollinearTextureBasis, refusesBentLightAndTexture,
                       weldsOnlyNearlyEqualNormals, keepsMaterialsAndCreasesApart, capsCorners, refusesInconsistentTables,
                       prunesAStraightCornerEveryFaceDrops, keepsCornersAnotherFaceTurnsAt,
                       keepsBentAttributesAndTheFanOrigin, prunesARunOfStraightCorners} )
    {
        try
        {
            test();
        }
        catch ( const std::exception& error )
        {
            std::cerr << error.what() << '\n';
            ++failures;
        }
    }
    return failures ? 1 : 0;
}
