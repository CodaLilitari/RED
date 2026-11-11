#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sed/assets/cog_schema.hpp"
#include "sed/level.hpp"

namespace sed::level
{

/** Positional parameter kinds understood as level references. */
enum class CogReferenceKind
{
    other,
    thing,
    sector,
    surface,
    cog,
};

/** Classification of one raw COG index against the placed COG list. */
enum class CogIndexStatus
{
    invalid,
    null,
    level,
    unplacedUnchecked,
    staticUnchecked,
};

/** Resolution status of one described binding target. */
enum class CogBindingTargetStatus
{
    unknown,
    notReference,
    missing,
    invalid,
    null,
    level,
    unplacedUnchecked,
    staticUnchecked,
};

/** Script-schema state for one placed COG. */
enum class CogScriptStatus
{
    missing,
    nameMismatch,
    complete,
    incomplete,
};

/** Measured-registry-free reference kind name used in diagnostics. */
std::string_view cogReferenceKindName(CogReferenceKind kind) noexcept;

/** Coda script-status spelling (`missing` / `nameMismatch` / `complete` / `incomplete`). */
std::string_view cogScriptStatusName(CogScriptStatus status) noexcept;

/** Complete decimal 32-bit index with optional leading sign; nothing else. */
bool tryParseCogIndex(std::string_view text, std::int32_t& value) noexcept;

/** Classifies a raw COG index; bit 0x8000 selects the static world. */
CogIndexStatus checkCogIndex(std::string_view text, std::int32_t placedCogCount) noexcept;

/** Reference kind declared by one schema symbol kind. */
CogReferenceKind cogReferenceKindForSymbol(assets::CogSchemaSymbolKind kind) noexcept;

struct CogBindingTarget
{
    CogReferenceKind kind{ CogReferenceKind::other };
    CogBindingTargetStatus status{ CogBindingTargetStatus::unknown };
    std::optional<std::int32_t> index{};
    std::optional<std::int32_t> sectorIndex{};
    std::optional<std::int32_t> surfaceIndex{};
    std::string detail{};
};

struct CogBindingRow
{
    std::int32_t cogIndex{};
    std::int32_t parameterIndex{};
    std::optional<std::string> symbolName{};
    assets::CogSchemaSymbolKind symbolKind{ assets::CogSchemaSymbolKind::unknown };
    std::optional<std::string> rawValue{};
    const assets::CogSymbol* symbol{ nullptr };
    CogBindingTarget target{};
    std::optional<bool> eventLinkCandidate{};
};

/**
 * Read-only typed inspection of one placed COG. Reference status and
 * event-link candidacy stay separate: `eventLinkSemantics` carries the
 * fixed static-candidate disclaimer wherever rows are reported.
 */
struct CogBindingDescription
{
    std::int32_t cogIndex{};
    std::string scriptName{};
    CogScriptStatus scriptStatus{ CogScriptStatus::missing };
    bool schemaComplete{ false };
    std::int32_t actualParameterCount{};
    std::optional<std::int32_t> expectedParameterCount{};
    std::optional<bool> parameterCountMatches{};
    std::vector<assets::CogSchemaDiagnostic> diagnostics{};
    std::vector<CogBindingRow> rows{};
};

/** Fixed wording: static candidate only, never runtime or multiplayer proof. */
inline constexpr std::string_view kCogEventLinkSemantics =
    "OpenJKDF2 reference-symbol modifiers; static candidate only, not runtime or multiplayer proof";

/**
 * Describes one placed COG against a captured schema (or none). Rows run
 * to max(actual, expected); missing schemas leave raw values untyped.
 * Surface indices resolve through the retained imported surface map when
 * present, else through current serialized sector-major order. Throws
 * std::out_of_range when `cogIndex` is outside the placed COG list.
 */
CogBindingDescription describeBindings(
    const Level& level,
    std::int32_t cogIndex,
    const assets::CogSchema* schema);

/** One typed positional parameter supplied by a complete schema. */
struct CogParameterDefinition
{
    std::string name{};
    CogReferenceKind kind{ CogReferenceKind::other };
};

/**
 * Returns the typed parameters only when the schema resolves for this COG,
 * is complete and name-matching, and its level-value count exactly equals
 * the COG's value count. Otherwise returns no parameters: raw values are
 * never guessed to be references.
 */
std::optional<std::vector<CogParameterDefinition>> resolveCogParameters(
    const Cog& cog,
    const assets::CogSchema* schema);

enum class CogReferenceIssueSeverity
{
    error,
    warning,
};

struct CogReferenceIssue
{
    CogReferenceIssueSeverity severity{ CogReferenceIssueSeverity::warning };
    std::int32_t cogIndex{};
    std::int32_t parameterIndex{ -1 };
    std::string message{};
};

/** Supplies the captured schema for one placed COG name, per call only. */
using CogSchemaResolver =
    std::function<std::optional<assets::CogSchema>(std::string_view cogName)>;

/**
 * Read-only COG reference consistency pass. Without a resolvable complete
 * schema and exact count, each placed COG earns one schema-unavailable
 * warning and its raw values stay untyped. With a schema, non-decimal
 * references and out-of-range thing/sector/surface indices are errors,
 * while static/unplaced COG references are warnings.
 */
std::vector<CogReferenceIssue> checkCogReferences(
    const Level& level,
    const CogSchemaResolver& resolveSchema);

/** Authored script-name limit for new placed COGs (legacy 32-byte field). */
inline constexpr std::size_t kMaxAuthoredCogScriptNameLength = 31;

/** Maximum named bindings accepted for one newly authored placed COG. */
inline constexpr std::size_t kMaxAuthoredCogBindings = 256;

/** Editor work limits for append-closure and thing-append inspection. */
inline constexpr std::size_t kMaxCogsForAppendClosure = 4096;
inline constexpr std::size_t kMaxCogParametersForAppendClosure = 100000;

/** Stable refusal for a guarded mutation or append guard. */
struct CogMutationFailure
{
    std::string code{};
    std::string message{};
};

/** One named level value supplied for a newly authored placed COG.
 *
 * Thing, sector, surface and cog symbols take `targetIndex` (-1 for none).
 * Every other kind (flex, float, int, vector, sound, material, model,
 * template, keyframe, ai) takes either `valueText`, parsed with the symbol's
 * own type exactly as the editor parses a typed value, or `useDefault`, the
 * script's declared default. Nothing is inferred: a non-reference symbol
 * with neither is refused, and so is `useDefault` for a symbol that declares
 * no default.
 */
struct CogNamedBinding
{
    std::string symbolName{};
    CogReferenceKind kind{ CogReferenceKind::other };
    std::int32_t targetIndex{ -1 };
    std::optional<std::string> valueText{};
    bool useDefault{ false };
};

/** Index a COG surface symbol uses for the surface `surface` of sector
 * `sector`: its position in the level's surface numbering, which follows an
 * imported JKL's own order when one is retained. Empty when the surface does
 * not exist or the numbering cannot be proven (the same cases in which a
 * surface binding is refused). */
std::optional<std::int32_t> cogSurfaceTargetIndex(
    const Level& level, std::int32_t sector, std::int32_t surface);

/** True for an ASCII `.cog` basename that may name a newly authored COG. */
bool isValidAuthoredCogScriptName(std::string_view name) noexcept;

/** Lowercase hex SHA-256 of a captured schema's retained source text. */
std::string cogSchemaSha256Hex(const assets::CogSchema& schema);

/**
 * A validated create-placed-COG step. Prepare never mutates; `apply`
 * appends exactly one COG whose values follow declaration order. Apply
 * through `Document::applyUndoable` with `DocumentDirty::Cogs` so the
 * creation is one undo step.
 */
struct PreparedAddCog
{
    std::optional<CogMutationFailure> failure{};
    std::string scriptName{};
    std::string scriptSha256{};
    std::int32_t newCogIndex{ -1 };
    Cog preparedCog{};

    bool ok() const noexcept
    {
        return !failure.has_value();
    }

    /** Appends the prepared COG; false without mutation when stale. */
    bool apply(Level& level) const;
};

/**
 * A validated single-reference rebind step. Prepare never mutates;
 * `apply` rewrites exactly one positional value to its canonical
 * decimal spelling and leaves every other raw value byte-identical.
 * Apply through `Document::applyUndoable` with `DocumentDirty::Cogs`
 * so the edit is one undo step.
 */
struct PreparedRebindCogReference
{
    std::optional<CogMutationFailure> failure{};
    std::int32_t cogIndex{ -1 };
    std::int32_t parameterIndex{ -1 };
    std::int32_t targetIndex{ -1 };
    std::string scriptSha256{};
    std::string expectedExistingRaw{};

    bool ok() const noexcept
    {
        return !failure.has_value();
    }

    /** Rewrites the prepared value; false without mutation when stale. */
    bool apply(Level& level) const;
};

/**
 * Validates a new placed COG against the current level and captured
 * schema. Every external symbol must be supplied exactly once by name
 * with a matching kind; no defaults are inferred. Existing placed COGs
 * are re-inspected on every call so an append cannot capture or shift
 * an unchecked COG reference. `expectedScriptSha256`, when supplied,
 * must match the captured schema bytes.
 */
PreparedAddCog prepareAddCog(
    const Level& level,
    std::string_view scriptName,
    const assets::CogSchema* schema,
    const std::vector<CogNamedBinding>& bindings,
    const CogSchemaResolver& resolveExistingSchema,
    std::optional<std::string_view> expectedScriptSha256 = std::nullopt);

/**
 * Validates rebinding one positional reference. The schema must be
 * complete with an exact value count; the prepared step never pads or
 * discards values. Only `-1` authors null, and a surface target needs
 * understood surface identity even when nulling.
 */
PreparedRebindCogReference prepareRebindCogReference(
    const Level& level,
    std::int32_t cogIndex,
    std::int32_t parameterIndex,
    std::string_view expectedSymbol,
    assets::CogSchemaSymbolKind expectedKind,
    std::int32_t targetIndex,
    const assets::CogSchema* schema,
    std::optional<std::string_view> expectedScriptSha256 = std::nullopt);

/**
 * Thing-append guard: before appending a thing, every placed COG must
 * have a matching complete schema with an exact count and every
 * external `thing` value must be the canonical `-1` or a currently
 * placed index. Returns no failure when appending may proceed. The
 * guard analyses positional symbols only; it does not analyse script
 * code, computed indices, or template-attached scripts, and it
 * revalidates on every call without mutating the level.
 */
std::optional<CogMutationFailure> validateThingAppend(
    const Level& level,
    const CogSchemaResolver& resolveSchema);

} // namespace sed::level
