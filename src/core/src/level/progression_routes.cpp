#include "sed/level/progression_routes.hpp"
#include "portal_graph.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>

namespace sed::level
{
namespace
{
using namespace portal_detail;

struct Link
{
    std::size_t state{};
    PortalHop hop;
};

struct State
{
    std::int32_t sector{};
    AbilityMask abilities{};
    std::size_t parent{};
    std::optional<PortalHop> via;
    std::vector<Link> outgoing, incoming;
};

bool contains(AbilityMask held, AbilityMask needed)
{
    return (held & needed) == needed;
}

bool isTarget(const State& state, const ProgressionRequest& request)
{
    return state.sector == request.targetSector && contains(state.abilities, request.requiredAbilities);
}

ProgressionWitness witness(const std::vector<ProgressionStep>& steps)
{
    ProgressionWitness out;
    out.endState = steps.back();
    out.stepCount = steps.size();
    out.complete = steps.size() <= 128;
    out.steps.assign(steps.begin(), steps.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(steps.size(), 128)));
    return out;
}

std::vector<ProgressionStep> pathTo(const std::vector<State>& states, std::size_t target,
    const std::vector<std::optional<Link>>* parents = nullptr)
{
    std::vector<ProgressionStep> path;
    for ( auto id = target;; )
    {
        const auto& state = states[id];
        path.push_back({state.sector, state.abilities, id == 0 ? std::nullopt
            : parents ? std::optional<PortalHop>{(*parents)[id]->hop} : state.via});
        if ( id == 0 ) break;
        id = parents ? (*parents)[id]->state : state.parent;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

struct ReverseSearch
{
    std::vector<bool> reached;
    std::vector<std::optional<Link>> next;
};

template<class Predicate>
ReverseSearch reverseSearch(const std::vector<State>& states, Predicate seed)
{
    ReverseSearch out{std::vector<bool>(states.size()), std::vector<std::optional<Link>>(states.size())};
    std::vector<std::size_t> queue;
    for ( std::size_t i = 0; i < states.size(); ++i )
    {
        if ( seed(states[i]) )
        {
            out.reached[i] = true;
            queue.push_back(i);
        }
    }
    for ( std::size_t cursor = 0; cursor < queue.size(); ++cursor )
    {
        const auto id = queue[cursor];
        for ( const auto& predecessor : states[id].incoming )
        {
            if ( out.reached[predecessor.state] ) continue;
            out.reached[predecessor.state] = true;
            out.next[predecessor.state] = Link{id, predecessor.hop};
            queue.push_back(predecessor.state);
        }
    }
    return out;
}
} // namespace

ProgressionReport analyzeProgressionRoutes(const Level& level, const ProgressionRequest& request)
{
    if ( request.excludePortals.size() > kPortalRouteMaxExclusions )
    {
        throw std::length_error("progression admits at most 256 excluded endpoints");
    }
    PortalRouteRequest topology;
    topology.startSector = request.startSector;
    topology.targetSector = request.targetSector;
    topology.policy = request.policy;
    topology.excludePortals = request.excludePortals;
    validateRequest(level, topology);
    const auto returnSector = request.returnSector.value_or(request.startSector);
    if ( !inRange(returnSector, level.sectors.size()) || request.abilityCount > 12
        || request.maxStates < 1 || request.maxStates > 65536
        || request.maxWorkUnits < 1 || request.maxWorkUnits > 2'000'000 )
    {
        throw std::invalid_argument("invalid progression sector, ability count or search budget");
    }
    if ( request.grants.size() > 128 || request.gates.size() > 256 )
    {
        throw std::length_error("progression admits at most 128 grants and 256 gated pairs");
    }
    const auto permitted = static_cast<AbilityMask>((1u << request.abilityCount) - 1u);
    const auto validMask = [&](AbilityMask mask) { return (mask & ~permitted) == 0; };
    if ( !validMask(request.initialAbilities) || !validMask(request.requiredAbilities) )
    {
        throw std::invalid_argument("ability mask names undeclared abilities");
    }
    std::vector<std::vector<ProgressionGrant>> grants(level.sectors.size());
    for ( const auto& grant : request.grants )
    {
        if ( !inRange(grant.sector, level.sectors.size()) || !grant.provides
            || !validMask(grant.requiresAbilities) || !validMask(grant.provides) )
        {
            throw std::invalid_argument("invalid progression grant");
        }
        grants[grant.sector].push_back(grant);
    }
    std::map<SurfaceRef, AbilityMask, decltype(&before)> gates(&before);
    for ( const auto& gate : request.gates )
    {
        const auto face = gate.portal;
        if ( !inRange(face.sector, level.sectors.size())
            || !inRange(face.surface, level.sectors[face.sector].surfaces.size())
            || !gate.requiresAbilities || !validMask(gate.requiresAbilities) )
        {
            throw std::invalid_argument("invalid progression gate");
        }
        const auto peer = level.sectors[face.sector].surfaces[face.surface].adjoin;
        if ( !peer || !referenceProblem(level, face, *peer).empty()
            || !gates.emplace(before(face, *peer) ? face : *peer, gate.requiresAbilities).second )
        {
            throw std::invalid_argument("gates require distinct reciprocal portal pairs");
        }
    }
    PortalRouteReport diagnostics;
    const auto graph = buildGraph(level, topology, diagnostics);
    if ( diagnostics.invalidCount )
    {
        throw std::invalid_argument("progression requires valid reciprocal references; inspect portalRoutes diagnostics");
    }
    // Only grant-bearing sectors need a closure table. At most 128 total
    // grants, 4096 input masks and 13 passes bound this preprocessing; arrival
    // during the search is then constant-time even across parallel portals.
    std::vector<std::vector<AbilityMask>> closures(level.sectors.size());
    for ( std::size_t sector = 0; sector < grants.size(); ++sector )
    {
        if ( grants[sector].empty() ) continue;
        auto& closure = closures[sector];
        closure.resize(1u << request.abilityCount);
        for ( std::size_t mask = 0; mask < closure.size(); ++mask )
        {
            auto held = static_cast<AbilityMask>(mask);
            AbilityMask previous;
            do
            {
                previous = held;
                for ( const auto& grant : grants[sector] )
                {
                    if ( contains(held, grant.requiresAbilities) ) held |= grant.provides;
                }
            } while ( held != previous );
            closure[mask] = held;
        }
    }
    const auto acquire = [&](std::int32_t sector, AbilityMask held)
    {
        return closures[sector].empty() ? held : closures[sector][held];
    };
    const auto key = [](std::int32_t sector, AbilityMask held)
    {
        return (static_cast<std::uint64_t>(sector) << 12) | held;
    };
    ProgressionReport report;
    report.complete = true;
    std::vector<State> states;
    states.reserve(request.maxStates);
    states.push_back({request.startSector, acquire(request.startSector, request.initialAbilities), 0, {}, {}, {}});
    std::unordered_map<std::uint64_t, std::size_t> indices;
    indices.emplace(key(states[0].sector, states[0].abilities), 0);
    for ( std::size_t cursor = 0; cursor < states.size() && report.complete; ++cursor )
    {
        // Copy these before appending states; no reference survives vector growth.
        const auto sector = states[cursor].sector;
        const auto held = states[cursor].abilities;
        for ( const auto& arc : graph.outgoing[sector] )
        {
            if ( report.workUnits == request.maxWorkUnits )
            {
                report.complete = false;
                report.stoppedBy = "workBudget";
                break;
            }
            ++report.workUnits;
            const auto canonical = before(arc.hop.from, arc.hop.to) ? arc.hop.from : arc.hop.to;
            const auto gate = gates.find(canonical);
            if ( arc.closed || (gate != gates.end() && !contains(held, gate->second)) ) continue;
            const auto nextSector = arc.hop.to.sector;
            const auto nextHeld = acquire(nextSector, held);
            const auto nextKey = key(nextSector, nextHeld);
            auto found = indices.find(nextKey);
            if ( found == indices.end() )
            {
                if ( states.size() == request.maxStates )
                {
                    report.complete = false;
                    report.stoppedBy = "stateBudget";
                    break;
                }
                const auto id = states.size();
                states.push_back({nextSector, nextHeld, cursor, arc.hop, {}, {}});
                found = indices.emplace(nextKey, id).first;
            }
            const auto next = found->second;
            states[cursor].outgoing.push_back({next, arc.hop});
            states[next].incoming.push_back({cursor, arc.hop});
            ++report.transitionCount;
        }
    }
    report.stateCount = states.size();
    const auto returns = reverseSearch(states, [&](const State& state) { return state.sector == returnSector; });
    for ( std::size_t id = 0; id < states.size(); ++id )
    {
        if ( !isTarget(states[id], request) ) continue;
        report.targetReachable = true;
        if ( !report.targetPath ) report.targetPath = witness(pathTo(states, id));
        if ( !report.roundTripPath && returns.reached[id] )
        {
            report.returnPossible = true;
            auto path = pathTo(states, id);
            for ( auto next = returns.next[id]; next; next = returns.next[next->state] )
            {
                const auto& state = states[next->state];
                path.push_back({state.sector, state.abilities, next->hop});
            }
            report.roundTripPath = witness(path);
        }
    }
    if ( report.complete )
    {
        if ( !report.targetReachable ) report.targetReachable = false;
        if ( *report.targetReachable && !report.returnPossible ) report.returnPossible = false;
        const auto wins = reverseSearch(states, [&](const State& state) { return isTarget(state, request); });
        // A target is a terminal success for softlock classification. Do not
        // call a post-success excursion a softlock, or hide an alternate trap
        // route merely because the first BFS parent happened to pass the target.
        std::vector<std::optional<Link>> parents(states.size());
        std::vector<std::size_t> queue{0};
        parents[0] = Link{0, {}};
        report.possibleSoftlock = false;
        for ( std::size_t cursor = 0; cursor < queue.size(); ++cursor )
        {
            const auto id = queue[cursor];
            if ( !wins.reached[id] )
            {
                report.possibleSoftlock = true;
                report.softlockPath = witness(pathTo(states, id, &parents));
                break;
            }
            if ( isTarget(states[id], request) ) continue;
            for ( const auto& link : states[id].outgoing )
            {
                if ( !parents[link.state] )
                {
                    parents[link.state] = Link{id, link.hop};
                    queue.push_back(link.state);
                }
            }
        }
    }
    return report;
}

} // namespace sed::level
