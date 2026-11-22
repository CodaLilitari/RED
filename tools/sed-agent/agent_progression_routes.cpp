#include "agent_query_details.hpp"

#include "agent_workspace.hpp"
#include "sed/level/progression_routes.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>

namespace sed_agent
{
namespace
{
using namespace sed::level;

[[noreturn]] void invalid(const std::string& message)
{
    throw AgentError{ kExitUsage, message, "progressionRouteInvalid" };
}

void fields(const Json& object, std::initializer_list<std::string_view> allowed, const char* context)
{
    if ( !object.isObject() )
    {
        invalid(std::string(context) + " must be an object");
    }
    for ( const auto& [key, value] : object.object )
    {
        if ( std::find(allowed.begin(), allowed.end(), key) == allowed.end() )
        {
            invalid(std::string(context) + " contains an unknown field");
        }
    }
}

std::int32_t integer(const Json& object, const char* field, std::int32_t lower = 0,
    std::int32_t upper = std::numeric_limits<std::int32_t>::max())
{
    const auto* value = object.find(field);
    if ( !value || value->type != Json::Type::Number || !std::isfinite(value->number)
        || value->number < lower || value->number > upper || std::floor(value->number) != value->number )
    {
        invalid(std::string(field) + " must be an integer in the supported range");
    }
    return static_cast<std::int32_t>(value->number);
}

std::span<const Json> entries(const Json& object, const char* field, std::size_t maximum, bool required = false)
{
    const auto* value = object.find(field);
    if ( !value && !required )
    {
        return {};
    }
    if ( !value || value->type != Json::Type::Array )
    {
        invalid(std::string(field) + " must be an array");
    }
    if ( value->array.size() > maximum )
    {
        throw AgentError{ kExitRefusal, std::string(field) + " exceeds its entry limit", "progressionRouteTooLarge" };
    }
    return value->array;
}

sed::SurfaceRef endpoint(const Json& value)
{
    fields(value, {"sector", "surface"}, "portal endpoint");
    return {integer(value, "sector"), integer(value, "surface")};
}

bool blank(std::string_view name)
{
    // Input has already passed UTF-8 validation. Whole code points in the Unicode
    // White_Space set can be compared directly without normalization or a locale.
    constexpr std::u8string_view whitespace = u8" \t\r\n\v\f\u0085\u00a0\u1680\u2000\u2001\u2002\u2003\u2004"
        u8"\u2005\u2006\u2007\u2008\u2009\u200a\u2028\u2029\u202f\u205f\u3000";
    const std::string_view bytes(reinterpret_cast<const char*>(whitespace.data()), whitespace.size());
    while ( !name.empty() )
    {
        const auto lead = static_cast<unsigned char>(name.front());
        const auto width = lead < 0x80 ? 1u : lead < 0xe0 ? 2u : lead < 0xf0 ? 3u : 4u;
        if ( bytes.find(name.substr(0, width)) == std::string_view::npos )
        {
            return false;
        }
        name.remove_prefix(width);
    }
    return true;
}

std::vector<std::string> abilityNames(const Json& object)
{
    std::vector<std::string> names;
    for ( const auto& entry : entries(object, "abilities", 12, true) )
    {
        if ( entry.type != Json::Type::String || entry.str.size() > 64 || !isValidJsonUtf8(entry.str)
            || blank(entry.str) || std::find(names.begin(), names.end(), entry.str) != names.end() )
        {
            invalid("abilities must contain distinct, nonblank UTF-8 names of at most 64 bytes");
        }
        names.push_back(entry.str);
    }
    return names;
}

AbilityMask abilityMask(const Json& object, const char* field, const std::vector<std::string>& names, bool nonempty = false)
{
    AbilityMask result{};
    for ( const auto& entry : entries(object, field, 12, nonempty) )
    {
        const auto it = std::find(names.begin(), names.end(), entry.str);
        if ( entry.type != Json::Type::String || it == names.end() )
        {
            invalid(std::string(field) + " references an undeclared ability");
        }
        const auto bit = static_cast<AbilityMask>(1u << (it - names.begin()));
        if ( result & bit )
        {
            invalid(std::string(field) + " repeats an ability");
        }
        result |= bit;
    }
    if ( nonempty && !result )
    {
        invalid(std::string(field) + " must name at least one ability");
    }
    return result;
}

ProgressionRequest parseRequest(const Json& object, const std::vector<std::string>& names)
{
    fields(object, {"collection", "schemaVersion", "sourceRevision", "inputSha256", "startSector", "targetSector",
        "returnSector", "policy", "excludePortals", "abilities", "initialAbilities", "requiredAbilities",
        "grants", "gates", "maxStates", "maxWorkUnits"}, "progressionRoutes");
    if ( object.find("schemaVersion") )
    {
        (void)integer(object, "schemaVersion", 1, 1);
    }
    ProgressionRequest result;
    result.startSector = integer(object, "startSector");
    result.targetSector = integer(object, "targetSector");
    if ( object.find("returnSector") ) result.returnSector = integer(object, "returnSector");
    if ( object.find("maxStates") ) result.maxStates = integer(object, "maxStates", 1, 65536);
    if ( object.find("maxWorkUnits") ) result.maxWorkUnits = integer(object, "maxWorkUnits", 1, 2'000'000);
    if ( const auto* policy = object.find("policy") )
    {
        if ( policy->type != Json::Type::String || (policy->str != "movementBit" && policy->str != "paired") )
        {
            invalid("policy must be movementBit or paired");
        }
        result.policy = policy->str == "paired" ? PortalRoutePolicy::paired : PortalRoutePolicy::movementBit;
    }
    result.abilityCount = static_cast<std::uint8_t>(names.size());
    result.initialAbilities = abilityMask(object, "initialAbilities", names);
    result.requiredAbilities = abilityMask(object, "requiredAbilities", names);
    for ( const auto& face : entries(object, "excludePortals", kPortalRouteMaxExclusions) )
    {
        result.excludePortals.push_back(endpoint(face));
    }
    for ( const auto& grant : entries(object, "grants", 128) )
    {
        fields(grant, {"sector", "requires", "provides"}, "grant");
        result.grants.push_back({integer(grant, "sector"), abilityMask(grant, "requires", names),
            abilityMask(grant, "provides", names, true)});
    }
    for ( const auto& gate : entries(object, "gates", 256) )
    {
        fields(gate, {"portal", "requires"}, "gate");
        const auto* face = gate.find("portal");
        if ( !face ) invalid("gate requires a portal endpoint");
        result.gates.push_back({endpoint(*face), abilityMask(gate, "requires", names, true)});
    }
    return result;
}

Json address(sed::SurfaceRef face)
{
    Json out = Json::makeObject();
    out.set("sector", Json::makeNumber(face.sector));
    out.set("surface", Json::makeNumber(face.surface));
    return out;
}

Json describeStep(const ProgressionStep& step, const std::vector<std::string>& names)
{
    Json row = Json::makeObject();
    row.set("sector", Json::makeNumber(step.sector));
    Json held = Json::makeArray();
    for ( std::size_t i = 0; i < names.size(); ++i )
    {
        if ( step.abilities & (1u << i) ) held.array.push_back(Json::makeString(names[i]));
    }
    row.set("abilities", std::move(held));
    Json via;
    if ( step.via )
    {
        via.set("from", address(step.via->from));
        via.set("to", address(step.via->to));
        via.set("fromFlags", Json::makeNumber(step.via->fromFlags));
        via.set("toFlags", Json::makeNumber(step.via->toFlags));
    }
    row.set("via", std::move(via));
    return row;
}

Json witness(const std::optional<ProgressionWitness>& path, const std::vector<std::string>& names)
{
    if ( !path ) return {};
    Json steps = Json::makeArray();
    for ( const auto& step : path->steps )
    {
        steps.array.push_back(describeStep(step, names));
    }
    Json out = Json::makeObject();
    out.set("complete", Json::makeBool(path->complete));
    out.set("stepCount", Json::makeNumber(static_cast<double>(path->stepCount)));
    out.set("steps", std::move(steps));
    out.set("endState", describeStep(path->endState, names));
    return out;
}

Json verdict(std::optional<bool> result)
{
    return result ? Json::makeBool(*result) : Json{};
}
} // namespace

Json queryProgressionRoutes(const sed::Level& level, const Json& object)
{
    const auto names = abilityNames(object);
    const auto request = parseRequest(object, names);
    ProgressionReport report;
    try
    {
        report = analyzeProgressionRoutes(level, request);
    }
    catch ( const std::length_error& error )
    {
        throw AgentError{ kExitRefusal, error.what(), "progressionRouteTooLarge" };
    }
    catch ( const std::invalid_argument& error )
    {
        invalid(error.what());
    }

    Json out = Json::makeObject();
    out.set("command", Json::makeString("query"));
    out.set("ok", Json::makeBool(true));
    out.set("collection", Json::makeString("progressionRoutes"));
    out.set("schemaVersion", Json::makeNumber(1));
    out.set("modelScope", Json::makeString("declaredMonotonePortalModel"));
    out.set("requestSha256", Json::makeString(sha256Hex(toJsonString(object))));
    out.set("geometryVerified", Json::makeBool(false));
    out.set("scriptsEvaluated", Json::makeBool(false));
    out.set("runtimeVerified", Json::makeBool(false));
    out.set("unmodeledPlacedCogs", Json::makeNumber(static_cast<double>(level.cogs.size())));
    out.set("policy", Json::makeString(request.policy == PortalRoutePolicy::paired ? "paired" : "movementBit"));
    out.set("startSector", Json::makeNumber(request.startSector));
    out.set("targetSector", Json::makeNumber(request.targetSector));
    out.set("returnSector", Json::makeNumber(request.returnSector.value_or(request.startSector)));
    out.set("complete", Json::makeBool(report.complete));
    out.set("stoppedBy", report.stoppedBy.empty() ? Json{} : Json::makeString(report.stoppedBy));
    out.set("stateCount", Json::makeNumber(static_cast<double>(report.stateCount)));
    out.set("transitionCount", Json::makeNumber(static_cast<double>(report.transitionCount)));
    out.set("workUnits", Json::makeNumber(static_cast<double>(report.workUnits)));
    out.set("targetReachable", verdict(report.targetReachable));
    out.set("returnPossible", verdict(report.returnPossible));
    out.set("possibleSoftlock", verdict(report.possibleSoftlock));
    out.set("targetPath", witness(report.targetPath, names));
    out.set("roundTripPath", witness(report.roundTripPath, names));
    out.set("softlockPath", witness(report.softlockPath, names));
    return out;
}
} // namespace sed_agent
