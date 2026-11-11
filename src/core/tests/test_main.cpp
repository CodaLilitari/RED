#include "test_common.hpp"

#include <cstring>
#include <exception>
#include <iostream>
#include <string>

namespace sed::tests
{

void runGeometryMathTests();
void runLevelTests();
void runLevelGeometryTests();
void runLevelLightingTests();
void runLightLimitsTests();
void runLevelPickingTests();
void runLevelAdjoinTests();
void runLevelBuildTests();
void runLevelExtrudeTests();
void runExtrudeCollinearSupportTests();
void runExtrudeContainedRingTests();
void runExtrudeArrangementRegistryTests();
void runLevelClosureContractTests();
void runLevelCleaveTests();
void runCleaveSubdividedPortalTests();
void runLevelConnectContractTests();
void runLevelCogReferenceTests();
void runLevelClipboardTests();
void runLevelEditTests();
void runLevelMergeTests();
void runLevelSurfaceMergeUvRegressionTests();
void runLevelThingTests();
void runThingFramesTests();
void runRecipeRequestTests();
void runLevelTransformTests();
void runConnectedSurfaceUvTests();
void runSurfaceExtrudeUvRegressionTests();
void runLevelUvTests();
void runLevelLayerTests();
void runEditorTests();
void runPendingCogImportTests();
void runLevelBackupTests();
void runSurfaceAngleTests();
void runShapeTplRemoveTests();
void runSurfaceFlagRepairTests();
void runSealedSeamTests();
void runComponentRebuildTests();
void runMakeDoorTests();
void runRecipeTests();
void runPrefabTests();
void runStatisticsTests();
void runFanDegeneracyTests();
void runDxfTests();
void runJedPluginHostTests();
void runJedPluginBridgeTests();
void runTextTests();
void runFloatCharsTests();
void runBinaryReaderTests();

void runJklLevelTests();
void runJklStructureTests();
void runJklTemplateClosureTests();
void runArchLightingTests();
void runMotsArchLightingClipboardTests();
void runArchPreviewTests();
void runJklSaveTests();
void runJklPreservationParityTests();
void runJklExportTransactionTests();
void runJklGeoPreservationTests();
void runJklLegacyFrameTests();
void runNdySaveTests();
void runJedLevelTests();
void runSedLevelTests();
bool writeSedLevelTestVectors();
bool writeSedIjimTestVectors();
void runModel3doTests();
void runModel3doWriterTests();
void runMatTransparencyTests();
void runKeyTests();
void runTplTests();
void runResourceStreamFailureTests();
void runResourceLocatorSharingTests();
void runResourceLocatorOverlayTests();
void runItemsDatTests();
void runMatCmpTests();
void runAssetWriterTests();
void runObjImportTests();
void runGltfImportTests();
void runJpegTests();
void runModelsDatTests();
void runParTests();
void runPupTests();
void runResourceLocatorDiskTests();
void runResourceLocatorBoundedReadTests();
void runCogScriptTests();
void runCogSchemaTests();
void runCogPreflightTests();
void runCogBindingTests();
void runCogMutationTests();
void runSpatialSelectionTests();
void runDynamicVertexColorTests();
void runWorkbenchTests();
void runPackageTests();
void runOverviewTests();
void runSoftwareSnapshotTests();
void runUpdaterTests();
void runSourceLightMetadataTests();
void runSedExtensionTests();
void runArchValidationTests();
void runArchAuthoringTests();
void runSndTests();
void runStringTableTests();
void runSprTests();
void runGobTests();
void runIndyWvTests();
void runJlhTests();
void runDf2JkTests();
void runEpisodeTests();
void runAscTests();
void runLevTests();
void runWavTests();

} // namespace sed::tests

int main(const int argc, char** argv)
{
    if ( argc == 2 && std::strcmp(argv[1], "--light-limits") == 0 )
    {
        sed::tests::runLightLimitsTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if ( argc == 2 && std::strcmp(argv[1], "--jkl-template-closure") == 0 )
    {
        sed::tests::runJklTemplateClosureTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if ( argc == 2 && std::strcmp(argv[1], "--jkl-structure") == 0 )
    {
        sed::tests::runJklStructureTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if ( argc == 2 && std::strcmp(argv[1], "--sealed-seams") == 0 )
    {
        sed::tests::runSealedSeamTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--write-sed-vectors") == 0)
    {
        return sed::tests::writeSedLevelTestVectors() ? 0 : 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--write-sed-ijim-vector") == 0)
    {
        return sed::tests::writeSedIjimTestVectors() ? 0 : 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--level-clipboard") == 0)
    {
        sed::tests::runLevelClipboardTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--mat-cmp") == 0)
    {
        sed::tests::runMatCmpTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--ndy-save") == 0)
    {
        sed::tests::runNdySaveTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--cog-script") == 0)
    {
        sed::tests::runCogScriptTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--arch-lighting") == 0)
    {
        sed::tests::runArchLightingTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--arch-lighting-clipboard") == 0)
    {
        sed::tests::runMotsArchLightingClipboardTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--arch-preview") == 0)
    {
        sed::tests::runArchPreviewTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--jkl") == 0)
    {
        sed::tests::runJklLevelTests();
        sed::tests::runJklSaveTests();
        sed::tests::runNdySaveTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--lev") == 0)
    {
        sed::tests::runLevTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--resource-authoring") == 0)
    {
        sed::tests::runSndTests();
        sed::tests::runStringTableTests();
        sed::tests::runSprTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if (argc == 2 && std::strcmp(argv[1], "--resource-bounded") == 0)
    {
        sed::tests::runResourceLocatorBoundedReadTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }

    if ( argc == 2 && std::strcmp(argv[1], "--surface-merge-uv") == 0 )
    {
        sed::tests::runLevelSurfaceMergeUvRegressionTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if ( argc == 2 && std::strcmp(argv[1], "--legacy-tools") == 0 )
    {
        sed::tests::runLevelBackupTests();
        sed::tests::runSurfaceAngleTests();
        sed::tests::runShapeTplRemoveTests();
        sed::tests::runSurfaceFlagRepairTests();
        sed::tests::runComponentRebuildTests();
        sed::tests::runMakeDoorTests();
        sed::tests::runRecipeTests();
        sed::tests::runJedPluginHostTests();
        sed::tests::runJedPluginBridgeTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if ( argc == 2 && std::strcmp(argv[1], "--pending-cog-import") == 0 )
    {
        sed::tests::runPendingCogImportTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if ( argc == 2 && std::strcmp(argv[1], "--model-3do") == 0 )
    {
        sed::tests::runModel3doTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if ( argc == 2 && std::strcmp(argv[1], "--thing-frames") == 0 )
    {
        sed::tests::runThingFramesTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }
    if ( argc == 2 && std::strcmp(argv[1], "--recipe-operations") == 0 )
    {
        sed::tests::runThingFramesTests();
        sed::tests::runRecipeRequestTests();
        return sed::tests::failures == 0 ? 0 : 1;
    }

    const bool traceGroups =
        argc == 2 && std::strcmp(argv[1], "--trace-groups") == 0;
    const auto runGroup = [traceGroups](
        const char* const name,
        void (*const function)())
    {
        if (traceGroups)
        {
            std::cerr << "RUN: " << name << '\n' << std::flush;
        }
        try
        {
            function();
        }
        catch ( const std::exception& exception )
        {
            const std::string message =
                "Uncaught exception in test group '" + std::string(name)
                + "': " + exception.what();
            sed::tests::check(false, message.c_str());
        }
        catch ( ... )
        {
            const std::string message =
                "Uncaught non-standard exception in test group '"
                + std::string(name) + "'";
            sed::tests::check(false, message.c_str());
        }
    };

    runGroup("geometry-math", sed::tests::runGeometryMathTests);
    runGroup("level", sed::tests::runLevelTests);
    runGroup("level-geometry", sed::tests::runLevelGeometryTests);
    runGroup("level-lighting", sed::tests::runLevelLightingTests);
    runGroup("light-limits", sed::tests::runLightLimitsTests);
    runGroup("level-picking", sed::tests::runLevelPickingTests);
    runGroup("level-adjoin", sed::tests::runLevelAdjoinTests);
    runGroup("level-build", sed::tests::runLevelBuildTests);
    runGroup("level-extrude", sed::tests::runLevelExtrudeTests);
    runGroup("extrude-collinear-support", sed::tests::runExtrudeCollinearSupportTests);
    runGroup("extrude-contained-ring", sed::tests::runExtrudeContainedRingTests);
    runGroup("extrude-arrangement-registry", sed::tests::runExtrudeArrangementRegistryTests);
    runGroup("level-closure-contract", sed::tests::runLevelClosureContractTests);
    runGroup("level-cleave", sed::tests::runLevelCleaveTests);
    runGroup("cleave-subdivided-portal", sed::tests::runCleaveSubdividedPortalTests);
    runGroup("level-connect-contract", sed::tests::runLevelConnectContractTests);
    runGroup("level-cog-reference", sed::tests::runLevelCogReferenceTests);
    runGroup("level-clipboard", sed::tests::runLevelClipboardTests);
    runGroup("level-edit", sed::tests::runLevelEditTests);
    runGroup("level-merge", sed::tests::runLevelMergeTests);
    runGroup("level-surface-merge-uv", sed::tests::runLevelSurfaceMergeUvRegressionTests);
    runGroup("level-thing", sed::tests::runLevelThingTests);
    runGroup("thing-frames", sed::tests::runThingFramesTests);
    runGroup("recipe-requests", sed::tests::runRecipeRequestTests);
    runGroup("level-transform", sed::tests::runLevelTransformTests);
    runGroup("connected-surface-uv", sed::tests::runConnectedSurfaceUvTests);
    runGroup("surface-extrude-uv", sed::tests::runSurfaceExtrudeUvRegressionTests);
    runGroup("level-uv", sed::tests::runLevelUvTests);
    runGroup("level-layer", sed::tests::runLevelLayerTests);
    runGroup("editor", sed::tests::runEditorTests);
    runGroup("pending-cog-import", sed::tests::runPendingCogImportTests);
    runGroup("level-backup", sed::tests::runLevelBackupTests);
    runGroup("surface-angle", sed::tests::runSurfaceAngleTests);
    runGroup("shape-tpl-remove", sed::tests::runShapeTplRemoveTests);
    runGroup("surface-flag-repair", sed::tests::runSurfaceFlagRepairTests);
    runGroup("component-rebuild", sed::tests::runComponentRebuildTests);
    runGroup("make-door", sed::tests::runMakeDoorTests);
    runGroup("recipes", sed::tests::runRecipeTests);
    runGroup("prefabs", sed::tests::runPrefabTests);
runGroup("level-statistics", sed::tests::runStatisticsTests);
    runGroup("fan-degeneracy", sed::tests::runFanDegeneracyTests);
    runGroup("dxf", sed::tests::runDxfTests);
    runGroup("jed-plugin-host", sed::tests::runJedPluginHostTests);
    runGroup("jed-plugin-bridge", sed::tests::runJedPluginBridgeTests);
    runGroup("text", sed::tests::runTextTests);
    runGroup("float-chars", sed::tests::runFloatCharsTests);
    runGroup("binary-reader", sed::tests::runBinaryReaderTests);

    runGroup("jkl-level", sed::tests::runJklLevelTests);
    runGroup("jkl-structure", sed::tests::runJklStructureTests);
    runGroup("jkl-template-closure", sed::tests::runJklTemplateClosureTests);
    runGroup("sealed-seams", sed::tests::runSealedSeamTests);
    runGroup("arch-lighting", sed::tests::runArchLightingTests);
    runGroup("arch-lighting-clipboard", sed::tests::runMotsArchLightingClipboardTests);
    runGroup("arch-preview", sed::tests::runArchPreviewTests);
    runGroup("jkl-save", sed::tests::runJklSaveTests);
    runGroup("jkl-preservation-parity", sed::tests::runJklPreservationParityTests);
    runGroup("jkl-export-transaction", sed::tests::runJklExportTransactionTests);
    runGroup("jkl-geo-preservation", sed::tests::runJklGeoPreservationTests);
    runGroup("jkl-legacy-frame", sed::tests::runJklLegacyFrameTests);
    runGroup("ndy-save", sed::tests::runNdySaveTests);
    runGroup("jed-level", sed::tests::runJedLevelTests);
    runGroup("sed-level", sed::tests::runSedLevelTests);
    runGroup("model-3do", sed::tests::runModel3doTests);
    runGroup("model-3do-writer", sed::tests::runModel3doWriterTests);
    runGroup("mat-transparency", sed::tests::runMatTransparencyTests);
    runGroup("key", sed::tests::runKeyTests);
    runGroup("resource-overlay", sed::tests::runResourceLocatorOverlayTests);
    runGroup("resource-sharing", sed::tests::runResourceLocatorSharingTests);
    runGroup("tpl", sed::tests::runTplTests);
    runGroup("resource-stream-failure", sed::tests::runResourceStreamFailureTests);
    runGroup("items-dat", sed::tests::runItemsDatTests);
    runGroup("mat-cmp", sed::tests::runMatCmpTests);
    runGroup("asset-writers", sed::tests::runAssetWriterTests);
    runGroup("obj-import", sed::tests::runObjImportTests);
    runGroup("gltf-import", sed::tests::runGltfImportTests);
    runGroup("jpeg", sed::tests::runJpegTests);
    runGroup("models-dat", sed::tests::runModelsDatTests);
    runGroup("par", sed::tests::runParTests);
    runGroup("pup", sed::tests::runPupTests);
    runGroup("resource-locator-disk",
        sed::tests::runResourceLocatorDiskTests);
    runGroup("resource-locator-bounded",
        sed::tests::runResourceLocatorBoundedReadTests);
    runGroup("cog-script", sed::tests::runCogScriptTests);
    runGroup("cog-schema", sed::tests::runCogSchemaTests);
    runGroup("cog-preflight", sed::tests::runCogPreflightTests);
    runGroup("cog-binding", sed::tests::runCogBindingTests);
    runGroup("cog-mutation", sed::tests::runCogMutationTests);
    runGroup("spatial-selection", sed::tests::runSpatialSelectionTests);
    runGroup("dynamic-vertex-colors", sed::tests::runDynamicVertexColorTests);
    runGroup("workbench", sed::tests::runWorkbenchTests);
    runGroup("package", sed::tests::runPackageTests);
    runGroup("overview", sed::tests::runOverviewTests);
    runGroup("software-snapshot", sed::tests::runSoftwareSnapshotTests);
    runGroup("updater", sed::tests::runUpdaterTests);
    runGroup("source-light-metadata", sed::tests::runSourceLightMetadataTests);
    runGroup("sed-extensions", sed::tests::runSedExtensionTests);
    runGroup("arch-validation", sed::tests::runArchValidationTests);
    runGroup("arch-authoring", sed::tests::runArchAuthoringTests);
    runGroup("snd", sed::tests::runSndTests);
    runGroup("string-table", sed::tests::runStringTableTests);
    runGroup("spr", sed::tests::runSprTests);
    runGroup("gob", sed::tests::runGobTests);
    runGroup("indy-wv", sed::tests::runIndyWvTests);
    runGroup("jlh", sed::tests::runJlhTests);
    runGroup("df2-jk", sed::tests::runDf2JkTests);
    runGroup("episode", sed::tests::runEpisodeTests);
    runGroup("asc", sed::tests::runAscTests);
    runGroup("lev", sed::tests::runLevTests);
    runGroup("wav", sed::tests::runWavTests);

    return sed::tests::failures == 0 ? 0 : 1;
}
