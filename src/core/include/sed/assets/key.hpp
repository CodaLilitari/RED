#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <string>
#include <vector>

#include "sed/geometry.hpp"

namespace sed::assets
{

struct KeyEntry
{
    std::string label;
    double frame{};
    std::uint32_t flags{};

    TVector position{};
    TVector rotation{};

    TVector dPosition{};
    TVector dRotation{};
};

struct KeyNode
{
    std::string meshName;
    std::vector<KeyEntry> entries;
    // A negative value is reserved for newly authored positional nodes.
    // Imported sparse NODE values are indices into the JOINTS table.
    std::int32_t nodeIndex{ -1 };
};

struct KeyMarker
{
    double frame{};
    std::int32_t markerId{};
};

struct KeyFile
{
    std::uint32_t flags{};
    std::int32_t frames{};
    double fps{};
    std::vector<KeyNode> nodes;
    std::uint32_t type{};
    // New objects derive the joint count from their node indices. Imports
    // retain the declared count, including joints without an animation track.
    std::int32_t joints{ -1 };
    std::vector<KeyMarker> markers;
    // The preview reader may recover modeled fields from malformed or
    // extended input. Such input must never be silently serialized with loss.
    bool serializationSafe{ true };
};

KeyFile loadFromKEY(std::istream& in);
KeyFile loadFromKEY(const std::filesystem::path& path);

/** Serializes the supported classic engine KEY grammar.
 *
 * Retains TYPE, JOINTS, sparse NODE indices, MARKERS, and fractional
 * FPS/frame positions. Numeric row labels are regenerated in entry order;
 * comments and whitespace are not preserved. Doubles use round-trip
 * spelling. Unsupported/malformed imports or invalid authored values
 * throw before output is opened, rather than silently dropping data. */
std::string saveToKEYText(const KeyFile& key);
void saveToKEY(const KeyFile& key, const std::filesystem::path& path);

/** Inserts one timeline entry into a node, keeping its entries
 * ordered by frame (stable for equal frames). An unsorted existing track
 * is ordered in a private candidate before the replacement is published.
 *
 * Refuses, leaving the file unchanged, when the node does not exist,
 * the frame is non-finite or negative, the frame exceeds the declared frame count
 * when one is set, any position/rotation component is outside the finite
 * engine range, or the imported file is unsafe to serialize.
 * This is the rigid-trajectory timeline edit (E-12); it never touches
 * Thing path-mover frames, which are a separate contract. */
bool keyInsertEntry(KeyFile& key, std::size_t node, const KeyEntry& entry);

/** Removes one entry by index. Refuses an unknown node or entry. */
bool keyEraseEntry(KeyFile& key, std::size_t node, std::size_t entry);

/** Stable-sorts one node's entries by frame. Unknown nodes are ignored. */
void keySortNodeEntries(KeyFile& key, std::size_t node);

} // namespace sed::assets
