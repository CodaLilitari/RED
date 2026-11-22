#pragma once

#include "sed/level/portal_routes.hpp"

#include <set>
#include <stdexcept>
#include <tuple>

namespace sed::level::portal_detail
{

inline constexpr std::size_t kMaxSectors = 4096;
inline constexpr std::size_t kMaxSurfaces = 262144;
inline constexpr std::size_t kDetailLimit = 128;
inline constexpr std::size_t kProbeBudget = 2'000'000;

inline bool before(SurfaceRef a, SurfaceRef b)
{
    return std::tie(a.sector, a.surface) < std::tie(b.sector, b.surface);
}

inline bool inRange(std::int32_t index, std::size_t size)
{
    return index >= 0 && static_cast<std::size_t>(index) < size;
}

inline std::string_view referenceProblem(const Level& level, SurfaceRef from, SurfaceRef to)
{
    if ( !inRange(to.sector, level.sectors.size()) )
    {
        return "sectorOutOfRange";
    }
    const auto& faces = level.sectors[to.sector].surfaces;
    if ( !inRange(to.surface, faces.size()) )
    {
        return "surfaceOutOfRange";
    }
    if ( from == to )
    {
        return "selfReference";
    }
    if ( !faces[to.surface].adjoin )
    {
        return "missingMirror";
    }
    if ( *faces[to.surface].adjoin != from )
    {
        return "wrongMirror";
    }
    return {};
}

struct Arc
{
    PortalHop hop;
    bool closed{};
};

struct Graph
{
    std::vector<std::vector<Arc>> outgoing;
    std::size_t arcCount{};
};

inline Graph buildGraph(const Level& level, const PortalRouteRequest& request, PortalRouteReport& report)
{
    std::set<SurfaceRef, decltype(&before)> exclusions(&before);
    for ( const auto face : request.excludePortals )
    {
        if ( !inRange(face.sector, level.sectors.size())
            || !inRange(face.surface, level.sectors[face.sector].surfaces.size()) )
        {
            throw std::invalid_argument("excluded portal endpoint is outside the level");
        }
        const auto& peer = level.sectors[face.sector].surfaces[face.surface].adjoin;
        if ( !peer || !referenceProblem(level, face, *peer).empty() )
        {
            throw std::invalid_argument("excluded portal endpoint has no valid reciprocal pair");
        }
        exclusions.insert(before(face, *peer) ? face : *peer);
    }

    Graph graph;
    graph.outgoing.resize(level.sectors.size());
    for ( std::size_t s = 0; s < level.sectors.size(); ++s )
    {
        const auto& faces = level.sectors[s].surfaces;
        for ( std::size_t f = 0; f < faces.size(); ++f )
        {
            const auto& face = faces[f];
            if ( !face.adjoin )
            {
                continue;
            }
            const SurfaceRef from{ static_cast<std::int32_t>(s), static_cast<std::int32_t>(f) };
            const SurfaceRef to = *face.adjoin;
            const auto problem = referenceProblem(level, from, to);
            if ( !problem.empty() )
            {
                ++report.invalidCount;
                if ( report.invalid.size() < kDetailLimit )
                {
                    std::optional<SurfaceRef> reverse;
                    if ( inRange(to.sector, level.sectors.size())
                        && inRange(to.surface, level.sectors[to.sector].surfaces.size()) )
                    {
                        reverse = level.sectors[to.sector].surfaces[to.surface].adjoin;
                    }
                    report.invalid.push_back({ from, to, reverse, problem });
                }
                continue;
            }
            const PortalHop hop{ from, to, static_cast<std::uint32_t>(face.adjoinFlags),
                static_cast<std::uint32_t>(level.sectors[to.sector].surfaces[to.surface].adjoinFlags) };
            const bool canonical = before(from, to);
            const bool closed = exclusions.contains(canonical ? from : to);
            if ( canonical )
            {
                ++report.pairCount;
                if ( closed )
                {
                    report.excluded.push_back(hop);
                }
                if ( hop.fromFlags != hop.toFlags )
                {
                    ++report.asymmetricCount;
                    if ( report.asymmetric.size() < kDetailLimit )
                    {
                        report.asymmetric.push_back(hop);
                    }
                    if ( (hop.fromFlags ^ hop.toFlags) & SAF_Move )
                    {
                        ++report.movementMismatchCount;
                    }
                }
            }
            if ( request.policy == PortalRoutePolicy::paired || (hop.fromFlags & SAF_Move) )
            {
                graph.outgoing[s].push_back({ hop, closed });
                ++graph.arcCount;
            }
        }
    }
    return graph;
}

inline void validateRequest(const Level& level, const PortalRouteRequest& request)
{
    if ( request.policy != PortalRoutePolicy::movementBit && request.policy != PortalRoutePolicy::paired )
    {
        throw std::invalid_argument("unsupported portal route policy");
    }
    if ( level.sectors.size() > kMaxSectors || request.excludePortals.size() > kPortalRouteMaxExclusions )
    {
        throw std::length_error("portal route analysis admits at most 4096 sectors and 256 excluded endpoints");
    }
    if ( !inRange(request.startSector, level.sectors.size())
        || (request.targetSector && !inRange(*request.targetSector, level.sectors.size())) )
    {
        throw std::invalid_argument("startSector and targetSector must address sectors in the loaded level");
    }
    std::size_t surfaces = 0;
    for ( const auto& sector : level.sectors )
    {
        if ( sector.surfaces.size() > kMaxSurfaces - surfaces )
        {
            throw std::length_error("portal route analysis admits at most 262144 surfaces");
        }
        surfaces += sector.surfaces.size();
    }

}

} // namespace sed::level::portal_detail
