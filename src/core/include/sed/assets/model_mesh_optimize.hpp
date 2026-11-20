#pragma once

#include "sed/assets/3do.hpp"

#include <cstddef>
#include <span>

namespace sed::assets
{

/**
 * Clean-up for generated world meshes, from the Sith Council conversion.
 *
 * Strict and Loose both:
 * - weld vertices that share a position and every light channel when their normals differ by
 *   at most 0.5 degrees, keeping the first normal. Only dynamic light reads vertex normals, so
 *   baked light, positions and texture coordinates are unchanged;
 * - share texture vertices whose coordinates are identical;
 * - merge neighbouring faces into convex polygons of up to 8 corners when they share an edge
 *   (the same vertex and texture-vertex indices), material, flags, modes and extra light, lie in
 *   one plane, and their per-corner light and texture coordinates vary linearly across the
 *   merged polygon. The engine draws a polygon as a fan of triangles, so a merge that is not
 *   linear would change the light blend or slide the texture inside the face.
 *
 * Strict allows each corner's light to sit within half an 8-bit step of the fitted plane and its
 * texture coordinates within 1/32 texel. Loose allows 4/255 and half a texel. The old triangles
 * and the new fan both interpolate corners within that distance of the plane, so a pixel can
 * change by up to twice it: one 8-bit step of light in strict. When a fit is rejected, a second
 * pass retries it from the widest corner pair at half the tolerance, which bounds that pass's
 * change to the tolerance itself. Off leaves a mesh untouched.
 *
 * Loose also removes straight corners across a whole scene; see pruneStraightCorners.
 */
enum class MeshOptimization
{
    Off,
    Strict,
    Loose,
};

struct MeshOptimizationLimits
{
    /** Largest angle between two normals that still weld; the first normal is kept. */
    double weldNormalDegrees{0.5};
    /** Largest departure of any light channel from the merged polygon's linear fit. */
    double lightTolerance{0.5 / 255.0};
    /** The same for texture coordinates, in the mesh's texture-vertex units (texels for 3DO 2.1). */
    double uvTolerance{1.0 / 32.0};
    /** Most corners a merged face may have; never more than the loader's 24. */
    std::size_t maxCorners{8};
};

MeshOptimizationLimits meshOptimizationLimits(MeshOptimization level);
const char* meshOptimizationName(MeshOptimization level);

struct MeshOptimizationStats
{
    std::size_t verticesIn{};
    std::size_t verticesOut{};
    std::size_t texVerticesIn{};
    std::size_t texVerticesOut{};
    std::size_t facesIn{};
    std::size_t facesOut{};
    /** Largest angle any corner's normal turned through when its vertex welded. */
    double worstNormalTurnDegrees{};
};

/**
 * Rewrites one mesh in place. Corners keep their positions, light and texture coordinates, and
 * faces keep their winding; vertices and texture vertices nothing references are kept.
 * Throws std::invalid_argument when the mesh's tables disagree.
 */
MeshOptimizationStats optimizeModel3doMesh(Model3doMesh& mesh, const MeshOptimizationLimits& limits);

/** A mesh and the world position of its model's origin. */
struct MeshPlacement
{
    Model3doMesh* mesh{};
    TVector offset{};
};

struct StraightCornerStats
{
    std::size_t cornersRemoved{};
    std::size_t verticesIn{};
    std::size_t verticesOut{};
    std::size_t texVerticesIn{};
    std::size_t texVerticesOut{};
    /** Triangles the engine draws: each face of n corners is a fan of n - 2. */
    std::size_t fanTrianglesIn{};
    std::size_t fanTrianglesOut{};
};

/**
 * Removes face corners that lie on the straight edge between their neighbours, each of which is
 * one fewer fan triangle for the engine to draw. Merged faces keep such corners because a face on
 * the other side of the edge turns there.
 *
 * A position is removed only from every face of every given mesh that has a corner there, and
 * only when no fixed position (sector vertices, for example) is there, so no face is left with a
 * T-junction that could open single-pixel cracks. On each final edge, every removed corner must lie
 * within 1e-6 units of the edge, in order, with its light within half `lightTolerance`, texture
 * coordinates within half `uvTolerance` and normal within `weldNormalDegrees` of the straight-line
 * interpolation. A face's first corner, its fan origin, is kept, and no face drops below three
 * corners. Positions match within 1e-5 units across meshes.
 *
 * Dynamic lights are computed at vertices, so they are sampled at fewer points afterwards. Baked
 * light, texture coordinates and silhouettes are unchanged within the tolerances. Vertices and
 * texture vertices left unreferenced are removed. Throws std::invalid_argument when a mesh's
 * tables disagree.
 */
StraightCornerStats pruneStraightCorners(std::span<const MeshPlacement> meshes, std::span<const TVector> fixedPositions,
                                         const MeshOptimizationLimits& limits);

} // namespace sed::assets
