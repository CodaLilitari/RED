#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sed/values.hpp"

namespace sed::assets
{

/** Declared kind of one `symbols` entry (authoring-grade schema spelling). */
enum class CogSchemaSymbolKind
{
    unknown,
    message,
    int_,
    flex,
    float_,
    vector,
    thing,
    sector,
    surface,
    sound,
    material,
    model,
    template_,
    keyframe,
    ai,
    cog,
};

/** A source-layout problem that prevents reliable positional typed edits. */
struct CogSchemaDiagnostic
{
    std::string code{};
    std::optional<std::int32_t> lineNumber{};
    std::string message{};
    std::optional<std::string> rawDeclaration{};
};

/** One entry of a script's `symbols` block, retained in source order. */
struct CogSymbol
{
    CogSchemaSymbolKind kind{ CogSchemaSymbolKind::unknown };
    std::string name{};
    std::optional<std::string> defaultText{};
    bool local{ false };
    std::optional<std::string> description{};
    std::string rawDeclaration{};
    std::int32_t lineNumber{};
    std::vector<std::string> rawModifiers{};
    bool noLink{ false };
    std::optional<std::int32_t> explicitLinkId{};
    std::optional<std::uint32_t> explicitMask{};
    // OpenJKDF2 reference-symbol interpretation; not proof of a runtime
    // subscription. Resolved in source order, so a later `linkid=` wins
    // over an earlier `nolink`.
    std::int32_t effectiveLinkId{ 0 };
    std::uint32_t effectiveMask{ 0x401u };

    /** True when the level supplies this symbol's value. */
    bool takesLevelValue() const noexcept
    {
        return !local && kind != CogSchemaSymbolKind::message;
    }

    /** Loader-compatible value kind for this declared symbol kind. */
    CogType cogType() const noexcept;
};

/**
 * A parsed `.cog` script's `symbols` block: the ordered level-value contract.
 * Only a completely understood layout (`isComplete()`) is eligible for
 * typed positional edits; every layout problem is a diagnostic.
 */
struct CogSchema
{
    std::string name{};
    std::string sourceText{};
    std::vector<CogSymbol> symbols{};
    std::vector<std::string> rawSymbolDeclarations{};
    bool hasSymbolsBlock{ false };
    bool hasSymbolsEnd{ false };
    std::vector<CogSchemaDiagnostic> diagnostics{};

    bool isComplete() const noexcept
    {
        return hasSymbolsBlock && hasSymbolsEnd && diagnostics.empty();
    }

    /** Non-local, non-message symbols in declaration order. */
    std::vector<const CogSymbol*> levelValues() const;
};

/** Parses captured `.cog` text without rereading any resource. Never throws on malformed input. */
CogSchema parseCogSchema(std::string_view name, std::string_view text);

/** One retained archive entry candidate for script resolution. */
struct CogScriptCandidate
{
    std::string entryName{};
    std::string text{};
};

/** One archive's entries, consulted in outer-vector priority order. */
using CogScriptArchive = std::vector<CogScriptCandidate>;

/**
 * Resolves a placed script name through ordered archive candidates.
 * Within one archive the `cog/` folder wins, then the exact path, then a
 * unique basename. A first matching archive with other than exactly one
 * match refuses resolution instead of falling through to a lower archive.
 * Returns no schema when the script is missing or ambiguous.
 */
std::optional<CogSchema> resolveCogSchema(
    std::string_view requestedName,
    const std::vector<CogScriptArchive>& archives);

/** Slash-normalized, case-insensitive script-name comparison. */
bool cogSchemaNamesMatch(std::string_view left, std::string_view right) noexcept;

} // namespace sed::assets
