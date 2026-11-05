#include "test_common.hpp"

#include "sed/assets/3do.hpp"
#include "sed/level/arch_lighting.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace sed::tests
{
namespace
{

std::string makeSparseSharedSource3do()
{
    std::string source =
        "SECTION: HEADER\n"
        "3DO 2.1\n"
        "SECTION: GEOMETRYDEF\n"
        "GEOSETS 1\n"
        "GEOSET 0\n"
        "MESHES 4\n";
    for (std::size_t meshIndex = 0; meshIndex < 4; ++meshIndex)
    {
        source += "MESH " + std::to_string(meshIndex)
            + "\nNAME mesh" + std::to_string(meshIndex)
            + "\nVERTICES 1\n0: "
            + std::to_string(meshIndex + 1u) + " 0 0\nFACES 0\n";
    }
    source +=
        "SECTION: HIERARCHYDEF\n"
        "HIERARCHY NODES 2\n"
        "0: 0x0 0x1 3 -1 -1 -1 0 10 0 0 0 0 0 0 0 0 first\n"
        "1: 0x0 0x1 3 -1 -1 -1 0 20 0 0 0 0 0 0 0 0 second\n";
    return source;
}

std::pair<assets::Model3do, assets::Model3do> loadSparseSharedSource3do()
{
    const std::string source = makeSparseSharedSource3do();
    const auto bytes = std::as_bytes(std::span(source.data(), source.size()));
    assets::Model3do transformed = assets::loadFrom3DO(bytes);
    assets::Model3doLoadOptions rawOptions{};
    rawOptions.applyHierarchyTransforms = false;
    assets::Model3do raw = assets::loadFrom3DO(bytes, rawOptions);
    return { std::move(transformed), std::move(raw) };
}

bool isZeroVertex(const MotsArchLightVertex& vertex) noexcept
{
    return vertex == MotsArchLightVertex{ 0.0f, 0.0f, 0.0f, 0.0f };
}

void runCompactedSourcePreviewTests()
{
    const auto [transformed, raw] = loadSparseSharedSource3do();
    check(transformed.hierarchyTransformsApplied
            && transformed.meshes.size() == 2
            && transformed.meshSourceIndices
                == std::vector<std::int32_t>{ 3, 3 }
            && transformed.meshes[0].name == "mesh3"
            && transformed.meshes[1].name == "mesh3"
            && transformed.nodes.size() == 2
            && transformed.nodes[0].mesh == 0
            && transformed.nodes[1].mesh == 1,
        "ARCH.preview: the loader compacts three orphan meshes and retains both authored source identities");
    check(!raw.hierarchyTransformsApplied && raw.meshes.size() == 4
            && raw.meshSourceIndices
                == std::vector<std::int32_t>{ 0, 1, 2, 3 },
        "ARCH.preview: the raw loader retains all four authored meshes in source order");

    const std::optional<std::size_t> firstSource =
        level::motsArchLightingSourceMeshIndex(transformed, 0);
    const std::optional<std::size_t> secondSource =
        level::motsArchLightingSourceMeshIndex(transformed, 1);
    check(firstSource == std::optional<std::size_t>{ 3 }
            && secondSource == std::optional<std::size_t>{ 3 }
            && !level::motsArchLightingSourceMeshIndex(transformed, 2),
        "ARCH.preview: compacted render meshes resolve to authored source mesh 3, not compacted indices");

    const std::optional<MotsArchLightObject> preview =
        level::makeDefaultMotsArchLightingPreview(transformed);
    check(preview && preview->meshes.size() == 4,
        "ARCH.preview: a compacted model allocates authored source slots through mesh 3");
    if (preview && preview->meshes.size() == 4)
    {
        check(preview->meshes[0].vertices.empty()
                && preview->meshes[1].vertices.empty()
                && preview->meshes[2].vertices.empty(),
            "ARCH.preview: orphan source slots remain empty instead of restoring orphan rendering");
        check(preview->meshes[3].wireIndex == 3
                && preview->meshes[3].vertices.size() == 1
                && isZeroVertex(preview->meshes[3].vertices[0]),
            "ARCH.preview: shared source mesh 3 receives one default-zero archive vertex");
    }

    const std::optional<MotsArchLightObject> rawPreview =
        level::makeDefaultMotsArchLightingPreview(raw);
    check(rawPreview && rawPreview->meshes.size() == 4,
        "ARCH.preview: raw models retain one archive slot per authored source mesh");
    if (rawPreview && rawPreview->meshes.size() == 4)
    {
        bool matches = true;
        for (std::size_t meshIndex = 0; meshIndex < 4; ++meshIndex)
        {
            const MotsArchLightMesh& mesh = rawPreview->meshes[meshIndex];
            matches = matches
                && mesh.wireIndex == static_cast<std::int32_t>(meshIndex)
                && mesh.vertices.size() == 1
                && isZeroVertex(mesh.vertices[0]);
        }
        check(matches,
            "ARCH.preview: raw archive slots retain source wire indices and zero-valued vertices");
    }

    assets::Model3do rawFallback = raw;
    rawFallback.meshSourceIndices.clear();
    check(level::motsArchLightingSourceMeshIndex(rawFallback, 0)
            == std::optional<std::size_t>{ 0 }
            && level::motsArchLightingSourceMeshIndex(rawFallback, 3)
                == std::optional<std::size_t>{ 3 },
        "ARCH.preview: raw models without a mapping retain authored mesh-order fallback");
    const std::optional<MotsArchLightObject> fallbackPreview =
        level::makeDefaultMotsArchLightingPreview(rawFallback);
    check(fallbackPreview && fallbackPreview->meshes.size() == 4,
        "ARCH.preview: raw mesh-order fallback builds the complete default record");

    assets::Model3do differentCloneSizes = transformed;
    differentCloneSizes.meshes[1].vertices.resize(2);
    const std::optional<MotsArchLightObject> merged =
        level::makeDefaultMotsArchLightingPreview(differentCloneSizes);
    check(merged && merged->meshes.size() == 4
            && merged->meshes[3].vertices.size() == 2,
        "ARCH.preview: clones sharing a source use the maximum vertex count instead of summing instances");
}

void runMalformedSourcePreviewTests()
{
    assets::Model3do model{};
    model.meshes.resize(2);
    model.meshes[0].vertices.resize(1);
    model.meshes[1].vertices.resize(1);
    model.hierarchyTransformsApplied = true;
    model.nodes = {
        assets::Model3doNode{ .mesh = 0, .parent = -1 },
        assets::Model3doNode{ .mesh = 1, .parent = -1 },
    };

    model.meshSourceIndices = { 0 };
    check(!level::motsArchLightingSourceMeshIndex(model, 0)
            && !level::makeDefaultMotsArchLightingPreview(model),
        "ARCH.preview: a source table whose size differs from the render meshes is rejected");

    model.meshSourceIndices = { 0, -1 };
    check(!level::motsArchLightingSourceMeshIndex(model, 1)
            && !level::makeDefaultMotsArchLightingPreview(model),
        "ARCH.preview: negative authored source ordinals are rejected");

    model.meshSourceIndices.clear();
    check(!level::motsArchLightingSourceMeshIndex(model, 0)
            && !level::makeDefaultMotsArchLightingPreview(model),
        "ARCH.preview: transformed hierarchy data without source identities fails closed");

    model.meshSourceIndices = {
        std::numeric_limits<std::int32_t>::max(),
        std::numeric_limits<std::int32_t>::max(),
    };
    check(!level::makeDefaultMotsArchLightingPreview(model),
        "ARCH.preview: excessive source capacity is refused before allocation");
}

} // namespace

void runArchPreviewTests()
{
    runCompactedSourcePreviewTests();
    runMalformedSourcePreviewTests();
}

} // namespace sed::tests
