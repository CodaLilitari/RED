#pragma once

#include <cstddef>
#include <span>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sed::assets
{
inline constexpr std::size_t kRuntimeArchiveMaxEntries = 4096;
inline constexpr std::size_t kRuntimeArchiveMaxSourceBytes = 256u * 1024u * 1024u;
inline constexpr std::size_t kRuntimeArchiveMaxBytes = 512u * 1024u * 1024u;

enum class RuntimeArchiveErrorCode
{
    InvalidRequest,
    InvalidEntryName,
    DuplicateEntry,
    SourceTooLarge,
    ArchiveTooLarge,
    InvalidEntry,
    Readback
};

class RuntimeArchiveError : public std::runtime_error
{
public:
    RuntimeArchiveError(const RuntimeArchiveErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code)
    {
    }
    RuntimeArchiveErrorCode code() const noexcept
    {
        return code_;
    }
private:
    RuntimeArchiveErrorCode code_;
};

struct RuntimeArchiveInput
{
    std::string name;
    std::string bytes;
};

struct RuntimeArchiveEntry
{
    std::string name;
    std::size_t bytes{};
    std::string sha256;
    std::string readBack;
};

struct RuntimeArchiveResult
{
    std::string outputData;
    std::vector<RuntimeArchiveEntry> entries;
    std::vector<std::string> warnings;
};

/** Allocation-free byte/count admission for adapters before capturing payloads.
 * Includes the GOB header and physical index. Throws a typed refusal. */
void validateRuntimeArchiveSizes(std::span<const std::size_t> sizes);

/** Incremental capture adapter boundary. Each add validates before the caller
 * reads the next source, preserving the CLI's failure precedence. A failed add
 * poisons this builder; finish requires exactly the declared number of entries. */
class RuntimeArchiveBuilder
{
public:
    explicit RuntimeArchiveBuilder(std::size_t entryCount);
    ~RuntimeArchiveBuilder();
    RuntimeArchiveBuilder(const RuntimeArchiveBuilder&) = delete;
    RuntimeArchiveBuilder& operator=(const RuntimeArchiveBuilder&) = delete;
    void add(RuntimeArchiveInput input);
    RuntimeArchiveResult finish();
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/** Builds an engine-facing archive from immutable captured source bytes.
 * Consumes owned payloads to avoid duplicating the entire capture. Physical names
 * are canonicalized; logical resource references and payload bytes are untouched.
 * Known MAT/3DO/COG-symbol/TPL readers and final GOB readback run before return.
 * No filesystem discovery, publication, asset closure or runtime proof is implied. */
RuntimeArchiveResult buildRuntimeArchive(std::vector<RuntimeArchiveInput> inputs);
} // namespace sed::assets
