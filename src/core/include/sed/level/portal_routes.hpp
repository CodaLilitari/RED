#pragma once

#include "sed/level.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace sed::level
{

enum class PortalRoutePolicy { movementBit, paired };

struct PortalRouteRequest
{
    std::int32_t startSector{};
    std::optional<std::int32_t> targetSector;
    PortalRoutePolicy policy{ PortalRoutePolicy::movementBit };
    std::vector<SurfaceRef> excludePortals;
};

struct PortalHop
{
    SurfaceRef from, to;
    std::uint32_t fromFlags{}, toFlags{};
};

struct PortalRouteState
{
    std::vector<std::int32_t> reachable;
    // Null for absent/unreachable target; empty for start == target.
    std::optional<std::vector<PortalHop>> targetPath;
};

struct PortalReferenceIssue
{
    SurfaceRef from, to;
    std::optional<SurfaceRef> peerPointsTo;
    std::string_view reason;
};

struct PortalRouteProbes
{
    std::size_t checked{};
    bool complete{};
    std::vector<PortalHop> unavoidable;
    std::optional<std::vector<PortalHop>> alternative;
};

struct PortalRouteReport
{
    PortalRouteState baseline, after;
    std::vector<std::int32_t> lost;
    std::vector<PortalHop> excluded;
    std::size_t pairCount{}, asymmetricCount{}, movementMismatchCount{}, invalidCount{};
    std::vector<PortalHop> asymmetric;
    std::vector<PortalReferenceIssue> invalid;
    std::optional<PortalRouteProbes> probes;
};

inline constexpr std::size_t kPortalRouteMaxExclusions = 256;

/** Structural analysis of the loaded level's reciprocal surface references.
 * movementBit follows 0x2 on each departing face. paired follows both faces.
 * Throws std::invalid_argument for invalid addresses/requests and std::length_error
 * for levels or exclusion lists exceeding admission limits. Never mutates the level. */
[[nodiscard]] PortalRouteReport analyzePortalRoutes(const Level& level, const PortalRouteRequest& request);

} // namespace sed::level
