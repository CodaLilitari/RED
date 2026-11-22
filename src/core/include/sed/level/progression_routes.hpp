#pragma once

#include "sed/level/portal_routes.hpp"

#include <string>

namespace sed::level
{

using AbilityMask = std::uint16_t;

struct ProgressionGrant
{
    std::int32_t sector{};
    AbilityMask requiresAbilities{}, provides{};
};

struct ProgressionGate
{
    SurfaceRef portal;
    AbilityMask requiresAbilities{};
};

struct ProgressionRequest
{
    std::int32_t startSector{}, targetSector{};
    std::optional<std::int32_t> returnSector;
    PortalRoutePolicy policy{PortalRoutePolicy::movementBit};
    std::vector<SurfaceRef> excludePortals;
    std::uint8_t abilityCount{};
    AbilityMask initialAbilities{}, requiredAbilities{};
    std::vector<ProgressionGrant> grants;
    std::vector<ProgressionGate> gates;
    std::size_t maxStates{65536}, maxWorkUnits{2'000'000};
};

struct ProgressionStep
{
    std::int32_t sector{};
    AbilityMask abilities{};
    std::optional<PortalHop> via;
};

struct ProgressionWitness
{
    std::vector<ProgressionStep> steps;
    ProgressionStep endState;
    std::size_t stepCount{};
    bool complete{};
};

struct ProgressionReport
{
    bool complete{};
    std::string stoppedBy;
    std::size_t stateCount{}, transitionCount{}, workUnits{};
    // Positive witnesses survive budget exhaustion. Unproved outcomes are null.
    // Return is null when no target state was discovered.
    std::optional<bool> targetReachable, returnPossible, possibleSoftlock;
    std::optional<ProgressionWitness> targetPath, roundTripPath, softlockPath;
};

/** Finite authored model over reciprocal portal hops and monotone abilities.
 * Grants at a sector fire to a fixed point when their requirements are held.
 * Gate requirements apply to both faces; permanent exclusions stay closed.
 * No geometry, physics, COG execution or actual actor ability is inferred.
 * A softlock witness reaches a state unable to reach a satisfying target,
 * without first passing a satisfying target. Return is a separate question.
 * Throws invalid_argument/length_error for invalid or over-limit inputs. */
[[nodiscard]] ProgressionReport analyzeProgressionRoutes(const Level& level, const ProgressionRequest& request);

} // namespace sed::level
