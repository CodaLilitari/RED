#pragma once

#include "sed/assets/cog_preflight.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sed::assets
{

/**
 * The COG compiler: it reads a script the way retail JK 1.01 or MotS loads it
 * and reports what the engine would do with it.
 *
 * The header is read like the engine's config reader: comments, lowercasing,
 * continuation lines, and comma or blank separated arguments. The code
 * section is lexed and parsed with the retail grammar (OpenJKDF2's cog.l and
 * cog.y, 0BSD), and compiled to the engine's bytecode. Where the engine
 * misbehaves silently instead of refusing a script, the compiler says so:
 * exhausted symbol, link, handler and label tables, unknown verbs and messages,
 * missing labels, and verbs called with the wrong number of arguments.
 *
 * `loads` is the engine's own verdict: false only when the retail loader would
 * refuse the script (a header the reader rejects, or a syntax error). A script
 * that loads can still have errors that break it at run time.
 */

enum class CogDiagnosticSeverity
{
    error,
    warning,
    note,
};

struct CogDiagnostic
{
    CogDiagnosticSeverity severity{ CogDiagnosticSeverity::error };
    std::string code{};
    std::string message{};
    /** 1-based line in the file; 0 when the finding has no single line. */
    std::int32_t line{};
};

/** One entry of the script's own symbol table, in engine order. */
struct CogCompiledSymbol
{
    std::string name{};
    /**
     * The declared type (thing, surface, sector, sound, template, model,
     * keyframe, cog, material, ai, flex, int, vector, message), or "implicit"
     * for an identifier the code introduced, or "string" for a literal.
     */
    std::string kind{};
    std::int32_t line{};
    bool local{ false };
    /** The symbol's value or default as declared, when it has one. */
    std::string value{};
};

struct CogCompiledHandler
{
    std::string message{};
    std::int32_t messageId{};
    /** Code address the handler starts at; -1 when the script has no label for it. */
    std::int32_t address{ -1 };
};

struct CogCompileLimits
{
    std::size_t symbols{};
    std::size_t symbolLimit{};
    std::size_t links{};
    std::size_t linkLimit{};
    std::size_t handlers{};
    std::size_t handlerLimit{};
    std::size_t labels{};
    std::size_t labelLimit{};
};

struct CogCompileResult
{
    std::string fileName{};
    std::string sourceSha256{};
    CogPreflightTarget target{ CogPreflightTarget::jk };
    /** The engine's verdict: false when the retail loader refuses the script. */
    bool loads{ false };
    std::uint32_t flags{};
    std::vector<CogDiagnostic> diagnostics{};
    std::vector<CogCompiledSymbol> symbols{};
    std::vector<CogCompiledHandler> handlers{};
    CogCompileLimits limits{};
    /**
     * The compiled code, as the engine lays it out, with a final return. A
     * global symbol (a verb, message or global variable) is encoded as
     * 0x100 plus its position in RED's registry, which is not necessarily
     * the engine's own numbering.
     */
    std::vector<std::int32_t> code{};
    /** A readable listing of `code`. */
    std::string disassembly{};
    /** The 1-based file line the code section starts on. */
    std::int32_t codeLine{};

    [[nodiscard]] std::size_t count(CogDiagnosticSeverity severity) const noexcept;
};

/**
 * Compiles one script for a retail target. `ijim` is refused: there is no
 * measured registry for it. The text is only read.
 */
CogCompileResult compileCog(std::string_view fileName, std::string_view text, CogPreflightTarget target);

/** "error", "warning" or "note". */
std::string_view cogDiagnosticSeverityName(CogDiagnosticSeverity severity) noexcept;

/** The generated verb signature table is intact (its recorded digest matches). */
bool cogVerbSignaturesIntact();

/**
 * A verb's arguments in call order and its result kind, from the generated
 * table; nullopt for a name that isn't a retail verb.
 */
struct CogVerbInfo
{
    std::vector<std::string> args{};
    std::string result{};
    /** Set when the retail games run another verb's code under this name. */
    std::string runs{};
};
std::optional<CogVerbInfo> cogVerbSignature(std::string_view name);

} // namespace sed::assets
