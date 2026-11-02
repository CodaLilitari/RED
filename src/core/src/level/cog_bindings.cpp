#include "sed/level/cog_bindings.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "sed/assets/jkl.hpp"
#include "sed/text.hpp"

namespace sed::level
{
namespace
{

std::string rawValueText(const CogValue& value)
{
    // Raw/imported values keep their exact wire spelling in `stringValue`;
    // typed values serialize canonically.
    if (!value.stringValue.empty())
    {
        return value.stringValue;
    }
    return value.asString();
}

struct SurfaceMap
{
    std::vector<SurfaceRef> ordered{};
    std::optional<std::string> error{};
};

SurfaceMap buildSurfaceMap(const Level& level)
{
    SurfaceMap map{};

    // A retained source whose text no longer matches its captured hash
    // cannot prove surface identity.
    if (!level.retainedJklSourceText.empty() && !level.retainedJklSourceHash.empty()
        && !sed::text::iequalsAscii(
            assets::jklSourceSha256Hex(level.retainedJklSourceText),
            level.retainedJklSourceHash))
    {
        map.error = "Retained JKL source no longer matches its captured hash; surface identity is unresolved.";
        return map;
    }

    std::size_t totalSurfaces = 0;
    for (const Sector& sector : level.sectors)
    {
        totalSurfaces += sector.surfaces.size();
    }

    if (!level.importedJklSurfaceRefs.empty())
    {
        map.ordered = level.importedJklSurfaceRefs;
        bool valid = map.ordered.size() == totalSurfaces;
        if (valid)
        {
            std::vector<SurfaceRef> sorted = map.ordered;
            std::sort(sorted.begin(), sorted.end(), [](const SurfaceRef& a, const SurfaceRef& b)
            {
                return a.sector != b.sector ? a.sector < b.sector : a.surface < b.surface;
            });
            for (std::size_t i = 0; i < sorted.size(); ++i)
            {
                const SurfaceRef ref = sorted[i];
                const bool inRange = ref.sector >= 0 && ref.surface >= 0
                    && static_cast<std::size_t>(ref.sector) < level.sectors.size()
                    && static_cast<std::size_t>(ref.surface)
                        < level.sectors[static_cast<std::size_t>(ref.sector)].surfaces.size();
                const bool duplicate = i > 0 && sorted[i - 1] == ref;
                if (!inRange || duplicate)
                {
                    valid = false;
                    break;
                }
            }
        }
        if (!valid)
        {
            map.error = "Imported surface identity does not match the current level surfaces.";
            map.ordered.clear();
        }
        return map;
    }

    map.ordered.reserve(totalSurfaces);
    for (std::size_t sectorIndex = 0; sectorIndex < level.sectors.size(); ++sectorIndex)
    {
        const Sector& sector = level.sectors[sectorIndex];
        for (std::size_t surfaceIndex = 0; surfaceIndex < sector.surfaces.size(); ++surfaceIndex)
        {
            map.ordered.push_back(SurfaceRef{
                static_cast<std::int32_t>(sectorIndex),
                static_cast<std::int32_t>(surfaceIndex) });
        }
    }
    return map;
}

CogBindingTarget resolveTarget(
    const Level& level,
    const assets::CogSymbol* symbol,
    const std::optional<std::string>& value,
    const SurfaceMap& surfaceMap)
{
    if (symbol == nullptr)
    {
        return CogBindingTarget{ CogReferenceKind::other, CogBindingTargetStatus::unknown,
            std::nullopt, std::nullopt, std::nullopt,
            "No unambiguous symbol type is available for this raw parameter." };
    }

    const CogReferenceKind kind = cogReferenceKindForSymbol(symbol->kind);
    if (!value)
    {
        return CogBindingTarget{ kind, CogBindingTargetStatus::missing,
            std::nullopt, std::nullopt, std::nullopt,
            "No authored positional value is present." };
    }
    if (kind == CogReferenceKind::other)
    {
        return CogBindingTarget{ kind, CogBindingTargetStatus::notReference };
    }

    std::int32_t index = 0;
    if (!tryParseCogIndex(*value, index))
    {
        return CogBindingTarget{ kind, CogBindingTargetStatus::invalid,
            std::nullopt, std::nullopt, std::nullopt,
            "Reference is not a complete decimal 32-bit index." };
    }
    if (index < 0)
    {
        return CogBindingTarget{ kind, CogBindingTargetStatus::null, index,
            std::nullopt, std::nullopt,
            index == -1 ? std::string{}
                        : std::string("Negative legacy sentinel retained; typed authoring only writes -1.") };
    }

    if (kind == CogReferenceKind::cog)
    {
        switch (checkCogIndex(*value, static_cast<std::int32_t>(level.cogs.size())))
        {
            case CogIndexStatus::level:
                return CogBindingTarget{ kind, CogBindingTargetStatus::level, index };
            case CogIndexStatus::staticUnchecked:
                return CogBindingTarget{ kind, CogBindingTargetStatus::staticUnchecked, index,
                    std::nullopt, std::nullopt,
                    "Static-world COG index; this placed level cannot resolve it." };
            case CogIndexStatus::unplacedUnchecked:
                return CogBindingTarget{ kind, CogBindingTargetStatus::unplacedUnchecked, index,
                    std::nullopt, std::nullopt,
                    "May identify a runtime/template instance beyond the placed COG list; unchecked." };
            default:
                return CogBindingTarget{ kind, CogBindingTargetStatus::invalid, index };
        }
    }

    if (kind == CogReferenceKind::surface)
    {
        if (surfaceMap.error)
        {
            return CogBindingTarget{ kind, CogBindingTargetStatus::unknown, index,
                std::nullopt, std::nullopt, *surfaceMap.error };
        }
        if (static_cast<std::size_t>(index) >= surfaceMap.ordered.size())
        {
            return CogBindingTarget{ kind, CogBindingTargetStatus::invalid, index,
                std::nullopt, std::nullopt,
                "Surface index exceeds the current serialized surface count." };
        }
        const SurfaceRef ref = surfaceMap.ordered[static_cast<std::size_t>(index)];
        return CogBindingTarget{ kind, CogBindingTargetStatus::level, index,
            ref.sector, ref.surface, std::string{} };
    }

    const std::int32_t count = kind == CogReferenceKind::thing
        ? static_cast<std::int32_t>(level.things.size())
        : static_cast<std::int32_t>(level.sectors.size());
    if (index < count)
    {
        return CogBindingTarget{ kind, CogBindingTargetStatus::level, index };
    }
    return CogBindingTarget{ kind, CogBindingTargetStatus::invalid, index,
        std::nullopt, std::nullopt,
        "Reference exceeds the current placed "
            + std::string(cogReferenceKindName(kind)) + " count "
            + std::to_string(count) + "." };
}

} // namespace

std::string_view cogReferenceKindName(const CogReferenceKind kind) noexcept
{
    switch (kind)
    {
        case CogReferenceKind::thing: return "thing";
        case CogReferenceKind::sector: return "sector";
        case CogReferenceKind::surface: return "surface";
        case CogReferenceKind::cog: return "cog";
        case CogReferenceKind::other: return "other";
    }
    return "other";
}

std::string_view cogScriptStatusName(const CogScriptStatus status) noexcept
{
    switch (status)
    {
        case CogScriptStatus::missing: return "missing";
        case CogScriptStatus::nameMismatch: return "nameMismatch";
        case CogScriptStatus::complete: return "complete";
        case CogScriptStatus::incomplete: return "incomplete";
    }
    return "missing";
}

bool tryParseCogIndex(const std::string_view text, std::int32_t& value) noexcept
{
    const std::string_view token = sed::text::trimView(text);
    if (token.empty())
    {
        return false;
    }
    const std::size_t firstDigit = (token.front() == '+' || token.front() == '-') ? 1u : 0u;
    if (firstDigit == token.size())
    {
        return false;
    }
    for (std::size_t i = firstDigit; i < token.size(); ++i)
    {
        if (token[i] < '0' || token[i] > '9')
        {
            return false;
        }
    }
    // std::from_chars rejects a leading '+', which the measured grammar allows.
    const std::string_view digits = token.front() == '+' ? token.substr(1) : token;
    const std::optional<std::int32_t> parsed = sed::text::parseInt<std::int32_t>(digits, 10);
    if (!parsed)
    {
        return false;
    }
    value = *parsed;
    return true;
}

CogIndexStatus checkCogIndex(const std::string_view text, const std::int32_t placedCogCount) noexcept
{
    std::int32_t value = 0;
    if (!tryParseCogIndex(text, value))
    {
        return CogIndexStatus::invalid;
    }
    if (value < 0)
    {
        return CogIndexStatus::null;
    }
    // The static world is addressed through bit 0x8000.
    if ((value & 0x8000) != 0)
    {
        return CogIndexStatus::staticUnchecked;
    }
    if (value < placedCogCount)
    {
        return CogIndexStatus::level;
    }
    // Template/runtime instances allocate after placed COGs, so the placed
    // list cannot prove an upper bound.
    return CogIndexStatus::unplacedUnchecked;
}

CogReferenceKind cogReferenceKindForSymbol(const assets::CogSchemaSymbolKind kind) noexcept
{
    switch (kind)
    {
        case assets::CogSchemaSymbolKind::thing: return CogReferenceKind::thing;
        case assets::CogSchemaSymbolKind::sector: return CogReferenceKind::sector;
        case assets::CogSchemaSymbolKind::surface: return CogReferenceKind::surface;
        case assets::CogSchemaSymbolKind::cog: return CogReferenceKind::cog;
        default: return CogReferenceKind::other;
    }
}

CogBindingDescription describeBindings(
    const Level& level,
    const std::int32_t cogIndex,
    const assets::CogSchema* const schema)
{
    if (cogIndex < 0 || static_cast<std::size_t>(cogIndex) >= level.cogs.size())
    {
        throw std::out_of_range("COG index is outside the placed COG list.");
    }

    const Cog& cog = level.cogs[static_cast<std::size_t>(cogIndex)];

    CogBindingDescription description{};
    description.cogIndex = cogIndex;
    description.scriptName = cog.name;
    description.actualParameterCount = static_cast<std::int32_t>(cog.values.size());
    description.scriptStatus = schema == nullptr
        ? CogScriptStatus::missing
        : !assets::cogSchemaNamesMatch(cog.name, schema->name)
            ? CogScriptStatus::nameMismatch
            : schema->isComplete() ? CogScriptStatus::complete : CogScriptStatus::incomplete;
    description.schemaComplete = description.scriptStatus == CogScriptStatus::complete;

    std::vector<const assets::CogSymbol*> levelValues{};
    if (description.schemaComplete && schema != nullptr)
    {
        levelValues = schema->levelValues();
        description.expectedParameterCount = static_cast<std::int32_t>(levelValues.size());
        description.parameterCountMatches =
            description.expectedParameterCount == description.actualParameterCount;
    }

    if (schema != nullptr)
    {
        description.diagnostics = schema->diagnostics;
    }
    if (description.scriptStatus == CogScriptStatus::missing)
    {
        description.diagnostics.push_back(assets::CogSchemaDiagnostic{
            "missingScript", std::nullopt,
            "Script schema is unavailable; raw values are not guessed to be references.",
            std::nullopt });
    }
    if (description.scriptStatus == CogScriptStatus::nameMismatch)
    {
        description.diagnostics.push_back(assets::CogSchemaDiagnostic{
            "scriptNameMismatch", std::nullopt,
            "The supplied script does not match this placed COG's script name.",
            std::nullopt });
    }
    if (description.parameterCountMatches && !*description.parameterCountMatches)
    {
        description.diagnostics.push_back(assets::CogSchemaDiagnostic{
            "parameterCountMismatch", std::nullopt,
            "The COG has " + std::to_string(description.actualParameterCount)
                + " values; its complete schema requires "
                + std::to_string(*description.expectedParameterCount) + ".",
            std::nullopt });
    }

    const SurfaceMap surfaceMap = buildSurfaceMap(level);
    const std::int32_t expected = description.expectedParameterCount.value_or(0);
    const std::int32_t rowCount = std::max(description.actualParameterCount, expected);
    description.rows.reserve(static_cast<std::size_t>(rowCount));
    for (std::int32_t i = 0; i < rowCount; ++i)
    {
        const assets::CogSymbol* symbol = description.schemaComplete
                && static_cast<std::size_t>(i) < levelValues.size()
            ? levelValues[static_cast<std::size_t>(i)]
            : nullptr;
        std::optional<std::string> rawValue{};
        if (i < description.actualParameterCount)
        {
            rawValue = rawValueText(cog.values[static_cast<std::size_t>(i)]);
        }

        CogBindingRow row{};
        row.cogIndex = cogIndex;
        row.parameterIndex = i;
        if (symbol != nullptr)
        {
            row.symbolName = symbol->name;
            row.symbolKind = symbol->kind;
        }
        row.rawValue = rawValue;
        row.symbol = symbol;
        row.target = resolveTarget(level, symbol, rawValue, surfaceMap);
        if (symbol != nullptr)
        {
            row.eventLinkCandidate =
                row.target.status == CogBindingTargetStatus::level
                && (row.target.kind == CogReferenceKind::thing
                    || row.target.kind == CogReferenceKind::sector
                    || row.target.kind == CogReferenceKind::surface)
                && symbol->effectiveLinkId >= 0
                && symbol->effectiveMask != 0u;
        }
        description.rows.push_back(std::move(row));
    }

    return description;
}

std::optional<std::vector<CogParameterDefinition>> resolveCogParameters(
    const Cog& cog,
    const assets::CogSchema* const schema)
{
    if (schema == nullptr || !schema->isComplete()
        || !assets::cogSchemaNamesMatch(cog.name, schema->name))
    {
        return std::nullopt;
    }

    const std::vector<const assets::CogSymbol*> levelValues = schema->levelValues();
    if (levelValues.size() != cog.values.size())
    {
        return std::nullopt;
    }

    std::vector<CogParameterDefinition> parameters{};
    parameters.reserve(levelValues.size());
    for (const assets::CogSymbol* symbol : levelValues)
    {
        parameters.push_back(CogParameterDefinition{
            symbol->name, cogReferenceKindForSymbol(symbol->kind) });
    }
    return parameters;
}

std::vector<CogReferenceIssue> checkCogReferences(
    const Level& level,
    const CogSchemaResolver& resolveSchema)
{
    std::vector<CogReferenceIssue> issues{};

    std::size_t surfaceCount = 0;
    for (const Sector& sector : level.sectors)
    {
        surfaceCount += sector.surfaces.size();
    }

    // Schemas are captured per call only; no cache survives this pass.
    std::unordered_map<std::string, std::optional<assets::CogSchema>> captured{};
    const auto schemaFor = [&](const Cog& cog) -> const assets::CogSchema*
    {
        const std::string key = sed::text::lowerAscii(cog.name);
        auto found = captured.find(key);
        if (found == captured.end())
        {
            found = captured.emplace(key, resolveSchema(cog.name)).first;
        }
        return found->second ? &*found->second : nullptr;
    };

    for (std::size_t cogIndex = 0; cogIndex < level.cogs.size(); ++cogIndex)
    {
        const Cog& cog = level.cogs[cogIndex];
        const std::optional<std::vector<CogParameterDefinition>> parameters =
            resolveCogParameters(cog, schemaFor(cog));
        if (!parameters)
        {
            issues.push_back(CogReferenceIssue{
                CogReferenceIssueSeverity::warning,
                static_cast<std::int32_t>(cogIndex), -1,
                "COG script schema unavailable; positional references were not checked." });
            continue;
        }

        for (std::size_t parameterIndex = 0; parameterIndex < parameters->size(); ++parameterIndex)
        {
            const CogParameterDefinition& parameter = (*parameters)[parameterIndex];
            if (parameter.kind == CogReferenceKind::other)
            {
                continue;
            }

            const std::string valueText = rawValueText(cog.values[parameterIndex]);
            std::optional<std::string> problem{};
            CogReferenceIssueSeverity severity = CogReferenceIssueSeverity::error;
            std::int32_t value = 0;
            if (!tryParseCogIndex(valueText, value))
            {
                problem = "must be a complete decimal 32-bit index";
            }
            else if (parameter.kind == CogReferenceKind::cog)
            {
                const CogIndexStatus status = checkCogIndex(
                    valueText, static_cast<std::int32_t>(level.cogs.size()));
                if (status == CogIndexStatus::staticUnchecked)
                {
                    severity = CogReferenceIssueSeverity::warning;
                    problem = "references a static-world COG; its runtime bounds are unverified";
                }
                else if (status == CogIndexStatus::unplacedUnchecked)
                {
                    severity = CogReferenceIssueSeverity::warning;
                    problem = "references an unplaced/runtime COG; the placed list cannot establish its bounds";
                }
            }
            else
            {
                const std::size_t count = parameter.kind == CogReferenceKind::thing
                    ? level.things.size()
                    : parameter.kind == CogReferenceKind::sector
                        ? level.sectors.size()
                        : surfaceCount;
                if (value >= 0 && static_cast<std::size_t>(value) >= count)
                {
                    problem = "index " + std::to_string(value)
                        + " is outside the level's "
                        + std::string(cogReferenceKindName(parameter.kind))
                        + " range (count " + std::to_string(count) + ")";
                }
            }

            if (problem)
            {
                issues.push_back(CogReferenceIssue{
                    severity,
                    static_cast<std::int32_t>(cogIndex),
                    static_cast<std::int32_t>(parameterIndex),
                    "COG " + std::to_string(cogIndex) + " parameter " + parameter.name
                        + ": " + *problem });
            }
        }
    }

    return issues;
}

bool isValidAuthoredCogScriptName(const std::string_view name) noexcept
{
    if (name.empty() || name.size() > kMaxAuthoredCogScriptNameLength)
    {
        return false;
    }
    if (name.size() <= 4 || !sed::text::iendsWithAscii(name, ".cog"))
    {
        return false;
    }
    for (const char ch : name)
    {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte < 0x21 || byte > 0x7E)
        {
            return false;
        }
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?'
            || ch == '"' || ch == '<' || ch == '>' || ch == '|')
        {
            return false;
        }
    }
    return true;
}

std::string cogSchemaSha256Hex(const assets::CogSchema& schema)
{
    return assets::jklSourceSha256Hex(schema.sourceText);
}

namespace
{

CogMutationFailure mutationFailure(std::string code, std::string message)
{
    return CogMutationFailure{ std::move(code), std::move(message) };
}

std::optional<assets::CogSchema> resolveExistingCogSchema(
    const CogSchemaResolver& resolver,
    const std::string_view cogName)
{
    if (!resolver)
    {
        return std::nullopt;
    }
    try
    {
        return resolver(cogName);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::size_t totalPlacedCogParameters(const Level& level) noexcept
{
    std::size_t total = 0;
    for (const Cog& cog : level.cogs)
    {
        total += cog.values.size();
    }
    return total;
}

const CogNamedBinding* findNamedBinding(
    const std::vector<CogNamedBinding>& bindings,
    const std::string_view symbolName)
{
    for (const CogNamedBinding& binding : bindings)
    {
        if (sed::text::iequalsAscii(binding.symbolName, symbolName))
        {
            return &binding;
        }
    }
    return nullptr;
}

std::optional<CogMutationFailure> validateReferenceTarget(
    const Level& level,
    const CogReferenceKind kind,
    const std::int32_t targetIndex,
    const SurfaceMap& surfaceMap)
{
    if (targetIndex < -1)
    {
        return mutationFailure("invalidCogTarget",
            "Target index " + std::to_string(targetIndex)
                + " is below -1; only -1 authors null.");
    }
    if (kind == CogReferenceKind::surface)
    {
        if (surfaceMap.error)
        {
            return mutationFailure("unresolvedSurfaceIdentity", *surfaceMap.error);
        }
        if (targetIndex == -1)
        {
            return std::nullopt;
        }
        if (static_cast<std::size_t>(targetIndex) < surfaceMap.ordered.size())
        {
            return std::nullopt;
        }
        return mutationFailure("invalidCogTarget",
            "Surface target " + std::to_string(targetIndex)
                + " is outside the serialized surface count "
                + std::to_string(surfaceMap.ordered.size()) + ".");
    }
    if (targetIndex == -1)
    {
        return std::nullopt;
    }
    if (kind == CogReferenceKind::cog)
    {
        if (targetIndex >= 0
            && static_cast<std::size_t>(targetIndex) < level.cogs.size())
        {
            return std::nullopt;
        }
        return mutationFailure("invalidCogTarget",
            "COG target " + std::to_string(targetIndex)
                + " does not identify a placed COG or -1; static and runtime COG indices are not authored here.");
    }
    const std::size_t count = kind == CogReferenceKind::thing
        ? level.things.size()
        : level.sectors.size();
    if (static_cast<std::size_t>(targetIndex) < count)
    {
        return std::nullopt;
    }
    return mutationFailure("invalidCogTarget",
        "Target " + std::to_string(targetIndex) + " is outside the placed "
            + std::string(cogReferenceKindName(kind)) + " count "
            + std::to_string(count) + ".");
}

CogValue authoredReferenceValue(
    const assets::CogSymbol& symbol,
    const std::int32_t targetIndex)
{
    CogValue value{};
    value.name = symbol.name;
    value.cogType = symbol.cogType();
    value.type = cogReferenceKindForSymbol(symbol.kind) == CogReferenceKind::cog
        ? ValueType::integer
        : ValueType::ptr;
    value.intValue = targetIndex;
    value.stringValue = std::to_string(targetIndex);
    value.local = false;
    value.description = symbol.description.value_or(std::string{});
    value.explicitLinkId = symbol.explicitLinkId;
    value.explicitMask = symbol.explicitMask;
    value.noLink = symbol.noLink;
    value.mask = symbol.explicitMask
        ? static_cast<std::int32_t>(*symbol.explicitMask)
        : 0;
    return value;
}

} // namespace

bool PreparedAddCog::apply(Level& level) const
{
    if (!ok())
    {
        return false;
    }
    if (level.cogs.size() != static_cast<std::size_t>(newCogIndex))
    {
        return false;
    }
    if (!level.canAllocateIds(1))
    {
        return false;
    }
    Cog& created = level.newCog();
    created.name = scriptName;
    created.values = preparedCog.values;
    return true;
}

bool PreparedRebindCogReference::apply(Level& level) const
{
    if (!ok())
    {
        return false;
    }
    if (cogIndex < 0 || static_cast<std::size_t>(cogIndex) >= level.cogs.size())
    {
        return false;
    }
    Cog& cog = level.cogs[static_cast<std::size_t>(cogIndex)];
    if (parameterIndex < 0
        || static_cast<std::size_t>(parameterIndex) >= cog.values.size())
    {
        return false;
    }
    CogValue& value = cog.values[static_cast<std::size_t>(parameterIndex)];
    if (rawValueText(value) != expectedExistingRaw)
    {
        return false;
    }
    value.intValue = targetIndex;
    value.stringValue = std::to_string(targetIndex);
    return true;
}

PreparedAddCog prepareAddCog(
    const Level& level,
    const std::string_view scriptName,
    const assets::CogSchema* const schema,
    const std::vector<CogNamedBinding>& bindings,
    const CogSchemaResolver& resolveExistingSchema,
    const std::optional<std::string_view> expectedScriptSha256)
{
    PreparedAddCog prepared{};
    prepared.scriptName = std::string(scriptName);
    prepared.newCogIndex = static_cast<std::int32_t>(level.cogs.size());
    const auto fail = [&prepared](std::string code, std::string message)
    {
        prepared.failure = mutationFailure(std::move(code), std::move(message));
        return prepared;
    };

    if (!isValidAuthoredCogScriptName(scriptName))
    {
        return fail("invalidCogScriptName",
            "New COG script name '" + std::string(scriptName)
                + "' must be an ASCII .cog basename of at most 31 characters.");
    }
    if (schema == nullptr || !schema->isComplete()
        || !assets::cogSchemaNamesMatch(scriptName, schema->name))
    {
        return fail("incompleteCogSchema",
            "A complete schema matching the new COG script is required; missing or incomplete schemas do not type raw integers.");
    }
    prepared.scriptSha256 = cogSchemaSha256Hex(*schema);
    if (expectedScriptSha256
        && !sed::text::iequalsAscii(*expectedScriptSha256, prepared.scriptSha256))
    {
        return fail("scriptHashMismatch",
            "The captured script bytes differ from the reviewed SHA-256; no COG was prepared.");
    }

    const std::vector<const assets::CogSymbol*> levelValues = schema->levelValues();
    if (bindings.size() > kMaxAuthoredCogBindings
        || levelValues.size() > kMaxAuthoredCogBindings)
    {
        return fail("cogBindingBudget",
            "A new COG accepts at most 256 bindings and the schema declares "
                + std::to_string(levelValues.size()) + " level values.");
    }
    if (bindings.size() != levelValues.size())
    {
        return fail("cogParameterCountMismatch",
            "The schema requires " + std::to_string(levelValues.size())
                + " bindings but " + std::to_string(bindings.size())
                + " were supplied; no defaults are inferred.");
    }
    for (std::size_t i = 0; i < bindings.size(); ++i)
    {
        for (std::size_t j = i + 1; j < bindings.size(); ++j)
        {
            if (sed::text::iequalsAscii(bindings[i].symbolName, bindings[j].symbolName))
            {
                return fail("duplicateCogBinding",
                    "Binding for symbol '" + bindings[i].symbolName + "' was supplied more than once.");
            }
        }
    }
    for (const assets::CogSymbol* symbol : levelValues)
    {
        const CogNamedBinding* binding = findNamedBinding(bindings, symbol->name);
        if (binding == nullptr)
        {
            return fail("cogSymbolMismatch",
                "No binding was supplied for symbol '" + symbol->name + "'.");
        }
        if (binding->kind != cogReferenceKindForSymbol(symbol->kind))
        {
            return fail("cogSymbolMismatch",
                "Binding '" + symbol->name + "' kind does not match its declared kind.");
        }
    }
    for (const CogNamedBinding& binding : bindings)
    {
        bool known = false;
        for (const assets::CogSymbol* symbol : levelValues)
        {
            if (sed::text::iequalsAscii(binding.symbolName, symbol->name))
            {
                known = true;
                break;
            }
        }
        if (!known)
        {
            return fail("cogSymbolMismatch",
                "Binding '" + binding.symbolName + "' does not name a level value of the schema.");
        }
    }
    for (const assets::CogSymbol* symbol : levelValues)
    {
        if (cogReferenceKindForSymbol(symbol->kind) == CogReferenceKind::other)
        {
            return fail("unsupportedCogBindingType",
                "Symbol '" + symbol->name + "' cannot be bound for a new COG; only thing, sector, surface, and cog values are supported, so other kinds must be script-local constants.");
        }
    }

    const SurfaceMap surfaceMap = buildSurfaceMap(level);
    for (const assets::CogSymbol* symbol : levelValues)
    {
        const CogNamedBinding* binding = findNamedBinding(bindings, symbol->name);
        if (const std::optional<CogMutationFailure> problem = validateReferenceTarget(
                level, cogReferenceKindForSymbol(symbol->kind), binding->targetIndex, surfaceMap))
        {
            prepared.failure = *problem;
            return prepared;
        }
    }

    if (level.cogs.size() > kMaxCogsForAppendClosure
        || totalPlacedCogParameters(level) > kMaxCogParametersForAppendClosure)
    {
        return fail("cogClosureBudget",
            "Appending a COG would inspect more than 4096 placed COGs or 100000 parameters.");
    }
    const std::int32_t placedCogCount = static_cast<std::int32_t>(level.cogs.size());
    for (std::size_t cogIndex = 0; cogIndex < level.cogs.size(); ++cogIndex)
    {
        const Cog& existing = level.cogs[cogIndex];
        const std::optional<assets::CogSchema> existingSchema =
            resolveExistingCogSchema(resolveExistingSchema, existing.name);
        if (!existingSchema || !existingSchema->isComplete()
            || !assets::cogSchemaNamesMatch(existing.name, existingSchema->name))
        {
            return fail("incompleteExistingCogSchema",
                "Existing COG " + std::to_string(cogIndex) + " ('" + existing.name
                    + "') has no matching complete schema, so appending cannot be proven safe.");
        }
        const std::vector<const assets::CogSymbol*> existingValues = existingSchema->levelValues();
        if (existingValues.size() != existing.values.size())
        {
            return fail("existingCogParameterCountMismatch",
                "Existing COG " + std::to_string(cogIndex) + " ('" + existing.name + "') has "
                    + std::to_string(existing.values.size()) + " values but its schema requires "
                    + std::to_string(existingValues.size()) + ".");
        }
        for (std::size_t parameterIndex = 0; parameterIndex < existingValues.size(); ++parameterIndex)
        {
            if (cogReferenceKindForSymbol(existingValues[parameterIndex]->kind) != CogReferenceKind::cog)
            {
                continue;
            }
            const std::string raw = rawValueText(existing.values[parameterIndex]);
            std::int32_t parsed = 0;
            if (!tryParseCogIndex(raw, parsed))
            {
                return fail("existingCogRuntimeReference",
                    "Existing COG " + std::to_string(cogIndex) + " parameter "
                        + existingValues[parameterIndex]->name + " value '" + raw
                        + "' cannot be proven to survive an append. The value is not assumed invalid; resolve it explicitly before appending.");
            }
            const CogIndexStatus before = checkCogIndex(raw, placedCogCount);
            const CogIndexStatus after = checkCogIndex(raw, placedCogCount + 1);
            if (before == CogIndexStatus::unplacedUnchecked && after == CogIndexStatus::level)
            {
                return fail("existingCogReferenceCapture",
                    "Existing COG " + std::to_string(cogIndex) + " parameter "
                        + existingValues[parameterIndex]->name + " value '" + raw
                        + "' would be captured by the appended COG. The value is not assumed invalid; resolve it explicitly before appending.");
            }
            if (before == CogIndexStatus::unplacedUnchecked)
            {
                return fail("existingCogRuntimeReference",
                    "Existing COG " + std::to_string(cogIndex) + " parameter "
                        + existingValues[parameterIndex]->name + " value '" + raw
                        + "' stays outside the placed COG list, so an append could shift later runtime allocation. The value is not assumed invalid; resolve it explicitly before appending.");
            }
        }
    }

    prepared.preparedCog.name = std::string(scriptName);
    prepared.preparedCog.values.reserve(levelValues.size());
    for (const assets::CogSymbol* symbol : levelValues)
    {
        const CogNamedBinding* binding = findNamedBinding(bindings, symbol->name);
        prepared.preparedCog.values.push_back(authoredReferenceValue(*symbol, binding->targetIndex));
    }
    return prepared;
}

PreparedRebindCogReference prepareRebindCogReference(
    const Level& level,
    const std::int32_t cogIndex,
    const std::int32_t parameterIndex,
    const std::string_view expectedSymbol,
    const assets::CogSchemaSymbolKind expectedKind,
    const std::int32_t targetIndex,
    const assets::CogSchema* const schema,
    const std::optional<std::string_view> expectedScriptSha256)
{
    PreparedRebindCogReference prepared{};
    prepared.cogIndex = cogIndex;
    prepared.parameterIndex = parameterIndex;
    prepared.targetIndex = targetIndex;
    const auto fail = [&prepared](std::string code, std::string message)
    {
        prepared.failure = mutationFailure(std::move(code), std::move(message));
        return prepared;
    };

    if (cogIndex < 0 || static_cast<std::size_t>(cogIndex) >= level.cogs.size())
    {
        return fail("cogParameterOutOfRange",
            "COG index " + std::to_string(cogIndex) + " is outside the placed COG list.");
    }
    const Cog& cog = level.cogs[static_cast<std::size_t>(cogIndex)];
    if (schema == nullptr || !schema->isComplete()
        || !assets::cogSchemaNamesMatch(cog.name, schema->name))
    {
        return fail("incompleteCogSchema",
            "A complete schema matching COG '" + cog.name + "' is required for a typed rebind.");
    }
    prepared.scriptSha256 = cogSchemaSha256Hex(*schema);
    if (expectedScriptSha256
        && !sed::text::iequalsAscii(*expectedScriptSha256, prepared.scriptSha256))
    {
        return fail("scriptHashMismatch",
            "The captured script bytes differ from the reviewed SHA-256; no rebind was prepared.");
    }
    const std::vector<const assets::CogSymbol*> levelValues = schema->levelValues();
    if (levelValues.size() != cog.values.size())
    {
        return fail("cogParameterCountMismatch",
            "COG '" + cog.name + "' has " + std::to_string(cog.values.size())
                + " values but its complete schema requires " + std::to_string(levelValues.size())
                + "; values are never padded or discarded.");
    }
    if (parameterIndex < 0
        || static_cast<std::size_t>(parameterIndex) >= levelValues.size())
    {
        return fail("cogParameterOutOfRange",
            "Parameter index " + std::to_string(parameterIndex) + " is outside the "
                + std::to_string(levelValues.size()) + " schema values.");
    }
    const assets::CogSymbol& symbol = *levelValues[static_cast<std::size_t>(parameterIndex)];
    if (!sed::text::iequalsAscii(expectedSymbol, symbol.name) || expectedKind != symbol.kind)
    {
        return fail("cogSymbolMismatch",
            "Parameter " + std::to_string(parameterIndex) + " declares '" + symbol.name
                + "' but the caller expected '" + std::string(expectedSymbol) + "'.");
    }
    const CogReferenceKind kind = cogReferenceKindForSymbol(symbol.kind);
    if (kind == CogReferenceKind::other)
    {
        return fail("unsupportedCogBindingType",
            "Symbol '" + symbol.name + "' cannot be rebound as a reference; only thing, sector, surface, and cog values are supported.");
    }
    const std::string existingRaw = rawValueText(cog.values[static_cast<std::size_t>(parameterIndex)]);
    prepared.expectedExistingRaw = existingRaw;
    if (kind == CogReferenceKind::cog)
    {
        const CogIndexStatus existingStatus = checkCogIndex(
            existingRaw, static_cast<std::int32_t>(level.cogs.size()));
        if (existingStatus == CogIndexStatus::staticUnchecked
            || existingStatus == CogIndexStatus::unplacedUnchecked)
        {
            return fail("unsupportedCogReference",
                "Existing COG value '" + existingRaw
                    + "' is a static or runtime reference and is refused rather than reinterpreted.");
        }
    }
    const SurfaceMap surfaceMap = buildSurfaceMap(level);
    if (const std::optional<CogMutationFailure> problem =
            validateReferenceTarget(level, kind, targetIndex, surfaceMap))
    {
        prepared.failure = *problem;
        return prepared;
    }
    return prepared;
}

std::optional<CogMutationFailure> validateThingAppend(
    const Level& level,
    const CogSchemaResolver& resolveSchema)
{
    const std::string_view scopeNote =
        " The guard analyses positional symbols only; it does not analyse script code, computed indices, or template-attached scripts.";
    if (level.cogs.size() > kMaxCogsForAppendClosure)
    {
        return mutationFailure("thingAppendCogBudget",
            "Appending a thing would inspect more than 4096 placed COGs." + std::string(scopeNote));
    }
    if (totalPlacedCogParameters(level) > kMaxCogParametersForAppendClosure)
    {
        return mutationFailure("thingAppendCogBudget",
            "Appending a thing would inspect more than 100000 COG parameters." + std::string(scopeNote));
    }
    for (std::size_t cogIndex = 0; cogIndex < level.cogs.size(); ++cogIndex)
    {
        const Cog& cog = level.cogs[cogIndex];
        const std::optional<assets::CogSchema> schema =
            resolveExistingCogSchema(resolveSchema, cog.name);
        if (!schema || !schema->isComplete()
            || !assets::cogSchemaNamesMatch(cog.name, schema->name))
        {
            return mutationFailure("incompleteExistingCogSchema",
                "COG " + std::to_string(cogIndex) + " ('" + cog.name
                    + "') has no matching complete schema, so its thing references cannot be checked before an append."
                    + std::string(scopeNote));
        }
        const std::vector<const assets::CogSymbol*> levelValues = schema->levelValues();
        if (levelValues.size() != cog.values.size())
        {
            return mutationFailure("existingCogParameterCountMismatch",
                "COG " + std::to_string(cogIndex) + " ('" + cog.name + "') has "
                    + std::to_string(cog.values.size()) + " values but its schema requires "
                    + std::to_string(levelValues.size()) + "." + std::string(scopeNote));
        }
        for (std::size_t parameterIndex = 0; parameterIndex < levelValues.size(); ++parameterIndex)
        {
            if (cogReferenceKindForSymbol(levelValues[parameterIndex]->kind) != CogReferenceKind::thing)
            {
                continue;
            }
            const std::string raw = rawValueText(cog.values[parameterIndex]);
            std::int32_t parsed = 0;
            if (!tryParseCogIndex(raw, parsed) || raw != std::to_string(parsed))
            {
                return mutationFailure("invalidExistingThingReference",
                    "COG " + std::to_string(cogIndex) + " parameter "
                        + levelValues[parameterIndex]->name + " value '" + raw
                        + "' is not the canonical -1 or a placed thing index." + std::string(scopeNote));
            }
            if (parsed == -1)
            {
                continue;
            }
            if (parsed < -1)
            {
                return mutationFailure("invalidExistingThingReference",
                    "COG " + std::to_string(cogIndex) + " parameter "
                        + levelValues[parameterIndex]->name + " value '" + raw
                        + "' is not the canonical -1 or a placed thing index." + std::string(scopeNote));
            }
            if (static_cast<std::size_t>(parsed) >= level.things.size())
            {
                return mutationFailure("existingThingRuntimeReference",
                    "COG " + std::to_string(cogIndex) + " parameter "
                        + levelValues[parameterIndex]->name + " value '" + raw
                        + "' does not identify a currently placed thing, so appending could capture a runtime reference."
                        + std::string(scopeNote));
            }
        }
    }
    return std::nullopt;
}

} // namespace sed::level
