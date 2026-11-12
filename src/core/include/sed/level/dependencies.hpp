#pragma once

#include "sed/assets/gob.hpp"
#include "sed/level/package.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sed::level
{

struct DependencyLimits
{
    std::size_t maxNodes{4096};
    std::size_t maxEdges{8192};
    std::size_t maxDepth{64};
    std::size_t maxWorkUnits{1000000};
    std::size_t maxIndexEntries{100000};
    std::size_t maxPending{4096};
    std::size_t maxScanBytes{64u * 1024u * 1024u};
    std::size_t maxResourceBytes{128u * 1024u * 1024u};
    std::size_t maxStringBytes{4u * 1024u * 1024u};
    std::size_t maxDetailRecords{512};
    std::size_t maxDetailBytes{256u * 1024u};
    std::size_t maxIssues{128};
};

struct DependencyLimitSpec
{
    std::string_view name;
    std::size_t DependencyLimits::*member;
    std::size_t minimum;
    std::size_t maximum;
};

std::span<const DependencyLimitSpec> dependencyLimitSpecs();

/** Captured archives only. Copies share immutable bytes; no path is reopened. */
struct DependencyArchiveInput
{
    sed::assets::GobArchive archive;
    std::string label;
    bool stock{false};
};

/** Borrowed inputs need to remain alive only for the inspection call. */
struct DependencyRequest
{
    std::string_view inputBytes;
    std::string_view levelFileName;
    std::span<const DependencyArchiveInput> archives;
    std::span<const PackageExtraResource> extraResources;
    DependencyLimits limits{};
};

enum class DependencyResolution { Source, Resolved, Missing, Ambiguous };
enum class DependencyScan { Pending, Scanned, Leaf, Unsupported, Incomplete, BudgetStopped };

struct DependencyProvenance
{
    std::size_t archiveOrdinal{};
    std::size_t entryOrdinal{};
    std::string entryName;
    std::size_t offset{};
    std::size_t bytes{};
    std::string_view lookupTier;
};

struct DependencyNode
{
    std::size_t id{};
    std::size_t depth{};
    std::string path;
    DependencyResolution resolution{DependencyResolution::Source};
    DependencyScan inspection{DependencyScan::Pending};
    std::string sha256;
    bool stock{false};
    std::optional<DependencyProvenance> provenance;
};

struct DependencyEdge
{
    std::size_t parent{};
    std::size_t target{};
    std::string authoredName;
    std::string folder;
    std::string_view relation;
    bool reused{false};
};

struct DependencyIssue
{
    std::size_t node{};
    std::string_view code;
    std::string_view message;
};

struct DependencyArchiveInfo
{
    std::size_t ordinal{};
    std::string label;
    std::string sha256;
    std::size_t bytes{};
    std::size_t entries{};
    bool stock{false};
};

struct DependencyCounts
{
    std::size_t nodes{};
    std::size_t edges{};
    std::size_t issues{};
    std::size_t missing{};
    std::size_t ambiguous{};
    std::size_t unsupported{};
};

struct DependencyUsage
{
    std::size_t workUnits{};
    std::size_t indexEntries{};
    std::size_t scanBytes{};
    std::size_t resourceBytes{};
    std::size_t stringBytes{};
    std::size_t peakPending{};
    std::size_t detailRecords{};
    std::size_t detailBytes{};
};

/** Original byte strings are retained here; the agent encodes them for JSON.
 * Observed counts are lower bounds unless scanComplete. No runtime closure claim. */
struct DependencyInspection
{
    std::string inputSha256;
    DependencyLimits limits{};
    bool scanComplete{true};
    bool detailsComplete{true};
    std::optional<bool> allReferencesResolved{true};
    DependencyCounts counts{};
    DependencyUsage usage{};
    std::vector<DependencyArchiveInfo> sourceArchives;
    std::vector<DependencyNode> nodes;
    std::vector<DependencyEdge> edges;
    std::vector<DependencyIssue> issues;
    std::vector<std::string> unverifiedScopes;
    std::vector<std::string> scanReasons;
    std::vector<std::string> detailReasons;
};

DependencyInspection inspectDependencies(const DependencyRequest& request);

} // namespace sed::level
