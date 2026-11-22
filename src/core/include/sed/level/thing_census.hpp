#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <variant>

namespace sed
{
struct Level;
}

namespace sed::level
{
// Difficulty indices follow the JK/MotS world loader: easy, medium, hard.
using DifficultyCounts = std::array<std::size_t, 3>;

struct PlacementCounts
{
    std::size_t placements{};
    std::size_t invalidSector{};
    std::size_t unresolvedTemplate{};
    std::size_t unknownEligibility{};
    DifficultyCounts singlePlayer{};
    DifficultyCounts multiplayer{};
};

struct PlacementGroup
{
    std::int32_t sector{};
    std::int32_t layer{};
    std::string templateName;
    std::string type; // Empty when no supported type can be established.

    auto operator<=>(const PlacementGroup&) const = default;
};

struct ThingCensus
{
    PlacementCounts summary;
    std::map<PlacementGroup, PlacementCounts> groups;
    DifficultyCounts confirmedPlayerStarts{};
    DifficultyCounts possibleAdditionalPlayerStarts{};
};

enum class ThingCensusError { unsupportedTarget, tooLarge };

// Summarize the authored, embedded template/placement records only. No resource
// lookup or runtime simulation. A refused scan never returns partial groups.
std::variant<ThingCensus, ThingCensusError> countThingPlacements(const Level& level);
}
