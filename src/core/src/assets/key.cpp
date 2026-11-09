#include "sed/assets/key.hpp"

#include "atomic_file_write.hpp"
#include "key_input.hpp"

#include "sed/format_number.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <locale>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sed::assets
{

namespace
{

using sed::text::iequalsAscii;
using sed::text::istartsWithAscii;
using sed::text::parseDouble;
using sed::text::parseHex;
using sed::text::parseInt;
using sed::text::splitWords;
using sed::text::stripHashComment;
using sed::text::trim;
using sed::text::upperAscii;

constexpr const char* kUnsafeKeyMessage =
    "KEY contains unsupported or malformed data; refusing lossy serialization.";

bool engineFloat(const double value) noexcept
{
    return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max();
}

bool finiteVector(const TVector& value) noexcept
{
    return engineFloat(value.x) && engineFloat(value.y) && engineFloat(value.z);
}

std::optional<std::uint32_t> parseFlags(const std::string_view text) noexcept
{
    if ( istartsWithAscii(text, "0x") )
    {
        return parseHex(text);
    }
    const auto value = parseInt<std::int32_t>(text);
    return value ? std::optional<std::uint32_t>(std::bit_cast<std::uint32_t>(*value)) : std::nullopt;
}

bool numericEntryLabel(const std::string_view label) noexcept
{
    if ( label.size() < 2 || label.back() != ':' )
    {
        return false;
    }
    const auto index = parseInt<std::int32_t>(label.substr(0, label.size() - 1));
    return index && *index >= 0;
}

std::int32_t effectiveNodeIndex(const KeyNode& node, const std::size_t position)
{
    if ( node.nodeIndex >= 0 )
    {
        return node.nodeIndex;
    }
    if ( node.nodeIndex != -1 || position >= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) )
    {
        throw std::runtime_error(kUnsafeKeyMessage);
    }
    return static_cast<std::int32_t>(position);
}

std::int32_t checkedJointCount(const KeyFile& key)
{
    if ( !key.serializationSafe || key.frames < 0 || !engineFloat(key.fps) || key.fps <= 0.0
        || key.joints < -1 || key.nodes.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())
        || key.markers.size() > 8u )
    {
        throw std::runtime_error(kUnsafeKeyMessage);
    }
    std::int32_t requiredJoints = 0;
    std::unordered_set<std::int32_t> nodeIndices;
    for ( std::size_t i = 0; i < key.nodes.size(); ++i )
    {
        const KeyNode& node = key.nodes[i];
        const std::int32_t index = effectiveNodeIndex(node, i);
        if ( index == std::numeric_limits<std::int32_t>::max() || !nodeIndices.insert(index).second
            || node.meshName.empty() || node.meshName.size() >= 32u
            || node.meshName.find_first_of(" \t\r\n\v\f#\0", 0, 8) != std::string::npos
            || node.entries.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) )
        {
            throw std::runtime_error(kUnsafeKeyMessage);
        }
        requiredJoints = std::max(requiredJoints, index + 1);
        for ( const KeyEntry& entry : node.entries )
        {
            if ( !engineFloat(entry.frame) || entry.frame < 0.0
                || (key.frames > 0 && entry.frame > key.frames)
                || (!entry.label.empty() && entry.label != "FRAME" && !numericEntryLabel(entry.label))
                || !finiteVector(entry.position) || !finiteVector(entry.rotation)
                || !finiteVector(entry.dPosition) || !finiteVector(entry.dRotation) )
            {
                throw std::runtime_error(kUnsafeKeyMessage);
            }
        }
    }
    for ( const KeyMarker& marker : key.markers )
    {
        if ( !engineFloat(marker.frame) || marker.frame < 0.0
            || (key.frames > 0 && marker.frame > key.frames) )
        {
            throw std::runtime_error(kUnsafeKeyMessage);
        }
    }
    if ( key.joints >= 0 && key.joints < requiredJoints )
    {
        throw std::runtime_error(kUnsafeKeyMessage);
    }
    return key.joints < 0 ? requiredJoints : key.joints;
}

} // namespace

KeyFile loadFromKEY(std::istream& input)
{
    std::istringstream in(detail::readKeyInput(input));
    KeyFile out{};
    std::unordered_map<std::string, std::vector<std::string>> sections;
    std::unordered_set<std::string> seenSections;
    std::string currentSection;
    std::string line;

    while (std::getline(in, line))
    {
        stripHashComment(line);
        std::string trimmed = trim(line);
        if (trimmed.empty())
        {
            continue;
        }

        if (iequalsAscii(trimmed, "END"))
        {
            currentSection.clear();
            continue;
        }

        if (istartsWithAscii(trimmed, "SECTION:"))
        {
            const auto words = splitWords(trimmed);
            currentSection = (words.size() >= 2) ? upperAscii(words[1]) : std::string{};
            if ( !seenSections.insert(currentSection).second
                || (currentSection != "HEADER" && currentSection != "MARKERS" && currentSection != "KEYFRAME")
                || (words.size() != 2
                    && !(words.size() == 3 && currentSection == "KEYFRAME" && iequalsAscii(words[2], "NODES"))) )
            {
                out.serializationSafe = false;
            }
            continue;
        }

        if (!currentSection.empty())
        {
            sections[currentSection].push_back(std::move(trimmed));
        }
        else
        {
            out.serializationSafe = false;
        }
    }

    if (in.bad() || (in.fail() && !in.eof()))
    {
        throw std::runtime_error("Failed to read KEY.");
    }

    std::unordered_set<std::string> headerFields;
    if (const auto it = sections.find("HEADER"); it != sections.end())
    {
        for (const std::string& raw : it->second)
        {
            const auto tokens = splitWords(raw);
            if (tokens.size() < 2)
            {
                out.serializationSafe = false;
                continue;
            }

            const std::string key = upperAscii(tokens[0]);
            if ( tokens.size() != 2 || !headerFields.insert(key).second )
            {
                out.serializationSafe = false;
            }
            if (key == "FLAGS")
            {
                const auto value = parseFlags(tokens[1]);
                out.flags = value.value_or(0);
                out.serializationSafe = out.serializationSafe && value.has_value();
            }
            else if (key == "TYPE")
            {
                const auto value = parseHex(tokens[1]);
                out.type = value.value_or(0);
                out.serializationSafe = out.serializationSafe && value.has_value();
            }
            else if (key == "FRAMES")
            {
                const auto value = parseInt<std::int32_t>(tokens[1]);
                out.frames = value.value_or(0);
                out.serializationSafe = out.serializationSafe && value && *value >= 0;
            }
            else if (key == "FPS")
            {
                const auto value = parseDouble(tokens[1]);
                out.fps = value.value_or(0.0);
                out.serializationSafe = out.serializationSafe && value && engineFloat(*value) && *value > 0.0;
            }
            else if (key == "JOINTS")
            {
                const auto value = parseInt<std::int32_t>(tokens[1]);
                out.joints = value.value_or(-1);
                out.serializationSafe = out.serializationSafe && value && *value >= 0;
            }
            else
            {
                out.serializationSafe = false;
            }
        }
    }
    for ( const char* field : { "FLAGS", "TYPE", "FRAMES", "FPS", "JOINTS" } )
    {
        out.serializationSafe = out.serializationSafe && headerFields.contains(field);
    }

    if ( const auto it = sections.find("MARKERS"); it != sections.end() )
    {
        const auto& rows = it->second;
        std::int32_t count = -1;
        if ( !rows.empty() )
        {
            const auto header = splitWords(rows[0]);
            if ( header.size() == 2 && iequalsAscii(header[0], "MARKERS") )
            {
                count = parseInt<std::int32_t>(header[1]).value_or(-1);
            }
        }
        if ( count < 0 || count > 8 || rows.size() != static_cast<std::size_t>(count) + 1u )
        {
            out.serializationSafe = false;
        }
        for ( std::int32_t i = 0; i < count && static_cast<std::size_t>(i) + 1u < rows.size(); ++i )
        {
            const auto values = splitWords(rows[static_cast<std::size_t>(i) + 1u]);
            if ( values.size() != 2 )
            {
                out.serializationSafe = false;
                continue;
            }
            const auto frame = parseDouble(values[0]);
            const auto id = parseInt<std::int32_t>(values[1]);
            if ( !frame || !engineFloat(*frame) || !id )
            {
                out.serializationSafe = false;
                continue;
            }
            out.markers.push_back(KeyMarker{ *frame, *id });
        }
    }

    if (const auto it = sections.find("KEYFRAME"); it != sections.end())
    {
        const auto& lines = it->second;
        std::size_t idx = 0;

        std::int32_t nodeCount = 0;
        if (idx < lines.size())
        {
            const auto hdr = splitWords(lines[idx++]);
            if (hdr.size() == 2 && iequalsAscii(hdr[0], "NODES"))
            {
                const auto value = parseInt<std::int32_t>(hdr[1]);
                nodeCount = value.value_or(0);
                out.serializationSafe = out.serializationSafe && value && *value >= 0;
            }
            else
            {
                out.serializationSafe = false;
            }
        }
        else
        {
            out.serializationSafe = false;
        }

        const std::size_t boundedNodeCount = std::min(
            static_cast<std::size_t>(std::max<std::int32_t>(0, nodeCount)),
            lines.size() - idx);
        out.nodes.reserve(boundedNodeCount);
        for (std::int32_t n = 0; n < nodeCount && idx < lines.size(); ++n)
        {
            KeyNode node{};

            const auto nodeTokens = splitWords(lines[idx]);
            if (nodeTokens.size() == 2 && iequalsAscii(nodeTokens[0], "NODE"))
            {
                const auto value = parseInt<std::int32_t>(nodeTokens[1]);
                node.nodeIndex = value.value_or(-1);
                out.serializationSafe = out.serializationSafe && value && *value >= 0;
                ++idx;
            }
            else
            {
                out.serializationSafe = false;
            }

            if (idx < lines.size())
            {
                const auto tokens = splitWords(lines[idx++]);
                if (tokens.size() == 3 && iequalsAscii(tokens[0], "MESH") && iequalsAscii(tokens[1], "NAME"))
                {
                    node.meshName = std::string(tokens[2]);
                }
                else
                {
                    out.serializationSafe = false;
                }
            }
            else
            {
                out.serializationSafe = false;
            }

            std::int32_t entryCount = 0;
            if (idx < lines.size())
            {
                const auto tokens = splitWords(lines[idx++]);
                if (tokens.size() == 2 && iequalsAscii(tokens[0], "ENTRIES"))
                {
                    const auto value = parseInt<std::int32_t>(tokens[1]);
                    entryCount = value.value_or(0);
                    out.serializationSafe = out.serializationSafe && value && *value >= 0;
                }
                else
                {
                    out.serializationSafe = false;
                }
            }
            else
            {
                out.serializationSafe = false;
            }

            const std::size_t boundedEntryCount = std::min(
                static_cast<std::size_t>(std::max<std::int32_t>(0, entryCount)),
                lines.size() - idx);
            node.entries.reserve(boundedEntryCount);
            for (std::int32_t e = 0; e < entryCount && idx < lines.size(); ++e)
            {
                KeyEntry entry{};

                const auto tokens = splitWords(lines[idx++]);
                if (tokens.size() == 9)
                {
                    entry.label = std::string(tokens[0]);
                    const auto frame = parseDouble(tokens[1]);
                    const auto flags = parseHex(tokens[2]);
                    entry.frame = frame.value_or(0.0);
                    entry.flags = flags.value_or(0);
                    out.serializationSafe = out.serializationSafe && numericEntryLabel(entry.label)
                        && frame && engineFloat(*frame) && flags.has_value();
                    for ( std::size_t component = 3; component < 9; ++component )
                    {
                        const auto value = parseDouble(tokens[component]);
                        out.serializationSafe = out.serializationSafe && value && engineFloat(*value);
                    }

                    entry.position.x = parseDouble(tokens[3]).value_or(0.0);
                    entry.position.y = parseDouble(tokens[4]).value_or(0.0);
                    entry.position.z = parseDouble(tokens[5]).value_or(0.0);

                    entry.rotation.pitch = parseDouble(tokens[6]).value_or(0.0);
                    entry.rotation.yaw = parseDouble(tokens[7]).value_or(0.0);
                    entry.rotation.roll = parseDouble(tokens[8]).value_or(0.0);
                }
                else
                {
                    out.serializationSafe = false;
                }

                if (idx < lines.size())
                {
                    const auto tokens = splitWords(lines[idx++]);
                    if (tokens.size() == 6)
                    {
                        for ( const auto token : tokens )
                        {
                            const auto value = parseDouble(token);
                            out.serializationSafe = out.serializationSafe && value && engineFloat(*value);
                        }
                        entry.dPosition.x = parseDouble(tokens[0]).value_or(0.0);
                        entry.dPosition.y = parseDouble(tokens[1]).value_or(0.0);
                        entry.dPosition.z = parseDouble(tokens[2]).value_or(0.0);

                        entry.dRotation.pitch = parseDouble(tokens[3]).value_or(0.0);
                        entry.dRotation.yaw = parseDouble(tokens[4]).value_or(0.0);
                        entry.dRotation.roll = parseDouble(tokens[5]).value_or(0.0);
                    }
                    else
                    {
                        out.serializationSafe = false;
                    }
                }
                else
                {
                    out.serializationSafe = false;
                }

                node.entries.push_back(std::move(entry));
            }
            if ( node.entries.size() != static_cast<std::size_t>(std::max(0, entryCount)) )
            {
                out.serializationSafe = false;
            }

            out.nodes.push_back(std::move(node));
        }
        if ( out.nodes.size() != static_cast<std::size_t>(std::max(0, nodeCount)) || idx != lines.size() )
        {
            out.serializationSafe = false;
        }
    }
    else
    {
        out.serializationSafe = false;
    }

    if ( out.serializationSafe )
    {
        try
        {
            (void)checkedJointCount(out);
        }
        catch ( const std::runtime_error& )
        {
            out.serializationSafe = false;
        }
    }

    return out;
}

KeyFile loadFromKEY(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Failed to open KEY file.");
    }
    return loadFromKEY(file);
}

std::string saveToKEYText(const KeyFile& key)
{
    const std::int32_t joints = checkedJointCount(key);
    std::ostringstream out{};
    out.imbue(std::locale::classic());
    out << "SECTION: HEADER\n";
    out << "FLAGS " << std::bit_cast<std::int32_t>(key.flags) << "\n";
    out << "TYPE 0x" << std::uppercase << std::hex << key.type << std::dec << "\n";
    out << "FRAMES " << key.frames << "\n";
    out << "FPS " << sed::format::authoredDouble(key.fps) << "\n";
    out << "JOINTS " << joints << "\n";
    if ( !key.markers.empty() )
    {
        out << "SECTION: MARKERS\n";
        out << "MARKERS " << key.markers.size() << "\n";
        for ( const KeyMarker& marker : key.markers )
        {
            out << sed::format::authoredDouble(marker.frame) << ' ' << marker.markerId << '\n';
        }
    }
    out << "SECTION: KEYFRAME NODES\n";
    out << "NODES " << key.nodes.size() << "\n";
    for (std::size_t nodeIndex = 0; nodeIndex < key.nodes.size(); ++nodeIndex)
    {
        const KeyNode& node = key.nodes[nodeIndex];
        out << "NODE " << effectiveNodeIndex(node, nodeIndex) << "\n";
        out << "MESH NAME " << node.meshName << "\n";
        out << "ENTRIES " << node.entries.size() << "\n";
        for (std::size_t entryIndex = 0; entryIndex < node.entries.size(); ++entryIndex)
        {
            const KeyEntry& entry = node.entries[entryIndex];
            out << entryIndex << ": " << sed::format::authoredDouble(entry.frame)
                << " 0x" << std::uppercase << std::hex << entry.flags << std::dec
                << ' ' << sed::format::authoredDouble(entry.position.x)
                << ' ' << sed::format::authoredDouble(entry.position.y)
                << ' ' << sed::format::authoredDouble(entry.position.z)
                << ' ' << sed::format::authoredDouble(entry.rotation.pitch)
                << ' ' << sed::format::authoredDouble(entry.rotation.yaw)
                << ' ' << sed::format::authoredDouble(entry.rotation.roll) << "\n";
            out << sed::format::authoredDouble(entry.dPosition.x)
                << ' ' << sed::format::authoredDouble(entry.dPosition.y)
                << ' ' << sed::format::authoredDouble(entry.dPosition.z)
                << ' ' << sed::format::authoredDouble(entry.dRotation.pitch)
                << ' ' << sed::format::authoredDouble(entry.dRotation.yaw)
                << ' ' << sed::format::authoredDouble(entry.dRotation.roll) << "\n";
        }
    }
    return out.str();
}

void saveToKEY(const KeyFile& key, const std::filesystem::path& path)
{
    // Validate before staging, then replace only after all canonical bytes
    // have been written, flushed and closed. Failure preserves the old asset.
    const std::string text = saveToKEYText(key);
    const auto destination = std::filesystem::absolute(path);
    detail::StagedAssetFile staged(destination);
    staged.writeAndClose(std::as_bytes(std::span(text.data(), text.size())));
    staged.publish(destination);
}

bool keyInsertEntry(KeyFile& key, const std::size_t node, const KeyEntry& entry)
{
    if (!key.serializationSafe || node >= key.nodes.size() || !engineFloat(entry.frame) || entry.frame < 0
        || (key.frames > 0 && entry.frame > key.frames))
    {
        return false;
    }
    if (!finiteVector(entry.position) || !finiteVector(entry.rotation)
        || !finiteVector(entry.dPosition) || !finiteVector(entry.dRotation)
        || (!entry.label.empty() && entry.label != "FRAME" && !numericEntryLabel(entry.label)))
    {
        return false;
    }
    std::vector<KeyEntry>& entries = key.nodes[node].entries;
    const auto byFrame = [](const KeyEntry& left, const KeyEntry& right)
    {
        return left.frame < right.frame;
    };
    if ( !std::is_sorted(entries.begin(), entries.end(), byFrame) )
    {
        // Imported tracks need not already be sorted. Build the ordered
        // replacement privately so an allocation failure preserves the track.
        std::vector<KeyEntry> candidate = entries;
        candidate.push_back(entry);
        std::stable_sort(candidate.begin(), candidate.end(), byFrame);
        entries.swap(candidate);
        return true;
    }
    const auto at = std::upper_bound(entries.begin(), entries.end(), entry.frame,
        [](const double frame, const KeyEntry& current)
        {
            return frame < current.frame;
        });
    entries.insert(at, entry);
    return true;
}

bool keyEraseEntry(KeyFile& key, const std::size_t node, const std::size_t entry)
{
    if (!key.serializationSafe || node >= key.nodes.size() || entry >= key.nodes[node].entries.size())
    {
        return false;
    }
    std::vector<KeyEntry>& entries = key.nodes[node].entries;
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(entry));
    return true;
}

void keySortNodeEntries(KeyFile& key, const std::size_t node)
{
    if (!key.serializationSafe || node >= key.nodes.size())
    {
        return;
    }
    std::stable_sort(key.nodes[node].entries.begin(), key.nodes[node].entries.end(),
        [](const KeyEntry& left, const KeyEntry& right)
        {
            return left.frame < right.frame;
        });
}

} // namespace sed::assets
