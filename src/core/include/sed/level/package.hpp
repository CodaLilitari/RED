#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sed::level
{

/** One ordered resource archive the planner may resolve against (D-03). */
struct PackageArchiveInput
{
    /** Display label used in entry sources and the manifest. */
    std::string label{};
    /** Stock archives satisfy references without contributing bytes. */
    bool stock{ false };
    /** Normalized entry name (forward slashes, any case) -> bytes. */
    std::vector<std::pair<std::string, std::string>> entries{};
};

/** One additional root reference resolved like a level reference. */
struct PackageExtraResource
{
    std::string name{};
    std::string folder{};
};

/** One immutable resource winner supplied by an application locator. */
struct PackageResolvedResource
{
    std::string source;
    std::string bytes;
    bool stock{ false };
};

/** Planner budgets; zero depth/bytes means the 128 MiB package cap only. */
struct PackageOptions
{
    /** Counts the episode, level and distinct resources, including stock omissions. */
    std::size_t maxNodes{ 4096 };
    std::size_t maxEdges{ 8192 };
    std::size_t maxDepth{ 0 };
    std::size_t maxPackageBytes{ 128ull * 1024 * 1024 };
    bool allowUnverifiedReferences{ false };
};

/** Everything the deterministic planner needs; archives are pre-hashed by the caller. */
struct PackageRequest
{
    std::string inputBytes{};
    std::string levelFileName{};
    std::string episodeTitle{};
    std::int32_t episodeType{ 1 };
    std::string archiveExtension{ ".gob" };
    std::vector<PackageArchiveInput> archives{};
    std::vector<PackageExtraResource> extraResources{};
    PackageOptions options{};
    /** Optional primary resolver for original authored resource names, called with the
     * remaining byte budget. Missing results fall back to ordered archives.
     * The caller must provide a stable captured source and honor maxBytes.
     * The planner independently rejects oversized results before publication.
     */
    std::function<std::optional<PackageResolvedResource>(std::string_view, std::size_t)> resolveResource;
};

/** One output entry (or stock omission) in deterministic plan order. */
struct PackageEntry
{
    /** Canonical lowercase runtime destination, with forward slashes for receipts. */
    std::string path{};
    std::string source{};
    std::size_t bytes{ 0 };
    std::string sha256{};
    bool omittedStock{ false };
};

/** One structured plan issue; severity is "error" or "warning". */
struct PackageIssue
{
    std::string severity{ "error" };
    std::string code{};
    std::string reference{};
    std::string message{};
};

/** The reviewed plan: identical inputs always produce identical hashes/bytes. */
struct PackagePlan
{
    bool canBuild{ false };
    /** Always false: packaging never proves retail/CMP runtime closure. */
    bool runtimeClosureVerified{ false };
    std::string archiveExtension{};
    std::string outputSha256{};
    std::string manifestSha256{};
    std::size_t outputBytes{ 0 };
    std::string outputData{};
    std::size_t nodes{ 0 };
    std::size_t edges{ 0 };
    std::vector<PackageEntry> entries{};
    std::vector<PackageEntry> stockOmissions{};
    std::vector<PackageIssue> issues{};
    std::vector<std::string> unverifiedScopes{};
};

/**
 * Plans (and, when buildable, builds) a reviewed level package from JKL
 * source bytes (D-03). Direct references come from the JKL resource
 * tables, template resource parameters, placed COG scripts, GEORESOURCE
 * colormaps, extra roots, 3DO material dependencies, sprite materials,
 * sound-class WAV alternatives, puppet keyframes, and COG symbol-default
 * resources. Uninspected
 * dependency families are disclosed in `unverifiedScopes`, never
 * silently treated as closed.
 */
PackagePlan planLevelPackage(const PackageRequest& request);

} // namespace sed::level
