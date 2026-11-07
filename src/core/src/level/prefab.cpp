#include "sed/level/prefab.hpp"

#include "sed/assets/jkl.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>
#include <utility>

namespace sed::level
{
namespace
{

std::optional<std::string> hashPayload(const SectorClipboardPayload& payload)
{
    const EntityClipboard clipboard{ payload };
    const std::optional<std::vector<std::byte>> bytes = encodeEntityClipboard(clipboard);
    if (!bytes || bytes->empty())
    {
        return std::nullopt;
    }
    return assets::jklSourceSha256Hex(std::string_view(
        reinterpret_cast<const char*>(bytes->data()), bytes->size()));
}

std::string toHex(const std::vector<std::byte>& bytes)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::byte byte : bytes)
    {
        const unsigned value = std::to_integer<unsigned>(byte);
        out += kDigits[(value >> 4) & 0xFu];
        out += kDigits[value & 0xFu];
    }
    return out;
}

std::optional<std::vector<std::byte>> fromHex(const std::string& text)
{
    if (text.empty() || text.size() % 2 != 0)
    {
        return std::nullopt;
    }
    const auto nibble = [](const char c) -> int
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f')
        {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F')
        {
            return c - 'A' + 10;
        }
        return -1;
    };
    std::vector<std::byte> out;
    out.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2)
    {
        const int hi = nibble(text[i]);
        const int lo = nibble(text[i + 1]);
        if (hi < 0 || lo < 0)
        {
            return std::nullopt;
        }
        out.push_back(static_cast<std::byte>((hi << 4) | lo));
    }
    return out;
}

} // namespace

std::optional<Prefab> makePrefab(
    const Level& level,
    const std::span<const std::int32_t> sectors,
    const std::int32_t primarySector)
{
    const std::optional<EntityClipboard> clipboard =
        copySectorsToClipboard(level, sectors, primarySector);
    if (!clipboard)
    {
        return std::nullopt;
    }
    const auto* payload = std::get_if<SectorClipboardPayload>(&*clipboard);
    if (!payload || payload->sectors.empty())
    {
        return std::nullopt;
    }

    Prefab prefab{};
    prefab.version = kPrefabVersion;
    prefab.sourceProject = payload->sourceProject;
    prefab.payload = *payload;
    for (std::size_t sector = 0; sector < prefab.payload.sectors.size(); ++sector)
    {
        const Sector& captured = prefab.payload.sectors[sector];
        for (std::size_t surface = 0; surface < captured.surfaces.size(); ++surface)
        {
            if (!captured.surfaces[surface].adjoin)
            {
                prefab.attachments.push_back(PrefabAttachment{
                    static_cast<std::int32_t>(sector),
                    static_cast<std::int32_t>(surface),
                });
            }
        }
    }
    const std::optional<std::string> hash = hashPayload(prefab.payload);
    if (!hash)
    {
        return std::nullopt;
    }
    prefab.contentSha256Hex = *hash;
    return prefab;
}

bool prefabHasValidHash(const Prefab& prefab)
{
    if (prefab.version != kPrefabVersion
        || prefab.payload.sectors.empty()
        || prefab.payload.sourceProject != prefab.sourceProject
        || prefab.contentSha256Hex.empty())
    {
        return false;
    }
    for (const PrefabAttachment& attachment : prefab.attachments)
    {
        if (attachment.sector < 0
            || static_cast<std::size_t>(attachment.sector) >= prefab.payload.sectors.size())
        {
            return false;
        }
        const Sector& sector = prefab.payload.sectors[static_cast<std::size_t>(attachment.sector)];
        if (attachment.surface < 0
            || static_cast<std::size_t>(attachment.surface) >= sector.surfaces.size()
            || sector.surfaces[static_cast<std::size_t>(attachment.surface)].adjoin)
        {
            return false;
        }
    }
    const std::optional<std::string> hash = hashPayload(prefab.payload);
    return hash && *hash == prefab.contentSha256Hex;
}

std::optional<std::string> savePrefabText(const Prefab& prefab)
{
    if (!prefabHasValidHash(prefab))
    {
        return std::nullopt;
    }
    const EntityClipboard clipboard{ prefab.payload };
    const std::optional<std::vector<std::byte>> bytes = encodeEntityClipboard(clipboard);
    if (!bytes || bytes->empty())
    {
        return std::nullopt;
    }
    std::ostringstream out;
    out << "RED-PREFAB 1\n";
    out << "project " << static_cast<int>(prefab.sourceProject) << '\n';
    out << "sha256 " << prefab.contentSha256Hex << '\n';
    out << "attachments " << prefab.attachments.size() << '\n';
    for (const PrefabAttachment& attachment : prefab.attachments)
    {
        out << "attach " << attachment.sector << ' ' << attachment.surface << '\n';
    }
    out << "data " << toHex(*bytes) << '\n';
    return out.str();
}

std::optional<Prefab> loadPrefabFromText(const std::string_view text)
{
    std::istringstream in{ std::string(text) };
    std::string line;
    if (!std::getline(in, line) || line != "RED-PREFAB 1")
    {
        return std::nullopt;
    }
    const auto field = [&in](const char* key, std::string& value) -> bool
    {
        std::string row;
        if (!std::getline(in, row))
        {
            return false;
        }
        std::istringstream parsed(row);
        std::string name;
        if (!(parsed >> name >> value) || name != key)
        {
            return false;
        }
        return true;
    };
    std::string projectText;
    std::string hash;
    std::string countText;
    std::string dataHex;
    if (!field("project", projectText) || !field("sha256", hash)
        || !field("attachments", countText))
    {
        return std::nullopt;
    }
    int projectValue = -1;
    std::size_t attachmentCount = 0;
    try
    {
        projectValue = std::stoi(projectText);
        attachmentCount = static_cast<std::size_t>(std::stoull(countText));
    }
    catch (...)
    {
        return std::nullopt;
    }
    if (projectValue < 0 || projectValue > 3 || attachmentCount > 100000u)
    {
        return std::nullopt;
    }
    std::vector<PrefabAttachment> attachments;
    attachments.reserve(attachmentCount);
    for (std::size_t i = 0; i < attachmentCount; ++i)
    {
        std::string row;
        if (!std::getline(in, row))
        {
            return std::nullopt;
        }
        std::istringstream parsed(row);
        std::string name;
        std::int32_t sector = -1;
        std::int32_t surface = -1;
        if (!(parsed >> name >> sector >> surface) || name != "attach")
        {
            return std::nullopt;
        }
        attachments.push_back(PrefabAttachment{ sector, surface });
    }
    if (!field("data", dataHex))
    {
        return std::nullopt;
    }
    const std::optional<std::vector<std::byte>> bytes = fromHex(dataHex);
    if (!bytes)
    {
        return std::nullopt;
    }
    const std::optional<EntityClipboard> clipboard = decodeEntityClipboard(*bytes);
    if (!clipboard)
    {
        return std::nullopt;
    }
    const auto* payload = std::get_if<SectorClipboardPayload>(&*clipboard);
    if (!payload || payload->sectors.empty())
    {
        return std::nullopt;
    }
    Prefab prefab{};
    prefab.version = kPrefabVersion;
    prefab.sourceProject = static_cast<ProjectType>(projectValue);
    prefab.payload = *payload;
    prefab.attachments = std::move(attachments);
    prefab.contentSha256Hex = hash;
    if (prefab.payload.sourceProject != prefab.sourceProject
        || !prefabHasValidHash(prefab))
    {
        return std::nullopt;
    }
    return prefab;
}

std::optional<EntityPasteResult> insertPrefab(
    Level& level,
    const Prefab& prefab,
    const std::optional<TVector> target,
    const UvTextureDimensionsResolver& resolveDimensions)
{
    if (!prefabHasValidHash(prefab) || prefab.sourceProject != level.kind)
    {
        return std::nullopt;
    }
    const EntityClipboard clipboard{ prefab.payload };
    return pasteEntityClipboard(level, clipboard, target, resolveDimensions);
}

std::optional<std::vector<std::int32_t>> levelBuildSectorArray(
    Level& level,
    const std::span<const std::int32_t> sectors,
    const PlacementLayout& layout)
{
    if (sectors.empty())
    {
        return std::nullopt;
    }
    const PlacementPositions positions = computePlacementPositions(layout);
    if (!positions.ok())
    {
        return std::nullopt;
    }
    std::vector<std::int32_t> sorted(sectors.begin(), sectors.end());
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    const std::optional<Prefab> prefab = makePrefab(level, sorted, sorted.front());
    if (!prefab)
    {
        return std::nullopt;
    }
    Level candidate = level;
    std::vector<std::int32_t> built{};
    for (const TVector& position : positions.positions)
    {
        const std::optional<EntityPasteResult> inserted = insertPrefab(candidate, *prefab, position);
        if (!inserted)
        {
            return std::nullopt;
        }
        built.insert(built.end(), inserted->indices.begin(), inserted->indices.end());
    }
    level = std::move(candidate);
    return built;
}

} // namespace sed::level
