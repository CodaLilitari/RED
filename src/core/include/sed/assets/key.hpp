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
    std::int32_t frame{};
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
};

struct KeyFile
{
    std::uint32_t flags{};
    std::int32_t frames{};
    std::int32_t fps{};
    std::vector<KeyNode> nodes;
};

KeyFile loadFromKEY(std::istream& in);
KeyFile loadFromKEY(const std::filesystem::path& path);

/** Serializes a KEY file in the text form `loadFromKEY` reads.
 *
 * Doubles use the shortest round-trip spelling, so loading the
 * output reproduces every stored value exactly. An entry with an
 * empty label is written with the conventional `FRAME` label. */
std::string saveToKEYText(const KeyFile& key);
void saveToKEY(const KeyFile& key, const std::filesystem::path& path);

/** Inserts one timeline entry into a node, keeping its entries
 * ordered by frame (stable for equal frames).
 *
 * Refuses, leaving the file unchanged, when the node does not exist,
 * the frame is negative, the frame exceeds the declared frame count
 * when one is set, or any position/rotation component is not finite.
 * This is the rigid-trajectory timeline edit (E-12); it never touches
 * Thing path-mover frames, which are a separate contract. */
bool keyInsertEntry(KeyFile& key, std::size_t node, const KeyEntry& entry);

/** Removes one entry by index. Refuses an unknown node or entry. */
bool keyEraseEntry(KeyFile& key, std::size_t node, std::size_t entry);

/** Stable-sorts one node's entries by frame. Unknown nodes are ignored. */
void keySortNodeEntries(KeyFile& key, std::size_t node);

} // namespace sed::assets
