
#include "test_common.hpp"

#include "sed/assets/cog_schema.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/level/cog_bindings.hpp"

#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace sed::tests
{
namespace
{

// Slice 2 is read-only: A-15..A-22 assert every describe-side expectation
// (statuses, targets, candidacy, untouched raw values). Assertions that
// need PrepareRebind/Apply (refusal codes, undo, save/reopen) belong to
// the guarded-mutation step and are intentionally not claimed here.

sed::CogValue rawParam(const std::string& text, const std::size_t index)
{
    sed::CogValue value{};
    value.name = "param" + std::to_string(index);
    value.type = sed::ValueType::str;
    value.cogType = sed::CogType::unk;
    value.stringValue = text;
    return value;
}

sed::Cog makeCog(const std::string& name, const std::initializer_list<std::string> texts)
{
    sed::Cog cog{};
    cog.name = name;
    std::size_t index = 0;
    for (const std::string& text : texts)
    {
        cog.values.push_back(rawParam(text, index++));
    }
    return cog;
}

// Two things, two sectors (one surface, then two surfaces), two COGs.
sed::Level makeBindingLevel(sed::Cog fixture)
{
    sed::Level level{};
    level.newThing();
    level.newThing();
    sed::Sector& first = level.newSector();
    first.surfaces.push_back(sed::Surface{});
    sed::Sector& second = level.newSector();
    second.surfaces.push_back(sed::Surface{});
    second.surfaces.push_back(sed::Surface{});
    level.cogs.push_back(std::move(fixture));
    sed::Cog other{};
    other.name = "other.cog";
    level.cogs.push_back(std::move(other));
    return level;
}

sed::assets::CogSchema fixtureSchema(const std::string& declarations)
{
    return sed::assets::parseCogSchema("fixture.cog", "symbols\n" + declarations + "\nend");
}

bool hasDiagnosticCode(const sed::level::CogBindingDescription& description, const std::string_view code)
{
    for (const sed::assets::CogSchemaDiagnostic& diagnostic : description.diagnostics)
    {
        if (diagnostic.code == code)
        {
            return true;
        }
    }
    return false;
}

std::vector<std::string> rawTexts(const sed::Cog& cog)
{
    std::vector<std::string> texts{};
    for (const sed::CogValue& value : cog.values)
    {
        texts.push_back(value.stringValue);
    }
    return texts;
}

} // namespace

void runCogBindingTests()
{
    using sed::assets::CogSchemaSymbolKind;
    using sed::level::CogBindingTargetStatus;
    using sed::level::CogReferenceKind;

    check(sed::level::kCogEventLinkSemantics
            == "OpenJKDF2 reference-symbol modifiers; static candidate only, not runtime or multiplayer proof",
        "CogBindings.disclaimer");

    // A-07 (derived from Coda CogSchemaResolutionTests): archive precedence,
    // ambiguity refusal without fallthrough, cog/ preference, basename fallback.
    {
        const std::string sectorScript = "symbols\nsector target\nend\n";
        const std::string thingScript = "symbols\nthing target\nend\n";
        using sed::assets::CogScriptArchive;

        const auto duplicate = sed::assets::resolveCogSchema("original.cog",
            std::vector<CogScriptArchive>{ CogScriptArchive{
                { "cog/original.cog", sectorScript }, { "cog/original.cog", thingScript } } });
        check(!duplicate, "CogBindings.A07.duplicateUnresolved");
        check(!sed::assets::resolveCogSchema("ORIGINAL.COG",
                std::vector<CogScriptArchive>{ CogScriptArchive{
                    { "cog/original.cog", sectorScript }, { "cog/original.cog", thingScript } } }),
            "CogBindings.A07.duplicateCaseInsensitive");

        const std::vector<CogScriptArchive> ambiguousHigh{
            CogScriptArchive{ { "a/original.cog", sectorScript }, { "b/original.cog", thingScript } },
            CogScriptArchive{ { "cog/original.cog", sectorScript } },
        };
        check(!sed::assets::resolveCogSchema("original.cog", ambiguousHigh),
            "CogBindings.A07.ambiguousBlocksFallthrough");
        check(!sed::assets::resolveCogSchema("original.cog", ambiguousHigh),
            "CogBindings.A07.ambiguousStable");

        const auto standardWins = sed::assets::resolveCogSchema("original.cog",
            std::vector<CogScriptArchive>{ CogScriptArchive{
                { "alternate/original.cog", thingScript }, { "cog/original.cog", sectorScript } } });
        check(standardWins && standardWins->isComplete()
                && standardWins->levelValues().size() == 1
                && standardWins->levelValues()[0]->kind == CogSchemaSymbolKind::sector,
            "CogBindings.A07.standardFolderWins");

        const auto exact = sed::assets::resolveCogSchema("original.cog",
            std::vector<CogScriptArchive>{ CogScriptArchive{ { "original.cog", sectorScript } } });
        check(exact && exact->name == "original.cog", "CogBindings.A07.exactBasename");
        const auto nestedRequest = sed::assets::resolveCogSchema("original.cog",
            std::vector<CogScriptArchive>{ CogScriptArchive{ { "elsewhere/original.cog", sectorScript } } });
        check(nestedRequest && nestedRequest->levelValues().size() == 1,
            "CogBindings.A07.uniqueBasenameFallback");
        const auto pathed = sed::assets::resolveCogSchema("elsewhere\\original.cog",
            std::vector<CogScriptArchive>{ CogScriptArchive{ { "elsewhere/original.cog", sectorScript } } });
        check(pathed && pathed->name == "elsewhere\\original.cog",
            "CogBindings.A07.pathRequestRetained");

        const std::vector<CogScriptArchive> priority{
            CogScriptArchive{ { "cog/unrelated.cog", thingScript } },
            CogScriptArchive{ { "cog/original.cog", sectorScript } },
            CogScriptArchive{ { "cog/original.cog", thingScript } },
        };
        const auto firstWins = sed::assets::resolveCogSchema("original.cog", priority);
        check(firstWins && firstWins->levelValues().size() == 1
                && firstWins->levelValues()[0]->kind == CogSchemaSymbolKind::sector,
            "CogBindings.A07.firstArchivePriority");
        check(!sed::assets::resolveCogSchema("missing.cog", priority),
            "CogBindings.A07.missingUnresolved");
    }

    // A-08: incomplete schemas never supply typed parameters, even when the
    // permissive count happens to match.
    {
        const std::vector<std::string> layouts{
            "symbols\nfuturetype unseen\nsector target\nend\n",
            "symbols\nsector target\n",
            "symbols\nsector target\nsector TARGET\nend\n",
        };
        for (const std::string& source : layouts)
        {
            const sed::assets::CogSchema schema = sed::assets::parseCogSchema("original.cog", source);
            check(!schema.isComplete(), "CogBindings.A08.incomplete");
            sed::Cog cog{};
            cog.name = "original.cog";
            std::size_t index = 0;
            for (const sed::assets::CogSymbol* symbol : schema.levelValues())
            {
                (void)symbol;
                cog.values.push_back(rawParam("999", index++));
            }
            check(!sed::level::resolveCogParameters(cog, &schema), "CogBindings.A08.noTypedParameters");

            sed::Level level{};
            level.cogs.push_back(cog);
            const auto issues = sed::level::checkCogReferences(level,
                [&](const std::string_view) -> std::optional<sed::assets::CogSchema>
                {
                    return schema;
                });
            check(issues.size() == 1
                    && issues[0].severity == sed::level::CogReferenceIssueSeverity::warning
                    && issues[0].cogIndex == 0 && issues[0].parameterIndex == -1
                    && issues[0].message.find("schema unavailable") != std::string::npos,
                "CogBindings.A08.singleWarning");
        }
    }

    // A-09: complete schema with a count mismatch supplies no typed parameters.
    {
        const sed::assets::CogSchema schema = sed::assets::parseCogSchema(
            "original.cog", "symbols\nsector target\nend\n");
        check(schema.isComplete(), "CogBindings.A09.complete");
        for (const std::size_t count : { std::size_t{ 0 }, std::size_t{ 2 } })
        {
            sed::Cog cog{};
            cog.name = "original.cog";
            for (std::size_t i = 0; i < count; ++i)
            {
                cog.values.push_back(rawParam("999", i));
            }
            check(!sed::level::resolveCogParameters(cog, &schema),
                "CogBindings.A09.countMismatchRefused");
        }
    }

    // A-10: complete exact schema retains kinds in order; locals and
    // messages stay out of the level-value contract.
    {
        const sed::assets::CogSchema schema = sed::assets::parseCogSchema("original.cog",
            "symbols\nthing a\nsector b\nsurface c\ncog d\nint e\nthing private local\nmessage startup\nend\n");
        check(schema.isComplete(), "CogBindings.A10.complete");
        sed::Cog cog{};
        cog.name = "original.cog";
        for (std::size_t i = 0; i < 5; ++i)
        {
            cog.values.push_back(rawParam("0", i));
        }
        const auto parameters = sed::level::resolveCogParameters(cog, &schema);
        check(parameters && parameters->size() == 5, "CogBindings.A10.count");
        if (parameters && parameters->size() == 5)
        {
            check((*parameters)[0].kind == CogReferenceKind::thing
                    && (*parameters)[1].kind == CogReferenceKind::sector
                    && (*parameters)[2].kind == CogReferenceKind::surface
                    && (*parameters)[3].kind == CogReferenceKind::cog
                    && (*parameters)[4].kind == CogReferenceKind::other,
                "CogBindings.A10.kinds");
            check((*parameters)[0].name == "a" && (*parameters)[4].name == "e",
                "CogBindings.A10.names");
        }
    }

    // A-11: references and potential event links are reported separately.
    {
        sed::Level level = makeBindingLevel(makeCog("fixture.cog",
            { "0", "1", "2", "1", "-1", "0", "1" }));
        const sed::assets::CogSchema schema = fixtureSchema(
            "thing a nolink\nthing b linkid=7\nsurface face\ncog other\nthing empty\nsector muted mask=0\nint scalar");
        const sed::level::CogBindingDescription description =
            sed::level::describeBindings(level, 0, &schema);
        check(description.schemaComplete, "CogBindings.A11.schemaComplete");
        check(description.parameterCountMatches && *description.parameterCountMatches,
            "CogBindings.A11.countMatches");
        check(description.rows.size() == 7, "CogBindings.A11.rows");
        const bool expectedCandidates[] = { false, true, true, false, false, false, false };
        for (std::size_t i = 0; i < 7 && i < description.rows.size(); ++i)
        {
            const std::string label = "CogBindings.A11.candidate" + std::to_string(i);
            check(description.rows[i].eventLinkCandidate
                    && *description.rows[i].eventLinkCandidate == expectedCandidates[i],
                label.c_str());
        }
        if (description.rows.size() == 7)
        {
            check(description.rows[0].target.status == CogBindingTargetStatus::level
                    && description.rows[0].target.kind == CogReferenceKind::thing,
                "CogBindings.A11.thingLevel");
            check(description.rows[1].symbol && description.rows[1].symbol->effectiveLinkId == 7,
                "CogBindings.A11.linkId");
            check(description.rows[2].target.sectorIndex && *description.rows[2].target.sectorIndex == 1
                    && description.rows[2].target.surfaceIndex
                    && *description.rows[2].target.surfaceIndex == 1,
                "CogBindings.A11.surfaceAddress");
            check(description.rows[4].target.status == CogBindingTargetStatus::null,
                "CogBindings.A11.null");
            check(description.rows[6].target.status == CogBindingTargetStatus::notReference,
                "CogBindings.A11.notReference");
        }
    }

    // A-12: COG index spellings keep their raw text and exact status.
    {
        const std::vector<std::pair<std::string, CogBindingTargetStatus>> cases{
            { "-1", CogBindingTargetStatus::null },
            { "-8", CogBindingTargetStatus::null },
            { "1", CogBindingTargetStatus::level },
            { "2", CogBindingTargetStatus::unplacedUnchecked },
            { "32768", CogBindingTargetStatus::staticUnchecked },
            { "1tail", CogBindingTargetStatus::invalid },
            { "2147483648", CogBindingTargetStatus::invalid },
            { "1.0", CogBindingTargetStatus::invalid },
        };
        const sed::assets::CogSchema schema = fixtureSchema("cog linked");
        for (const auto& [value, expected] : cases)
        {
            sed::Level level = makeBindingLevel(makeCog("fixture.cog", { value }));
            const auto description = sed::level::describeBindings(level, 0, &schema);
            const std::string label = "CogBindings.A12." + value;
            check(description.rows.size() == 1, label.c_str());
            if (description.rows.size() == 1)
            {
                check(description.rows[0].rawValue && *description.rows[0].rawValue == value,
                    label.c_str());
                check(description.rows[0].target.status == expected, label.c_str());
                check(description.rows[0].eventLinkCandidate
                        && !*description.rows[0].eventLinkCandidate,
                    label.c_str());
            }
        }
    }

    // A-13: an index equal to the placed count is invalid; declared
    // capacity is not headroom (the port Level carries no capacity field,
    // so the placed count is the only bound).
    {
        const std::vector<std::pair<std::string, std::string>> cases{
            { "thing target", "2" },
            { "sector target", "2" },
            { "surface target", "3" },
        };
        for (const auto& [declaration, value] : cases)
        {
            sed::Level level = makeBindingLevel(makeCog("fixture.cog", { value }));
            const sed::assets::CogSchema schema = fixtureSchema(declaration);
            const auto description = sed::level::describeBindings(level, 0, &schema);
            const std::string label = "CogBindings.A13." + declaration;
            check(description.rows.size() == 1
                    && description.rows[0].target.status == CogBindingTargetStatus::invalid
                    && description.rows[0].eventLinkCandidate
                    && !*description.rows[0].eventLinkCandidate,
                label.c_str());
        }
    }

    // A-14: consistency pass severity taxonomy with COG+parameter indices.
    {
        sed::Level level{};
        level.cogs.push_back(makeCog("missing.cog", { "0" }));
        level.cogs.push_back(makeCog("typed.cog", { "5" }));
        level.cogs.push_back(makeCog("typed.cog", { "1tail" }));
        level.cogs.push_back(makeCog("cogref.cog", { "32768" }));
        level.cogs.push_back(makeCog("cogref.cog", { "9" }));
        const auto issues = sed::level::checkCogReferences(level,
            [](const std::string_view name) -> std::optional<sed::assets::CogSchema>
            {
                if (name == "typed.cog")
                {
                    return sed::assets::parseCogSchema("typed.cog", "symbols\nthing target\nend\n");
                }
                if (name == "cogref.cog")
                {
                    return sed::assets::parseCogSchema("cogref.cog", "symbols\ncog other\nend\n");
                }
                return std::nullopt;
            });
        check(issues.size() == 5, "CogBindings.A14.issueCount");
        if (issues.size() == 5)
        {
            check(issues[0].severity == sed::level::CogReferenceIssueSeverity::warning
                    && issues[0].cogIndex == 0 && issues[0].parameterIndex == -1
                    && issues[0].message.find("schema unavailable") != std::string::npos,
                "CogBindings.A14.schemaUnavailable");
            check(issues[1].severity == sed::level::CogReferenceIssueSeverity::error
                    && issues[1].cogIndex == 1 && issues[1].parameterIndex == 0
                    && issues[1].message.find("outside the level's thing range") != std::string::npos,
                "CogBindings.A14.outOfRange");
            check(issues[2].severity == sed::level::CogReferenceIssueSeverity::error
                    && issues[2].cogIndex == 2 && issues[2].parameterIndex == 0
                    && issues[2].message.find("complete decimal") != std::string::npos,
                "CogBindings.A14.nonDecimal");
            check(issues[3].severity == sed::level::CogReferenceIssueSeverity::warning
                    && issues[3].cogIndex == 3 && issues[3].parameterIndex == 0
                    && issues[3].message.find("static-world COG") != std::string::npos,
                "CogBindings.A14.staticWarning");
            check(issues[4].severity == sed::level::CogReferenceIssueSeverity::warning
                    && issues[4].cogIndex == 4 && issues[4].parameterIndex == 0
                    && issues[4].message.find("unplaced/runtime COG") != std::string::npos,
                "CogBindings.A14.unplacedWarning");
        }
    }

    // A-15: missing/incomplete/mismatched schemas leave raw values untyped.
    {
        const std::vector<std::optional<sed::assets::CogSchema>> scripts{
            std::nullopt,
            fixtureSchema("thing a\nfuturetype b"),
            fixtureSchema("thing a\nthing A"),
            sed::assets::parseCogSchema("wrong.cog", "symbols\nthing a\nend"),
        };
        const std::vector<sed::level::CogScriptStatus> expectedStatus{
            sed::level::CogScriptStatus::missing,
            sed::level::CogScriptStatus::incomplete,
            sed::level::CogScriptStatus::incomplete,
            sed::level::CogScriptStatus::nameMismatch,
        };
        for (std::size_t i = 0; i < scripts.size(); ++i)
        {
            sed::Level level = makeBindingLevel(makeCog("fixture.cog", { "0" }));
            const sed::assets::CogSchema* schema = scripts[i] ? &*scripts[i] : nullptr;
            const auto description = sed::level::describeBindings(level, 0, schema);
            const std::string label = "CogBindings.A15." + std::to_string(i);
            check(!description.schemaComplete, label.c_str());
            check(description.scriptStatus == expectedStatus[i], label.c_str());
            check(!description.expectedParameterCount && !description.parameterCountMatches,
                label.c_str());
            check(description.rows.size() == 1, label.c_str());
            if (description.rows.size() == 1)
            {
                check(!description.rows[0].symbolName, label.c_str());
                check(description.rows[0].target.status == CogBindingTargetStatus::unknown,
                    label.c_str());
                check(!description.rows[0].eventLinkCandidate, label.c_str());
            }
            check(rawTexts(level.cogs[0]) == std::vector<std::string>{ "0" }, label.c_str());
        }
    }

    // A-16: count mismatches describe extra rows but never pad or discard.
    {
        const sed::assets::CogSchema schema = fixtureSchema("thing a\nsector b");

        sed::Level shortLevel = makeBindingLevel(makeCog("fixture.cog", { "0" }));
        const auto shortDescription = sed::level::describeBindings(shortLevel, 0, &schema);
        check(shortDescription.parameterCountMatches
                && !*shortDescription.parameterCountMatches,
            "CogBindings.A16.shortMismatch");
        check(hasDiagnosticCode(shortDescription, "parameterCountMismatch"),
            "CogBindings.A16.shortDiagnostic");
        check(shortDescription.rows.size() == 2, "CogBindings.A16.shortRows");
        if (shortDescription.rows.size() == 2)
        {
            check(!shortDescription.rows[1].rawValue
                    && shortDescription.rows[1].target.status == CogBindingTargetStatus::missing,
                "CogBindings.A16.missingRow");
        }
        check(shortLevel.cogs[0].values.size() == 1, "CogBindings.A16.shortUnchanged");

        sed::Level longLevel = makeBindingLevel(makeCog("fixture.cog", { "0", "1", "2" }));
        const auto longDescription = sed::level::describeBindings(longLevel, 0, &schema);
        check(longDescription.rows.size() == 3, "CogBindings.A16.longRows");
        if (longDescription.rows.size() == 3)
        {
            check(longDescription.rows[2].target.status == CogBindingTargetStatus::unknown,
                "CogBindings.A16.extraRowUnknown");
        }
        check(longLevel.cogs[0].values.size() == 3, "CogBindings.A16.longUnchanged");
    }

    // A-17 (describe side): non-canonical spellings stay byte-exact in rows
    // and describing never mutates the placed values.
    {
        sed::Level level = makeBindingLevel(makeCog("fixture.cog",
            { " +000 ", " 1.2500", "texture.mat" }));
        const sed::assets::CogSchema schema = fixtureSchema(
            "thing chosen nolink\nflex speed\nmaterial texture");
        const std::vector<std::string> before = rawTexts(level.cogs[0]);
        const auto description = sed::level::describeBindings(level, 0, &schema);
        check(description.rows.size() == 3, "CogBindings.A17.rows");
        if (description.rows.size() == 3)
        {
            check(description.rows[0].rawValue && *description.rows[0].rawValue == " +000 ",
                "CogBindings.A17.spellingKept");
            check(description.rows[0].target.status == CogBindingTargetStatus::level,
                "CogBindings.A17.target");
        }
        check(rawTexts(level.cogs[0]) == before, "CogBindings.A17.untouched");
    }

    // A-18 (describe side): each supported kind resolves to a placed target,
    // and out-of-range spellings classify without mutating anything.
    {
        const std::vector<std::pair<std::string, CogReferenceKind>> kinds{
            { "thing target", CogReferenceKind::thing },
            { "sector target", CogReferenceKind::sector },
            { "surface target", CogReferenceKind::surface },
            { "cog target", CogReferenceKind::cog },
        };
        for (const auto& [declaration, kind] : kinds)
        {
            sed::Level level = makeBindingLevel(makeCog("fixture.cog", { "1" }));
            const sed::assets::CogSchema schema = fixtureSchema(declaration);
            const auto description = sed::level::describeBindings(level, 0, &schema);
            const std::string label = "CogBindings.A18." + declaration;
            check(description.rows.size() == 1
                    && description.rows[0].target.kind == kind
                    && description.rows[0].target.status == CogBindingTargetStatus::level,
                label.c_str());
        }

        sed::Level level = makeBindingLevel(makeCog("fixture.cog", { "2" }));
        const sed::assets::CogSchema thingSchema = fixtureSchema("thing target");
        const auto outOfRange = sed::level::describeBindings(level, 0, &thingSchema);
        check(outOfRange.rows.size() == 1
                && outOfRange.rows[0].target.status == CogBindingTargetStatus::invalid,
            "CogBindings.A18.outOfRange");
        check(rawTexts(level.cogs[0]) == std::vector<std::string>{ "2" },
            "CogBindings.A18.untouched");
    }

    // A-19: existing static/unplaced COG values describe as unchecked.
    {
        const std::vector<std::pair<std::string, CogBindingTargetStatus>> cases{
            { "2", CogBindingTargetStatus::unplacedUnchecked },
            { "32768", CogBindingTargetStatus::staticUnchecked },
        };
        for (const auto& [value, expected] : cases)
        {
            sed::Level level = makeBindingLevel(makeCog("fixture.cog", { value }));
            const sed::assets::CogSchema cogSchema = fixtureSchema("cog target");
            const auto description = sed::level::describeBindings(level, 0, &cogSchema);
            const std::string label = "CogBindings.A19." + value;
            check(description.rows.size() == 1
                    && description.rows[0].target.status == expected,
                label.c_str());
            check(rawTexts(level.cogs[0]) == std::vector<std::string>{ value }, label.c_str());
        }
    }

    // A-20: a non-reference kind never describes as an index reference.
    {
        sed::Level level = makeBindingLevel(makeCog("fixture.cog", { "0" }));
        const sed::assets::CogSchema intSchema = fixtureSchema("int count");
        const auto description = sed::level::describeBindings(level, 0, &intSchema);
        check(description.rows.size() == 1
                && description.rows[0].target.kind == CogReferenceKind::other
                && description.rows[0].target.status == CogBindingTargetStatus::notReference
                && description.rows[0].eventLinkCandidate
                && !*description.rows[0].eventLinkCandidate,
            "CogBindings.A20.notReference");
    }

    // A-21 (describe side): a noncanonical imported surface order resolves
    // through the retained wire map, not sector traversal order.
    {
        sed::Level level = makeBindingLevel(makeCog("fixture.cog", { "0" }));
        level.importedJklSurfaceRefs = { sed::SurfaceRef{ 1, 1 }, sed::SurfaceRef{ 0, 0 },
            sed::SurfaceRef{ 1, 0 } };
        const sed::assets::CogSchema surfaceSchema = fixtureSchema("surface target");
        const auto description = sed::level::describeBindings(level, 0, &surfaceSchema);
        check(description.rows.size() == 1
                && description.rows[0].target.status == CogBindingTargetStatus::level
                && description.rows[0].target.sectorIndex
                && *description.rows[0].target.sectorIndex == 1
                && description.rows[0].target.surfaceIndex
                && *description.rows[0].target.surfaceIndex == 1,
            "CogBindings.A21.importedOrder");

        sed::Level serialized = makeBindingLevel(makeCog("fixture.cog", { "0" }));
        const auto serializedDescription = sed::level::describeBindings(
            serialized, 0, &surfaceSchema);
        check(serializedDescription.rows.size() == 1
                && serializedDescription.rows[0].target.sectorIndex
                && *serializedDescription.rows[0].target.sectorIndex == 0
                && serializedDescription.rows[0].target.surfaceIndex
                && *serializedDescription.rows[0].target.surfaceIndex == 0,
            "CogBindings.A21.serializedOrder");
    }

    // A-22 (describe side): broken retained surface identity is Unknown.
    // Port forms of Coda's two breaks: the retained source text no longer
    // matches its captured hash, and the retained map no longer names each
    // current surface exactly once.
    {
        sed::Level edited = makeBindingLevel(makeCog("fixture.cog", { "0" }));
        edited.importedJklSurfaceRefs = { sed::SurfaceRef{ 1, 1 }, sed::SurfaceRef{ 0, 0 },
            sed::SurfaceRef{ 1, 0 } };
        edited.retainedJklSourceText = "edited source";
        edited.retainedJklSourceHash = sed::assets::jklSourceSha256Hex("original source");
        const sed::assets::CogSchema surfaceSchema = fixtureSchema("surface target");
        const auto editedDescription = sed::level::describeBindings(edited, 0, &surfaceSchema);
        check(editedDescription.rows.size() == 1
                && editedDescription.rows[0].target.status == CogBindingTargetStatus::unknown,
            "CogBindings.A22.editedSourceUnknown");
        check(rawTexts(edited.cogs[0]) == std::vector<std::string>{ "0" },
            "CogBindings.A22.editedUntouched");

        sed::Level broken = makeBindingLevel(makeCog("fixture.cog", { "0" }));
        broken.importedJklSurfaceRefs = { sed::SurfaceRef{ 0, 0 }, sed::SurfaceRef{ 0, 0 },
            sed::SurfaceRef{ 1, 0 } };
        const auto brokenDescription = sed::level::describeBindings(broken, 0, &surfaceSchema);
        check(brokenDescription.rows.size() == 1
                && brokenDescription.rows[0].target.status == CogBindingTargetStatus::unknown,
            "CogBindings.A22.brokenMapUnknown");
        check(rawTexts(broken.cogs[0]) == std::vector<std::string>{ "0" },
            "CogBindings.A22.brokenUntouched");
    }
}

} // namespace sed::tests
