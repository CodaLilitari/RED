#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sed::assets
{

/**
 * Target whose measured retail registry a lexical preflight runs against.
 * `jk` is the original JK 1.01 executable and `mots` the original MotS
 * executable; the two measured tables stay distinct and are never merged.
 * `ijim` has no measured registry in this slice: preflight reports it as
 * target-unmeasured instead of substituting another target's table.
 * Enhanced-engine, cogext, and dynamic-POV spellings are not retail
 * availability and never appear in either retail table.
 */
enum class CogPreflightTarget
{
    jk,
    mots,
    ijim,
};

/** Whether measured findings exist for the requested target. */
enum class CogPreflightStatus
{
    measured,
    targetUnmeasured,
};

/** Method label carried by every preflight result (lexical, not a compiler). */
inline constexpr std::string_view kCogPreflightMethod =
    "lexical-preflight-v3-not-a-compiler";

/** One inventoried native-call spelling and its 1-based source lines. */
struct CogPreflightCall
{
    std::string name{};
    std::vector<std::int32_t> lines{};
};

/** A call spelling absent from the target's measured verb table. */
struct CogPreflightAbsentCall
{
    std::string name{};
    std::vector<std::int32_t> lines{};
    bool presentInOtherRetailTable{ false };
};

/** One inventoried `symbols` declaration (known and unknown types alike). */
struct CogPreflightDeclaration
{
    std::string type{};
    std::string semanticType{};
    std::string name{};
    std::string tail{};
    std::int32_t line{};
    /** For `message` declarations: the alias-resolved registered name. */
    std::optional<std::string> registeredMessage{};
};

/** One code label and its 1-based source lines. */
struct CogPreflightLabel
{
    std::string name{};
    std::vector<std::int32_t> lines{};
};

/** Provenance of one target's measured registry tables. */
struct CogRegistryInfo
{
    CogPreflightTarget target{ CogPreflightTarget::jk };
    bool measured{ false };
    std::string_view retailBinarySha256{};
    std::string_view registrySourceJsonSha256{};
    std::size_t verbCount{};
    std::size_t messageCount{};
    std::size_t globalFloatCount{};
};

struct CogPreflightOptions
{
    /** Strict mode turns any availability finding into a failure result. */
    bool strict{ false };
};

/**
 * The lexical-preflight record for one script. Availability findings are
 * `absentCalls` plus `absentMessages`; message-handler, label, and
 * identifier inventories are separate review findings and never upgrade a
 * spelling to available. This is a lexical preflight, not a compiler: it
 * proves nothing about argument counts/types, label reachability, resource
 * existence, bytecode layout, multiplayer checksums, or runtime behaviour.
 */
struct CogPreflightResult
{
    std::string fileName{};
    std::string sourceSha256{};
    CogPreflightTarget target{ CogPreflightTarget::jk };
    std::string targetName{};
    CogPreflightStatus status{ CogPreflightStatus::measured };
    bool measured{ false };
    std::string method{ std::string(kCogPreflightMethod) };
    std::optional<std::string> retailBinarySha256{};
    std::optional<std::string> registrySourceJsonSha256{};
    std::optional<std::string> flags{};
    bool hasSymbols{ false };
    bool hasCode{ false };
    std::vector<CogPreflightCall> calls{};
    std::vector<CogPreflightAbsentCall> absentCalls{};
    std::vector<CogPreflightDeclaration> declarations{};
    std::vector<CogPreflightDeclaration> unknownDeclarations{};
    std::vector<std::string> declaredMessages{};
    std::vector<std::string> absentMessages{};
    std::vector<CogPreflightDeclaration> nonstandardMessageBindings{};
    std::vector<CogPreflightLabel> labels{};
    std::vector<CogPreflightLabel> duplicateLabels{};
    std::vector<std::string> missingMessageHandlers{};
    std::vector<std::string> undeclaredMessageHandlers{};
    std::vector<std::string> undeclaredIdentifiers{};
    std::vector<std::string> literalResourceReferences{};
    std::vector<std::string> warnings{};
    std::vector<std::string> notes{};
    std::size_t availabilityFindings{};
    bool strictFailure{ false };
};

/** Stable target spelling used in reports (`jk` / `mots` / `ijim`). */
std::string_view cogPreflightTargetName(CogPreflightTarget target) noexcept;

/** Parses a target spelling, case-insensitively. */
std::optional<CogPreflightTarget> parseCogPreflightTarget(
    std::string_view name) noexcept;

/** Measured-registry provenance for one target (unmeasured for `ijim`). */
CogRegistryInfo cogRegistryInfo(CogPreflightTarget target) noexcept;

/**
 * True only when the spelling occurs in the target's measured verb table.
 * Lookup folds ASCII case. Never true for `ijim`, cogext, dynamic-POV, or
 * enhanced-engine spellings, which are absent from both retail tables.
 */
bool cogVerbAvailable(CogPreflightTarget target, std::string_view spelling) noexcept;

/** True only when the spelling occurs in the target's measured message table. */
bool cogMessageAvailable(CogPreflightTarget target, std::string_view spelling) noexcept;

/**
 * Recomputes the recorded digest over the generated registry tables.
 * False means a table was hand-patched without regenerating from the
 * measured registry JSON.
 */
bool cogRegistryTablesIntact();

/**
 * Runs lexical preflight over captured script text. The text is only read;
 * it is never rewritten, executed, or compiled. Call and message spellings
 * are checked against the measured registry for the explicit target; there
 * is no default target.
 */
CogPreflightResult runCogPreflight(
    std::string_view fileName,
    std::string_view text,
    CogPreflightTarget target,
    CogPreflightOptions options = {});

} // namespace sed::assets
