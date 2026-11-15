#pragma once
#include "sed/render/software_snapshot.hpp"
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace sed::render
{
/** Preparation budgets, independent of rasterizer work and parser admission. */
struct SnapshotThingLimits
{
    std::size_t maxThings{4096};
    // Conservative aggregate comparison estimate along actual template chains.
    // Admission only: the existing raw-name/last-field resolver remains authoritative.
    std::size_t maxTemplateLookupWork{64000000};
    std::size_t maxResources{128};
    std::size_t maxResourceBytes{16u * 1024u * 1024u};
    std::size_t maxTotalResourceBytes{64u * 1024u * 1024u};
    std::size_t maxCachedModelBytes{128u * 1024u * 1024u};
    std::size_t maxVertices{250000};
    std::size_t maxCorners{1000000};
    std::size_t maxMeshes{65536};
};
struct SnapshotThingDiagnostic
{
    std::size_t thingIndex{};
    std::string resource;
    std::string status;
    std::string message;
};
struct SnapshotThingScene
{
    std::vector<SnapshotGeometry> geometry;
    std::vector<SnapshotThingDiagnostic> diagnostics;
    // Counts instances with prepared visible geometry, not screen-visible pixels.
    std::size_t models{};
    std::size_t sprites{};
};
// Resolver bytes must remain immutable and alive for the duration of preparation.
using SnapshotThingResourceResolver = std::function<std::optional<std::string_view>(std::string_view)>;
/**
 * Builds transient static LOD0/bind-pose geometry without modifying the Level.
 * Resolves only local/embedded template fields and caller-captured resource bytes;
 * performs no filesystem reads. Classic JK/MotS sprites are camera-facing cel0
 * cards; other project sprite profiles are explicitly unsupported.
 * One diagnostic is emitted per Thing. Missing/invalid resources and invalid
 * placements are skipped without placeholder icons. Resource names are capped
 * at 255 bytes; oversized primary references and exhausted budgets throw
 * std::runtime_error. Model material names exceeding the cap are invalid assets.
 * The cache limit accounts retained decoded capacities after the independently
 * bounded core parser returns, before caching or instance expansion. It is not
 * a replacement for the parser's own peak-allocation limits.
 * Vertex/mesh/corner budgets include every instance, even when assets are cached.
 * This preparation is not engine lighting, animation or gameplay verification.
 */
SnapshotThingScene prepareSnapshotThings(const Level&, const SnapshotCamera&,
                                         const SnapshotThingResourceResolver&,
                                         const SnapshotThingLimits& = {});
} // namespace sed::render
