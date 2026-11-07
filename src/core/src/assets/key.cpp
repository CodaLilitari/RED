#include "sed/assets/key.hpp"

#include "sed/format_number.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
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

} // namespace

KeyFile loadFromKEY(std::istream& in)
{
    std::unordered_map<std::string, std::vector<std::string>> sections;
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
            continue;
        }

        if (!currentSection.empty())
        {
            sections[currentSection].push_back(std::move(trimmed));
        }
    }

    if (in.bad() || (in.fail() && !in.eof()))
    {
        throw std::runtime_error("Failed to read KEY.");
    }

    KeyFile out{};

    if (const auto it = sections.find("HEADER"); it != sections.end())
    {
        for (const std::string& raw : it->second)
        {
            const auto tokens = splitWords(raw);
            if (tokens.size() < 2)
            {
                continue;
            }

            const std::string key = upperAscii(tokens[0]);
            if (key == "FLAGS")
            {
                out.flags = parseHex(tokens[1]).value_or(0);
            }
            else if (key == "FRAMES")
            {
                out.frames = parseInt<std::int32_t>(tokens[1]).value_or(0);
            }
            else if (key == "FPS")
            {
                out.fps = parseInt<std::int32_t>(tokens[1]).value_or(0);
            }
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
            if (hdr.size() >= 2 && iequalsAscii(hdr[0], "NODES"))
            {
                nodeCount = parseInt<std::int32_t>(hdr[1]).value_or(0);
            }
        }

        const std::size_t boundedNodeCount = std::min(
            static_cast<std::size_t>(std::max<std::int32_t>(0, nodeCount)),
            lines.size() - idx);
        out.nodes.reserve(boundedNodeCount);
        for (std::int32_t n = 0; n < nodeCount && idx < lines.size(); ++n)
        {
            KeyNode node{};

            const auto nodeTokens = splitWords(lines[idx]);
            if (!nodeTokens.empty() && iequalsAscii(nodeTokens[0], "NODE"))
            {
                ++idx;
            }

            if (idx < lines.size())
            {
                const auto tokens = splitWords(lines[idx++]);
                if (tokens.size() >= 3 && iequalsAscii(tokens[0], "MESH") && iequalsAscii(tokens[1], "NAME"))
                {
                    node.meshName = std::string(tokens[2]);
                }
            }

            std::int32_t entryCount = 0;
            if (idx < lines.size())
            {
                const auto tokens = splitWords(lines[idx++]);
                if (tokens.size() >= 2 && iequalsAscii(tokens[0], "ENTRIES"))
                {
                    entryCount = parseInt<std::int32_t>(tokens[1]).value_or(0);
                }
            }

            const std::size_t boundedEntryCount = std::min(
                static_cast<std::size_t>(std::max<std::int32_t>(0, entryCount)),
                lines.size() - idx);
            node.entries.reserve(boundedEntryCount);
            for (std::int32_t e = 0; e < entryCount && idx < lines.size(); ++e)
            {
                KeyEntry entry{};

                const auto tokens = splitWords(lines[idx++]);
                if (tokens.size() >= 9)
                {
                    entry.label = std::string(tokens[0]);
                    entry.frame = parseInt<std::int32_t>(tokens[1]).value_or(0);
                    entry.flags = parseHex(tokens[2]).value_or(0);

                    entry.position.x = parseDouble(tokens[3]).value_or(0.0);
                    entry.position.y = parseDouble(tokens[4]).value_or(0.0);
                    entry.position.z = parseDouble(tokens[5]).value_or(0.0);

                    entry.rotation.pitch = parseDouble(tokens[6]).value_or(0.0);
                    entry.rotation.yaw = parseDouble(tokens[7]).value_or(0.0);
                    entry.rotation.roll = parseDouble(tokens[8]).value_or(0.0);
                }

                if (idx < lines.size())
                {
                    const auto tokens = splitWords(lines[idx++]);
                    if (tokens.size() >= 6)
                    {
                        entry.dPosition.x = parseDouble(tokens[0]).value_or(0.0);
                        entry.dPosition.y = parseDouble(tokens[1]).value_or(0.0);
                        entry.dPosition.z = parseDouble(tokens[2]).value_or(0.0);

                        entry.dRotation.pitch = parseDouble(tokens[3]).value_or(0.0);
                        entry.dRotation.yaw = parseDouble(tokens[4]).value_or(0.0);
                        entry.dRotation.roll = parseDouble(tokens[5]).value_or(0.0);
                    }
                }

                node.entries.push_back(std::move(entry));
            }

            out.nodes.push_back(std::move(node));
        }
    }

    return out;
}

KeyFile loadFromKEY(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file)
    {
        throw std::runtime_error("Failed to open KEY file.");
    }
    return loadFromKEY(file);
}

std::string saveToKEYText(const KeyFile& key)
{
    std::ostringstream out{};
    out << "SECTION: HEADER\n";
    out << "FLAGS 0x" << std::uppercase << std::hex << key.flags << std::dec << "\n";
    out << "FRAMES " << key.frames << "\n";
    out << "FPS " << key.fps << "\n";
    out << "END\n";
    out << "SECTION: KEYFRAME\n";
    out << "NODES " << key.nodes.size() << "\n";
    for (std::size_t nodeIndex = 0; nodeIndex < key.nodes.size(); ++nodeIndex)
    {
        const KeyNode& node = key.nodes[nodeIndex];
        out << "NODE " << nodeIndex << "\n";
        out << "MESH NAME " << node.meshName << "\n";
        out << "ENTRIES " << node.entries.size() << "\n";
        for (const KeyEntry& entry : node.entries)
        {
            out << (entry.label.empty() ? "FRAME" : entry.label) << ' ' << entry.frame
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
    out << "END\n";
    return out.str();
}

void saveToKEY(const KeyFile& key, const std::filesystem::path& path)
{
    std::ofstream file(path);
    if (!file)
    {
        throw std::runtime_error("Failed to open KEY file for writing.");
    }
    file << saveToKEYText(key);
    if (!file)
    {
        throw std::runtime_error("Failed to write KEY file.");
    }
}

bool keyInsertEntry(KeyFile& key, const std::size_t node, const KeyEntry& entry)
{
    if (node >= key.nodes.size() || entry.frame < 0
        || (key.frames > 0 && entry.frame > key.frames))
    {
        return false;
    }
    const auto finite = [](const TVector& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    if (!finite(entry.position) || !finite(entry.rotation)
        || !finite(entry.dPosition) || !finite(entry.dRotation))
    {
        return false;
    }
    std::vector<KeyEntry>& entries = key.nodes[node].entries;
    const auto at = std::upper_bound(entries.begin(), entries.end(), entry.frame,
        [](const std::int32_t frame, const KeyEntry& current)
        {
            return frame < current.frame;
        });
    entries.insert(at, entry);
    return true;
}

bool keyEraseEntry(KeyFile& key, const std::size_t node, const std::size_t entry)
{
    if (node >= key.nodes.size() || entry >= key.nodes[node].entries.size())
    {
        return false;
    }
    std::vector<KeyEntry>& entries = key.nodes[node].entries;
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(entry));
    return true;
}

void keySortNodeEntries(KeyFile& key, const std::size_t node)
{
    if (node >= key.nodes.size())
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
