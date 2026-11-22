#include "sed/level/portal_routes.hpp"
#include "portal_graph.hpp"

#include <algorithm>
#include <iterator>
#include <set>
#include <stdexcept>
#include <tuple>

namespace sed::level
{
namespace
{
using namespace portal_detail;

struct Search
{
    std::vector<int> queue;
    std::vector<const PortalHop*> incoming;
};

Search traverse(const Graph& graph, int start, bool applyClosures,
    std::optional<SurfaceRef> probe = {}, std::optional<int> stop = {})
{
    Search search;
    search.incoming.resize(graph.outgoing.size());
    search.queue.push_back(start);
    for ( std::size_t head = 0; head < search.queue.size(); ++head )
    {
        const int sector = search.queue[head];
        if ( stop == sector )
        {
            break;
        }
        for ( const auto& arc : graph.outgoing[sector] )
        {
            const auto& hop = arc.hop;
            const int next = hop.to.sector;
            if ( next == start || search.incoming[next] || (applyClosures && arc.closed)
                || probe == hop.from || probe == hop.to )
            {
                continue;
            }
            search.incoming[next] = &hop;
            search.queue.push_back(next);
        }
    }
    return search;
}

std::optional<std::vector<PortalHop>> path(const Search& search, int start, std::optional<int> target)
{
    if ( !target || (*target != start && !search.incoming[*target]) )
    {
        return std::nullopt;
    }
    std::vector<PortalHop> hops;
    for ( int sector = *target; sector != start; )
    {
        const auto& hop = *search.incoming[sector];
        hops.push_back(hop);
        sector = hop.from.sector;
    }
    std::reverse(hops.begin(), hops.end());
    return hops;
}

PortalRouteState state(const Graph& graph, const PortalRouteRequest& request, bool applyClosures)
{
    auto search = traverse(graph, request.startSector, applyClosures);
    std::sort(search.queue.begin(), search.queue.end());
    return { std::move(search.queue), path(search, request.startSector, request.targetSector) };
}

} // namespace

PortalRouteReport analyzePortalRoutes(const Level& level, const PortalRouteRequest& request)
{
    validateRequest(level, request);

    PortalRouteReport report;
    const auto graph = buildGraph(level, request, report);
    report.baseline = state(graph, request, false);
    report.after = report.excluded.empty() ? report.baseline : state(graph, request, true);
    std::set_difference(report.baseline.reachable.begin(), report.baseline.reachable.end(),
        report.after.reachable.begin(), report.after.reachable.end(), std::back_inserter(report.lost));
    if ( report.after.targetPath )
    {
        auto& probes = report.probes.emplace();
        const auto& witness = *report.after.targetPath;
        // Every mandatory physical portal is on every witness. Removing each witness pair
        // in turn is sufficient; never substitute a capped path enumeration for this test.
        const auto maxProbes = kProbeBudget / (level.sectors.size() + graph.arcCount);
        for ( const auto& hop : witness )
        {
            if ( probes.checked == maxProbes )
            {
                break;
            }
            ++probes.checked;
            const auto search = traverse(graph, request.startSector, true, hop.from, request.targetSector);
            if ( !search.incoming[*request.targetSector] )
            {
                probes.unavoidable.push_back(hop);
            }
            else if ( !probes.alternative )
            {
                probes.alternative = path(search, request.startSector, request.targetSector);
            }
        }
        probes.complete = probes.checked == witness.size();
    }
    return report;
}

} // namespace sed::level
