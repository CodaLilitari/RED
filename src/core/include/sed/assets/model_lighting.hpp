#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "sed/color.hpp"
#include "sed/geometry.hpp"

namespace sed::assets
{

enum class ModelLightingProfile
{
    ClassicScalar,
    IjimRgb,
};

enum class ModelLightingBlend
{
    Replace,
    Add,
};

enum class ModelLightKind
{
    Point,
    Directional,
};

struct ModelLightingLight
{
    ModelLightKind kind{ ModelLightKind::Point };
    TVector position{};
    // From the sample toward the light, not the direction photons travel.
    TVector directionToLight{ 0.0, 0.0, 1.0 };
    ColorF color{ ColorWhite };
    double intensity{ 1.0 };
    double range{ 1.0 };
};

// Operational limits, not game-format limits. Callers may lower these values;
// raising any value above its default is refused before parsing or baking.
// Geometry totals cover all LODs. Work units charge source bytes for each scan
// and parser pass, hierarchy visits, polygon validation, sample/light pairs,
// potential shadow tests and output patches. Source and output share the byte
// ceiling; requesting a very low work budget can refuse even inspection.
struct ModelLightingLimits
{
    std::size_t sourceBytes{ 16u * 1024u * 1024u };
    std::size_t lines{ 262144u };
    std::size_t tokensPerLine{ 4096u };
    std::size_t geosets{ 32u };
    std::size_t meshes{ 4096u };
    std::size_t vertices{ 262144u };
    std::size_t triangles{ 524288u };
    std::size_t hierarchyNodes{ 4096u };
    std::size_t lights{ 256u };
    std::size_t workUnits{ 50000000u };
};

struct ModelLightingModeCount
{
    int mode{};
    std::size_t faces{};
};

struct ModelLightingMeshInspection
{
    std::size_t geoset{};
    std::size_t sourceMesh{};
    std::string name;
    std::size_t vertices{};
    std::size_t faces{};
    std::size_t renderInstances{};
    int meshLightMode{};
    std::vector<ModelLightingModeCount> effectiveLightModes;
    ColorF minimumVertexLight{};
    ColorF maximumVertexLight{};
    ColorF minimumFaceExtra{};
    ColorF maximumFaceExtra{};
    std::size_t missingNormals{};
    std::size_t zeroNormals{};
    std::size_t nonUnitNormals{};
    std::size_t saturatedVertices{};
};

struct ModelLightingInspection
{
    double version{};
    std::size_t geosets{};
    std::size_t sourceMeshes{};
    std::size_t renderMeshes{};
    std::size_t vertices{};
    std::size_t orphanMeshes{};
    std::size_t sharedMeshes{};
    std::vector<ModelLightingMeshInspection> meshes;
};

struct ModelLightingOptions
{
    ModelLightingProfile profile{ ModelLightingProfile::ClassicScalar };
    ModelLightingBlend blend{ ModelLightingBlend::Replace };
    ColorF ambient{ 0.0f, 0.0f, 0.0f, 1.0f };
    std::vector<ModelLightingLight> lights;
    bool selfOcclusion{ false };
    double shadowBias{ 1e-6 };
    ModelLightingLimits limits{};
    // True cancels with an exception; no partial output is returned.
    std::function<bool()> cancelled;
};

struct ModelLightingBakeResult
{
    std::string text;
    ModelLightingInspection before;
    ModelLightingInspection after;
    std::size_t changedVertices{};
    std::size_t clampedVertices{};
    std::size_t occludedSamples{};
    std::size_t zeroNormalVertices{};
};

// Strict admission of 2.1 scalar/grey-RGBA and 2.3 RGBA files. Inspects every
// source mesh in every LOD. Counts report stored source vertices, not clones.
// Throws std::runtime_error with a "Model lighting: " diagnostic on refusal.
ModelLightingInspection inspectModelLighting(
    std::span<const std::byte> source,
    const ModelLightingLimits& limits = {});

// Offline bind-pose authoring recipe: ambient + ordered Lambert lights; point
// lights use (1-distance/range)^2 inside range. Only lighting numeric tokens
// change; alpha, geometry, hierarchy, comments and unknown sections retain
// their exact bytes. Orphans are retained unchanged. Multiple hierarchy
// instances of one source mesh are refused because one stored sample cannot
// represent their different placements. LODs are evaluated independently.
// Optional self-occlusion uses same-LOD drawn, opaque two-sided polygon fans
// after validating convex planar topology; it does not sample MAT cutouts.
// Evaluated vertex lighting is clamped to [0,1]; orphan data is preserved.
// This is not a retail runtime/baker simulation.
ModelLightingBakeResult bakeModelLighting(
    std::span<const std::byte> source,
    const ModelLightingOptions& options);

} // namespace sed::assets
