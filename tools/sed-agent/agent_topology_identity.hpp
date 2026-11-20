#pragma once

#include "agent_json.hpp"
#include "sed/level.hpp"

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <utility>

namespace sed_agent
{

// Native entity IDs and snapshot slots have independent meanings. Legacy levels can
// contain missing or duplicate IDs. A slot remains usable within a reviewed input.
inline Json sectorIdentity(const sed::Level& level, const std::size_t slot)
{
    const auto id = level.sectors.at(slot).id;
    const bool usable = id > 0 && id < std::numeric_limits<std::int32_t>::max();
    const bool unique = usable && std::count_if(level.sectors.begin(), level.sectors.end(),
        [id](const sed::Sector& sector) { return sector.id == id; }) == 1;
    auto row = Json::makeObject();
    row.set("slot", Json::makeNumber(static_cast<double>(slot)));
    row.set("nativeId", unique ? Json::makeNumber(id) : Json{});
    if (!unique)
    {
        const char* reason = id <= 0 ? "missingId" : usable ? "duplicateId" : "reservedId";
        row.set("reason", Json::makeString(reason));
    }
    return row;
}

inline Json sectorLineage(const char* operation, Json inputs, Json outputs)
{
    Json map = Json::makeObject();
    map.set("kind", Json::makeString("sectorLineageV1"));
    map.set("operation", Json::makeString(operation));
    map.set("inputs", std::move(inputs));
    map.set("outputs", std::move(outputs));
    return map;
}

inline Json sectorIdentities(std::initializer_list<Json> rows)
{
    Json array = Json::makeArray();
    array.array = rows;
    return array;
}

} // namespace sed_agent
