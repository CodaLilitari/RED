#pragma once

#include <filesystem>
#include <cstdint>
#include <istream>
#include <string>
#include <string_view>
#include <vector>
#include <ostream>

#include "sed/diagnostic.hpp"
#include "sed/level.hpp"

namespace sed::assets
{

class ResourceLocator;
class Templates;

struct JklLoadOptions
{
    ProjectType projectType{ProjectType::JKDF2};
    const ResourceLocator* resources{};
    DiagnosticSink onDiagnostic{};
};

struct JklSaveOptions
{
    const ResourceLocator* resources{};
    const Templates* templatesDb{};
    DiagnosticSink onDiagnostic{};

    // The editor-owned sections are useful to SED but are not required by
    // the game loader.  Keep them enabled by default for backwards
    // compatible exports; callers can omit either section when producing a
    // runtime-only JKL/NDY file.
    bool includeLights{true};
    bool includeLayers{true};

    // Emit the parser-compatible compact text dialect: LF line endings,
    // tab-separated fields without padded columns, and shortest safe decimal
    // spellings for floating point values.
    bool compact{false};

    // Clamp exported signed surface extra-light scalar/RGB values to [-1, 1].
    // Alpha keeps the existing [0, 1] wire clamp in all modes.
    bool clampExtraLight{true};

    // Omit all-zero MotS ARCHLIGHTING records and write -1 for their Things.
    // This opts into the engine's 3DO vertex-light fallback and changes the
    // rendered lighting; leave disabled to preserve the bound zero default.
    bool omitDefaultMotsArchLighting{false};

    // Share exactly coincident world coordinates by default. Nearby
    // world-vertex welding is an explicit export opt-in and uses this maximum
    // Euclidean distance when enabled.
    bool weldNearbyVertices{false};
    double vertexWeldDistance{0.00001};
};

/** One growable runtime resource pool as it will be written to JKL/NDY. */
struct JklExportBufferUsage
{
    std::uint64_t collected{};
    // Retained for source compatibility with older callers. V1 now exposes
    // every spare slot through `additional`, so this is always zero.
    std::uint64_t automaticReserve{};
    std::uint64_t additional{};
    std::uint64_t total{};

    constexpr bool operator==(const JklExportBufferUsage&) const noexcept = default;
};

/** Exact resource-capacity calculation shared with the JKL/NDY writer. */
struct JklExportBufferReport
{
    JklExportBufferUsage sounds{};
    JklExportBufferUsage materials{};
    JklExportBufferUsage aiClasses{};
    JklExportBufferUsage models{};
    JklExportBufferUsage sprites{};
    JklExportBufferUsage keyframes{};
    JklExportBufferUsage puppets{};
    JklExportBufferUsage soundClasses{};
    JklExportBufferUsage cogScripts{};
    JklExportBufferUsage cogs{};
    JklExportBufferUsage templates{};
    JklExportBufferUsage things{};
    bool dependencyScanComplete{true};

    constexpr bool operator==(const JklExportBufferReport&) const noexcept = default;
};

// Parses textual Sith level data: JKDF2/MotS JKL and IJIM NDY.
Level loadFromJKL(std::istream& in, const JklLoadOptions& options = {});
Level loadFromJKL(const std::filesystem::path& path, const JklLoadOptions& options = {});

/**
 * Completes the resource-dependent UV part of a previously decoded JKL/NDY.
 *
 * Callers that must determine the project kind from the file before choosing
 * a resource locator can first load with `JklLoadOptions::resources ==
 * nullptr`, then call this routine after staging the authoritative locator.
 * Conversion is failure-atomic and retains the legacy pixel-space Level when
 * a material dimension or material-less default cannot be resolved.
 */
void finalizeJklImportUv(
    Level& level,
    const ResourceLocator& resources,
    DiagnosticSink onDiagnostic = {});

/**
 * Completes every resource-dependent part of a previously decoded JKL/NDY.
 *
 * This includes COG schema/value hydration and, for classic pixel-space
 * input, MAT-backed UV normalization.  Callers that first decode a VERSION 1
 * JKL without resources must invoke this after staging the authoritative
 * project locator; the operation is failure-atomic for UV conversion and
 * preserves raw COG arguments when a script is unavailable.
 */
void finalizeJklImportResources(
    Level& level,
    const ResourceLocator& resources,
    DiagnosticSink onDiagnostic = {});

/**
 * Crawl the same template/resource graph as saveToJKL and report the exact
 * required-record, editable-extra, and final capacities without
 * serializing geometry.
 */
JklExportBufferReport inspectJklExportBuffers(
    const Level& level,
    const JklSaveOptions& options = {});

void saveToJKL(const Level& level, std::ostream& out);
void saveToJKL(const Level& level, const std::filesystem::path& path);
void saveToJKL(const Level& level, std::ostream& out, const JklSaveOptions& options);
void saveToJKL(const Level& level, const std::filesystem::path& path, const JklSaveOptions& options);

/** Lowercase hex SHA-256 of JKL/NDY source text (exported for tests/tooling). */
std::string jklSourceSha256Hex(std::string_view text);

/**
 * One fully serialized JKL/NDY export plus the resource registrations
 * it proposes. Produced by buildJklExport without mutating the Level:
 * `materials`/`colormaps` are the tables as they will be written
 * (colormaps empty for IJIM) and `sectorColormapIndices` holds the
 * resolved exact colormap slot per sector (-1 when unbound/IJIM).
 */
struct JklBuiltExport
{
    std::string text{};
    std::vector<std::string> materials{};
    std::vector<std::string> colormaps{};
    std::vector<std::int32_t> sectorColormapIndices{};
    JklExportBufferReport buffers{};
    // GEORESOURCE wire-slot outcome (F-06). False means the imported
    // surface/vertex/texvertex/adjoin slot numbers are preserved.
    // True means topology changed them (or no identity was retained),
    // so slots were regenerated; `geoResourceRenumberReason` says why.
    // Always false for levels with no imported baseline.
    bool geoResourceRenumbered{};
    std::string geoResourceRenumberReason{};
    // Proposed GEORESOURCE identity for the written bytes, installed
    // by commitJklExport so a rebuild preserves the new numbering.
    std::vector<Vertex> geoWorldVertices{};
    std::vector<TexVertex> geoWorldTexVertices{};
    std::vector<std::uint32_t> geoAdjoinFlags{};
    std::vector<std::int32_t> geoAdjoinMirrors{};
    std::vector<std::vector<std::int32_t>> geoVertexIndices{};
    std::vector<std::vector<std::int32_t>> geoSurfaceIndices{};
    std::vector<std::vector<std::vector<std::int32_t>>> geoTexVertexIndices{};
    std::vector<std::vector<std::int32_t>> geoAdjoinIndices{};
    std::vector<std::vector<std::int32_t>> geoMaterialIndices{};
    std::int32_t geoSurfaceCount{ -1 };
    std::int32_t geoAdjoinCount{ -1 };
};

/**
 * Serializes `level` without mutating it (the transactional Build).
 * Throws before producing output when the retained source no longer
 * matches its captured hash, when a sector colormap slot mismatches
 * or is ambiguous, or when any value cannot be represented; a throw
 * therefore always means zero bytes were written anywhere.
 */
JklBuiltExport buildJklExport(const Level& level, const JklSaveOptions& options = {});

/**
 * Publishes a built export to `path` through a staged sibling file
 * and, only after the replacement succeeds, commits its registrations
 * to `level` (material/colormap tables, resolved sector colormap
 * slots, and the retained-source baseline). A failed publication
 * leaves the previous file and every registration untouched, so the
 * same built export can be retried unchanged.
 */
void commitJklExport(Level& level, const JklBuiltExport& built, const std::filesystem::path& path);

} // namespace sed::assets
