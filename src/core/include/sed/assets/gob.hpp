#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sed::assets
{

struct GobEntry
{
    std::string name;
    std::uint32_t offset{};
    std::uint32_t size{};
};

class GobArchive
{
public:
    static GobArchive open(const std::filesystem::path& path);
    /** Parses and retains one immutable captured archive. Reads never reopen
     * sourceLabel; it is retained only for provenance and diagnostics.
     * Copies share the capture. Callers own admission limits for the bytes. */
    static GobArchive fromBytes(std::vector<std::byte> bytes,
        const std::filesystem::path& sourceLabel = {});

    const std::filesystem::path& path() const noexcept;

    const std::vector<GobEntry>& entries() const noexcept;
    std::optional<GobEntry> find(const std::string_view name) const;
    bool contains(const std::string_view name) const;

    std::vector<std::byte> read(const std::string_view name) const;

    /** Lowercase hex SHA-256 of one entry's bytes. */
    std::string entrySha256Hex(const std::string_view name) const;

private:
    static GobArchive parseIndex(std::istream& input, std::uint64_t archiveSize,
        const std::filesystem::path& path);
    static std::string normalizeEntryName(const std::string_view name);

    std::filesystem::path path_;
    std::vector<GobEntry> entries_;
    std::unordered_map<std::string, std::size_t> index_;
    std::shared_ptr<const std::vector<std::byte>> capturedBytes_;
};


/** Serializes entries (name, bytes) as a GOB archive in the given order.
 * The caller validates names; see the agent's archive-create for the rules. */
std::string buildGobBytes(const std::vector<std::pair<std::string, std::string>>& files);

} // namespace sed::assets
