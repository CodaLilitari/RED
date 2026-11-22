#pragma once

#include "agent_json.hpp"

#include "sed/level/shadow_cuts.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace sed_agent
{

struct ReceiverOutputCheck
{
    struct Face
    {
        sed::SurfaceRef ref;
        std::int32_t ownerId{};
        std::size_t ownersWithId{};
        std::vector<std::array<double, 3>> corners;
    };
    std::size_t reportIndex{};
    double maxEdge{};
    std::vector<Face> faces;
};

struct ReceiverPatchState
{
    sed::level::ReceiverWorkBudget budget;
    std::vector<ReceiverOutputCheck> checks;
};

void verifyReceiverOutput(const ReceiverPatchState& state, const sed::Level& edited,
    const sed::Level& persisted, Json& editReports);

// Operations that give the agent the RED app's remaining Edit, Sector,
// Surface and Transform commands over the shared core: deleteThings,
// deleteLights, deleteCogs, setSectorProperties, setSurfaceProperties,
// unadjoinSurfaces, makeUnderwater, placeSectorLights,
// removeRedundantVertices, flipSurfaces, mirrorSectors, duplicateSector,
// fitAndAdjoin, stitchSurfaces, mergeSurfaces, mergeSectors,
// subdivideSurfaces, insetSurface, planarizeSurfaces, repairSurfaces,
// rotateThings, rotateLights, mirrorThings and mirrorLights.
// Returns false for another operation family. On success `report` holds the
// operation's receipt entry; throws AgentError for an invalid or refused
// request, with the caller's candidate Level to be discarded.
bool tryApplyParityOperation(sed::Level& level, const Json& operation, Json& report,
    ReceiverPatchState& receivers, std::size_t reportIndex);

} // namespace sed_agent
