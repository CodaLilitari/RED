#pragma once

#include "sed/assets/3do.hpp"

#include <cstddef>

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
 * Strict allows light to depart from that plane by half an 8-bit step and texture coordinates
 * by 1/32 texel. Loose allows 4/255 and half a texel. Off leaves a mesh untouched.
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

} // namespace sed::assets
