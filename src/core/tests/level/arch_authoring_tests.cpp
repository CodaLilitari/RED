#include "test_common.hpp"

#include "sed/assets/jkl.hpp"
#include "sed/editor/document.hpp"
#include "sed/level/arch_authoring.hpp"

#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace sed::tests
{
namespace
{

using namespace sed::level;

MotsArchLightingTopology probeTopology()
{
    MotsArchLightingTopology topology{};
    topology.geosets = { { 2 }, { 1 } };
    return topology;
}

sed::Level makeAuthoringLevel()
{
    sed::Level level{};
    level.kind = sed::ProjectType::MOTS;
    level.header.version = 1;

    sed::MotsArchLightObject imported{};
    imported.wireIndex = 0;
    sed::MotsArchLightMesh mesh{};
    mesh.wireIndex = 0;
    mesh.vertices = {
        sed::MotsArchLightVertex{ 0.25f, 0.5f, 0.75f, 1.0f },
        sed::MotsArchLightVertex{ 0.125f, 0.25f, 0.375f, 0.5f } };
    imported.meshes.push_back(mesh);
    level.motsArchLighting.push_back(imported);

    const auto addThing = [&level](const char* name)
    {
        sed::Thing& thing = level.newThing();
        thing.templateName = "arch_probe";
        thing.name = name;
        thing.archLightingIndex = 0;
        sed::TplValue flags{};
        flags.name = "thingflags";
        flags.type = sed::ValueType::integer;
        flags.intValue = 0x8;
        thing.values.push_back(flags);
        sed::TplValue model{};
        model.name = "model3d";
        model.type = sed::ValueType::str;
        model.stringValue = "probe.3do";
        thing.values.push_back(model);
    };
    addThing("target");
    addThing("sharer");
    return level;
}

bool applyThroughDocument(
    sed::editor::Document& document, const PreparedArchAuthoring& prepared)
{
    return document.applyUndoable(
        "ARCH authoring", sed::editor::DocumentDirty::All,
        [&prepared](sed::editor::DocumentState& state)
        {
            return prepared.apply(state.level);
        });
}

std::vector<std::vector<sed::TVector>> probeVertices()
{
    return { { sed::TVector{ 0.0, 0.0, 0.0 }, sed::TVector{ 1.0, 0.0, 0.0 } } };
}

sed::SedLight probeLight()
{
    sed::SedLight light{};
    light.position = sed::TVector{ 0.0, 0.0, 0.0 };
    light.range = 2.0;
    light.intensity = 4.0;
    light.color = sed::ColorF{ 1.0f, 0.5f, 0.0f, 1.0f };
    return light;
}

} // namespace

void runArchAuthoringTests()
{
    const MotsArchLightingTopology topology = probeTopology();
    const std::string modelHash(64, 'a');

    // Imported samples start unknown, and no origin token ever
    // claims an editor bake for them.
    {
        const sed::Level level = makeAuthoringLevel();
        check(archBindingStatus(level, 0) == ArchBindingStatus::UnknownImported,
            "archAuthoring.provenance.importedUnknown");
        bool anyBakeClaim = false;
        const std::array<MotsArchSampleOrigin, 5> origins{
            MotsArchSampleOrigin::ImportedUnknown,
            MotsArchSampleOrigin::BoundExistingSamples,
            MotsArchSampleOrigin::ResetZeroed,
            MotsArchSampleOrigin::BakedSourcePointsV1,
            MotsArchSampleOrigin::BakedWorldOccludedV1 };
        for (const MotsArchSampleOrigin origin : origins)
        {
            anyBakeClaim = anyBakeClaim
                || archSampleOriginToken(origin) == "verified-bake"
                || archSampleOriginToken(origin) == "verifiedBake";
        }
        check(!anyBakeClaim
                && archSampleOriginToken(MotsArchSampleOrigin::ImportedUnknown)
                    == "imported-unknown"
                && archSampleOriginToken(MotsArchSampleOrigin::BoundExistingSamples)
                    == "bound-existing"
                && archSampleOriginToken(MotsArchSampleOrigin::BakedSourcePointsV1)
                    == "baked-source-points-v1",
            "archAuthoring.provenance.tokensDistinct");
    }

    // Bind takes a private copy, touches only the target, and
    // undo restores the exact prior binding; redo replays it.
    {
        sed::editor::Document document{};
        document.level() = makeAuthoringLevel();
        const std::size_t objectsBefore =
            document.level().motsArchLighting.size();

        const PreparedArchAuthoring prepared = prepareBindArchSamples(
            document.level(), 0, 0, topology, "probe.3do", modelHash);
        check(prepared.ok(), "archAuthoring.bind.prepared");
        check(applyThroughDocument(document, prepared)
                && document.undoDepth() == 1,
            "archAuthoring.bind.applied");
        check(document.level().things[0].archLightingIndex == 1
                && document.level().things[1].archLightingIndex == 0
                && document.level().motsArchLighting.size() == objectsBefore + 1
                && document.level().motsArchLighting[1].meshes
                    == document.level().motsArchLighting[0].meshes,
            "archAuthoring.bind.privateCopyOnlyTarget");
        check(archBindingStatus(document.level(), 0)
                == ArchBindingStatus::BoundExistingSamples,
            "archAuthoring.bind.staysNotABake");

        check(document.undo()
                && document.level().things[0].archLightingIndex == 0
                && document.level().motsArchLighting.size() == objectsBefore
                && document.level().motsArchAuthoring.empty()
                && archBindingStatus(document.level(), 0)
                    == ArchBindingStatus::UnknownImported,
            "archAuthoring.bind.undoRestoresExactPrior");
        check(document.redo()
                && document.level().things[0].archLightingIndex == 1
                && document.level().motsArchLighting[1].meshes
                    == document.level().motsArchLighting[0].meshes
                && archBindingStatus(document.level(), 0)
                    == ArchBindingStatus::BoundExistingSamples,
            "archAuthoring.bind.redoReplaysCapturedSamples");
    }

    // A bind whose lower LOD overflows is refused unchanged.
    {
        const sed::Level level = makeAuthoringLevel();
        MotsArchLightingTopology overflow = topology;
        overflow.geosets = { { 2 }, { 3 } };
        const PreparedArchAuthoring prepared = prepareBindArchSamples(
            level, 0, 0, overflow, "probe.3do", modelHash);
        check(!prepared.ok()
                && prepared.failure->code == "archLowerLodOverflow",
            "archAuthoring.bind.lowerLodOverflowRefused");
        sed::Level copy = level;
        check(!prepared.apply(copy)
                && copy.things[0].archLightingIndex == 0
                && copy.motsArchLighting.size() == 1,
            "archAuthoring.bind.refusalUnchanged");
    }

    // Reset allocates zeroed samples for the current model.
    {
        sed::editor::Document document{};
        document.level() = makeAuthoringLevel();
        const PreparedArchAuthoring prepared = prepareResetArchBinding(
            document.level(), 0, topology, "probe.3do", modelHash);
        check(prepared.ok() && applyThroughDocument(document, prepared),
            "archAuthoring.reset.applied");
        const sed::MotsArchLightObject& bound =
            document.level().motsArchLighting.back();
        check(bound.meshes.size() == 1 && bound.meshes[0].vertices.size() == 2
                && isDefaultMotsArchLighting(bound)
                && archBindingStatus(document.level(), 0)
                    == ArchBindingStatus::ResetZeroed,
            "archAuthoring.reset.zeroedAllocation");
        check(document.undo()
                && document.level().things[0].archLightingIndex == 0,
            "archAuthoring.reset.undoRestoresPrior");
    }

    // Bake uses the explicit source-points recipe, keeps null
    // and explicit-zero RGB distinct, and tracks its inputs.
    {
        sed::editor::Document document{};
        document.level() = makeAuthoringLevel();
        const std::vector<std::vector<sed::TVector>> vertices = probeVertices();
        const std::array<sed::SedLight, 1> lights{ probeLight() };

        const PreparedArchAuthoring prepared = prepareBakeArchSamples(
            document.level(), 0, topology, vertices, lights,
            ArchBakeProfile::SourcePointsV1, {}, "probe.3do", modelHash);
        check(prepared.ok() && applyThroughDocument(document, prepared),
            "archAuthoring.bake.applied");
        const sed::MotsArchLightObject& baked =
            document.level().motsArchLighting.back();
        check(baked.meshes[0].vertices.size() == 2
                && close(baked.meshes[0].vertices[0].mono, 4.0)
                && close(baked.meshes[0].vertices[0].red, 4.0)
                && close(baked.meshes[0].vertices[0].green, 2.0)
                && close(baked.meshes[0].vertices[1].mono, 1.0)
                && close(baked.meshes[0].vertices[1].red, 1.0),
            "archAuthoring.bake.sourcePointsValues");
        const std::string inputDigest = archBakeInputDigestHex(
            ArchBakeProfile::SourcePointsV1, lights, vertices);
        check(archBindingStatus(document.level(), 0, inputDigest)
                == ArchBindingStatus::BakedCurrentInputs
                && archBindingStatus(document.level(), 0)
                    == ArchBindingStatus::BakedSamplesIntact
                && archBindingStatus(document.level(), 0,
                        std::string_view("stale"))
                    == ArchBindingStatus::BakedStaleInputs,
            "archAuthoring.bake.inputStatus");
        document.level().motsArchLighting.back().meshes[0].vertices[0].mono =
            9.0f;
        check(archBindingStatus(document.level(), 0, inputDigest)
                == ArchBindingStatus::BakedOverridden,
            "archAuthoring.bake.overriddenStatus");

        sed::SedLight zeroRgb = probeLight();
        zeroRgb.rgbIntensity = 0.0;
        const std::array<sed::SedLight, 1> zeroLights{ zeroRgb };
        const PreparedArchAuthoring zeroBake = prepareBakeArchSamples(
            makeAuthoringLevel(), 0, topology, vertices, zeroLights,
            ArchBakeProfile::SourcePointsV1, {}, "probe.3do", modelHash);
        check(zeroBake.ok()
                && zeroBake.newObject.meshes[0].vertices[0].mono == 4.0f
                && zeroBake.newObject.meshes[0].vertices[0].red == 0.0f,
            "archAuthoring.bake.explicitZeroRgbDistinct");
    }

    // The occluded profile blocks lit segments; NoBlock bypasses.
    {
        const std::vector<std::vector<sed::TVector>> vertices = probeVertices();
        const std::array<sed::SedLight, 1> lights{ probeLight() };
        const std::array<ArchOccluderTriangle, 1> wall{
            ArchOccluderTriangle{
                sed::TVector{ 0.5, -1.0, -1.0 },
                sed::TVector{ 0.5, 1.0, -1.0 },
                sed::TVector{ 0.5, 0.0, 1.0 } } };
        const PreparedArchAuthoring occluded = prepareBakeArchSamples(
            makeAuthoringLevel(), 0, topology, vertices, lights,
            ArchBakeProfile::SourcePointsWorldOccludedV1, wall,
            "probe.3do", modelHash);
        check(occluded.ok()
                && occluded.newObject.meshes[0].vertices[0].mono == 4.0f
                && occluded.newObject.meshes[0].vertices[1].mono == 0.0f,
            "archAuthoring.bake.occludedBlocks");

        sed::SedLight piercing = probeLight();
        piercing.flags = 0x1;
        const std::array<sed::SedLight, 1> piercingLights{ piercing };
        const PreparedArchAuthoring bypassed = prepareBakeArchSamples(
            makeAuthoringLevel(), 0, topology, vertices, piercingLights,
            ArchBakeProfile::SourcePointsWorldOccludedV1, wall,
            "probe.3do", modelHash);
        check(bypassed.ok()
                && bypassed.newObject.meshes[0].vertices[1].mono == 1.0f,
            "archAuthoring.bake.noBlockBypasses");
    }

    // Unbind retains the arrays and clears only the target.
    {
        sed::editor::Document document{};
        document.level() = makeAuthoringLevel();
        const PreparedArchAuthoring prepared =
            prepareUnbindArch(document.level(), 0);
        check(prepared.ok() && applyThroughDocument(document, prepared),
            "archAuthoring.unbind.applied");
        check(document.level().things[0].archLightingIndex == -1
                && document.level().things[1].archLightingIndex == 0
                && document.level().motsArchLighting.size() == 1
                && archBindingStatus(document.level(), 0)
                    == ArchBindingStatus::Unbound,
            "archAuthoring.unbind.retainsArrays");
        check(document.undo()
                && document.level().things[0].archLightingIndex == 0,
            "archAuthoring.unbind.undoRestores");
    }

    // Provenance survives a JKL save/reopen; edited arrays with
    // stale authoring metadata are rejected, not applied.
    {
        sed::Level level = makeAuthoringLevel();
        const PreparedArchAuthoring prepared = prepareBindArchSamples(
            level, 0, 0, topology, "probe.3do", modelHash);
        check(prepared.ok() && prepared.apply(level),
            "archAuthoring.roundTrip.applied");
        std::ostringstream output;
        sed::assets::saveToJKL(level, output);
        const std::string text = output.str();
        check(text.find("Section: SED_ARCH_AUTHORING\n") != std::string::npos
                && text.find("origin=bound-existing") != std::string::npos,
            "archAuthoring.roundTrip.sectionEmitted");

        std::istringstream input(text);
        sed::assets::JklLoadOptions options{};
        options.projectType = sed::ProjectType::MOTS;
        const sed::Level loaded = sed::assets::loadFromJKL(input, options);
        check(loaded.motsArchAuthoring.size() == 1
                && loaded.motsArchAuthoring[0].origin
                    == MotsArchSampleOrigin::BoundExistingSamples
                && archBindingStatus(loaded, 0)
                    == ArchBindingStatus::BoundExistingSamples,
            "archAuthoring.roundTrip.provenanceSurvives");

        std::string tampered = text;
        const std::size_t samplePos = tampered.find("0: 0.25000000");
        check(samplePos != std::string::npos,
            "archAuthoring.roundTrip.tamperAnchor");
        if (samplePos != std::string::npos)
        {
            tampered[samplePos + 3] = '9';
            checkThrowsWithMessage(
                [&tampered]
                {
                    std::istringstream tamperedInput(tampered);
                    sed::assets::JklLoadOptions loadOptions{};
                    loadOptions.projectType = sed::ProjectType::MOTS;
                    (void)sed::assets::loadFromJKL(tamperedInput, loadOptions);
                },
                "Cannot load JKL/NDY SED_ARCH_AUTHORING: ARCH_SHA256 does not match the ARCHLIGHTING arrays; metadata rejected.",
                "archAuthoring.roundTrip.staleMetadataRejected");
        }
    }
}

} // namespace sed::tests
