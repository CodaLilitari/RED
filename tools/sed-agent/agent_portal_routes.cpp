#include "agent_query_details.hpp"

#include "agent_workspace.hpp"
#include "sed/level/portal_routes.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace sed_agent
{
namespace
{
using namespace sed::level;

[[noreturn]] void invalid(const std::string& detail)
{
    throw AgentError{ kExitUsage, detail, "portalRouteInvalid" };
}

std::int32_t index(const Json& object, const char* key)
{
    const auto* v = object.find(key);
    if ( !v || v->type != Json::Type::Number || !std::isfinite(v->number) || v->number < 0
        || v->number > std::numeric_limits<std::int32_t>::max() || std::floor(v->number) != v->number )
    {
        invalid(std::string(key) + " must be a nonnegative 32-bit integer");
    }
    return static_cast<std::int32_t>(v->number);
}

PortalRouteRequest parse(const Json& object)
{
    for ( const auto& [key, value] : object.object )
    {
        if ( key != "collection" && key != "startSector" && key != "targetSector" && key != "policy"
            && key != "excludePortals" && key != "sourceRevision" && key != "inputSha256" && key != "schemaVersion" )
        {
            invalid("unknown portalRoutes field: " + key);
        }
    }
    if ( const auto* v = object.find("schemaVersion"); v && (v->type != Json::Type::Number || v->number != 1) )
    {
        invalid("portalRoutes supports schemaVersion 1");
    }
    PortalRouteRequest request;
    request.startSector = index(object, "startSector");
    if ( object.find("targetSector") )
    {
        request.targetSector = index(object, "targetSector");
    }
    if ( const auto* policy = object.find("policy") )
    {
        if ( policy->type != Json::Type::String || (policy->str != "movementBit" && policy->str != "paired") )
        {
            invalid("policy must be movementBit or paired");
        }
        request.policy = policy->str == "paired" ? PortalRoutePolicy::paired : PortalRoutePolicy::movementBit;
    }
    if ( const auto* excluded = object.find("excludePortals") )
    {
        if ( excluded->type != Json::Type::Array )
        {
            invalid("excludePortals must be an array of sector/surface addresses");
        }
        if ( excluded->array.size() > kPortalRouteMaxExclusions )
        {
            throw AgentError{ kExitRefusal, "excludePortals exceeds 256 input endpoints", "portalRouteTooLarge" };
        }
        for ( const auto& face : excluded->array )
        {
            if ( !face.isObject() || face.object.size() != 2 )
            {
                invalid("each excluded portal requires exactly sector and surface");
            }
            request.excludePortals.push_back({ index(face, "sector"), index(face, "surface") });
        }
    }
    return request;
}

Json endpoint(sed::SurfaceRef face)
{
    Json out = Json::makeObject();
    out.set("sector", Json::makeNumber(face.sector));
    out.set("surface", Json::makeNumber(face.surface));
    return out;
}

Json hops(const std::vector<PortalHop>& portals)
{
    Json rows = Json::makeArray();
    for ( const auto& hop : portals )
    {
        Json row = Json::makeObject();
        row.set("from", endpoint(hop.from));
        row.set("to", endpoint(hop.to));
        row.set("fromFlags", Json::makeNumber(hop.fromFlags));
        row.set("toFlags", Json::makeNumber(hop.toFlags));
        rows.array.push_back(std::move(row));
    }
    return rows;
}

Json sectors(const std::vector<std::int32_t>& indices)
{
    Json out = Json::makeArray();
    for ( const auto i : indices )
    {
        out.array.push_back(Json::makeNumber(i));
    }
    return out;
}

Json state(const PortalRouteState& state)
{
    Json out = Json::makeObject();
    out.set("reachableSectors", sectors(state.reachable));
    out.set("targetPath", state.targetPath ? hops(*state.targetPath) : Json{});
    return out;
}

Json probes(const PortalRouteProbes& review)
{
    Json out = Json::makeObject();
    out.set("checked", Json::makeNumber(static_cast<double>(review.checked)));
    out.set("complete", Json::makeBool(review.complete));
    out.set("unavoidable", hops(review.unavoidable));
    out.set("alternative", review.alternative ? hops(*review.alternative) : Json{});
    return out;
}

} // namespace

Json queryPortalRoutes(const sed::Level& level, const Json& object)
{
    const auto request = parse(object);
    PortalRouteReport report;
    try
    {
        report = analyzePortalRoutes(level, request);
    }
    catch ( const std::length_error& error )
    {
        throw AgentError{ kExitRefusal, error.what(), "portalRouteTooLarge" };
    }
    catch ( const std::invalid_argument& error )
    {
        invalid(error.what());
    }
    Json out = Json::makeObject();
    out.set("command", Json::makeString("query"));
    out.set("ok", Json::makeBool(true));
    out.set("collection", Json::makeString("portalRoutes"));
    out.set("policy", Json::makeString(request.policy == PortalRoutePolicy::paired ? "paired" : "movementBit"));
    out.set("policyRule", Json::makeString(request.policy == PortalRoutePolicy::paired
        ? "Follow both directions of a reciprocal pair."
        : "Follow each reciprocal face whose departing adjoin mask has bit 0x2 set."));
    out.set("scope", Json::makeString("Routes follow reciprocal references retained by the level loader. Geometry, actor clearance, "
        "jumps, dynamic doors, scripts and teleports are outside this graph."));
    out.set("startSector", Json::makeNumber(request.startSector));
    out.set("targetSector", request.targetSector ? Json::makeNumber(*request.targetSector) : Json{});
    out.set("baseline", state(report.baseline));
    out.set("after", state(report.after));
    out.set("lostSectors", sectors(report.lost));
    out.set("excludedPortals", hops(report.excluded));
    out.set("targetProbes", report.probes ? probes(*report.probes) : Json{});
    out.set("pairCount", Json::makeNumber(static_cast<double>(report.pairCount)));
    out.set("asymmetricCount", Json::makeNumber(static_cast<double>(report.asymmetricCount)));
    out.set("movementMismatchCount", Json::makeNumber(static_cast<double>(report.movementMismatchCount)));
    out.set("asymmetricPairs", hops(report.asymmetric));
    out.set("invalidCount", Json::makeNumber(static_cast<double>(report.invalidCount)));
    Json issues = Json::makeArray();
    for ( const auto& issue : report.invalid )
    {
        Json row = Json::makeObject();
        row.set("from", endpoint(issue.from));
        row.set("to", endpoint(issue.to));
        row.set("peerPointsTo", issue.peerPointsTo ? endpoint(*issue.peerPointsTo) : Json{});
        row.set("reason", Json::makeString(std::string(issue.reason)));
        issues.array.push_back(std::move(row));
    }
    out.set("invalidReferences", std::move(issues));
    return out;
}

} // namespace sed_agent
