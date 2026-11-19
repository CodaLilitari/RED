#pragma once

#include "sed/assets/3do.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace sed::assets
{

// Largest single mesh documented as drawing in both retail JK 1.01 renderers.
// The loader's independent 512-position / 768-UV limits still apply.
inline constexpr std::size_t kJoJaResidualMaxFaces = 479;
inline constexpr std::size_t kJoJaResidualMaxTriangles = kJoJaResidualMaxFaces;

struct JoJaResidualCorner
{
    Model3doVertex vertex; // Scaled world position and authored corner light.
    TVector normal;        // Target-space authored vertex normal.
    Model3doTexVertex uv;  // 3DO 2.1 texture pixels.
};

struct JoJaResidualTriangle
{
    std::array<JoJaResidualCorner, 3> corners;
    std::int32_t material{};
};

struct JoJaResidualFace
{
    std::vector<JoJaResidualCorner> corners;
    std::int32_t material{};
};

struct JoJaResidualFacePacket
{
    std::size_t firstFace{};
    TVector center;
    Model3doMesh mesh;
};

struct JoJaResidualMesh
{
    std::size_t firstTriangle{};
    TVector center;
    Model3doMesh mesh;
};

// Pass one sector/footstep/collision group at a time in source winding order.
// Face material indices retain the caller's numbering. Each mesh's local
// positions are relative to center; its faces cover consecutive input triangles.
// A smaller positive face budget is honored; larger budgets stop at the tested cap.
// sectorInterior is a proven interior point of the owning convex sector. The
// optional predicate certifies the final float32 pivot against its boundary planes.
// Throws when a corner is nonfinite or localization collapses or reverses a face in float32.
std::vector<JoJaResidualMesh>
packJoJaResidualMeshes(std::span<const JoJaResidualTriangle> triangles, std::size_t trianglesPerMesh,
                       std::optional<TVector> sectorInterior = std::nullopt,
                       const std::function<bool(const TVector&)>& containsTargetPivot = {});

// Each input is one convex, planar clipped source polygon with 3..24 ordered
// corners. Its face, independent per-position and UV seams, and corner order
// survive packing. The same pivot constraints and mesh limits apply.
std::vector<JoJaResidualFacePacket>
packJoJaResidualFaces(std::span<const JoJaResidualFace> faces, std::size_t facesPerMesh,
                      std::optional<TVector> sectorInterior = std::nullopt,
                      const std::function<bool(const TVector&)>& containsTargetPivot = {});

} // namespace sed::assets
