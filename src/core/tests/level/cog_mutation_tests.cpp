#include "test_common.hpp"

#include "sed/assets/cog_schema.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/sed.hpp"
#include "sed/editor/document.hpp"
#include "sed/level/cog_bindings.hpp"

#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace sed::tests
{
namespace
{

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

std::vector<std::string> rawTexts(const sed::Cog& cog)
{
    std::vector<std::string> texts{};
    for (const sed::CogValue& value : cog.values)
    {
        texts.push_back(value.stringValue);
    }
    return texts;
}

std::string serializedHash(const sed::Level& level)
{
    // Prefer the native serialized bytes; pending ambiguous imports are
    // refused by the writers, so those levels hash the canonical form
    // below over the same COG-relevant state.
    try
    {
        const std::vector<std::byte> fileBytes = sed::assets::saveToSedBytes(level);
        const std::array<std::byte, 32> fileDigest = sed::assets::calculateSedSha256(
            std::span<const std::byte>(fileBytes.data(), fileBytes.size()));
        static constexpr char kFileHex[] = "0123456789abcdef";
        std::string fileHex{};
        fileHex.reserve(fileDigest.size() * 2);
        for (const std::byte byte : fileDigest)
        {
            const auto value = static_cast<unsigned char>(byte);
            fileHex.push_back(kFileHex[value >> 4]);
            fileHex.push_back(kFileHex[value & 0x0f]);
        }
        return fileHex;
    }
    catch (...)
    {
    }
    std::string blob{};
    const auto field = [&blob](const std::string_view text)
    {
        blob += std::to_string(text.size());
        blob.push_back(':');
        blob += text;
        blob.push_back(';');
    };
    field(std::to_string(level.things.size()));
    field(std::to_string(level.sectors.size()));
    std::size_t surfaceCount = 0;
    for (const sed::Sector& sector : level.sectors)
    {
        surfaceCount += sector.surfaces.size();
    }
    field(std::to_string(surfaceCount));
    field(std::to_string(level.cogs.size()));
    for (const sed::SurfaceRef& ref : level.importedJklSurfaceRefs)
    {
        field(std::to_string(ref.sector) + "," + std::to_string(ref.surface));
    }
    field(level.retainedJklSourceText);
    field(level.retainedJklSourceHash);
    for (const sed::Cog& cog : level.cogs)
    {
        field(cog.name);
        field(std::to_string(cog.values.size()));
        for (const sed::CogValue& value : cog.values)
        {
            field(value.name);
            field(std::to_string(static_cast<int>(value.type)));
            field(std::to_string(static_cast<int>(value.cogType)));
            field(std::to_string(value.intValue));
            field(value.stringValue);
        }
    }
    const std::vector<std::byte> bytes(
        reinterpret_cast<const std::byte*>(blob.data()),
        reinterpret_cast<const std::byte*>(blob.data() + blob.size()));
    const std::array<std::byte, 32> digest = sed::assets::calculateSedSha256(
        std::span<const std::byte>(bytes.data(), bytes.size()));
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex{};
    hex.reserve(digest.size() * 2);
    for (const std::byte byte : digest)
    {
        const auto value = static_cast<unsigned char>(byte);
        hex.push_back(kHex[value >> 4]);
        hex.push_back(kHex[value & 0x0f]);
    }
    return hex;
}

// A refused prepare must carry the expected stable code and leave the
// level byte-identical through a prepare plus attempted apply cycle.
template <typename Prepared>
void checkRefusedUnchanged(
    const sed::Level& level,
    const Prepared& prepared,
    const std::string_view expectedCode,
    const char* const label)
{
    const std::string codeLabel = std::string(label) + ".code";
    check(!prepared.ok(), label);
    check(prepared.failure && prepared.failure->code == expectedCode, codeLabel.c_str());
    sed::editor::Document document{};
    document.level() = level;
    const std::string before = serializedHash(document.level());
    const bool applied = document.applyUndoable(
        "Refused COG edit",
        sed::editor::DocumentDirty::Cogs,
        [&prepared](sed::editor::DocumentState& state)
        {
            return prepared.apply(state.level);
        });
    const std::string unchangedLabel = std::string(label) + ".unchanged";
    check(!applied && document.undoDepth() == 0
            && serializedHash(document.level()) == before,
        unchangedLabel.c_str());
}

sed::level::CogSchemaResolver resolverReturning(
    const std::string& cogName,
    const std::string& scriptText)
{
    return [cogName, scriptText](const std::string_view name)
        -> std::optional<sed::assets::CogSchema>
    {
        if (name == cogName)
        {
            return sed::assets::parseCogSchema(std::string(name), scriptText);
        }
        return std::nullopt;
    };
}

sed::level::CogSchemaResolver emptyResolver()
{
    return [](const std::string_view) -> std::optional<sed::assets::CogSchema>
    {
        return std::nullopt;
    };
}

} // namespace

void runCogMutationTests()
{
    using sed::assets::CogSchemaSymbolKind;
    using sed::level::CogBindingTargetStatus;
    using sed::level::CogNamedBinding;
    using sed::level::CogReferenceKind;

    // A-17 (apply half): only the target changes to canonical "1";
    // undo restores every spelling and redo reapplies.
    {
        sed::editor::Document document{};
        document.level() = makeBindingLevel(makeCog("fixture.cog",
            { " +000 ", " 1.2500", "texture.mat" }));
        const sed::assets::CogSchema schema = fixtureSchema(
            "thing chosen nolink\nflex speed\nmaterial texture");
        const auto prepared = sed::level::prepareRebindCogReference(
            document.level(), 0, 0, "chosen", CogSchemaSymbolKind::thing, 1, &schema);
        check(prepared.ok(), "CogMutation.A17.prepared");
        const bool applied = document.applyUndoable(
            "Rebind COG reference",
            sed::editor::DocumentDirty::Cogs,
            [&prepared](sed::editor::DocumentState& state)
            {
                return prepared.apply(state.level);
            });
        check(applied && document.undoDepth() == 1, "CogMutation.A17.applied");
        check(rawTexts(document.level().cogs[0])
                == std::vector<std::string>{ "1", " 1.2500", "texture.mat" },
            "CogMutation.A17.onlyTargetChanged");
        check(document.level().cogs[0].values[0].intValue == 1,
            "CogMutation.A17.targetIndex");
        sed::editor::DocumentDirty dirty{};
        check(document.undo(&dirty)
                && rawTexts(document.level().cogs[0])
                    == std::vector<std::string>{ " +000 ", " 1.2500", "texture.mat" },
            "CogMutation.A17.undoRestoresSpellings");
        check(document.redo(&dirty)
                && rawTexts(document.level().cogs[0])
                    == std::vector<std::string>{ "1", " 1.2500", "texture.mat" },
            "CogMutation.A17.redoReapplies");
    }

    // A-18 (apply half): each supported kind rebinds and clears to -1.
    {
        const std::vector<std::pair<std::string, CogSchemaSymbolKind>> kinds{
            { "thing target", CogSchemaSymbolKind::thing },
            { "sector target", CogSchemaSymbolKind::sector },
            { "surface target", CogSchemaSymbolKind::surface },
            { "cog target", CogSchemaSymbolKind::cog },
        };
        for (const auto& [declaration, kind] : kinds)
        {
            const std::string label = "CogMutation.A18." + declaration;
            sed::editor::Document document{};
            document.level() = makeBindingLevel(makeCog("fixture.cog", { "0" }));
            const sed::assets::CogSchema schema = fixtureSchema(declaration);
            const auto toPlaced = sed::level::prepareRebindCogReference(
                document.level(), 0, 0, "target", kind, 1, &schema);
            check(toPlaced.ok(), label.c_str());
            check(document.applyUndoable("Rebind COG reference",
                    sed::editor::DocumentDirty::Cogs,
                    [&toPlaced](sed::editor::DocumentState& state)
                    {
                        return toPlaced.apply(state.level);
                    })
                    && rawTexts(document.level().cogs[0]) == std::vector<std::string>{ "1" },
                label.c_str());
            const auto toNull = sed::level::prepareRebindCogReference(
                document.level(), 0, 0, "target", kind, -1, &schema);
            check(toNull.ok(), label.c_str());
            check(document.applyUndoable("Clear COG reference",
                    sed::editor::DocumentDirty::Cogs,
                    [&toNull](sed::editor::DocumentState& state)
                    {
                        return toNull.apply(state.level);
                    })
                    && rawTexts(document.level().cogs[0]) == std::vector<std::string>{ "-1" },
                label.c_str());
        }
    }

    // A-18 (refusals): wrong name/type, out-of-range index, bad targets.
    {
        const sed::assets::CogSchema thingSchema = fixtureSchema("thing target");
        {
            sed::Level level = makeBindingLevel(makeCog("fixture.cog", { "0" }));
            const auto prepared = sed::level::prepareRebindCogReference(
                level, 0, 0, "wrong", CogSchemaSymbolKind::thing, 1, &thingSchema);
            checkRefusedUnchanged(level, prepared, "cogSymbolMismatch",
                "CogMutation.A18.wrongName");
        }
        {
            sed::Level level = makeBindingLevel(makeCog("fixture.cog", { "0" }));
            const auto prepared = sed::level::prepareRebindCogReference(
                level, 0, 0, "target", CogSchemaSymbolKind::sector, 1, &thingSchema);
            checkRefusedUnchanged(level, prepared, "cogSymbolMismatch",
                "CogMutation.A18.wrongType");
        }
        for (const std::int32_t parameterIndex : { -1, 1, 7 })
        {
            sed::Level level = makeBindingLevel(makeCog("fixture.cog", { "0" }));
            const auto prepared = sed::level::prepareRebindCogReference(
                level, 0, parameterIndex, "target", CogSchemaSymbolKind::thing, 1, &thingSchema);
            checkRefusedUnchanged(level, prepared, "cogParameterOutOfRange",
                "CogMutation.A18.outOfRange");
        }
        for (const std::int32_t targetIndex : { -2, 2,
                 std::numeric_limits<std::int32_t>::max() })
        {
            sed::Level level = makeBindingLevel(makeCog("fixture.cog", { "0" }));
            const auto prepared = sed::level::prepareRebindCogReference(
                level, 0, 0, "target", CogSchemaSymbolKind::thing, targetIndex, &thingSchema);
            checkRefusedUnchanged(level, prepared, "invalidCogTarget",
                "CogMutation.A18.badTarget");
        }
    }

    // A-19: existing static/runtime COG values and new static/runtime
    // targets refuse in opposite directions.
    {
        const sed::assets::CogSchema cogSchema = fixtureSchema("cog target");
        for (const std::string value : { std::string("2"), std::string("32768") })
        {
            sed::Level level = makeBindingLevel(makeCog("fixture.cog", { value }));
            const auto prepared = sed::level::prepareRebindCogReference(
                level, 0, 0, "target", CogSchemaSymbolKind::cog, 0, &cogSchema);
            checkRefusedUnchanged(level, prepared, "unsupportedCogReference",
                "CogMutation.A19.existingUnchecked");
        }
        for (const std::int32_t targetIndex : { 2, 32768 })
        {
            sed::Level level = makeBindingLevel(makeCog("fixture.cog", { "0" }));
            const auto prepared = sed::level::prepareRebindCogReference(
                level, 0, 0, "target", CogSchemaSymbolKind::cog, targetIndex, &cogSchema);
            checkRefusedUnchanged(level, prepared, "invalidCogTarget",
                "CogMutation.A19.newUncheckedTarget");
        }
    }

    // A-20: a non-reference kind cannot be rebound as a reference.
    {
        sed::Level level = makeBindingLevel(makeCog("fixture.cog", { "0" }));
        const sed::assets::CogSchema intSchema = fixtureSchema("int count");
        const auto prepared = sed::level::prepareRebindCogReference(
            level, 0, 0, "count", CogSchemaSymbolKind::int_, 0, &intSchema);
        checkRefusedUnchanged(level, prepared, "unsupportedCogBindingType",
            "CogMutation.A20.nonReference");
    }

    // Schema and count gates: incomplete schemas and exact counts.
    {
        sed::Level level = makeBindingLevel(makeCog("fixture.cog", { "0" }));
        const sed::assets::CogSchema incomplete = fixtureSchema("thing a\nfuturetype b");
        const auto incompletePrepared = sed::level::prepareRebindCogReference(
            level, 0, 0, "a", CogSchemaSymbolKind::thing, 1, &incomplete);
        checkRefusedUnchanged(level, incompletePrepared, "incompleteCogSchema",
            "CogMutation.gate.incompleteSchema");
        const auto missingPrepared = sed::level::prepareRebindCogReference(
            level, 0, 0, "a", CogSchemaSymbolKind::thing, 1, nullptr);
        checkRefusedUnchanged(level, missingPrepared, "incompleteCogSchema",
            "CogMutation.gate.missingSchema");
        const sed::assets::CogSchema wrongName =
            sed::assets::parseCogSchema("wrong.cog", "symbols\nthing a\nend");
        const auto mismatchedPrepared = sed::level::prepareRebindCogReference(
            level, 0, 0, "a", CogSchemaSymbolKind::thing, 1, &wrongName);
        checkRefusedUnchanged(level, mismatchedPrepared, "incompleteCogSchema",
            "CogMutation.gate.nameMismatch");

        const sed::assets::CogSchema twoValues = fixtureSchema("thing a\nsector b");
        sed::Level shortLevel = makeBindingLevel(makeCog("fixture.cog", { "0" }));
        const auto shortPrepared = sed::level::prepareRebindCogReference(
            shortLevel, 0, 0, "a", CogSchemaSymbolKind::thing, 1, &twoValues);
        checkRefusedUnchanged(shortLevel, shortPrepared, "cogParameterCountMismatch",
            "CogMutation.gate.shortCount");
        sed::Level longLevel = makeBindingLevel(makeCog("fixture.cog", { "0", "1", "2" }));
        const auto longPrepared = sed::level::prepareRebindCogReference(
            longLevel, 0, 0, "a", CogSchemaSymbolKind::thing, 1, &twoValues);
        checkRefusedUnchanged(longLevel, longPrepared, "cogParameterCountMismatch",
            "CogMutation.gate.longCount");
    }

    // A-21 (apply half): a noncanonical retained surface order resolves
    // through the wire map; undo restores "0".
    {
        sed::editor::Document document{};
        sed::Level level = makeBindingLevel(makeCog("fixture.cog", { "0" }));
        level.importedJklSurfaceRefs = { sed::SurfaceRef{ 1, 1 }, sed::SurfaceRef{ 0, 0 },
            sed::SurfaceRef{ 1, 0 } };
        document.level() = level;
        const sed::assets::CogSchema surfaceSchema = fixtureSchema("surface target");
        const auto before = sed::level::describeBindings(document.level(), 0, &surfaceSchema);
        check(before.rows.size() == 1
                && before.rows[0].target.sectorIndex
                && *before.rows[0].target.sectorIndex == 1
                && before.rows[0].target.surfaceIndex
                && *before.rows[0].target.surfaceIndex == 1,
            "CogMutation.A21.beforeAddress");
        const auto prepared = sed::level::prepareRebindCogReference(
            document.level(), 0, 0, "target", CogSchemaSymbolKind::surface, 2, &surfaceSchema);
        check(prepared.ok(), "CogMutation.A21.prepared");
        check(document.applyUndoable("Rebind surface reference",
                sed::editor::DocumentDirty::Cogs,
                [&prepared](sed::editor::DocumentState& state)
                {
                    return prepared.apply(state.level);
                }),
            "CogMutation.A21.applied");
        const auto after = sed::level::describeBindings(document.level(), 0, &surfaceSchema);
        check(after.rows.size() == 1
                && after.rows[0].target.sectorIndex
                && *after.rows[0].target.sectorIndex == 1
                && after.rows[0].target.surfaceIndex
                && *after.rows[0].target.surfaceIndex == 0
                && rawTexts(document.level().cogs[0]) == std::vector<std::string>{ "2" },
            "CogMutation.A21.afterAddress");
        sed::editor::DocumentDirty dirty{};
        check(document.undo(&dirty)
                && rawTexts(document.level().cogs[0]) == std::vector<std::string>{ "0" },
            "CogMutation.A21.undoRestoresZero");
        check(document.redo(&dirty)
                && rawTexts(document.level().cogs[0]) == std::vector<std::string>{ "2" },
            "CogMutation.A21.redoRestoresTwo");
    }

    // A-21 (JKL round trip): save and reopen preserve the rebound
    // surface binding address.
    {
        sed::Level level{};
        level.clear();
        level.kind = sed::ProjectType::JKDF2;
        level.header.version = 1;
        level.uvCoordinateSpace = sed::UvCoordinateSpace::TexturePixels;
        level.materials = { "fixture.mat" };
        const auto addTriangleSector = [&level](const double offsetX, const int surfaceCount)
        {
            sed::Sector& sector = level.newSector();
            sector.newVertex(offsetX, 0.0, 0.0);
            sector.newVertex(offsetX + 1.0, 0.0, 0.0);
            sector.newVertex(offsetX, 1.0, 0.0);
            for (int index = 0; index < surfaceCount; ++index)
            {
                sed::Surface surface{};
                surface.material = "fixture.mat";
                surface.vertices = { 0, 1, 2 };
                surface.texVertices.resize(3u);
                surface.normal = sed::TVector{ 0.0, 0.0, 1.0 };
                sector.addSurface(std::move(surface));
            }
        };
        addTriangleSector(0.0, 1);
        addTriangleSector(10.0, 2);
        level.cogs.push_back(makeCog("fixture.cog", { "0" }));
        sed::Cog other{};
        other.name = "other.cog";
        level.cogs.push_back(std::move(other));

        sed::editor::Document document{};
        document.level() = level;
        const sed::assets::CogSchema surfaceSchema = fixtureSchema("surface target");
        const auto prepared = sed::level::prepareRebindCogReference(
            document.level(), 0, 0, "target", CogSchemaSymbolKind::surface, 2, &surfaceSchema);
        check(prepared.ok(), "CogMutation.A21Jkl.prepared");
        check(document.applyUndoable("Rebind surface reference",
                sed::editor::DocumentDirty::Cogs,
                [&prepared](sed::editor::DocumentState& state)
                {
                    return prepared.apply(state.level);
                }),
            "CogMutation.A21Jkl.applied");
        std::ostringstream output;
        sed::assets::saveToJKL(document.level(), output);
        const std::string text = output.str();
        check(!text.empty(), "CogMutation.A21Jkl.saved");
        std::istringstream input(text);
        sed::assets::JklLoadOptions loadOptions{};
        loadOptions.projectType = sed::ProjectType::JKDF2;
        const sed::Level reopened = sed::assets::loadFromJKL(input, loadOptions);
        check(reopened.cogs.size() == 2 && reopened.cogs[0].values.size() == 1
                && reopened.cogs[0].values[0].stringValue == "2",
            "CogMutation.A21Jkl.reopenedToken");
        const auto reopenedDescription = sed::level::describeBindings(reopened, 0, &surfaceSchema);
        check(reopenedDescription.rows.size() == 1
                && reopenedDescription.rows[0].target.status == CogBindingTargetStatus::level
                && reopenedDescription.rows[0].target.sectorIndex
                && *reopenedDescription.rows[0].target.sectorIndex == 1
                && reopenedDescription.rows[0].target.surfaceIndex
                && *reopenedDescription.rows[0].target.surfaceIndex == 1,
            "CogMutation.A21Jkl.reopenedAddress");
    }

    // A-22: broken retained identity refuses rebind, including nulling,
    // and refuses a surface prepareAdd.
    {
        const sed::assets::CogSchema surfaceSchema = fixtureSchema("surface target");
        sed::Level broken = makeBindingLevel(makeCog("fixture.cog", { "0" }));
        broken.importedJklSurfaceRefs = { sed::SurfaceRef{ 0, 0 }, sed::SurfaceRef{ 0, 0 },
            sed::SurfaceRef{ 1, 0 } };
        for (const std::int32_t targetIndex : { 1, -1 })
        {
            const auto prepared = sed::level::prepareRebindCogReference(
                broken, 0, 0, "target", CogSchemaSymbolKind::surface, targetIndex, &surfaceSchema);
            checkRefusedUnchanged(broken, prepared, "unresolvedSurfaceIdentity",
                "CogMutation.A22.rebindRefused");
        }
        sed::Level edited = makeBindingLevel(makeCog("fixture.cog", { "0" }));
        edited.importedJklSurfaceRefs = { sed::SurfaceRef{ 1, 1 }, sed::SurfaceRef{ 0, 0 },
            sed::SurfaceRef{ 1, 0 } };
        edited.retainedJklSourceText = "edited source";
        edited.retainedJklSourceHash = sed::assets::jklSourceSha256Hex("original source");
        const sed::assets::CogSchema newSurfaceSchema =
            sed::assets::parseCogSchema("newface.cog", "symbols\nsurface target\nend");
        const auto addPrepared = sed::level::prepareAddCog(
            edited, "newface.cog", &newSurfaceSchema,
            std::vector<CogNamedBinding>{ CogNamedBinding{ "target", CogReferenceKind::surface, 1 } },
            emptyResolver());
        checkRefusedUnchanged(edited, addPrepared, "unresolvedSurfaceIdentity",
            "CogMutation.A22.addRefused");
    }

    // A-24: a fully local script creates an empty COG and a four-kind
    // script stores values in declaration order as one undo step.
    {
        sed::Level emptyLevel{};
        emptyLevel.newThing();
        const sed::assets::CogSchema localSchema = sed::assets::parseCogSchema(
            "localonly.cog", "symbols\nint hidden local\nmessage startup\nend");
        sed::editor::Document localDocument{};
        localDocument.level() = emptyLevel;
        const auto localPrepared = sed::level::prepareAddCog(
            localDocument.level(), "localonly.cog", &localSchema,
            std::vector<CogNamedBinding>{}, emptyResolver());
        check(localPrepared.ok() && localPrepared.newCogIndex == 0
                && localPrepared.preparedCog.values.empty(),
            "CogMutation.A24.localPrepared");
        check(localDocument.applyUndoable("Add COG",
                sed::editor::DocumentDirty::Cogs,
                [&localPrepared](sed::editor::DocumentState& state)
                {
                    return localPrepared.apply(state.level);
                })
                && localDocument.level().cogs.size() == 1
                && localDocument.level().cogs[0].name == "localonly.cog"
                && localDocument.level().cogs[0].values.empty(),
            "CogMutation.A24.localApplied");
        sed::editor::DocumentDirty dirty{};
        check(localDocument.undo(&dirty) && localDocument.level().cogs.empty(),
            "CogMutation.A24.localUndo");
        check(localDocument.redo(&dirty) && localDocument.level().cogs.size() == 1,
            "CogMutation.A24.localRedo");

        sed::Level level{};
        level.kind = sed::ProjectType::JKDF2;
        level.header.version = 1;
        level.uvCoordinateSpace = sed::UvCoordinateSpace::TexturePixels;
        level.materials = { "fixture.mat" };
        level.newThing();
        level.newThing();
        const auto addTriangleSector = [&level](const double offsetX, const int surfaceCount)
        {
            sed::Sector& sector = level.newSector();
            sector.newVertex(offsetX, 0.0, 0.0);
            sector.newVertex(offsetX + 1.0, 0.0, 0.0);
            sector.newVertex(offsetX, 1.0, 0.0);
            for (int index = 0; index < surfaceCount; ++index)
            {
                sed::Surface surface{};
                surface.material = "fixture.mat";
                surface.vertices = { 0, 1, 2 };
                surface.texVertices.resize(3u);
                surface.normal = sed::TVector{ 0.0, 0.0, 1.0 };
                sector.addSurface(std::move(surface));
            }
        };
        addTriangleSector(0.0, 1);
        addTriangleSector(10.0, 2);
        for (int index = 0; index < 2; ++index)
        {
            sed::Cog existing{};
            existing.name = "existing.cog";
            level.cogs.push_back(std::move(existing));
        }
        const sed::assets::CogSchema newSchema = sed::assets::parseCogSchema(
            "newdoor.cog", "symbols\nthing a\nsector b\nsurface c\ncog d\nend");
        const sed::level::CogSchemaResolver existingResolver = resolverReturning(
            "existing.cog", "symbols\nend");
        sed::editor::Document document{};
        document.level() = level;
        const std::vector<CogNamedBinding> reversed{
            CogNamedBinding{ "d", CogReferenceKind::cog, 1 },
            CogNamedBinding{ "c", CogReferenceKind::surface, 2 },
            CogNamedBinding{ "b", CogReferenceKind::sector, 1 },
            CogNamedBinding{ "a", CogReferenceKind::thing, 1 },
        };
        const auto prepared = sed::level::prepareAddCog(
            document.level(), "newdoor.cog", &newSchema, reversed, existingResolver);
        check(prepared.ok() && prepared.newCogIndex == 2, "CogMutation.A24.prepared");
        check(prepared.preparedCog.values.size() == 4
                && prepared.preparedCog.values[0].name == "a"
                && prepared.preparedCog.values[1].name == "b"
                && prepared.preparedCog.values[2].name == "c"
                && prepared.preparedCog.values[3].name == "d",
            "CogMutation.A24.declarationOrder");
        check(document.applyUndoable("Add COG",
                sed::editor::DocumentDirty::Cogs,
                [&prepared](sed::editor::DocumentState& state)
                {
                    return prepared.apply(state.level);
                })
                && document.level().cogs.size() == 3
                && rawTexts(document.level().cogs[2])
                    == std::vector<std::string>{ "1", "1", "2", "1" },
            "CogMutation.A24.appliedOrder");
        check(document.undoDepth() == 1, "CogMutation.A24.oneUndoStep");
        check(document.undo(&dirty) && document.level().cogs.size() == 2,
            "CogMutation.A24.undoRemoves");
        check(document.redo(&dirty) && document.level().cogs.size() == 3
                && rawTexts(document.level().cogs[2])
                    == std::vector<std::string>{ "1", "1", "2", "1" },
            "CogMutation.A24.redoRestores");

        // JKL save and reopen preserve the added binding tokens.
        std::ostringstream output;
        sed::assets::saveToJKL(document.level(), output);
        std::istringstream input(output.str());
        const sed::Level reopened = sed::assets::loadFromJKL(input);
        check(reopened.cogs.size() == 3 && reopened.cogs[2].values.size() == 4
                && reopened.cogs[2].values[0].stringValue == "1"
                && reopened.cogs[2].values[2].stringValue == "2",
            "CogMutation.A24.jklRoundTrip");
    }

    // A-25: prepareAdd refusal matrix with exact codes.
    {
        const sed::assets::CogSchema doorSchema = sed::assets::parseCogSchema(
            "newdoor.cog", "symbols\nthing a\nsector b\nend");
        const auto doorLevel = []()
        {
            sed::Level level{};
            level.newThing();
            level.newThing();
            level.newSector();
            level.newSector();
            return level;
        };
        const std::vector<CogNamedBinding> goodBindings{
            CogNamedBinding{ "a", CogReferenceKind::thing, 0 },
            CogNamedBinding{ "b", CogReferenceKind::sector, 1 },
        };
        for (const std::string badName : { std::string(28, 'a') + ".cog",
                 std::string("dir/newdoor.cog"), std::string("dir\\newdoor.cog"),
                 std::string("newdoor.txt"), std::string("caf\xC3\xA9.cog") })
        {
            sed::Level level = doorLevel();
            const sed::assets::CogSchema namedSchema =
                sed::assets::parseCogSchema(badName, "symbols\nthing a\nsector b\nend");
            const auto prepared = sed::level::prepareAddCog(
                level, badName, &namedSchema, goodBindings, emptyResolver());
            checkRefusedUnchanged(level, prepared, "invalidCogScriptName",
                "CogMutation.A25.badName");
        }
        {
            sed::Level level = doorLevel();
            const sed::assets::CogSchema incomplete = sed::assets::parseCogSchema(
                "newdoor.cog", "symbols\nthing a\nsector b\n");
            const auto prepared = sed::level::prepareAddCog(
                level, "newdoor.cog", &incomplete, goodBindings, emptyResolver());
            checkRefusedUnchanged(level, prepared, "incompleteCogSchema",
                "CogMutation.A25.incompleteSchema");
        }
        {
            sed::Level level = doorLevel();
            const auto prepared = sed::level::prepareAddCog(
                level, "newdoor.cog", &doorSchema,
                std::vector<CogNamedBinding>{
                    CogNamedBinding{ "a", CogReferenceKind::thing, 0 },
                    CogNamedBinding{ "A", CogReferenceKind::thing, 1 } },
                emptyResolver());
            checkRefusedUnchanged(level, prepared, "duplicateCogBinding",
                "CogMutation.A25.duplicate");
        }
        {
            sed::Level level = doorLevel();
            const auto prepared = sed::level::prepareAddCog(
                level, "newdoor.cog", &doorSchema,
                std::vector<CogNamedBinding>{
                    CogNamedBinding{ "a", CogReferenceKind::thing, 0 } },
                emptyResolver());
            checkRefusedUnchanged(level, prepared, "cogParameterCountMismatch",
                "CogMutation.A25.missingCount");
        }
        {
            sed::Level level = doorLevel();
            const auto prepared = sed::level::prepareAddCog(
                level, "newdoor.cog", &doorSchema,
                std::vector<CogNamedBinding>{
                    CogNamedBinding{ "a", CogReferenceKind::thing, 0 },
                    CogNamedBinding{ "zzz", CogReferenceKind::sector, 1 } },
                emptyResolver());
            checkRefusedUnchanged(level, prepared, "cogSymbolMismatch",
                "CogMutation.A25.extraName");
        }
        {
            sed::Level level = doorLevel();
            const auto prepared = sed::level::prepareAddCog(
                level, "newdoor.cog", &doorSchema,
                std::vector<CogNamedBinding>{
                    CogNamedBinding{ "a", CogReferenceKind::sector, 0 },
                    CogNamedBinding{ "b", CogReferenceKind::sector, 1 } },
                emptyResolver());
            checkRefusedUnchanged(level, prepared, "cogSymbolMismatch",
                "CogMutation.A25.wrongKind");
        }
        {
            sed::Level level = doorLevel();
            const sed::assets::CogSchema mixedSchema = sed::assets::parseCogSchema(
                "mixed.cog", "symbols\nthing a\nint count\nend");
            const auto prepared = sed::level::prepareAddCog(
                level, "mixed.cog", &mixedSchema,
                std::vector<CogNamedBinding>{
                    CogNamedBinding{ "a", CogReferenceKind::thing, 0 },
                    CogNamedBinding{ "count", CogReferenceKind::other, 0 } },
                emptyResolver());
            checkRefusedUnchanged(level, prepared, "unsupportedCogBindingType",
                "CogMutation.A25.unsupportedKind");
        }
        for (const std::int32_t targetIndex : { -2, 2 })
        {
            sed::Level level = doorLevel();
            const auto prepared = sed::level::prepareAddCog(
                level, "newdoor.cog", &doorSchema,
                std::vector<CogNamedBinding>{
                    CogNamedBinding{ "a", CogReferenceKind::thing, targetIndex },
                    CogNamedBinding{ "b", CogReferenceKind::sector, 1 } },
                emptyResolver());
            checkRefusedUnchanged(level, prepared, "invalidCogTarget",
                "CogMutation.A25.badTarget");
        }
    }

    // A-26: append closure capture, runtime, schema, and count refusals.
    {
        const sed::assets::CogSchema newSchema = sed::assets::parseCogSchema(
            "appended.cog", "symbols\nend");
        const auto levelWithHolder = [](const std::string& holderValue)
        {
            sed::Level level{};
            level.cogs.push_back(makeCog("holder.cog", { holderValue }));
            return level;
        };
        const sed::level::CogSchemaResolver holderResolver = resolverReturning(
            "holder.cog", "symbols\ncog link\nend");
        {
            sed::Level level = levelWithHolder("1");
            const auto prepared = sed::level::prepareAddCog(
                level, "appended.cog", &newSchema, std::vector<CogNamedBinding>{}, holderResolver);
            checkRefusedUnchanged(level, prepared, "existingCogReferenceCapture",
                "CogMutation.A26.capture");
            check(prepared.failure
                    && prepared.failure->message.find("not assumed invalid") != std::string::npos,
                "CogMutation.A26.captureWording");
        }
        {
            sed::Level level = levelWithHolder("5");
            const auto prepared = sed::level::prepareAddCog(
                level, "appended.cog", &newSchema, std::vector<CogNamedBinding>{}, holderResolver);
            checkRefusedUnchanged(level, prepared, "existingCogRuntimeReference",
                "CogMutation.A26.runtime");
        }
        {
            sed::Level level = levelWithHolder("0");
            const auto prepared = sed::level::prepareAddCog(
                level, "appended.cog", &newSchema, std::vector<CogNamedBinding>{}, emptyResolver());
            checkRefusedUnchanged(level, prepared, "incompleteExistingCogSchema",
                "CogMutation.A26.missingSchema");
        }
        {
            sed::Level level = levelWithHolder("0");
            const sed::level::CogSchemaResolver wideResolver = resolverReturning(
                "holder.cog", "symbols\ncog link\ncog extra\nend");
            const auto prepared = sed::level::prepareAddCog(
                level, "appended.cog", &newSchema, std::vector<CogNamedBinding>{}, wideResolver);
            checkRefusedUnchanged(level, prepared, "existingCogParameterCountMismatch",
                "CogMutation.A26.countMismatch");
        }
    }

    // A-27: the thing-append guard passes placed and -1 values without
    // mutation and refuses future, runtime, and malformed values.
    {
        const sed::level::CogSchemaResolver guardResolver = resolverReturning(
            "guard.cog", "symbols\nthing target\nend");
        const auto guardLevel = [](const std::string& value)
        {
            sed::Level level{};
            level.newThing();
            level.cogs.push_back(makeCog("guard.cog", { value }));
            return level;
        };
        for (const std::string value : { std::string("0"), std::string("-1") })
        {
            sed::Level level = guardLevel(value);
            const std::string before = serializedHash(level);
            const auto failure = sed::level::validateThingAppend(level, guardResolver);
            check(!failure && serializedHash(level) == before,
                "CogMutation.A27.passWithoutMutation");
        }
        for (const std::string value : { std::string("1"), std::string("999"),
                 std::string("32768") })
        {
            sed::Level level = guardLevel(value);
            const std::string before = serializedHash(level);
            const auto failure = sed::level::validateThingAppend(level, guardResolver);
            check(failure && failure->code == "existingThingRuntimeReference"
                    && serializedHash(level) == before,
                "CogMutation.A27.runtimeRefused");
        }
        for (const std::string value : { std::string("-2"), std::string("-01"),
                 std::string("1.0"), std::string("0.5"), std::string("0x0"),
                 std::string("1suffix"), std::string("2147483648"), std::string("") })
        {
            sed::Level level = guardLevel(value);
            const auto failure = sed::level::validateThingAppend(level, guardResolver);
            check(failure && failure->code == "invalidExistingThingReference",
                "CogMutation.A27.invalidRefused");
            check(failure
                    && failure->message.find("positional symbols only") != std::string::npos,
                "CogMutation.A27.scopeWording");
        }
        {
            sed::Level level = guardLevel("0");
            const auto failure = sed::level::validateThingAppend(level, emptyResolver());
            check(failure && failure->code == "incompleteExistingCogSchema",
                "CogMutation.A27.missingSchema");
        }
        {
            sed::Level level{};
            level.newThing();
            level.cogs.push_back(makeCog("guard.cog", { "0", "0" }));
            const auto failure = sed::level::validateThingAppend(level, guardResolver);
            check(failure && failure->code == "existingCogParameterCountMismatch",
                "CogMutation.A27.countMismatch");
        }
    }

    // A-28: scalar values and script-local thing constants are not
    // guessed to be references.
    {
        sed::Level level{};
        level.newThing();
        level.cogs.push_back(makeCog("guard.cog", { "32768", "0" }));
        const sed::level::CogSchemaResolver resolver = resolverReturning(
            "guard.cog", "symbols\nint big\nthing hidden local\nthing placed\nend");
        const auto failure = sed::level::validateThingAppend(level, resolver);
        check(!failure, "CogMutation.A28.scalarsAndLocalsPass");
    }

    // A-29 and A-30: budget boundaries refuse before resolving further
    // and every call revalidates against current state.
    {
        int resolveCalls = 0;
        const sed::level::CogSchemaResolver countingResolver =
            [&resolveCalls](const std::string_view name)
            -> std::optional<sed::assets::CogSchema>
        {
            ++resolveCalls;
            return sed::assets::parseCogSchema(std::string(name), "symbols\nend");
        };
        sed::Level atLimit{};
        for (int index = 0; index < 4096; ++index)
        {
            sed::Cog cog{};
            cog.name = "bulk.cog";
            atLimit.cogs.push_back(std::move(cog));
        }
        check(!sed::level::validateThingAppend(atLimit, countingResolver),
            "CogMutation.A29.cogLimitPasses");
        sed::Cog extra{};
        extra.name = "bulk.cog";
        atLimit.cogs.push_back(std::move(extra));
        resolveCalls = 0;
        const auto overLimit = sed::level::validateThingAppend(atLimit, countingResolver);
        check(overLimit && overLimit->code == "thingAppendCogBudget" && resolveCalls == 0,
            "CogMutation.A29.cogLimitRefusesFirst");

        sed::Level paramHeavy{};
        for (int cogIndex = 0; cogIndex < 2; ++cogIndex)
        {
            sed::Cog cog{};
            cog.name = "bulk.cog";
            for (int valueIndex = 0; valueIndex < 50001; ++valueIndex)
            {
                cog.values.push_back(rawParam("-1", static_cast<std::size_t>(valueIndex)));
            }
            paramHeavy.cogs.push_back(std::move(cog));
        }
        resolveCalls = 0;
        const auto paramOverLimit = sed::level::validateThingAppend(paramHeavy, countingResolver);
        check(paramOverLimit && paramOverLimit->code == "thingAppendCogBudget" && resolveCalls == 0,
            "CogMutation.A29.paramLimitRefusesFirst");

        {
            sed::Level level{};
            level.newThing();
            level.newSector();
            const sed::assets::CogSchema doorSchema = sed::assets::parseCogSchema(
                "newdoor.cog", "symbols\nthing a\nsector b\nend");
            std::vector<CogNamedBinding> manyBindings(
                257, CogNamedBinding{ "a", CogReferenceKind::thing, 0 });
            const auto prepared = sed::level::prepareAddCog(
                level, "newdoor.cog", &doorSchema, manyBindings, emptyResolver());
            checkRefusedUnchanged(level, prepared, "cogBindingBudget",
                "CogMutation.A29.bindingBudget");
        }

        sed::Level shrinking{};
        shrinking.newThing();
        shrinking.cogs.push_back(makeCog("guard.cog", { "0" }));
        const sed::level::CogSchemaResolver guardResolver = resolverReturning(
            "guard.cog", "symbols\nthing target\nend");
        check(!sed::level::validateThingAppend(shrinking, guardResolver),
            "CogMutation.A30.firstCallPasses");
        shrinking.things.clear();
        const auto shrunk = sed::level::validateThingAppend(shrinking, guardResolver);
        check(shrunk && shrunk->code == "existingThingRuntimeReference",
            "CogMutation.A30.shrunkCountRefuses");
        bool complete = true;
        const sed::level::CogSchemaResolver changingResolver =
            [&complete](const std::string_view name)
            -> std::optional<sed::assets::CogSchema>
        {
            return sed::assets::parseCogSchema(std::string(name),
                complete ? "symbols\nthing target\nend" : "symbols\nthing target\n");
        };
        sed::Level changing{};
        changing.newThing();
        changing.cogs.push_back(makeCog("guard.cog", { "0" }));
        check(!sed::level::validateThingAppend(changing, changingResolver),
            "CogMutation.A30.completePasses");
        complete = false;
        const auto changed = sed::level::validateThingAppend(changing, changingResolver);
        check(changed && changed->code == "incompleteExistingCogSchema",
            "CogMutation.A30.changedSchemaRefuses");
    }

    // Hash-bound script identity: the prepared step records the
    // captured bytes digest and a stale digest refuses unchanged.
    {
        sed::Level level = makeBindingLevel(makeCog("fixture.cog", { "0" }));
        const sed::assets::CogSchema schema = fixtureSchema("thing target");
        const std::string digest = sed::level::cogSchemaSha256Hex(schema);
        check(digest == sed::assets::jklSourceSha256Hex(schema.sourceText)
                && digest.size() == 64,
            "CogMutation.hash.digestRecorded");
        const auto reviewed = sed::level::prepareRebindCogReference(
            level, 0, 0, "target", CogSchemaSymbolKind::thing, 1, &schema, digest);
        check(reviewed.ok() && reviewed.scriptSha256 == digest,
            "CogMutation.hash.reviewedDigestPasses");
        const auto stale = sed::level::prepareRebindCogReference(
            level, 0, 0, "target", CogSchemaSymbolKind::thing, 1, &schema,
            std::string(64, '0'));
        checkRefusedUnchanged(level, stale, "scriptHashMismatch",
            "CogMutation.hash.staleDigestRefuses");
    }
}

} // namespace sed::tests

