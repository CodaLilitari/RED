#include "agent_map_requirements.hpp"
#include "agent_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <set>

namespace sed_agent
{
namespace
{
using Collection = sed::level::WorkbenchCollection;

[[noreturn]] void invalid(std::string message)
{
    throw AgentError{ kExitUsage, std::move(message), "invalidMapRequirements" };
}

void checkFields(const Json& object, std::string_view context, std::initializer_list<std::string_view> allowed)
{
    if ( !object.isObject() )
    {
        invalid(std::string(context) + " must be an object");
    }
    for ( const auto& [key, value] : object.object )
    {
        if ( std::find(allowed.begin(), allowed.end(), key) == allowed.end() )
        {
            invalid("Unknown " + std::string(context) + " field: "
                + (key.size() <= 128 && isValidJsonUtf8(key) ? key : "<unrecognized key>"));
        }
        if ( value.type == Json::Type::String && !isValidJsonUtf8(value.str) )
        {
            invalid(std::string(context) + " text must be valid UTF-8");
        }
    }
}

const Json& requiredField(const Json& object, const char* key)
{
    const Json* value = object.find(key);
    if ( !value )
    {
        invalid(std::string("Missing required field '") + key + "'");
    }
    return *value;
}

std::size_t unsignedValue(const Json& value, std::size_t maximum, const char* field)
{
    if ( value.type != Json::Type::Number || !std::isfinite(value.number)
        || value.number != std::floor(value.number) || value.number < 0 || value.number > static_cast<double>(maximum) )
    {
        invalid(std::string(field) + " must be a nonnegative integer within its documented limit");
    }
    return static_cast<std::size_t>(value.number);
}

struct CountRange
{
    std::size_t min{};
    std::size_t max{ std::numeric_limits<std::size_t>::max() };
};

CountRange countRange(const Json& expect)
{
    checkFields(expect, "expect", { "exactly", "min", "max" });
    constexpr auto maxCount = std::numeric_limits<std::int32_t>::max();
    if ( const auto* exact = expect.find("exactly") )
    {
        if ( expect.object.size() != 1 )
        {
            invalid("exactly cannot be combined with min or max");
        }
        const auto count = unsignedValue(*exact, maxCount, "exactly");
        return { count, count };
    }
    CountRange range;
    if ( const auto* min = expect.find("min") )
    {
        range.min = unsignedValue(*min, maxCount, "min");
    }
    if ( const auto* max = expect.find("max") )
    {
        range.max = unsignedValue(*max, maxCount, "max");
    }
    if ( expect.object.empty() || range.min > range.max )
    {
        invalid("expect requires exactly or an ordered inclusive min/max range");
    }
    return range;
}

ParsedWorkbenchQuery countQuery(const Json& object)
{
    checkFields(object, "query", { "collection", "sector", "search", "filter" });
    if ( const auto* filter = object.find("filter") )
    {
        checkFields(*filter, "filter", { "layer", "materialIndex", "name", "templateName",
            "flags", "surfFlags", "faceFlags", "adjoinFlags" });
        for ( const auto* key : { "flags", "surfFlags", "faceFlags", "adjoinFlags" } )
        {
            if ( const auto* mask = filter->find(key) )
            {
                checkFields(*mask, key, { "any", "all", "none" });
            }
        }
    }
    auto query = parseWorkbenchQuery(object);
    if ( query.collection == Collection::thingContainment )
    {
        invalid("Count requirements accept things, sectors, lights or surfaces");
    }
    if ( query.collection != Collection::things && object.find("search") )
    {
        invalid("search applies only to things");
    }
    return query;
}

void chargeQuery(const sed::Level& level, const ParsedWorkbenchQuery& query, std::size_t& remaining)
{
    std::size_t rows{};
    std::size_t perRow = 1;
    switch ( query.collection )
    {
    case Collection::things:
    case Collection::thingContainment:
        rows = level.things.size();
        break;
    case Collection::sectors:
        rows = level.sectors.size();
        break;
    case Collection::lights:
        rows = level.lights.size();
        break;
    case Collection::surfaces:
        // An absent or out-of-range sector is refused by queryWorkbench before it scans.
        if ( query.sector && *query.sector >= 0 && static_cast<std::size_t>(*query.sector) < level.sectors.size() )
        {
            rows = level.sectors[static_cast<std::size_t>(*query.sector)].surfaces.size();
        }
        if ( query.filter && query.filter->materialIndex )
        {
            perRow += level.materials.size();
        }
        break;
    }
    if ( rows > remaining / perRow )
    {
        throw AgentError{ kExitRefusal, "Count query exceeds the remaining map requirement work budget",
            "mapRequirementWorkLimit" };
    }
    remaining -= rows * perRow;
}

Json evaluateCheck(const sed::Level& level, const Json& check, std::set<std::string>& names, std::size_t& remaining)
{
    checkFields(check, "check", { "name", "query", "expect" });
    const auto& name = requiredField(check, "name");
    if ( name.type != Json::Type::String || name.str.size() > kMaxRequirementNameBytes
        || sed::text::trimView(name.str).empty() )
    {
        invalid("Check names must be nonblank UTF-8 text, at most 128 bytes each");
    }
    if ( !names.insert(name.str).second )
    {
        invalid("Duplicate check name: " + name.str);
    }
    const auto& expect = requiredField(check, "expect");
    const auto range = countRange(expect);
    const auto query = countQuery(requiredField(check, "query"));
    chargeQuery(level, query, remaining);
    const auto result = sed::level::queryWorkbench(level, query.collection, query.sector, query.filter, query.search, { 0, 1 });
    if ( result.failure )
    {
        throw AgentError{ kExitRefusal, result.failure->message, result.failure->code };
    }
    Json row = Json::makeObject();
    row.set("name", name);
    row.set("expect", expect);
    row.set("actual", Json::makeNumber(static_cast<double>(result.totalMatches)));
    row.set("passed", Json::makeBool(result.totalMatches >= range.min && result.totalMatches <= range.max));
    return row;
}
} // namespace

Json evaluateMapRequirements(const sed::Level& level, const Json& request)
{
    checkFields(request, "request", { "schemaVersion", "checks", "maxWorkUnits" });
    const auto& version = requiredField(request, "schemaVersion");
    if ( version.type != Json::Type::Number || version.number != 1 )
    {
        invalid("schemaVersion must be 1");
    }
    const auto& checks = requiredField(request, "checks");
    if ( checks.type != Json::Type::Array || checks.array.empty() || checks.array.size() > kMaxMapRequirements )
    {
        invalid("checks must contain 1 to 64 entries");
    }
    const auto* limit = request.find("maxWorkUnits");
    std::size_t remaining = limit ? unsignedValue(*limit, kMaxRequirementWorkUnits, "maxWorkUnits") : kMaxRequirementWorkUnits;
    std::set<std::string> names;
    Json rows = Json::makeArray();
    bool passed = true;
    for ( const auto& check : checks.array )
    {
        try
        {
            Json row = evaluateCheck(level, check, names, remaining);
            passed = passed && row.object.at("passed").boolean;
            rows.array.push_back(std::move(row));
        }
        catch ( const AgentError& error )
        {
            throw AgentError{ error.code, "checks[" + std::to_string(rows.array.size()) + "]: " + error.message, error.errorCode };
        }
    }
    Json out = Json::makeObject();
    out.set("ok", Json::makeBool(true));
    out.set("command", Json::makeString("map-requirements"));
    out.set("passed", Json::makeBool(passed));
    out.set("checks", std::move(rows));
    return out;
}

} // namespace sed_agent
