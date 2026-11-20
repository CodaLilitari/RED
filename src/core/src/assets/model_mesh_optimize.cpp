#include "sed/assets/model_mesh_optimize.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sed::assets
{
namespace
{

struct Vec
{
    double x{}, y{}, z{};
};

Vec sub(const Vec& a, const Vec& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec cross(const Vec& a, const Vec& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double dot(const Vec& a, const Vec& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double length(const Vec& a) { return std::sqrt(dot(a, a)); }

Vec position(const Model3doMesh& mesh, std::int32_t vertex)
{
    const auto& v = mesh.vertices[static_cast<std::size_t>(vertex)];
    return {v.x, v.y, v.z};
}

using Corner = std::pair<std::int32_t, std::int32_t>; // vertex, texture vertex

/** Newell normal of a ring of vertices; zero when the ring has no area. */
Vec newellNormal(const Model3doMesh& mesh, const std::vector<Corner>& ring)
{
    Vec n{};
    for ( std::size_t i = 0; i < ring.size(); ++i )
    {
        const auto p = position(mesh, ring[i].first);
        const auto q = position(mesh, ring[(i + 1) % ring.size()].first);
        n.x += (p.y - q.y) * (p.z + q.z);
        n.y += (p.z - q.z) * (p.x + q.x);
        n.z += (p.x - q.x) * (p.y + q.y);
    }
    const double l = length(n);
    if ( !(l > 0) || !std::isfinite(l) )
    {
        return {};
    }
    return {n.x / l, n.y / l, n.z / l};
}

bool sameFaceKey(const Model3doFace& a, const Model3doFace& b)
{
    return a.material == b.material && a.faceFlags == b.faceFlags && a.geo == b.geo && a.light == b.light &&
           a.tex == b.tex && a.extraLight.r == b.extraLight.r && a.extraLight.g == b.extraLight.g &&
           a.extraLight.b == b.extraLight.b && a.extraLight.a == b.extraLight.a;
}

/** Convex (or with straight corners) and flat to 1e-6 about the given unit normal. */
bool convexPlanar(const Model3doMesh& mesh, const std::vector<Corner>& ring, const Vec& normal)
{
    const std::size_t n = ring.size();
    for ( std::size_t k = 0; k < n; ++k )
    {
        const auto a = position(mesh, ring[k].first);
        const auto b = position(mesh, ring[(k + 1) % n].first);
        const auto c = position(mesh, ring[(k + 2) % n].first);
        if ( dot(cross(sub(b, a), sub(c, b)), normal) < -1e-12 )
        {
            return false;
        }
    }
    const double d0 = dot(normal, position(mesh, ring[0].first));
    return std::all_of(ring.begin(), ring.end(),
                       [&](const Corner& corner) { return std::abs(dot(normal, position(mesh, corner.first)) - d0) < 1e-6; });
}

/**
 * True when light and texture coordinates are linear functions of position in the polygon's
 * plane: the plane through three corners reproduces every other corner within tolerance.
 */
bool affineAttributes(const Model3doMesh& mesh, const std::vector<Corner>& ring, const Vec& normal,
                      const MeshOptimizationLimits& limits, bool useWidestBasis)
{
    if ( ring.size() <= 3 )
    {
        return true;
    }
    const Vec axis = std::abs(normal.x) < 0.9 ? Vec{1, 0, 0} : Vec{0, 1, 0};
    Vec e1 = cross(normal, axis);
    const double l = length(e1);
    e1 = {e1.x / l, e1.y / l, e1.z / l};
    const Vec e2 = cross(normal, e1);
    const auto origin = position(mesh, ring[0].first);
    std::vector<std::array<double, 2>> plane;
    plane.reserve(ring.size());
    for ( const auto& corner : ring )
    {
        const auto d = sub(position(mesh, corner.first), origin);
        plane.push_back({dot(d, e1), dot(d, e2)});
    }
    std::optional<std::pair<std::size_t, std::size_t>> basis;
    double det = 0;
    for ( std::size_t i = 1; i + 1 < ring.size() && (useWidestBasis || !basis); ++i )
    {
        for ( std::size_t j = i + 1; j < ring.size(); ++j )
        {
            const double candidate = plane[i][0] * plane[j][1] - plane[i][1] * plane[j][0];
            if ( std::abs(candidate) > 1e-12 && std::abs(candidate) > std::abs(det) )
            {
                det = candidate;
                basis = std::pair{i, j};
                if ( !useWidestBasis )
                {
                    break;
                }
            }
        }
    }
    if ( !basis )
    {
        return false;
    }
    const auto [i, j] = *basis;
    const auto fits = [&](const auto& value, double tolerance)
    {
        const double v0 = value(ring[0]), vi = value(ring[i]), vj = value(ring[j]);
        const double a = ((vi - v0) * plane[j][1] - (vj - v0) * plane[i][1]) / det;
        const double b = ((vj - v0) * plane[i][0] - (vi - v0) * plane[j][0]) / det;
        for ( std::size_t k = 0; k < ring.size(); ++k )
        {
            if ( !(std::abs(v0 + a * plane[k][0] + b * plane[k][1] - value(ring[k])) <= tolerance) )
            {
                return false;
            }
        }
        return true;
    };
    const auto color = [&](const Corner& c) -> const ColorF& { return mesh.vertices[static_cast<std::size_t>(c.first)].color; };
    const auto uv = [&](const Corner& c) -> const Model3doTexVertex& { return mesh.texVertices[static_cast<std::size_t>(c.second)]; };
    // With the widest basis, each fan interpolates residuals bounded by half the budget.
    // The difference between two such triangulations is therefore within the full budget.
    const double lightTolerance = limits.lightTolerance * (useWidestBasis ? 0.5 : 1.0);
    const double uvTolerance = limits.uvTolerance * (useWidestBasis ? 0.5 : 1.0);
    return fits([&](const Corner& c) { return static_cast<double>(color(c).r); }, lightTolerance) &&
           fits([&](const Corner& c) { return static_cast<double>(color(c).g); }, lightTolerance) &&
           fits([&](const Corner& c) { return static_cast<double>(color(c).b); }, lightTolerance) &&
           fits([&](const Corner& c) { return static_cast<double>(color(c).a); }, lightTolerance) &&
           fits([&](const Corner& c) { return static_cast<double>(uv(c).u); }, uvTolerance) &&
           fits([&](const Corner& c) { return static_cast<double>(uv(c).v); }, uvTolerance);
}

} // namespace

MeshOptimizationLimits meshOptimizationLimits(MeshOptimization level)
{
    switch ( level )
    {
    case MeshOptimization::Off:
    case MeshOptimization::Strict: return {};
    case MeshOptimization::Loose: return {0.5, 4.0 / 255.0, 0.5, 8};
    }
    throw std::invalid_argument("unknown mesh optimization level");
}

const char* meshOptimizationName(MeshOptimization level)
{
    switch ( level )
    {
    case MeshOptimization::Off: return "off";
    case MeshOptimization::Strict: return "strict";
    case MeshOptimization::Loose: return "loose";
    }
    throw std::invalid_argument("unknown mesh optimization level");
}

MeshOptimizationStats optimizeModel3doMesh(Model3doMesh& mesh, const MeshOptimizationLimits& limits)
{
    MeshOptimizationStats stats;
    stats.verticesIn = mesh.vertices.size();
    stats.texVerticesIn = mesh.texVertices.size();
    stats.facesIn = mesh.faces.size();
    const bool normals = !mesh.vertexNormals.empty();
    if ( normals && mesh.vertexNormals.size() != mesh.vertices.size() )
    {
        throw std::invalid_argument("vertex normal table does not match the vertex table");
    }
    if ( !mesh.faceNormals.empty() && mesh.faceNormals.size() != mesh.faces.size() )
    {
        throw std::invalid_argument("face normal table does not match the face table");
    }
    if ( !(limits.weldNormalDegrees >= 0 && limits.weldNormalDegrees <= 180) || !(limits.lightTolerance >= 0) ||
         !(limits.uvTolerance >= 0) || limits.maxCorners < 3 || limits.maxCorners > kModel3doMaxFaceVertices )
    {
        throw std::invalid_argument("mesh optimization limits are out of range");
    }
    for ( const auto& face : mesh.faces )
    {
        if ( face.vertices.size() != face.texVertices.size() || face.vertices.size() < 3 )
        {
            throw std::invalid_argument("a face needs matching vertex and texture-vertex lists of 3 or more");
        }
        for ( std::size_t c = 0; c < face.vertices.size(); ++c )
        {
            if ( face.vertices[c] < 0 || static_cast<std::size_t>(face.vertices[c]) >= mesh.vertices.size() ||
                 face.texVertices[c] < 0 || static_cast<std::size_t>(face.texVertices[c]) >= mesh.texVertices.size() )
            {
                throw std::invalid_argument("a face refers to a missing vertex or texture vertex");
            }
        }
    }

    // Weld. A vertex never welds to another corner of one of its own faces, so no face gains a
    // repeated vertex.
    using Key = std::array<double, 7>;
    const auto keyOf = [&](std::size_t i)
    {
        const auto& v = mesh.vertices[i];
        return Key{v.x, v.y, v.z, v.color.r, v.color.g, v.color.b, v.color.a};
    };
    std::vector<bool> pinned(mesh.vertices.size(), false);
    for ( const auto& face : mesh.faces )
    {
        for ( std::size_t a = 0; a < face.vertices.size(); ++a )
        {
            for ( std::size_t b = a + 1; b < face.vertices.size(); ++b )
            {
                const auto va = static_cast<std::size_t>(face.vertices[a]), vb = static_cast<std::size_t>(face.vertices[b]);
                if ( va != vb && keyOf(va) == keyOf(vb) )
                {
                    pinned[va] = pinned[vb] = true;
                }
            }
        }
    }
    const double cosLimit = std::cos(limits.weldNormalDegrees * std::numbers::pi / 180.0);
    const auto unit = [](const TVector& n) -> std::optional<Vec>
    {
        const double l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if ( !(l > 0) || !std::isfinite(l) )
        {
            return std::nullopt;
        }
        return Vec{n.x / l, n.y / l, n.z / l};
    };
    std::map<Key, std::vector<std::int32_t>> seeds;
    std::vector<std::int32_t> vertexMap(mesh.vertices.size());
    std::vector<Model3doVertex> vertices;
    std::vector<TVector> vertexNormals;
    for ( std::size_t i = 0; i < mesh.vertices.size(); ++i )
    {
        std::optional<std::int32_t> hit;
        if ( !pinned[i] )
        {
            auto& candidates = seeds[keyOf(i)];
            for ( const auto seed : candidates )
            {
                if ( !normals )
                {
                    hit = seed;
                    break;
                }
                const auto& mine = mesh.vertexNormals[i];
                const auto& theirs = vertexNormals[static_cast<std::size_t>(seed)];
                const auto a = unit(mine), b = unit(theirs);
                if ( a && b )
                {
                    const double c = std::clamp(dot(*a, *b), -1.0, 1.0);
                    if ( c >= cosLimit )
                    {
                        stats.worstNormalTurnDegrees =
                            std::max(stats.worstNormalTurnDegrees, std::acos(c) * 180.0 / std::numbers::pi);
                        hit = seed;
                        break;
                    }
                }
                else if ( mine.x == theirs.x && mine.y == theirs.y && mine.z == theirs.z )
                {
                    hit = seed;
                    break;
                }
            }
            if ( !hit )
            {
                candidates.push_back(static_cast<std::int32_t>(vertices.size()));
            }
        }
        if ( hit )
        {
            vertexMap[i] = *hit;
            continue;
        }
        vertexMap[i] = static_cast<std::int32_t>(vertices.size());
        vertices.push_back(mesh.vertices[i]);
        if ( normals )
        {
            vertexNormals.push_back(mesh.vertexNormals[i]);
        }
    }
    std::map<std::pair<float, float>, std::int32_t> uvSeeds;
    std::vector<std::int32_t> uvMap(mesh.texVertices.size());
    std::vector<Model3doTexVertex> texVertices;
    for ( std::size_t i = 0; i < mesh.texVertices.size(); ++i )
    {
        const auto& t = mesh.texVertices[i];
        const auto [at, added] = uvSeeds.try_emplace({t.u, t.v}, static_cast<std::int32_t>(texVertices.size()));
        if ( added )
        {
            texVertices.push_back(t);
        }
        uvMap[i] = at->second;
    }
    mesh.vertices = std::move(vertices);
    mesh.vertexNormals = std::move(vertexNormals);
    mesh.texVertices = std::move(texVertices);
    std::vector<std::vector<Corner>> rings(mesh.faces.size());
    for ( std::size_t f = 0; f < mesh.faces.size(); ++f )
    {
        auto& face = mesh.faces[f];
        for ( std::size_t c = 0; c < face.vertices.size(); ++c )
        {
            rings[f].push_back({vertexMap[static_cast<std::size_t>(face.vertices[c])],
                                uvMap[static_cast<std::size_t>(face.texVertices[c])]});
        }
    }

    // Merge across shared edges until nothing more merges.
    std::vector<Vec> faceNormal(mesh.faces.size());
    for ( std::size_t f = 0; f < mesh.faces.size(); ++f )
    {
        faceNormal[f] = newellNormal(mesh, rings[f]);
    }
    std::vector<bool> alive(mesh.faces.size(), true);
    const std::size_t maxCorners = std::min(limits.maxCorners, kModel3doMaxFaceVertices);
    bool useWidestBasis = false;
    for ( bool changed = true; changed; )
    {
        changed = false;
        bool hasRejectedFit = false;
        std::map<std::pair<Corner, Corner>, std::size_t> edges;
        for ( std::size_t f = 0; f < rings.size(); ++f )
        {
            if ( !alive[f] )
            {
                continue;
            }
            const auto& ring = rings[f];
            for ( std::size_t k = 0; k < ring.size(); ++k )
            {
                edges.emplace(std::pair{ring[k], ring[(k + 1) % ring.size()]}, f);
            }
        }
        for ( std::size_t a = 0; a < rings.size(); ++a )
        {
            if ( !alive[a] || length(faceNormal[a]) == 0 )
            {
                continue;
            }
            for ( std::size_t k = 0; k < rings[a].size(); ++k )
            {
                auto& p = rings[a];
                const auto from = p[k], to = p[(k + 1) % p.size()];
                const auto found = edges.find({to, from});
                if ( found == edges.end() )
                {
                    continue;
                }
                const std::size_t b = found->second;
                if ( b == a || !alive[b] || !sameFaceKey(mesh.faces[a], mesh.faces[b]) ||
                     dot(faceNormal[a], faceNormal[b]) < 1 - 1e-9 )
                {
                    continue;
                }
                const auto& q = rings[b];
                const auto at = std::find(q.begin(), q.end(), to);
                if ( at == q.end() || q[static_cast<std::size_t>(at - q.begin() + 1) % q.size()] != from )
                {
                    continue; // the edge index is stale: b changed earlier in this pass
                }
                const auto kb = static_cast<std::size_t>(at - q.begin());
                std::vector<Corner> merged(p.begin(), p.begin() + static_cast<std::ptrdiff_t>(k) + 1);
                for ( std::size_t t = 0; t + 2 < q.size(); ++t )
                {
                    merged.push_back(q[(kb + 2 + t) % q.size()]);
                }
                merged.insert(merged.end(), p.begin() + static_cast<std::ptrdiff_t>(k) + 1, p.end());
                if ( merged.size() > maxCorners )
                {
                    continue;
                }
                std::vector<std::int32_t> used;
                for ( const auto& corner : merged )
                {
                    used.push_back(corner.first);
                }
                std::sort(used.begin(), used.end());
                if ( std::adjacent_find(used.begin(), used.end()) != used.end() ||
                     !convexPlanar(mesh, merged, faceNormal[a]) )
                {
                    continue;
                }
                if ( !affineAttributes(mesh, merged, faceNormal[a], limits, useWidestBasis) )
                {
                    hasRejectedFit = true;
                    continue;
                }
                p = std::move(merged);
                alive[b] = false;
                changed = true;
                break;
            }
        }
        // Finish the original greedy merge order before trying a better-conditioned basis.
        // A retry can then only remove additional faces from each mesh.
        if ( !changed && hasRejectedFit && !useWidestBasis )
        {
            useWidestBasis = true;
            changed = true;
        }
    }
    std::vector<Model3doFace> faces;
    std::vector<TVector> faceNormals;
    for ( std::size_t f = 0; f < mesh.faces.size(); ++f )
    {
        if ( !alive[f] )
        {
            continue;
        }
        auto face = std::move(mesh.faces[f]);
        face.vertices.clear();
        face.texVertices.clear();
        for ( const auto& corner : rings[f] )
        {
            face.vertices.push_back(corner.first);
            face.texVertices.push_back(corner.second);
        }
        faces.push_back(std::move(face));
        if ( !mesh.faceNormals.empty() )
        {
            faceNormals.push_back(mesh.faceNormals[f]);
        }
    }
    mesh.faces = std::move(faces);
    mesh.faceNormals = std::move(faceNormals);
    stats.verticesOut = mesh.vertices.size();
    stats.texVerticesOut = mesh.texVertices.size();
    stats.facesOut = mesh.faces.size();
    return stats;
}

} // namespace sed::assets
