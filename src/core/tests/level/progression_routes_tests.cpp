#include "sed/level/progression_routes.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
using namespace sed;
using namespace sed::level;

void check(bool condition, const char* message)
{
    if ( !condition ) throw std::runtime_error(message);
}

SurfaceRef pair(Level& level, int a, int b, int forward = 2, int backward = 2)
{
    const SurfaceRef from{a, static_cast<int>(level.sectors[a].surfaces.size())};
    level.sectors[a].surfaces.emplace_back().adjoinFlags = forward;
    const SurfaceRef to{b, static_cast<int>(level.sectors[b].surfaces.size())};
    level.sectors[b].surfaces.emplace_back().adjoinFlags = backward;
    level.sectors[a].surfaces[from.surface].adjoin = to;
    level.sectors[b].surfaces[to.surface].adjoin = from;
    return from;
}

void gatedRoute()
{
    Level level;
    level.sectors.resize(3);
    const auto door = pair(level, 0, 1);
    pair(level, 1, 2);
    ProgressionRequest request;
    request.startSector = 0;
    request.targetSector = 2;
    request.abilityCount = 1;
    request.gates = {{door, 1}};
    request.grants = {{1, 0, 1}};
    auto result = analyzeProgressionRoutes(level, request);
    check(result.complete && result.targetReachable == false && result.possibleSoftlock == true,
        "a key behind its own door cannot open that door");
    request.grants[0].sector = 0;
    result = analyzeProgressionRoutes(level, request);
    check(result.complete && result.targetReachable == true && result.returnPossible == true
        && result.possibleSoftlock == false && result.targetPath->steps.size() == 3
        && result.targetPath->steps.back().abilities == 1, "reachable grant opens the modeled route and return");
    request.excludePortals = {door};
    check(analyzeProgressionRoutes(level, request).targetReachable == false, "permanent exclusion overrides a granted gate");
}

void trapsAndOrdering()
{
    Level level;
    level.sectors.resize(4);
    pair(level, 0, 1);
    pair(level, 1, 2);
    pair(level, 0, 3, 2, 0);
    ProgressionRequest request;
    request.targetSector = 2;
    auto result = analyzeProgressionRoutes(level, request);
    check(result.targetReachable == true && result.returnPossible == true && result.possibleSoftlock == true
        && result.softlockPath->steps.back().sector == 3, "one-way dead-end has a reachable trap witness");

    Level branches;
    branches.sectors.resize(4);
    pair(branches, 0, 1, 2, 0);
    pair(branches, 0, 2, 2, 0);
    pair(branches, 1, 3, 2, 0);
    pair(branches, 2, 3, 2, 0);
    request.targetSector = 3;
    request.abilityCount = 2;
    request.requiredAbilities = 3;
    request.grants = {{1, 0, 1}, {2, 0, 2}};
    result = analyzeProgressionRoutes(branches, request);
    check(result.complete && result.targetReachable == false,
        "tokens from mutually exclusive branches cannot be combined into a fictitious winning route");
}

void actorProfilesAndReturn()
{
    Level level;
    level.sectors.resize(2);
    const auto door = pair(level, 0, 1, 2, 0);
    ProgressionRequest request;
    request.targetSector = 1;
    request.abilityCount = 3;
    request.initialAbilities = 1;
    request.grants = {{0, 2, 4}, {0, 1, 2}};
    request.gates = {{door, 4}};
    auto player = analyzeProgressionRoutes(level, request);
    check(player.targetReachable == true && player.returnPossible == false
        && player.targetPath->steps.front().abilities == 7, "grants reach a fixed point and return uses directed edges");
    request.initialAbilities = 0;
    check(analyzeProgressionRoutes(level, request).targetReachable == false,
        "actor without the declared switch ability cannot grant itself an open door");
    request.initialAbilities = 1;
    request.policy = PortalRoutePolicy::paired;
    check(analyzeProgressionRoutes(level, request).returnPossible == true, "paired policy is explicit in the model");
}

void boundedEvidence()
{
    Level level;
    level.sectors.resize(3);
    pair(level, 0, 1);
    pair(level, 0, 2);
    ProgressionRequest request;
    request.targetSector = 2;
    request.maxStates = 1;
    auto result = analyzeProgressionRoutes(level, request);
    check(!result.complete && !result.targetReachable && !result.possibleSoftlock && !result.returnPossible,
        "state exhaustion leaves unproved outcomes unknown, never false");
    request.maxStates = 65536;
    request.maxWorkUnits = 1;
    request.targetSector = 1;
    result = analyzeProgressionRoutes(level, request);
    check(!result.complete && result.targetReachable == true && !result.possibleSoftlock && !result.returnPossible,
        "a discovered positive path survives incomplete exploration, return remains unknown");
    request.targetSector = 0;
    result = analyzeProgressionRoutes(level, request);
    check(result.targetReachable == true && result.returnPossible == true
        && result.targetPath->steps.size() == 1, "start target has an explicit zero-hop state witness");
    level = {};
    level.sectors.resize(3);
    pair(level, 0, 1);
    pair(level, 1, 2, 2, 0);
    request = {};
    request.targetSector = 1;
    request.maxWorkUnits = 2;
    result = analyzeProgressionRoutes(level, request);
    check(!result.complete && result.targetReachable == true && result.returnPossible == true
        && !result.possibleSoftlock && result.roundTripPath->steps.size() == 3
        && result.roundTripPath->steps[1].sector == 1 && result.roundTripPath->steps[2].sector == 0
        && result.roundTripPath->steps[2].via->from.sector == 1
        && result.roundTripPath->steps[2].via->to.sector == 0,
        "a nontrivial discovered return survives budget exhaustion with correctly oriented hops");
}

void successIsTerminal()
{
    Level level;
    level.sectors.resize(4);
    pair(level, 0, 1, 2, 0);
    pair(level, 1, 3, 2, 0);
    ProgressionRequest request;
    request.targetSector = 1;
    check(analyzeProgressionRoutes(level, request).possibleSoftlock == false,
        "a dead-end visited only after satisfying the target is not a softlock");
    pair(level, 0, 2, 2, 0);
    pair(level, 2, 3, 2, 0);
    pair(level, 2, 1, 2, 0);
    const auto result = analyzeProgressionRoutes(level, request);
    check(result.possibleSoftlock == true && result.softlockPath->steps.size() == 3
        && result.softlockPath->steps[1].sector == 2 && result.softlockPath->steps[2].sector == 3,
        "an alternate pre-success trap retains a witness even when the first search parent passed the target");
}

void boundedWitness()
{
    Level level;
    level.sectors.resize(140);
    for ( int i = 0; i < 139; ++i ) pair(level, i, i + 1);
    ProgressionRequest request;
    request.targetSector = 139;
    const auto result = analyzeProgressionRoutes(level, request);
    check(result.complete && result.targetReachable == true && result.returnPossible == true
        && result.targetPath->stepCount == 140 && result.targetPath->steps.size() == 128
        && !result.targetPath->complete && result.targetPath->endState.sector == 139
        && result.roundTripPath->stepCount == 279 && result.roundTripPath->endState.sector == 0,
        "witness detail truncation never masquerades as complete inline evidence or incomplete graph search");
    level.sectors.resize(142);
    pair(level, 0, 141);
    pair(level, 139, 140, 2, 0);
    request.targetSector = 141;
    request.abilityCount = 1;
    request.grants = {{139, 0, 1}};
    const auto trap = analyzeProgressionRoutes(level, request);
    check(trap.possibleSoftlock == true && !trap.softlockPath->complete
        && trap.softlockPath->stepCount == 141 && trap.softlockPath->endState.sector == 140
        && trap.softlockPath->endState.abilities == 1 && trap.softlockPath->endState.via->from.sector == 139,
        "a truncated trap witness still identifies its terminal sector, abilities and incoming hop");
}

void rejectedContracts()
{
    Level level;
    level.sectors.resize(2);
    const auto gate = pair(level, 0, 1);
    ProgressionRequest good;
    good.targetSector = 1;
    good.abilityCount = 1;
    const auto rejects = [&](auto alter)
    {
        auto l = level;
        auto request = good;
        alter(l, request);
        try { (void)analyzeProgressionRoutes(l, request); }
        catch (const std::invalid_argument&) { return; }
        throw std::runtime_error("invalid progression contract must refuse");
    };
    rejects([](auto&, auto& r) { r.initialAbilities = 2; });
    rejects([](auto&, auto& r) { r.maxStates = 0; });
    rejects([](auto&, auto& r) { r.policy = static_cast<PortalRoutePolicy>(99); });
    rejects([](auto&, auto& r) { r.grants = {{99, 0, 1}}; });
    rejects([&](auto&, auto& r) { r.gates = {{gate, 1}, {{1, 0}, 1}}; });
    rejects([](auto& l, auto&) { l.sectors[1].surfaces[0].adjoin.reset(); });
    good.excludePortals.resize(257);
    try
    {
        (void)analyzeProgressionRoutes(level, good);
        throw std::runtime_error("oversized native exclusion lists must refuse");
    }
    catch (const std::length_error&) {}
}
}

int main()
{
    try
    {
        gatedRoute();
        trapsAndOrdering();
        actorProfilesAndReturn();
        boundedEvidence();
        successIsTerminal();
        boundedWitness();
        rejectedContracts();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
