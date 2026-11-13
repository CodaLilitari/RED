#include "test_common.hpp"

#include "sed/assets/cog_schema.hpp"
#include "sed/assets/sed.hpp"
#include "sed/editor/document.hpp"
#include "sed/level/workbench.hpp"

#include <cmath>
#include <numbers>
#include <span>
#include <string>
#include <vector>

namespace sed::tests
{
namespace
{

using namespace sed::level;

std::string serializedHash(const sed::Level& level)
{
    const std::vector<std::byte> bytes = sed::assets::saveToSedBytes(level);
    const std::array<std::byte, 32> digest = sed::assets::calculateSedSha256(
        std::span<const std::byte>(bytes.data(), bytes.size()));
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex{};
    for (const std::byte byte : digest)
    {
        const auto value = static_cast<unsigned char>(byte);
        hex.push_back(kHex[value >> 4]);
        hex.push_back(kHex[value & 0x0f]);
    }
    return hex;
}

// Appends a sector through the level allocator so serialized fixtures
// carry unique entity ids, then installs the given geometry.
sed::Sector& addSector(sed::Level& level, sed::Sector geometry)
{
    sed::Sector& sector = level.newSector();
    const std::int32_t id = sector.id;
    const std::int32_t num = sector.num;
    sector = std::move(geometry);
    sector.id = id;
    sector.num = num;
    return sector;
}

// Cube sector whose surfaces carry authored corner UVs (corner i of each
// surface starts at (i, 2i)), for batch UV tests.
sed::Sector makeUvCubeSector(const double offsetX)
{
    sed::Sector sector{};
    appendTestCubeVertices(sector, offsetX);
    const std::int32_t faces[6][4] = {
        { 1, 5, 6, 2 }, { 0, 3, 7, 4 }, { 3, 2, 6, 7 },
        { 0, 4, 5, 1 }, { 4, 7, 6, 5 }, { 0, 1, 2, 3 },
    };
    for (const auto& face : faces)
    {
        sed::Surface surface{};
        surface.vertices = { face[0], face[1], face[2], face[3] };
        surface.texVertices.resize(surface.vertices.size());
        for (std::size_t i = 0; i < surface.texVertices.size(); ++i)
        {
            surface.texVertices[i].u = static_cast<float>(i);
            surface.texVertices[i].v = static_cast<float>(2 * i);
        }
        surface.normal = surfaceCalcNormalNewell(sector, surface);
        sector.surfaces.push_back(std::move(surface));
    }
    return sector;
}

sed::Sector makePolygonSector(const int corners)
{
    sed::Sector sector{};
    for (int i = 0; i < corners; ++i)
    {
        const double angle = 2.0 * std::numbers::pi_v<double> * i / corners;
        sector.addVertex(
            10.0 * std::cos(angle), 10.0 * std::sin(angle), 0.0);
    }
    sed::Surface surface{};
    for (int i = 0; i < corners; ++i)
    {
        surface.vertices.push_back(i);
        sed::TexVertex texVertex{};
        texVertex.u = static_cast<float>((i % 97) * 0.01);
        texVertex.v = 0.0f;
        surface.texVertices.push_back(texVertex);
    }
    surface.normal = sed::TVector{ 0.0, 0.0, 1.0 };
    sector.surfaces.push_back(std::move(surface));
    return sector;
}

sed::Level makeQueryLevel()
{
    sed::Level level{};
    level.materials = { "a.mat", "b.mat", "a.mat" };
    for (int i = 0; i < 3; ++i)
    {
        sed::Sector sector = makeTestCubeSector(4.0 * i);
        sector.num = i;
        level.sectors.push_back(std::move(sector));
    }
    level.sectors[0].flags = 0xB;
    level.sectors[0].layer = 2;
    level.sectors[1].flags = 0xA;
    level.sectors[1].layer = 1;
    level.sectors[2].flags = 0x3;
    level.sectors[2].layer = 2;

    sed::Surface& first = level.sectors[0].surfaces[0];
    first.material = "b.mat";
    first.surfaceFlags = 0x5;
    first.faceFlags = 0x3;
    sed::Surface& second = level.sectors[0].surfaces[1];
    second.surfaceFlags = 0x4;
    second.adjoin = sed::SurfaceRef{ 0, 0 };
    second.adjoinFlags = 0x2;
    level.sectors[0].surfaces[2].material = "a.mat";
    level.sectors[0].surfaces[3].material = "A.MAT";

    const auto addThing = [&level](
        const char* name, const char* templateName, const std::int32_t layer) {
        sed::Thing& thing = level.newThing();
        thing.name = name;
        thing.templateName = templateName;
        thing.layer = layer;
    };
    addThing("Alpha Crate", "crate", 1);
    addThing("beta crate", "Crate", 2);
    addThing("Gamma", "turret", 1);

    sed::SedLight& firstLight = level.newLight();
    firstLight.flags = 0x5;
    firstLight.layer = 1;
    sed::SedLight& secondLight = level.newLight();
    secondLight.flags = 0x2;
    secondLight.layer = 2;
    return level;
}

sed::Level makeArrayLevel()
{
    sed::Level level{};
    level.templates.push_back(sed::ThingTemplate{ "crate", "", {}, "", {} });
    addSector(level, makeTestCubeSector(0.0));
    return level;
}

template <typename Prepared>
void checkRefusedUnchanged(
    const sed::Level& level,
    const Prepared& prepared,
    const std::string_view expectedCode,
    const char* const label)
{
    const std::string codeLabel = std::string(label) + ".code";
    check(!prepared.ok(), label);
    check(prepared.failure && prepared.failure->code == expectedCode,
        codeLabel.c_str());
    sed::editor::Document document{};
    document.level() = level;
    const std::string before = serializedHash(document.level());
    const bool applied = document.applyUndoable(
        "Refused workbench edit", sed::editor::DocumentDirty::All,
        [&prepared](sed::editor::DocumentState& state) {
            return prepared.apply(state.level);
        });
    const std::string unchangedLabel = std::string(label) + ".unchanged";
    check(!applied && document.undoDepth() == 0
            && serializedHash(document.level()) == before,
        unchangedLabel.c_str());
}

bool uvNear(const sed::TexVertex& value, const double u, const double v)
{
    return close(value.u, u, 1e-6) && close(value.v, v, 1e-6);
}

bool sameUvVector(
    const std::vector<sed::TexVertex>& a,
    const std::vector<sed::TexVertex>& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].u != b[i].u || a[i].v != b[i].v)
        {
            return false;
        }
    }
    return true;
}

} // namespace

void runWorkbenchTests()
{
    // --- Exact queries: sectors ---------------------------------------
    {
        const sed::Level level = makeQueryLevel();

        ExactQueryFilter byAll{};
        byAll.flags = FlagMaskPredicate{ std::nullopt, 0x8u, std::nullopt };
        const QueryResult all = queryWorkbench(
            level, WorkbenchCollection::sectors, std::nullopt, byAll, "");
        check(all.ok() && all.indices == std::vector<std::int32_t>{ 0, 1 },
            "workbench.query.sectorFlagsAll");

        ExactQueryFilter byAnyZero{};
        byAnyZero.flags = FlagMaskPredicate{ 0u, std::nullopt, std::nullopt };
        const QueryResult anyZero = queryWorkbench(
            level, WorkbenchCollection::sectors, std::nullopt, byAnyZero, "");
        check(anyZero.ok() && anyZero.totalMatches == 0
                && anyZero.indices.empty(),
            "workbench.query.flagAnyZeroMatchesNothing");

        ExactQueryFilter byAllZero{};
        byAllZero.flags = FlagMaskPredicate{ std::nullopt, 0u, std::nullopt };
        const QueryResult allZero = queryWorkbench(
            level, WorkbenchCollection::sectors, std::nullopt, byAllZero, "");
        check(allZero.ok() && allZero.totalMatches == 3,
            "workbench.query.flagAllZeroVacuous");

        ExactQueryFilter byLayer{};
        byLayer.layer = 2;
        byLayer.flags = FlagMaskPredicate{ 0x2u, std::nullopt, std::nullopt };
        const QueryResult layered = queryWorkbench(
            level, WorkbenchCollection::sectors, std::nullopt, byLayer, "");
        check(layered.ok()
                && layered.indices == std::vector<std::int32_t>{ 0, 2 },
            "workbench.query.originalIndicesRetained");

        const QueryResult firstPage = queryWorkbench(level,
            WorkbenchCollection::sectors, std::nullopt, std::nullopt, "",
            QueryPageRequest{ 0, 2 });
        check(firstPage.ok()
                && firstPage.indices == std::vector<std::int32_t>{ 0, 1 }
                && firstPage.totalMatches == 3
                && firstPage.nextOffset == std::optional<std::size_t>{ 2 },
            "workbench.query.firstPage");
        const QueryResult secondPage = queryWorkbench(level,
            WorkbenchCollection::sectors, std::nullopt, std::nullopt, "",
            QueryPageRequest{ 2, 2 });
        check(secondPage.ok()
                && secondPage.indices == std::vector<std::int32_t>{ 2 }
                && !secondPage.nextOffset.has_value(),
            "workbench.query.paginatesMatchingRows");
        const QueryResult repeated = queryWorkbench(level,
            WorkbenchCollection::sectors, std::nullopt, byLayer, "");
        check(repeated.indices == layered.indices
                && repeated.totalMatches == layered.totalMatches,
            "workbench.query.deterministic");
    }

    // --- Exact queries: things ----------------------------------------
    {
        const sed::Level level = makeQueryLevel();

        ExactQueryFilter byName{};
        byName.name = "alpha crate";
        const QueryResult named = queryWorkbench(
            level, WorkbenchCollection::things, std::nullopt, byName, "");
        check(named.ok() && named.indices == std::vector<std::int32_t>{ 0 },
            "workbench.query.thingNameExactCaseInsensitive");

        ExactQueryFilter substringName{};
        substringName.name = "crate";
        const QueryResult notSubstring = queryWorkbench(level,
            WorkbenchCollection::things, std::nullopt, substringName, "");
        check(notSubstring.ok() && notSubstring.totalMatches == 0,
            "workbench.query.thingNameIsNotSubstring");

        ExactQueryFilter byTemplate{};
        byTemplate.templateName = "CRATE";
        const QueryResult templated = queryWorkbench(level,
            WorkbenchCollection::things, std::nullopt, byTemplate, "");
        check(templated.ok()
                && templated.indices == std::vector<std::int32_t>{ 0, 1 },
            "workbench.query.thingTemplateExact");

        const QueryResult searched = queryWorkbench(level,
            WorkbenchCollection::things, std::nullopt, std::nullopt, "CRATE");
        check(searched.ok()
                && searched.indices == std::vector<std::int32_t>{ 0, 1 },
            "workbench.query.searchSubstring");

        ExactQueryFilter byLayer{};
        byLayer.layer = 2;
        const QueryResult anded = queryWorkbench(level,
            WorkbenchCollection::things, std::nullopt, byLayer, "crate");
        check(anded.ok() && anded.indices == std::vector<std::int32_t>{ 1 },
            "workbench.query.filterAndSearchCombine");

        const QueryResult containment = queryWorkbench(level,
            WorkbenchCollection::thingContainment, std::nullopt, byTemplate,
            "");
        check(containment.ok() && containment.indices == templated.indices,
            "workbench.query.containmentSharesThingPredicates");
    }

    // --- Exact queries: surfaces and lights ---------------------------
    {
        const sed::Level level = makeQueryLevel();

        ExactQueryFilter byMaterial{};
        byMaterial.materialIndex = 1;
        const QueryResult material = queryWorkbench(
            level, WorkbenchCollection::surfaces, 0, byMaterial, "");
        check(material.ok()
                && material.indices == std::vector<std::int32_t>{ 0 },
            "workbench.query.surfaceMaterialIndex");

        ExactQueryFilter untextured{};
        untextured.materialIndex = -1;
        const QueryResult bare = queryWorkbench(
            level, WorkbenchCollection::surfaces, 0, untextured, "");
        check(bare.ok() && bare.totalMatches == 3,
            "workbench.query.surfaceUntextured");

        ExactQueryFilter duplicateName{};
        duplicateName.materialIndex = 0;
        const QueryResult duplicates = queryWorkbench(
            level, WorkbenchCollection::surfaces, 0, duplicateName, "");
        check(duplicates.ok()
                && duplicates.indices == std::vector<std::int32_t>{ 2, 3 },
            "workbench.query.duplicateMaterialNamesKeepFirstSlot");

        ExactQueryFilter surfAll{};
        surfAll.surfFlags = FlagMaskPredicate{ std::nullopt, 0x4u, std::nullopt };
        const QueryResult surf = queryWorkbench(
            level, WorkbenchCollection::surfaces, 0, surfAll, "");
        check(surf.ok() && surf.indices == std::vector<std::int32_t>{ 0, 1 },
            "workbench.query.surfaceSurfFlags");

        ExactQueryFilter faceAny{};
        faceAny.faceFlags = FlagMaskPredicate{ 0x2u, std::nullopt, std::nullopt };
        const QueryResult face = queryWorkbench(
            level, WorkbenchCollection::surfaces, 0, faceAny, "");
        check(face.ok() && face.indices == std::vector<std::int32_t>{ 0 },
            "workbench.query.surfaceFaceFlags");

        ExactQueryFilter adjoinVacuous{};
        adjoinVacuous.adjoinFlags =
            FlagMaskPredicate{ std::nullopt, 0u, std::nullopt };
        const QueryResult adjoined = queryWorkbench(
            level, WorkbenchCollection::surfaces, 0, adjoinVacuous, "");
        check(adjoined.ok()
                && adjoined.indices == std::vector<std::int32_t>{ 1 },
            "workbench.query.adjoinFlagsRequireAdjoin");

        ExactQueryFilter lightFlags{};
        lightFlags.flags = FlagMaskPredicate{ std::nullopt, 0x1u, std::nullopt };
        const QueryResult lights = queryWorkbench(
            level, WorkbenchCollection::lights, std::nullopt, lightFlags, "");
        check(lights.ok() && lights.indices == std::vector<std::int32_t>{ 0 },
            "workbench.query.lightFlags");
    }

    // --- Exact queries: refusals --------------------------------------
    {
        const sed::Level level = makeQueryLevel();

        const QueryResult emptyFilter = queryWorkbench(level,
            WorkbenchCollection::things, std::nullopt, ExactQueryFilter{},
            "");
        check(!emptyFilter.ok()
                && emptyFilter.failure->code == "emptyQueryFilter",
            "workbench.query.emptyFilterRefuses");

        ExactQueryFilter wrongField{};
        wrongField.name = "x";
        const QueryResult inapplicable = queryWorkbench(level,
            WorkbenchCollection::sectors, std::nullopt, wrongField, "");
        check(!inapplicable.ok()
                && inapplicable.failure->code
                    == "unsupportedQueryFilterField",
            "workbench.query.inapplicableFieldRefuses");

        ExactQueryFilter blankName{};
        blankName.name = "  ";
        const QueryResult blank = queryWorkbench(level,
            WorkbenchCollection::things, std::nullopt, blankName, "");
        check(!blank.ok()
                && blank.failure->code == "invalidQueryFilterValue",
            "workbench.query.blankTextRefuses");

        ExactQueryFilter emptyMask{};
        emptyMask.flags = FlagMaskPredicate{};
        const QueryResult mask = queryWorkbench(level,
            WorkbenchCollection::sectors, std::nullopt, emptyMask, "");
        check(!mask.ok() && mask.failure->code == "invalidQueryFilterValue",
            "workbench.query.emptyMaskRefuses");

        const QueryResult searchMisuse = queryWorkbench(level,
            WorkbenchCollection::sectors, std::nullopt, std::nullopt, "x");
        check(!searchMisuse.ok()
                && searchMisuse.failure->code == "unsupportedQuerySearch",
            "workbench.query.searchOnSectorsRefuses");

        const QueryResult noSector = queryWorkbench(level,
            WorkbenchCollection::surfaces, std::nullopt, std::nullopt, "");
        check(!noSector.ok()
                && noSector.failure->code == "querySectorRequired",
            "workbench.query.surfacesNeedSector");

        const QueryResult badSector = queryWorkbench(level,
            WorkbenchCollection::surfaces, 9, std::nullopt, "");
        check(!badSector.ok()
                && badSector.failure->code == "querySectorOutOfRange",
            "workbench.query.badSectorRefuses");

        const QueryResult sectorMisuse = queryWorkbench(level,
            WorkbenchCollection::things, 0, std::nullopt, "");
        check(!sectorMisuse.ok()
                && sectorMisuse.failure->code == "querySectorNotApplicable",
            "workbench.query.sectorOnThingsRefuses");

        const QueryResult badPage = queryWorkbench(level,
            WorkbenchCollection::sectors, std::nullopt, std::nullopt, "",
            QueryPageRequest{ 0, 0 });
        check(!badPage.ok() && badPage.failure->code == "invalidQueryPage",
            "workbench.query.zeroLimitRefuses");
    }

    // --- Batch UV: projection bases and shared origin ------------------
    {
        sed::Level level{};
        level.sectors.push_back(makeUvCubeSector(0.0));
        const std::array<std::pair<UvProjectionDirection, std::int32_t>, 6>
            bases = { {
                { UvProjectionDirection::positiveX, 0 },
                { UvProjectionDirection::negativeX, 0 },
                { UvProjectionDirection::positiveY, 2 },
                { UvProjectionDirection::negativeY, 3 },
                { UvProjectionDirection::positiveZ, 5 },
                { UvProjectionDirection::negativeZ, 5 },
            } };
        const std::array<std::pair<double, double>, 6> firstCorner = { {
            { -1.0, -1.0 }, { 1.0, -1.0 }, { 1.0, -1.0 },
            { -1.0, -1.0 }, { -1.0, -1.0 }, { 1.0, -1.0 },
        } };
        for (std::size_t i = 0; i < bases.size(); ++i)
        {
            const std::array<SurfaceRef, 1> target = {
                SurfaceRef{ 0, bases[i].second },
            };
            const PreparedBatchUv prepared = prepareProjectSurfaceUv(
                level, target, bases[i].first, sed::TVector{ 0, 0, 0 }, 1.0);
            check(prepared.ok(), "workbench.uv.projectionBasisPrepares");
            if (prepared.ok())
            {
                check(uvNear(prepared.surfaces[0].after[0],
                        firstCorner[i].first, firstCorner[i].second),
                    "workbench.uv.projectionBasisFirstCorner");
            }
        }

        const std::array<SurfaceRef, 1> ground = { SurfaceRef{ 0, 5 } };
        const PreparedBatchUv scaled = prepareProjectSurfaceUv(
            level, ground, UvProjectionDirection::positiveZ,
            sed::TVector{ 0, 0, 0 }, 2.0);
        check(scaled.ok()
                && uvNear(scaled.surfaces[0].after[0], -2.0, -2.0)
                && uvNear(scaled.surfaces[0].after[2], 2.0, 2.0),
            "workbench.uv.projectionUsesSharedDensity");
    }

    // --- Batch UV: document undo path ----------------------------------
    {
        sed::editor::Document document{};
        document.level().sectors.push_back(makeUvCubeSector(0.0));
        const std::string beforeHash = serializedHash(document.level());
        const std::array<SurfaceRef, 1> ground = { SurfaceRef{ 0, 5 } };
        const PreparedBatchUv prepared = prepareProjectSurfaceUv(
            document.level(), ground, UvProjectionDirection::positiveZ,
            sed::TVector{ 0, 0, 0 }, 2.0);
        check(prepared.ok(), "workbench.uv.documentPrepares");
        const float originalU = document.level()
            .sectors[0].surfaces[5].texVertices[0].u;
        const bool applied = document.applyUndoable(
            "Project surface UVs", sed::editor::DocumentDirty::LevelGeometry,
            [&prepared](sed::editor::DocumentState& state) {
                return prepared.apply(state.level);
            });
        check(applied && document.undoDepth() == 1,
            "workbench.uv.documentAppliesAsOneStep");
        check(uvNear(document.level().sectors[0].surfaces[5].texVertices[0],
                -2.0, -2.0),
            "workbench.uv.documentEffect");
        check(document.undo()
                && document.level().sectors[0].surfaces[5].texVertices[0].u
                    == originalU
                && serializedHash(document.level()) == beforeHash,
            "workbench.uv.undoRestoresExactPrior");
        check(document.redo()
                && uvNear(document.level()
                        .sectors[0].surfaces[5].texVertices[0], -2.0, -2.0),
            "workbench.uv.redoReplays");
    }

    // --- Batch UV: equal positions, seams, transform order -------------
    {
        sed::Level level{};
        level.sectors.push_back(makeUvCubeSector(0.0));
        level.sectors.push_back(makeUvCubeSector(0.0));
        const std::array<SurfaceRef, 2> grounds = {
            SurfaceRef{ 0, 5 }, SurfaceRef{ 1, 5 },
        };
        const PreparedBatchUv projected = prepareProjectSurfaceUv(level,
            grounds, UvProjectionDirection::positiveZ,
            sed::TVector{ 0, 0, 0 }, 1.0);
        check(projected.ok() && projected.surfaces.size() == 2
                && sameUvVector(projected.surfaces[0].after,
                    projected.surfaces[1].after),
            "workbench.uv.equalPositionsEqualUvs");

        const PreparedBatchUv moved = prepareTransformSurfaceUv(level,
            grounds, WorkbenchUvPoint{ 1.0, 1.0 },
            WorkbenchUvPoint{ 2.0, 3.0 }, 90.0,
            WorkbenchUvPoint{ 10.0, 20.0 });
        check(moved.ok(), "workbench.uv.transformPrepares");
        if (moved.ok())
        {
            // Corner (2, 4) about pivot (1, 1): scale -> (2, 9),
            // rotate 90 -> (-9, 2), re-add pivot and offset -> (2, 23).
            check(uvNear(moved.surfaces[0].after[2], 2.0, 23.0),
                "workbench.uv.transformOrderScaleRotateOffset");
            check(sameUvVector(moved.surfaces[0].after,
                    moved.surfaces[1].after),
                "workbench.uv.transformPreservesCoincidentUvs");
        }

        const PreparedBatchUv mirrored = prepareTransformSurfaceUv(level,
            grounds, WorkbenchUvPoint{ 1.0, 1.0 },
            WorkbenchUvPoint{ -1.0, 1.0 }, 0.0,
            WorkbenchUvPoint{ 0.0, 0.0 });
        check(mirrored.ok()
                && uvNear(mirrored.surfaces[0].after[2], 0.0, 4.0),
            "workbench.uv.negativeScaleMirrors");

        const PreparedBatchUv zeroScale = prepareTransformSurfaceUv(level,
            grounds, WorkbenchUvPoint{}, WorkbenchUvPoint{ 0.0, 1.0 }, 0.0,
            WorkbenchUvPoint{});
        check(!zeroScale.ok()
                && zeroScale.failure->code == "invalidBatchUvInput",
            "workbench.uv.zeroScaleRefuses");

        sed::Level colored{};
        colored.sectors.push_back(makeUvCubeSector(0.0));
        colored.sectors[0].surfaces[5].texVertices[0].color =
            sed::ColorRed;
        const std::array<SurfaceRef, 1> one = { SurfaceRef{ 0, 5 } };
        const PreparedBatchUv coloredMove = prepareTransformSurfaceUv(
            colored, one, WorkbenchUvPoint{}, WorkbenchUvPoint{ 1.0, 1.0 },
            0.0, WorkbenchUvPoint{ 5.0, 5.0 });
        check(coloredMove.ok() && coloredMove.apply(colored)
                && colored.sectors[0].surfaces[5].texVertices[0].color
                    == sed::ColorRed,
            "workbench.uv.vertexColoursPreserved");
    }

    // --- Batch UV: stale state refuses without mutation -----------------
    {
        sed::Level level{};
        level.sectors.push_back(makeUvCubeSector(0.0));
        const std::array<SurfaceRef, 1> ground = { SurfaceRef{ 0, 5 } };
        const PreparedBatchUv prepared = prepareTransformSurfaceUv(level,
            ground, WorkbenchUvPoint{}, WorkbenchUvPoint{ 1.0, 1.0 }, 0.0,
            WorkbenchUvPoint{ 5.0, 5.0 });
        level.sectors[0].surfaces[5].texVertices[0].u = 99.0f;
        // Stale apply returns false from a successful preparation and
        // leaves the document byte-identical with no undo entry.
        sed::editor::Document document{};
        document.level() = level;
        const std::string before = serializedHash(document.level());
        const bool applied = document.applyUndoable(
            "Stale UV edit", sed::editor::DocumentDirty::LevelGeometry,
            [&prepared](sed::editor::DocumentState& state) {
                return prepared.apply(state.level);
            });
        check(prepared.ok() && !applied && document.undoDepth() == 0
                && serializedHash(document.level()) == before,
            "workbench.uv.staleApplyRefusesUnchanged");
    }

    // --- Batch UV: cylindrical projection -------------------------------
    {
        sed::Level level{};
        sed::Sector sector{};
        sector.addVertex(1.0, -1.0, 0.0);
        sector.addVertex(1.0, 1.0, 0.0);
        sector.addVertex(1.0, 1.0, 1.0);
        sector.addVertex(1.0, -1.0, 1.0);
        sed::Surface quad{};
        quad.vertices = { 0, 1, 2, 3 };
        quad.texVertices.assign(4u, sed::TexVertex{ 0.0f, 0.0f, sed::ColorBlack });
        quad.normal = sed::TVector{ 1.0, 0.0, 0.0 };
        sector.surfaces.push_back(quad);
        level.sectors.push_back(std::move(sector));

        const std::array<SurfaceRef, 1> target = { SurfaceRef{ 0, 0 } };
        const PreparedBatchUv prepared = prepareCylindricalSurfaceUv(level, target,
            sed::TVector{ 0, 0, 0 }, sed::TVector{ 0, 0, 1 }, 1.0);
        check(prepared.ok(), "workbench.uv.cylindricalPrepares");
        if (prepared.ok())
        {
            const double arc = std::acos(-1.0) / 4.0 * std::sqrt(2.0);
            check(uvNear(prepared.surfaces[0].after[0], -arc, 0.0)
                    && uvNear(prepared.surfaces[0].after[1], arc, 0.0)
                    && uvNear(prepared.surfaces[0].after[2], arc, 1.0)
                    && uvNear(prepared.surfaces[0].after[3], -arc, 1.0),
                "workbench.uv.cylindricalUnwrapsArcLengthAndHeight");
            sed::Level applied = level;
            check(prepared.apply(applied)
                    && uvNear(applied.sectors[0].surfaces[0].texVertices[1], arc, 0.0),
                "workbench.uv.cylindricalApplies");
        }
        const PreparedBatchUv zeroAxis = prepareCylindricalSurfaceUv(level, target,
            sed::TVector{ 0, 0, 0 }, sed::TVector{ 0, 0, 0 }, 1.0);
        check(!zeroAxis.ok() && zeroAxis.failure->code == "invalidBatchUvInput",
            "workbench.uv.cylindricalZeroAxisRefuses");
        const PreparedBatchUv zeroDensity = prepareCylindricalSurfaceUv(level, target,
            sed::TVector{ 0, 0, 0 }, sed::TVector{ 0, 0, 1 }, 0.0);
        check(!zeroDensity.ok() && zeroDensity.failure->code == "invalidBatchUvInput",
            "workbench.uv.cylindricalZeroDensityRefuses");
        const std::span<const SurfaceRef> noSurfaces{};
        const PreparedBatchUv empty = prepareCylindricalSurfaceUv(level, noSurfaces,
            sed::TVector{ 0, 0, 0 }, sed::TVector{ 0, 0, 1 }, 1.0);
        check(!empty.ok() && empty.failure->code == "emptyBatchUvSelection",
            "workbench.uv.cylindricalEmptySelectionRefuses");
    }

    // --- Batch UV: spherical projection ---------------------------------
    {
        sed::Level level{};
        sed::Sector sector{};
        sector.addVertex(1.0, -1.0, 0.0);
        sector.addVertex(1.0, 1.0, 0.0);
        sector.addVertex(1.0, 1.0, 1.0);
        sector.addVertex(1.0, -1.0, 1.0);
        sed::Surface quad{};
        quad.vertices = { 0, 1, 2, 3 };
        quad.texVertices.assign(4u, sed::TexVertex{ 0.0f, 0.0f, sed::ColorBlack });
        quad.normal = sed::TVector{ 1.0, 0.0, 0.0 };
        sector.surfaces.push_back(quad);
        level.sectors.push_back(std::move(sector));

        const std::array<SurfaceRef, 1> target = { SurfaceRef{ 0, 0 } };
        const PreparedBatchUv prepared = prepareSphericalSurfaceUv(level, target,
            sed::TVector{ 0, 0, 0 }, sed::TVector{ 0, 0, 1 }, 1.0);
        check(prepared.ok(), "workbench.uv.sphericalPrepares");
        if (prepared.ok())
        {
            const double pi = std::acos(-1.0);
            const double equatorArc = pi / 4.0 * std::sqrt(2.0);
            const double equatorV = pi / 2.0 * std::sqrt(2.0);
            const double topArc = pi / 4.0 * std::sqrt(3.0);
            const double topV = std::acos(1.0 / std::sqrt(3.0)) * std::sqrt(3.0);
            check(uvNear(prepared.surfaces[0].after[0], -equatorArc, equatorV)
                    && uvNear(prepared.surfaces[0].after[1], equatorArc, equatorV)
                    && uvNear(prepared.surfaces[0].after[2], topArc, topV)
                    && uvNear(prepared.surfaces[0].after[3], -topArc, topV),
                "workbench.uv.sphericalUnwrapsAzimuthAndPolarArcLength");
            sed::Level applied = level;
            check(prepared.apply(applied)
                    && uvNear(applied.sectors[0].surfaces[0].texVertices[1], equatorArc, equatorV),
                "workbench.uv.sphericalApplies");
        }
        const PreparedBatchUv zeroPole = prepareSphericalSurfaceUv(level, target,
            sed::TVector{ 0, 0, 0 }, sed::TVector{ 0, 0, 0 }, 1.0);
        check(!zeroPole.ok() && zeroPole.failure->code == "invalidBatchUvInput",
            "workbench.uv.sphericalZeroPoleRefuses");
        const PreparedBatchUv zeroDensity = prepareSphericalSurfaceUv(level, target,
            sed::TVector{ 0, 0, 0 }, sed::TVector{ 0, 0, 1 }, 0.0);
        check(!zeroDensity.ok() && zeroDensity.failure->code == "invalidBatchUvInput",
            "workbench.uv.sphericalZeroDensityRefuses");
        const std::span<const SurfaceRef> noSurfaces{};
        const PreparedBatchUv empty = prepareSphericalSurfaceUv(level, noSurfaces,
            sed::TVector{ 0, 0, 0 }, sed::TVector{ 0, 0, 1 }, 1.0);
        check(!empty.ok() && empty.failure->code == "emptyBatchUvSelection",
            "workbench.uv.sphericalEmptySelectionRefuses");
    }

    // --- Batch UV: bounds refuse rather than truncate -------------------
    {
        sed::Level level{};
        for (int i = 0; i < 257; ++i)
        {
            addSector(level, makeUvCubeSector(4.0 * i));
        }
        std::vector<SurfaceRef> atLimit{};
        for (int i = 0; i < 256; ++i)
        {
            atLimit.push_back(SurfaceRef{ i, 5 });
        }
        const PreparedBatchUv passing = prepareProjectSurfaceUv(level,
            atLimit, UvProjectionDirection::positiveZ,
            sed::TVector{ 0, 0, 0 }, 1.0);
        check(passing.ok() && passing.cornerCount == 1024,
            "workbench.uv.faceLimitAtLimitPasses");

        std::vector<SurfaceRef> overLimit = atLimit;
        overLimit.push_back(SurfaceRef{ 256, 5 });
        const PreparedBatchUv refused = prepareProjectSurfaceUv(level,
            overLimit, UvProjectionDirection::positiveZ,
            sed::TVector{ 0, 0, 0 }, 1.0);
        checkRefusedUnchanged(
            level, refused, "batchUvSurfaceLimit", "workbench.uv.faceLimitPlusOne");

        sed::Level polygonLevel{};
        addSector(polygonLevel, makePolygonSector(4096));
        const std::array<SurfaceRef, 1> big = { SurfaceRef{ 0, 0 } };
        const PreparedBatchUv cornersPass = prepareTransformSurfaceUv(
            polygonLevel, big, WorkbenchUvPoint{},
            WorkbenchUvPoint{ 1.0, 1.0 }, 0.0, WorkbenchUvPoint{ 1.0, 1.0 });
        check(cornersPass.ok() && cornersPass.cornerCount == 4096,
            "workbench.uv.cornerLimitAtLimitPasses");

        sed::Level overPolygonLevel{};
        addSector(overPolygonLevel, makePolygonSector(4097));
        const PreparedBatchUv cornersRefused = prepareTransformSurfaceUv(
            overPolygonLevel, big, WorkbenchUvPoint{},
            WorkbenchUvPoint{ 1.0, 1.0 }, 0.0, WorkbenchUvPoint{ 1.0, 1.0 });
        checkRefusedUnchanged(overPolygonLevel, cornersRefused,
            "batchUvCornerLimit", "workbench.uv.cornerLimitPlusOne");

        sed::Level level2{};
        level2.sectors.push_back(makeUvCubeSector(0.0));
        const std::array<SurfaceRef, 2> duplicated = {
            SurfaceRef{ 0, 5 }, SurfaceRef{ 0, 5 },
        };
        const PreparedBatchUv duplicate = prepareTransformSurfaceUv(level2,
            duplicated, WorkbenchUvPoint{}, WorkbenchUvPoint{ 1.0, 1.0 },
            0.0, WorkbenchUvPoint{});
        check(!duplicate.ok()
                && duplicate.failure->code == "duplicateBatchUvSurface",
            "workbench.uv.duplicateSurfaceRefuses");
        const PreparedBatchUv empty = prepareTransformSurfaceUv(level2,
            std::span<const SurfaceRef>{}, WorkbenchUvPoint{},
            WorkbenchUvPoint{ 1.0, 1.0 }, 0.0, WorkbenchUvPoint{});
        check(!empty.ok() && empty.failure->code == "emptyBatchUvSelection",
            "workbench.uv.emptySelectionRefuses");
        const std::array<SurfaceRef, 1> ground = { SurfaceRef{ 0, 5 } };
        const PreparedBatchUv edgeOn = prepareProjectSurfaceUv(level2,
            ground, UvProjectionDirection::positiveX,
            sed::TVector{ 0, 0, 0 }, 1.0);
        check(!edgeOn.ok() && edgeOn.failure->code == "batchUvEdgeOnSurface",
            "workbench.uv.edgeOnProjectionRefuses");
    }

    // --- Placement layouts ----------------------------------------------
    {
        PlacementLayout linear{};
        linear.kind = PlacementLayoutKind::linear;
        linear.origin = sed::TVector{ 0, 0, 0 };
        linear.step = sed::TVector{ 1, 2, 3 };
        linear.count = 3;
        const PlacementPositions line = computePlacementPositions(linear);
        check(line.ok() && line.positions.size() == 3
                && closeVec(line.positions[2], sed::TVector{ 2, 4, 6 }),
            "workbench.array.linearOrder");

        PlacementLayout grid{};
        grid.kind = PlacementLayoutKind::grid;
        grid.stepU = sed::TVector{ 1, 0, 0 };
        grid.stepV = sed::TVector{ 0, 1, 0 };
        grid.countU = 2;
        grid.countV = 2;
        const PlacementPositions cells = computePlacementPositions(grid);
        check(cells.ok() && cells.positions.size() == 4
                && closeVec(cells.positions[1], sed::TVector{ 1, 0, 0 })
                && closeVec(cells.positions[2], sed::TVector{ 0, 1, 0 }),
            "workbench.array.gridUVariesFastest");

        PlacementLayout radial{};
        radial.kind = PlacementLayoutKind::radial;
        radial.axis = sed::TVector{ 0, 0, 1 };
        radial.radius = 2.0;
        radial.count = 4;
        radial.startDegrees = 0.0;
        radial.sweepDegrees = 360.0;
        const PlacementPositions ring = computePlacementPositions(radial);
        check(ring.ok() && ring.positions.size() == 4
                && closeVec(ring.positions[0], sed::TVector{ 2, 0, 0 })
                && closeVec(ring.positions[1], sed::TVector{ 0, 2, 0 })
                && closeVec(ring.positions[3], sed::TVector{ 0, -2, 0 }),
            "workbench.array.radialOrderEndpointExcluded");

        radial.count = 2;
        radial.sweepDegrees = 180.0;
        const PlacementPositions half = computePlacementPositions(radial);
        check(half.ok()
                && closeVec(half.positions[1], sed::TVector{ 0, 2, 0 }),
            "workbench.array.radialPartialSweepExcludesEndpoint");

        PlacementLayout zeroStep = linear;
        zeroStep.step = sed::TVector{ 0, 0, 0 };
        zeroStep.count = 2;
        const PlacementPositions duplicates =
            computePlacementPositions(zeroStep);
        check(!duplicates.ok()
                && duplicates.failure->code == "duplicatePlacementPosition",
            "workbench.array.duplicatePositionsRefuse");

        PlacementLayout overCount = linear;
        overCount.count = 257;
        const PlacementPositions refused =
            computePlacementPositions(overCount);
        check(!refused.ok()
                && refused.failure->code == "placementArrayLimit",
            "workbench.array.countLimitPlusOneRefuses");

        PlacementLayout bigGrid = grid;
        bigGrid.countU = 17;
        bigGrid.countV = 17;
        check(!computePlacementPositions(bigGrid).ok(),
            "workbench.array.gridProductLimitRefuses");
        bigGrid.countU = 16;
        bigGrid.countV = 16;
        check(computePlacementPositions(bigGrid).ok(),
            "workbench.array.gridProductAtLimitPasses");

        PlacementLayout zeroSweep = radial;
        zeroSweep.sweepDegrees = 0.0;
        check(!computePlacementPositions(zeroSweep).ok(),
            "workbench.array.zeroSweepRefuses");
    }

    // --- Thing arrays: apply, undo, determinism --------------------------
    {
        sed::editor::Document document{};
        document.level() = makeArrayLevel();
        const std::string beforeHash = serializedHash(document.level());
        PlacementLayout layout{};
        layout.kind = PlacementLayoutKind::radial;
        layout.origin = sed::TVector{ 0, 0, 0 };
        layout.axis = sed::TVector{ 0, 0, 1 };
        layout.radius = 0.5;
        layout.count = 4;
        layout.sweepDegrees = 360.0;
        const PlacementPositions ring = computePlacementPositions(layout);
        const PreparedThingArray prepared = prepareThingArray(
            document.level(), ring.positions, "crate", "box",
            sed::TVector{ 5, 6, 7 }, 0, std::nullopt);
        check(prepared.ok(), "workbench.thingArray.prepares");
        const bool applied = document.applyUndoable(
            "Create thing array", sed::editor::DocumentDirty::All,
            [&prepared](sed::editor::DocumentState& state) {
                return prepared.apply(state.level);
            });
        check(applied && document.level().things.size() == 4
                && prepared.firstCreatedIndex
                    == std::optional<std::size_t>{ 0 },
            "workbench.thingArray.appliesInOrder");
        bool orientationsFixed = true;
        bool namesOrdered = true;
        bool sectorsResolved = true;
        for (std::size_t i = 0; i < 4; ++i)
        {
            const sed::Thing& thing = document.level().things[i];
            orientationsFixed = orientationsFixed && thing.pch == 5.0
                && thing.yaw == 6.0 && thing.rol == 7.0;
            namesOrdered = namesOrdered
                && thing.name == "box_00" + std::to_string(i);
            sectorsResolved = sectorsResolved && thing.sector == 0
                && thing.templateName == "crate" && thing.values.empty()
                && thing.archLightingIndex == -1;
        }
        check(orientationsFixed, "workbench.thingArray.radialDoesNotRotate");
        check(namesOrdered, "workbench.thingArray.deterministicNames");
        check(sectorsResolved, "workbench.thingArray.freshThingsOnly");
        check(document.level().things[1].x == ring.positions[1].x
                && document.level().things[1].y == ring.positions[1].y,
            "workbench.thingArray.positionsFollowLayout");
        check(document.undo() && document.level().things.empty()
                && serializedHash(document.level()) == beforeHash,
            "workbench.thingArray.undoRemovesExactlyArray");
        check(document.redo() && document.level().things.size() == 4,
            "workbench.thingArray.redoRestoresArray");

        // Same input on a fresh identical level yields the same ids.
        sed::Level again = makeArrayLevel();
        const PreparedThingArray repeated =
            prepareThingArray(again, ring.positions, "crate", "box",
                sed::TVector{ 5, 6, 7 }, 0, std::nullopt);
        check(repeated.ok() && repeated.apply(again)
                && again.things[3].id == document.level().things[3].id
                && again.things[3].name == document.level().things[3].name,
            "workbench.thingArray.deterministicIds");
    }

    // --- Thing arrays: bounds and refusals -------------------------------
    {
        sed::Level level = makeArrayLevel();
        std::vector<sed::TVector> atLimit{};
        for (int i = 0; i < 256; ++i)
        {
            atLimit.push_back(sed::TVector{ -0.9 + 0.007 * i, 0, 0 });
        }
        const PreparedThingArray passing = prepareThingArray(
            level, atLimit, "crate", "box", sed::TVector{}, 0, std::nullopt);
        check(passing.ok(), "workbench.thingArray.atLimitPasses");

        std::vector<sed::TVector> overLimit = atLimit;
        overLimit.push_back(sed::TVector{ 0.9, 0.5, 0 });
        const PreparedThingArray refused = prepareThingArray(
            level, overLimit, "crate", "box", sed::TVector{}, 0, std::nullopt);
        checkRefusedUnchanged(level, refused, "placementArrayLimit",
            "workbench.thingArray.limitPlusOneRefuses");

        const std::array<sed::TVector, 1> one = { sed::TVector{ 0, 0, 0 } };
        const PreparedThingArray patchFull = prepareThingArray(
            level, one, "crate", "box", sed::TVector{}, 0, std::nullopt, 1024);
        checkRefusedUnchanged(level, patchFull, "placementPatchLimit",
            "workbench.thingArray.patchLimitPlusOneRefuses");
        const PreparedThingArray patchAtLimit = prepareThingArray(level,
            atLimit, "crate", "box", sed::TVector{}, 0, std::nullopt, 768);
        check(patchAtLimit.ok(), "workbench.thingArray.patchAtLimitPasses");

        sed::Thing& existing = level.newThing();
        existing.name = "BOX_001";
        const std::array<sed::TVector, 2> two = {
            sed::TVector{ -0.5, 0, 0 }, sed::TVector{ 0.5, 0, 0 },
        };
        const PreparedThingArray collision = prepareThingArray(
            level, two, "crate", "box", sed::TVector{}, 0, std::nullopt);
        checkRefusedUnchanged(level, collision, "placementNameCollision",
            "workbench.thingArray.nameCollisionRefuses");

        const PreparedThingArray noTemplate = prepareThingArray(
            level, one, "missing", "box", sed::TVector{}, 0, std::nullopt);
        check(!noTemplate.ok()
                && noTemplate.failure->code == "placementTemplateNotFound",
            "workbench.thingArray.missingTemplateRefuses");

        const std::array<sed::TVector, 1> outside = {
            sed::TVector{ 5, 0, 0 },
        };
        const PreparedThingArray outsideSector = prepareThingArray(
            level, outside, "crate", "box", sed::TVector{}, 0, std::nullopt);
        check(!outsideSector.ok()
                && outsideSector.failure->code == "placementOutsideSector",
            "workbench.thingArray.outsideSectorRefuses");

        const std::array<sed::TVector, 1> boundary = {
            sed::TVector{ 1, 0, 0 },
        };
        const PreparedThingArray onBoundary = prepareThingArray(
            level, boundary, "crate", "box", sed::TVector{}, 0, std::nullopt);
        check(!onBoundary.ok()
                && onBoundary.failure->code == "placementOnSectorBoundary",
            "workbench.thingArray.boundaryPointRefuses");
    }

    // --- Thing arrays: overlap, explicit sector, COG guard, stale --------
    {
        sed::Level level{};
        level.templates.push_back(
            sed::ThingTemplate{ "crate", "", {}, "", {} });
        addSector(level, makeTestCubeSector(0.0));
        addSector(level, makeTestCubeSector(0.0));
        const std::array<sed::TVector, 1> centre = {
            sed::TVector{ 0, 0, 0 },
        };
        const PreparedThingArray ambiguous = prepareThingArray(
            level, centre, "crate", "box", sed::TVector{}, 0, std::nullopt);
        check(!ambiguous.ok()
                && ambiguous.failure->code == "placementAmbiguousSector",
            "workbench.thingArray.ambiguousSectorRefuses");
        const PreparedThingArray pinned = prepareThingArray(
            level, centre, "crate", "box", sed::TVector{}, 0, 1);
        check(pinned.ok() && pinned.sectorIndices[0] == 1,
            "workbench.thingArray.explicitSectorResolvesOverlap");

        sed::Level guarded = makeArrayLevel();
        guarded.newThing();
        sed::Cog& cog = guarded.newCog();
        cog.name = "guard.cog";
        sed::CogValue value{};
        value.name = "param0";
        value.type = sed::ValueType::str;
        value.stringValue = "0";
        cog.values.push_back(value);
        const PreparedThingArray unguarded = prepareThingArray(
            guarded, centre, "crate", "box", sed::TVector{}, 0, std::nullopt);
        check(!unguarded.ok()
                && unguarded.failure->code == "cogAppendGuardRequired",
            "workbench.thingArray.cogGuardRequired");
        const CogSchemaResolver resolver =
            [](const std::string_view name)
                -> std::optional<sed::assets::CogSchema> {
            return sed::assets::parseCogSchema(
                std::string(name), "symbols\nthing target\nend");
        };
        const PreparedThingArray guardedOk = prepareThingArray(guarded,
            centre, "crate", "box", sed::TVector{}, 0, std::nullopt, 0,
            resolver);
        check(guardedOk.ok(), "workbench.thingArray.cogGuardPasses");

        sed::Level stale = makeArrayLevel();
        const PreparedThingArray preparedStale = prepareThingArray(
            stale, centre, "crate", "box", sed::TVector{}, 0, std::nullopt);
        sed::Thing& blocker = stale.newThing();
        blocker.name = "box_000";
        sed::editor::Document document{};
        document.level() = stale;
        const std::string before = serializedHash(document.level());
        const bool applied = document.applyUndoable(
            "Stale thing array", sed::editor::DocumentDirty::All,
            [&preparedStale](sed::editor::DocumentState& state) {
                return preparedStale.apply(state.level);
            });
        check(preparedStale.ok() && !applied && document.undoDepth() == 0
                && serializedHash(document.level()) == before,
            "workbench.thingArray.staleApplyRefusesUnchanged");
    }

    // --- Light arrays ------------------------------------------------------
    {
        sed::editor::Document document;
        document.level() = makeArrayLevel();
        document.level().kind = sed::ProjectType::MOTS;
        const std::array<sed::TVector, 1> positions{{{0,0,0}}};
        for ( const auto rgb : {std::optional<double>{}, std::optional<double>{0.0},
                std::optional<double>{0.75}, std::optional<double>{1000.0}} )
        {
            const auto prepared = prepareLightArray(document.level(), positions, sed::ColorWhite,
                0.75, 2.0, 0, 0, 0, rgb);
            check(prepared.ok() && document.applyUndoable("RGB array", sed::editor::DocumentDirty::Lights,
                [&](sed::editor::DocumentState& state) { return prepared.apply(state.level); }),
                "workbench.lightArray.nullableRgbApplies");
            check(!document.level().lights.empty() && document.level().lights.back().rgbIntensity == rgb,
                "workbench.lightArray.preservesAbsentZeroEqualAndBoundaryRgb");
        }
        check(document.undo() && document.level().lights.size() == 3
            && document.redo() && document.level().lights.back().rgbIntensity == 1000.0,
            "workbench.lightArray.nullableRgbUndoRedo");
        for ( const double bad : {-1.0, 1000.01, std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::quiet_NaN()} )
        {
            const auto refused = prepareLightArray(document.level(), positions, sed::ColorWhite,
                0.75, 2.0, 0, 0, 0, bad);
            checkRefusedUnchanged(document.level(), refused, "invalidPlacementInput",
                "workbench.lightArray.invalidRgbRefusesUnchanged");
        }
        const auto prepared = prepareLightArray(document.level(), positions, sed::ColorWhite,
            0.75, 2.0, 0, 0, 0, 0.0);
        document.level().kind = sed::ProjectType::JKDF2;
        const auto before = serializedHash(document.level());
        check(!prepared.apply(document.level()) && serializedHash(document.level()) == before,
            "workbench.lightArray.staleProjectRefusesRgbUnchanged");
        const auto refused = prepareLightArray(document.level(), positions, sed::ColorWhite,
            0.75, 2.0, 0, 0, 0, 0.0);
        checkRefusedUnchanged(document.level(), refused, "unsupportedPlacementGame",
            "workbench.lightArray.classicRgbRefusesUnchanged");
    }
    {
        sed::editor::Document document{};
        document.level() = makeArrayLevel();
        PlacementLayout grid{};
        grid.kind = PlacementLayoutKind::grid;
        grid.origin = sed::TVector{ -0.5, -0.5, 0 };
        grid.stepU = sed::TVector{ 0.5, 0, 0 };
        grid.stepV = sed::TVector{ 0, 0.5, 0 };
        grid.countU = 2;
        grid.countV = 2;
        const PlacementPositions cells = computePlacementPositions(grid);
        const PreparedLightArray prepared = prepareLightArray(
            document.level(), cells.positions,
            sed::ColorF{ 0.25f, 0.5f, 0.75f, 1.0f }, 3.0, 4.0, 1, 0);
        check(prepared.ok(), "workbench.lightArray.prepares");
        const bool applied = document.applyUndoable(
            "Create light array", sed::editor::DocumentDirty::All,
            [&prepared](sed::editor::DocumentState& state) {
                return prepared.apply(state.level);
            });
        check(applied && document.level().lights.size() == 4,
            "workbench.lightArray.appliesInOrder");
        const sed::SedLight& light = document.level().lights[3];
        check(light.position.x == cells.positions[3].x
                && light.position.y == cells.positions[3].y
                && light.intensity == 3.0 && light.range == 4.0
                && light.flags == 1
                && light.color == sed::ColorF(0.25f, 0.5f, 0.75f, 1.0f),
            "workbench.lightArray.fieldsPreserved");
        check(document.undo() && document.level().lights.empty()
                && document.redo() && document.level().lights.size() == 4,
            "workbench.lightArray.undoRedo");

        sed::Level level = makeArrayLevel();
        std::vector<sed::TVector> many{};
        for (int i = 0; i < 257; ++i)
        {
            many.push_back(sed::TVector{ 0.01 * i, 0, 0 });
        }
        const PreparedLightArray refused = prepareLightArray(
            level, many, sed::ColorWhite, 1.0, 1.0, 0, 0);
        checkRefusedUnchanged(level, refused, "placementArrayLimit",
            "workbench.lightArray.limitPlusOneRefuses");
        const std::array<sed::TVector, 1> one = { sed::TVector{ 0, 0, 0 } };
        const PreparedLightArray patchRefused = prepareLightArray(
            level, one, sed::ColorWhite, 1.0, 1.0, 0, 0, 1024);
        check(!patchRefused.ok()
                && patchRefused.failure->code == "placementPatchLimit",
            "workbench.lightArray.patchLimitPlusOneRefuses");

        const PreparedLightArray badIntensity = prepareLightArray(
            level, one, sed::ColorWhite, -1.0, 1.0, 0, 0);
        check(!badIntensity.ok()
                && badIntensity.failure->code == "invalidPlacementInput",
            "workbench.lightArray.negativeIntensityRefuses");
        const PreparedLightArray badLayer = prepareLightArray(
            level, one, sed::ColorWhite, 1.0, 1.0, 0, 5);
        check(!badLayer.ok()
                && badLayer.failure->code == "invalidPlacementLayer",
            "workbench.lightArray.unknownLayerRefuses");

        sed::Level ijim = makeArrayLevel();
        ijim.kind = sed::ProjectType::IJIM;
        const PreparedThingArray wrongGame = prepareThingArray(
            ijim, one, "crate", "box", sed::TVector{}, 0, std::nullopt);
        check(!wrongGame.ok()
                && wrongGame.failure->code == "unsupportedPlacementGame",
            "workbench.thingArray.ijimRefuses");
    }
}

} // namespace sed::tests
