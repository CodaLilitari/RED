#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
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
    /** Borrowed immutable capture; absent for archives opened from a path.
     * The span remains valid while this archive or a captured copy is alive. */
    std::optional<std::span<const std::byte>> capturedBytes() const noexcept;
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


/** Validates an engine-facing relative archive path and returns its physical
 * lowercase ASCII name with backslashes. Names occupy at most 127 bytes so
 * the engine's 128-byte lookup buffer retains its terminator. Empty/dot path
 * components, spaces, drive letters and nonportable ASCII punctuation refuse
 * with std::invalid_argument. Does not rewrite logical references or payloads. */
std::string canonicalRuntimeGobEntryName(std::string_view name);

/** Serializes entries (name, bytes) as a GOB archive in the given order,
 * preserving case and converting forward slashes to backslashes. The caller
 * validates names; engine-facing authoring uses canonicalRuntimeGobEntryName.
 * Raw archival callers retain their original physical spelling. */
std::string buildGobBytes(const std::vector<std::pair<std::string, std::string>>& files);

} // namespace sed::assets
