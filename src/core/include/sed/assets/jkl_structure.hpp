#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sed::assets
{

struct JklStructureOptions
{
    std::size_t maxSourceBytes{128u * 1024u * 1024u};
    std::size_t maxLineBytes{1024u * 1024u};
    std::size_t maxDiagnostics{256};
    std::size_t maxTrackedThingSlots{1000000};
};

struct JklStructureDiagnostic
{
    std::string code{};
    std::string section{};
    // One-based physical source line; EOF findings refer to their header line.
    std::size_t line{1};
    std::string message{};
};

struct JklStructureReport
{
    std::vector<JklStructureDiagnostic> diagnostics{};
    // False when a budget or ambiguous/incomplete text framing stops the scan.
    bool complete{true};
    bool inspectedMaterials{};
    bool inspectedGeoResource{};

    // Success within the supported sections present, not whole-file validity.
    [[nodiscard]] bool ok() const noexcept
    {
        return complete && diagnostics.empty();
    }
};

/**
 * Additive, read-only classic JK/MotS JKL preflight for MATERIALS/GEORESOURCE framing
 * and any TEMPLATES, MODELS and THINGS capacity tables present. This profile
 * excludes IJIM NDY. A present GEORESOURCE must contain COLORMAPS, VERTICES,
 * TEXTURE VERTICES, ADJOINS and SURFACES in order;
 * missing COLORMAPS never implies a different dialect.
 * Resource counts are capacities (zero spare slots are valid); geometry counts
 * are exact. Runtime surface normals must form a complete grouped block.
 * Entity tables count logical authoring rows. Thing slots must be unique and
 * in range; model row labels do not select model slots. MATERIALS and the
 * three entity tables require END. GEORESOURCE is count-delimited: complete geometry
 * and normals may end at EOF or the next SECTION without an END sentinel.
 * Unknown sections and COPYRIGHT are not assigned invented terminator rules.
 * The report identifies presence of the CLI-required MATERIALS and GEORESOURCE.
 * This helper does not certify that a complete level was supplied.
 * This does not load resources, change compatibility import, repair text, or
 * establish engine/gameplay validity. Input bytes and lines are bounded before
 * count-driven work; declared counts must never drive allocation.
 */
[[nodiscard]] JklStructureReport inspectJklStructure(
    std::string_view source, const JklStructureOptions& options = {});

} // namespace sed::assets
