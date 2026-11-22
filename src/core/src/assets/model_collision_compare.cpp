#include "sed/assets/model_collision_compare.hpp"

#include <algorithm>
#include <array>
#include <compare>

namespace sed::assets
{
namespace
{
struct FaceRef
{
    const Model3doMesh* mesh;
    std::size_t index;
};

template <class Point>
std::array<double, 3> coordinates(const Point& point)
{
    return { point.x, point.y, point.z };
}

// Both documents came through the bounded classic source reader: indices are
// in range, numeric values finite, and one authored normal exists per face.
std::partial_ordering compareFaces(const FaceRef a, const FaceRef b)
{
    const auto& av = a.mesh->faces[a.index].vertices;
    const auto& bv = b.mesh->faces[b.index].vertices;
    if ( const auto length = av.size() <=> bv.size(); length != 0 )
    {
        return length;
    }
    const auto normal = coordinates(a.mesh->faceNormals[a.index]) <=> coordinates(b.mesh->faceNormals[b.index]);
    if ( normal != 0 )
    {
        return normal;
    }
    for ( std::size_t i = 0; i < av.size(); ++i )
    {
        const auto& left = a.mesh->vertices[static_cast<std::size_t>(av[i])];
        const auto& right = b.mesh->vertices[static_cast<std::size_t>(bv[i])];
        if ( const auto vertex = coordinates(left) <=> coordinates(right); vertex != 0 )
        {
            return vertex;
        }
    }
    return std::partial_ordering::equivalent;
}

std::vector<FaceRef> collectFaces(const Model3do& model, ModelCollisionFaceCounts& counts)
{
    std::vector<FaceRef> result;
    counts.meshes = model.meshes.size();
    for ( const auto& mesh : model.meshes )
    {
        for ( std::size_t f = 0; f < mesh.faces.size(); ++f )
        {
            const auto& face = mesh.faces[f];
            counts.untexturedModeZeroFaces += face.material == -1 && face.geo == 0;
            result.push_back({ &mesh, f });
        }
    }
    counts.faces = result.size();
    return result;
}
} // namespace

ModelCollisionComparison compareModelCollision(const NativeModelDocument& source, const NativeModelDocument& candidate)
{
    ModelCollisionComparison result;
    const auto& original = source.geosets().front();
    const auto& replacement = candidate.geosets().front();
    auto left = collectFaces(original, result.source);
    auto right = collectFaces(replacement, result.candidate);
    result.sameMeshFaceCounts = std::ranges::equal(original.meshes, replacement.meshes, [](const auto& a, const auto& b)
    {
        return a.faces.size() == b.faces.size();
    });
    result.sameFaceSequence = std::ranges::equal(left, right, [](FaceRef a, FaceRef b) { return compareFaces(a, b) == 0; });
    if ( result.sameFaceSequence )
    {
        result.matchedFaces = left.size();
        return result;
    }

    // Sort references only. The length-first comparator avoids walking a wide
    // polygon's vertices for comparisons with smaller polygons.
    const auto less = [](FaceRef a, FaceRef b) { return compareFaces(a, b) < 0; };
    std::sort(left.begin(), left.end(), less);
    std::sort(right.begin(), right.end(), less);
    std::size_t i = 0, j = 0;
    while ( i < left.size() && j < right.size() )
    {
        const auto order = compareFaces(left[i], right[j]);
        if ( order < 0 )
        {
            ++i;
        }
        else if ( order > 0 )
        {
            ++j;
        }
        else
        {
            ++result.matchedFaces;
            ++i;
            ++j;
        }
    }
    result.missingFaces = left.size() - result.matchedFaces;
    result.extraFaces = right.size() - result.matchedFaces;
    return result;
}

} // namespace sed::assets
