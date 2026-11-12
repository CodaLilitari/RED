#pragma once

#include "sed/level/package.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace sed::level::detail
{

/** Borrowed reference strings are valid only during the reference callback. */
struct ReferenceView
{
    std::string_view folder;
    std::string_view name;
    std::string_view relation;
};

struct ReferenceCallbacks
{
    /** False stops discovery before another reference is emitted. */
    std::function<bool(ReferenceView)> reference;
    /** Optional admission, called before each physical line or token visit. */
    std::function<bool()> work;
    /** Existing package scope labels; all labels have static storage. */
    std::function<void(std::string_view)> scope;
    /** Additional inspection coverage labels; all labels have static storage. */
    std::function<void(std::string_view)> coverage;
    /** Optional COG admission before line/token preflight and schema parsing. */
    std::function<bool(std::string_view)> beforeCogParse;
};

/** Preserve the package's lexical rules and ordering without a reference vector.
 * False means a callback stopped discovery. Header/control lines incur line work
 * but do not tokenize. An empty source has no physical lines, and a trailing
 * newline does not create an additional empty line.
 */
bool visitRootReferences(std::string_view input,
    std::span<const PackageExtraResource> extras, const ReferenceCallbacks& callbacks);

/** Resource discovery only, not asset grammar validation. COG line/token work is
 * admitted before the existing parser; other supported readers stream token views.
 * Caller exceptions propagate, allowing the package adapter to retain its existing
 * PUP/SPR/SND exception boundary independently of the inspector's policy.
 */
bool visitChildReferences(std::string_view name, std::string_view bytes,
    const ReferenceCallbacks& callbacks);

/** Package normalization deliberately retains leading slashes. GobArchive's
 * name-based read normalization is separate and must not be substituted here.
 */
std::string normalizeReferenceName(std::string_view name);
std::string_view referenceBaseName(std::string_view name);
bool referenceEndsWithCi(std::string_view text, std::string_view suffix);

struct ArchiveMatch
{
    std::size_t ordinal;
    std::string_view tier;
};

class ReferenceArchiveIndex
{
public:
    /** Owns at most two string keys per physical row. The caller supplies and
     * admits these normalized strings before copying; insertion moves them.
     * Duplicate normalized keys retain the first row and basename aliases remain
     * ambiguous even when their bytes or normalized names are equal.
     */
    void add(std::string normalizedName, std::string normalizedBasename,
        std::size_t ordinal);

    /** Keys are already normalized. Transparent lookup allocates no strings. */
    std::optional<ArchiveMatch> exact(std::string_view normalizedFolderKey,
        std::string_view normalizedNameKey) const;
    std::optional<ArchiveMatch> basename(std::string_view normalizedBasename,
        bool& ambiguous) const;

private:
    struct BasenameRows
    {
        std::size_t first;
        bool ambiguous{false};
    };

    std::map<std::string, std::size_t, std::less<>> byKey_;
    std::map<std::string, BasenameRows, std::less<>> byBase_;
};

} // namespace sed::level::detail
