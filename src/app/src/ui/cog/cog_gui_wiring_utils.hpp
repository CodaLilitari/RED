#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "sed/assets/cog_compiler.hpp"
#include "sed/assets/cog_preflight.hpp"
#include "sed/assets/cog_schema.hpp"
#include "sed/level.hpp"
#include "sed/level/cog_bindings.hpp"

namespace sed::ui::cog
{

/**
 * Exact wording shown whenever placed-COG values are edited without a
 * complete, count-exact schema (COG_SLICE_SPEC 3.9). Typed rows are only
 * ever produced from a complete schema; anything else stays positional
 * and is labelled with this text instead of guessed types.
 */
inline constexpr std::string_view kCogPositionalFallbackLabel =
    "editing values positionally without guessed types";

/** Whether one placed COG may show typed rows or must stay positional. */
enum class PlacedCogRowMode
{
    typed,
    positionalFallback,
};

/** Typed rows require a complete schema with an exact parameter count. */
PlacedCogRowMode placedCogRowMode(
    const level::CogBindingDescription& description) noexcept;

struct PlacedCogMapSlot
{
    std::string name;
    level::CogBindingTarget target;
};

/** Includes explicitly unbound map parameters. Never guesses from positional values. */
std::vector<PlacedCogMapSlot> placedCogMapSlots(const level::CogBindingDescription& description);

/** One display row for the placed-COG bindings view. */
struct PlacedCogRowView
{
    std::int32_t parameterIndex{};
    /** Symbol name in typed mode; `value N` in positional mode. */
    std::string label{};
    /** Declared kind spelling in typed mode; empty when untyped. */
    std::string typeName{};
    std::string rawValue{};
    /** Resolved target status plus detail; never a guessed reference. */
    std::string targetText{};
    /** `linkid`/`mask` wording; empty when the row has no symbol. */
    std::string linkText{};
    bool eventLinkCandidate{ false };
};

/** Builds display rows; reference and event-link fields stay separate. */
std::vector<PlacedCogRowView> buildPlacedCogRows(
    const level::CogBindingDescription& description);

/**
 * Fallback banner for positional mode: the fallback label plus the reason
 * (missing, mismatched, incomplete, or count-mismatched schema). Empty in
 * typed mode.
 */
std::string placedCogFallbackNote(
    const level::CogBindingDescription& description);

/** Measured-registry target for one project kind; IJIM stays unmeasured. */
assets::CogPreflightTarget cogPreflightTargetForProject(
    ProjectType kind) noexcept;

/**
 * Display lines for one lexical preflight. The first line always carries
 * the not-a-compiler method qualifier; availability findings name their
 * source lines and whether the spelling exists in the other retail table.
 */
std::vector<std::string> formatCogPreflightLines(
    const assets::CogPreflightResult& result);

/**
 * Display lines for one compile: the engine's verdict (loads or refused),
 * the error and warning counts, then each finding as "line N: severity:
 * message". With `listing`, the compiled code follows.
 */
std::vector<std::string> formatCogCompileLines(
    const assets::CogCompileResult& result, bool listing = false);

/** Stable refusal text: the prepare code first, then its message. */
std::string formatCogMutationRefusal(
    const level::CogMutationFailure& failure);

/**
 * Keywords the COG lexer recognises but the grammar cannot execute
 * (`break`/`continue` have no statement production; `switch` is not a
 * captured keyword). Completion must never present them as working
 * statements (COG_SLICE_SPEC 3.9).
 */
bool cogCompletionWordIsNonWorkingStatement(
    std::string_view word) noexcept;

} // namespace sed::ui::cog
