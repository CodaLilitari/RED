#include "sed/assets/jo_ja_residual_mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace sed::assets
{
namespace
{
using PositionKey = std::array<double, 10>;
using UvKey = std::array<float, 2>;

PositionKey positionKey(const JoJaResidualCorner& corner)
{
    const auto& v = corner.vertex;
    const auto& n = corner.normal;
    return {v.x, v.y, v.z, n.x, n.y, n.z, v.color.r, v.color.g, v.color.b, v.color.a};
}

template <typename Key> std::int32_t indexFor(std::map<Key, std::int32_t>& indices, const Key& key)
{
    if ( !std::all_of(key.begin(), key.end(),
                      [](auto value)
                      {
                          return std::isfinite(value);
                      }) )
    {
        throw std::runtime_error("nonfinite JO/JA residual mesh corner");
    }
    return indices.try_emplace(key, static_cast<std::int32_t>(indices.size())).first->second;
}

TVector targetPoint(const TVector& point)
{
    constexpr double limit = std::numeric_limits<float>::max();
    for ( const double value : {point.x, point.y, point.z} )
    {
        if ( !std::isfinite(value) || std::abs(value) > limit )
        {
            throw std::runtime_error("JO/JA residual mesh exceeds target single precision");
        }
    }
    return {static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)};
}

TVector orientedArea(std::span<const TVector> ring)
{
    TVector sum{};
    for ( std::size_t i = 1; i + 1 < ring.size(); ++i )
    {
        sum = sum + (ring[i] - ring[0]).Cross(ring[i + 1] - ring[0]);
    }
    return sum;
}

struct TargetPrecisionError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

TVector supportingNormal(std::span<const TVector> ring, const TVector& area)
{
    // Subtracting nearly parallel large products for a narrow polygon can
    // tilt its summed area normal. Choose the existing-corner plane with the
    // smallest measured residual; the acceptance bound itself stays fixed.
    auto error = [&](const TVector& n)
    {
        double worst = 0;
        for ( const auto& p : ring )
        {
            worst = std::max(worst, std::abs((p - ring.front()).Dot(n)));
        }
        return worst;
    };
    auto best = area.Scaled(1 / area.Length());
    auto deviation = error(best);
    for ( std::size_t i = 0; i < ring.size() && deviation > 0; ++i )
    {
        const auto& p = ring[i];
        auto candidate = (ring[(i + 1) % ring.size()] - p).Cross(ring[(i + ring.size() - 1) % ring.size()] - p);
        if ( !(candidate.Dot(area) > 0) )
        {
            continue;
        }
        candidate = candidate.Scaled(1 / candidate.Length());
        const double value = error(candidate);
        if ( value < deviation )
        {
            best = candidate;
            deviation = value;
        }
    }
    return best;
}

void certifyPolygon(std::span<const TVector> ring, const TVector& normal, double rounding)
{
    double extent = 0;
    for ( const auto& p : ring )
    {
        extent = std::max(extent, p.Distance(ring.front()));
    }
    const double arithmetic = 512 * std::numeric_limits<double>::epsilon() * extent;
    const double planeTolerance = arithmetic + 4 * rounding;
    for ( std::size_t i = 0; i < ring.size(); ++i )
    {
        const auto& a = ring[i];
        const auto edge = ring[(i + 1) % ring.size()] - a;
        const double length = edge.Length();
        if ( !(length > 0) || std::abs((a - ring.front()).Dot(normal)) > planeTolerance )
        {
            throw std::runtime_error("JO/JA residual polygon has a collapsed edge or is nonplanar");
        }
        for ( std::size_t j = 0; j < ring.size(); ++j )
        {
            const auto offset = ring[j] - a;
            if ( (j != i && offset.x == 0 && offset.y == 0 && offset.z == 0) ||
                 edge.Cross(offset).Dot(normal) < -arithmetic * length )
            {
                throw std::runtime_error("JO/JA residual polygon is not a simple convex face");
            }
        }
    }
}

void localize(Model3doMesh& mesh, TVector& center, std::optional<TVector> sectorInterior,
              const std::function<bool(const TVector&)>& containsTargetPivot, std::optional<TVector> preferred = {})
{
    // The mean of the original corners belongs to their convex hull. The
    // midpoint of a 3D bounding box can be outside the owning sector.
    TVector centroid{};
    std::size_t cornerCount = 0;
    for ( const auto& face : mesh.faces )
    {
        cornerCount += face.vertices.size();
    }
    const double count = static_cast<double>(cornerCount);
    for ( const auto& face : mesh.faces )
    {
        for ( const auto index : face.vertices )
        {
            const auto& vertex = mesh.vertices[index];
            centroid.x += vertex.x / count;
            centroid.y += vertex.y / count;
            centroid.z += vertex.z / count;
        }
    }
    if ( preferred )
    {
        centroid = *preferred;
    }
    center = targetPoint(centroid);
    if ( sectorInterior )
    {
        const auto anchor = targetPoint(*sectorInterior);
        constexpr double inwardFraction = 1e-5;
        const TVector candidate = targetPoint({std::lerp(centroid.x, anchor.x, inwardFraction),
                                               std::lerp(centroid.y, anchor.y, inwardFraction),
                                               std::lerp(centroid.z, anchor.z, inwardFraction)});
        const bool stepLost = candidate.x == center.x && candidate.y == center.y && candidate.z == center.z;
        center = stepLost || (containsTargetPivot && !containsTargetPivot(candidate)) ? anchor : candidate;
    }
    if ( containsTargetPivot && !containsTargetPivot(center) )
    {
        throw std::runtime_error("JO/JA residual mesh has no verified in-sector target pivot");
    }
    std::vector<TVector> engineVertices;
    engineVertices.reserve(mesh.vertices.size());
    for ( auto& v : mesh.vertices )
    {
        v.x -= center.x;
        v.y -= center.y;
        v.z -= center.z;
        const double radius = std::hypot(v.x, v.y, v.z);
        if ( radius > std::numeric_limits<float>::max() )
        {
            throw std::runtime_error("JO/JA residual mesh exceeds target single precision");
        }
        const TVector target = targetPoint({v.x, v.y, v.z});
        engineVertices.push_back(target);
        mesh.radius = std::max({mesh.radius, radius, std::hypot(target.x, target.y, target.z)});
    }
    mesh.authoredRadius = mesh.radius;
    for ( const auto& face : mesh.faces )
    {
        std::array<TVector, kModel3doMaxFaceVertices> original{}, encoded{};
        double rounding = 0;
        for ( std::size_t i = 0; i < face.vertices.size(); ++i )
        {
            const auto index = face.vertices[i];
            const auto& v = mesh.vertices[index];
            original[i] = {v.x, v.y, v.z};
            encoded[i] = engineVertices[index];
            rounding = std::max(rounding, original[i].Distance(encoded[i]));
        }
        const std::span source(original.data(), face.vertices.size()), target(encoded.data(), face.vertices.size());
        const auto a = orientedArea(source);
        const auto b = orientedArea(target);
        if ( !(a.Dot(b) > 0) )
        {
            throw TargetPrecisionError("a JO/JA residual face collapses or reverses at target single precision");
        }
        if ( face.vertices.size() > 3 )
        {
            const auto normal = supportingNormal(source, a);
            certifyPolygon(source, normal, 0);
            try
            {
                certifyPolygon(target, normal, rounding);
            }
            catch ( const std::runtime_error& e )
            {
                throw TargetPrecisionError(e.what());
            }
        }
    }
}
void localizePacket(Model3doMesh& mesh, TVector& center, std::optional<TVector> interior,
                    const std::function<bool(const TVector&)>& containsTargetPivot, bool adaptive)
{
    if ( !adaptive || mesh.faces.size() != 1 )
    {
        localize(mesh, center, interior, containsTargetPivot);
        return;
    }
    // Moving the pivot does not move a world-space corner. A long singleton
    // face can have two distinct corners too close together at its centroid's
    // local float scale. Try each endpoint, with the same inward sector check.
    const Model3doMesh original = mesh;
    std::string issue;
    try
    {
        localize(mesh, center, interior, containsTargetPivot);
        return;
    }
    catch ( const TargetPrecisionError& e )
    {
        issue = e.what();
    }
    for ( const auto& v : original.vertices )
    {
        mesh = original;
        try
        {
            localize(mesh, center, interior, containsTargetPivot, TVector{v.x, v.y, v.z});
            return;
        }
        catch ( const TargetPrecisionError& )
        {
        }
    }
    throw TargetPrecisionError(issue);
}

template <typename Packet, typename SourceFace>
std::vector<Packet> pack(std::span<const SourceFace> faces, std::size_t facesPerMesh, bool adaptive,
                         std::optional<TVector> sectorInterior,
                         const std::function<bool(const TVector&)>& containsTargetPivot)
{
    if ( facesPerMesh == 0 )
    {
        throw std::runtime_error("JO/JA residual mesh face budget must be positive");
    }
    const auto maximum = std::min(facesPerMesh, kJoJaResidualMaxFaces);
    auto faceLimit = maximum;
    std::vector<Packet> packets;
    std::size_t next = 0;
    while ( next < faces.size() )
    {
        const auto first = next;
        auto& packet = packets.emplace_back(Packet{first, {}, {}});
        auto& mesh = packet.mesh;
        mesh.name = "world";
        mesh.geo = 4;
        mesh.light = 3;
        mesh.tex = 1;
        std::map<PositionKey, std::int32_t> positions;
        std::map<UvKey, std::int32_t> uvs;
        while ( next < faces.size() && mesh.faces.size() < faceLimit )
        {
            const auto& source = faces[next];
            if ( source.corners.size() < 3 || source.corners.size() > kModel3doMaxFaceVertices )
            {
                throw std::runtime_error("JO/JA residual mesh face requires 3 to 24 corners");
            }
            Model3doFace face;
            face.material = source.material;
            face.geo = mesh.geo;
            face.light = mesh.light;
            face.tex = mesh.tex;
            face.extraLight = ColorBlack;
            for ( const auto& corner : source.corners )
            {
                face.vertices.push_back(indexFor(positions, positionKey(corner)));
                face.texVertices.push_back(indexFor(uvs, UvKey{corner.uv.u, corner.uv.v}));
            }
            if ( positions.size() > kModel3doMaxMeshVertices || uvs.size() > kModel3doMaxMeshTextureVertices )
            {
                // Discard the lookup maps and retry this entire face in the next packet.
                break;
            }
            for ( std::size_t c = 0; c < source.corners.size(); ++c )
            {
                const auto& corner = source.corners[c];
                if ( static_cast<std::size_t>(face.vertices[c]) == mesh.vertices.size() )
                {
                    mesh.vertices.push_back(corner.vertex);
                    mesh.vertexNormals.push_back(corner.normal);
                }
                if ( static_cast<std::size_t>(face.texVertices[c]) == mesh.texVertices.size() )
                {
                    mesh.texVertices.push_back(corner.uv);
                }
            }
            mesh.faces.push_back(std::move(face));
            ++next;
        }
        // saveTo3DO derives geometric face normals from this unchanged winding.
        try
        {
            localizePacket(mesh, packet.center, sectorInterior, containsTargetPivot, adaptive);
            faceLimit = maximum;
        }
        catch ( const TargetPrecisionError& e )
        {
            if ( !adaptive || next == first + 1 )
            {
                throw std::runtime_error("residual face group beginning at " + std::to_string(first) + ": " + e.what());
            }
            // Rebase a smaller contiguous packet. A face is never modified
            // or dropped, and a singleton that still fails is a hard refusal.
            faceLimit = (next - first) / 2;
            next = first;
            packets.pop_back();
        }
    }
    return packets;
}
} // namespace

std::vector<JoJaResidualMesh> packJoJaResidualMeshes(std::span<const JoJaResidualTriangle> triangles,
                                                     std::size_t trianglesPerMesh,
                                                     std::optional<TVector> sectorInterior,
                                                     const std::function<bool(const TVector&)>& containsTargetPivot)
{
    return pack<JoJaResidualMesh>(triangles, trianglesPerMesh, false, sectorInterior, containsTargetPivot);
}

std::vector<JoJaResidualFacePacket>
packJoJaResidualFaces(std::span<const JoJaResidualFace> faces, std::size_t facesPerMesh,
                      std::optional<TVector> sectorInterior,
                      const std::function<bool(const TVector&)>& containsTargetPivot)
{
    return pack<JoJaResidualFacePacket>(faces, facesPerMesh, true, sectorInterior, containsTargetPivot);
}
} // namespace sed::assets
