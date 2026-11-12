#include "test_common.hpp"

#include "sed/assets/3do.hpp"
#include "sed/assets/model_lighting.hpp"

#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{

using namespace sed;
using namespace sed::assets;
using sed::tests::check;
using sed::tests::close;

// Original synthetic text fixtures. No game installation or proprietary asset
// is used. Deliberate whitespace/comments exercise the scoped-edit contract.
std::string meshText(const std::string_view version = "2.1")
{
    const bool rgb = version == "2.3";
    const std::string color = rgb ? "0.125 0.25 0.5 0.375" : "0.125";
    return "MESH 0\nNAME synthetic\nRADIUS 2\nGEOMETRYMODE 4\n"
        "LIGHTINGMODE 3\nTEXTUREMODE 3\nVERTICES 3\n"
        "0:\t0 0 0  " + color + " # keep first\n"
        "1: 1 0 0 " + color + "\n"
        "2: 0 1 0 " + color + "\n"
        "TEXTURE VERTICES 3\n0: 0 0\n1: 1 0\n2: 0 1\n"
        "VERTEX NORMALS\n0: 0 0 1\n1: 0 0 -1\n2: 1 0 0\n"
        "FACES 1\n0: 0 0x0 4 3 3 "
        + (rgb ? std::string("(0.1/0.2/0.3/1)") : std::string("0.25"))
        + " 3 0,0 1,1 2,2\nFACE NORMALS\n0: 0 0 1\n";
}

std::string modelText(const std::string_view version = "2.1", const int lods = 1)
{
    std::string text = "# original fixture\nSECTION: HEADER\n3DO ";
    text += version;
    text += "\nSECTION: MODELRESOURCE\nMATERIALS 1\n0: synthetic.mat\n"
        "SECTION: GEOMETRYDEF\nRADIUS 2\nINSERT OFFSET 0 0 0\nGEOSETS "
        + std::to_string(lods) + "\n";
    for (int lod = 0; lod < lods; ++lod)
    {
        text += "GEOSET " + std::to_string(lod) + "\nMESHES 1\n" + meshText(version);
    }
    text += "SECTION: HIERARCHYDEF\nHIERARCHY NODES 1\n"
        "0: 0x0 0x20 0 -1 -1 -1 0 0 0 0 0 0 0 0 0 0 synthetic\n"
        "SECTION: CUSTOM\n0:\t0 0 0  0.125 # do not edit\nEND\n";
    return text;
}

std::span<const std::byte> bytes(const std::string& text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}

void replaceOnce(std::string& text, const std::string_view old, const std::string_view replacement)
{
    const std::size_t at = text.find(old);
    if (at == std::string::npos)
    {
        throw std::runtime_error("Synthetic fixture replacement failed");
    }
    text.replace(at, old.size(), replacement);
}

ModelLightingOptions keyLight()
{
    ModelLightingOptions options;
    options.ambient = { 0.25f, 0.25f, 0.25f, 1.0f };
    ModelLightingLight light;
    light.kind = ModelLightKind::Directional;
    light.intensity = 0.5;
    options.lights.push_back(light);
    return options;
}

template <typename Action>
void refuses(Action action, const std::string_view detail, const char* label)
{
    bool refused = false;
    try
    {
        action();
    }
    catch (const std::runtime_error& error)
    {
        refused = std::string_view(error.what()).starts_with("Model lighting: ")
            && std::string_view(error.what()).find(detail) != std::string_view::npos;
    }
    check(refused, label);
}

void basicContracts()
{
    const std::string source = modelText();
    const auto inspection = inspectModelLighting(bytes(source));
    check(inspection.version == 2.1 && inspection.geosets == 1
            && inspection.sourceMeshes == 1 && inspection.renderMeshes == 1
            && inspection.vertices == 3 && inspection.orphanMeshes == 0,
        "model lighting inspection retains every source mesh and sample count");
    check(inspection.meshes.size() == 1
            && inspection.meshes[0].minimumVertexLight.r == 0.125f
            && inspection.meshes[0].maximumFaceExtra.r == 0.25f
            && inspection.meshes[0].zeroNormals == 0,
        "model lighting inspection reports stored vertex and extra ranges");
    const auto baked = bakeModelLighting(bytes(source), keyLight());
    std::string expected = source;
    replaceOnce(expected, "0:\t0 0 0  0.125", "0:\t0 0 0  0.75");
    replaceOnce(expected, "1: 1 0 0 0.125", "1: 1 0 0 0.25");
    replaceOnce(expected, "2: 0 1 0 0.125", "2: 0 1 0 0.25");
    check(baked.text == expected,
        "directional bake has analytic front/back values and preserves all non-light bytes");
    check(baked.changedVertices == 3 && baked.clampedVertices == 0
            && baked.before.vertices == 3 && baked.after.vertices == 3,
        "bake receipt reports actual changed samples");
    const auto repeated = bakeModelLighting(bytes(expected), keyLight());
    check(repeated.text == expected && repeated.changedVertices == 0,
        "replacement bake is repeatable and no-op preserves numeric spelling");
    std::string crlf;
    for (char c : source)
    {
        if (c == '\n')
        {
            crlf += '\r';
        }
        crlf += c;
    }
    const auto crlfResult = bakeModelLighting(bytes(crlf), keyLight());
    std::string crlfExpected;
    for (char c : expected)
    {
        if (c == '\n')
        {
            crlfExpected += '\r';
        }
        crlfExpected += c;
    }
    check(crlfResult.text == crlfExpected, "CRLF and unknown-section bytes remain exact");

    ModelLightingOptions add;
    add.blend = ModelLightingBlend::Add;
    add.ambient = { 0.25f, 0.25f, 0.25f, 1.0f };
    const auto added = bakeModelLighting(bytes(source), add);
    const auto parsed = loadFrom3DO(bytes(added.text));
    check(!parsed.meshes.empty() && parsed.meshes[0].vertices[0].color.r == 0.375f,
        "explicit additive mode includes original authored sample once");
    add.ambient = { 2.0f, 2.0f, 2.0f, 1.0f };
    const auto clamped = bakeModelLighting(bytes(source), add);
    check(clamped.clampedVertices == 3 && clamped.after.meshes.size() == 1
            && clamped.after.meshes[0].maximumVertexLight.r == 1.0f,
        "clamping is explicit in the bake receipt");
}

void rgbAndLodContracts()
{
    const std::string source = modelText("2.3");
    auto options = keyLight();
    options.profile = ModelLightingProfile::IjimRgb;
    options.ambient = { 0.0f, 0.125f, 0.25f, 1.0f };
    options.lights[0].color = { 1.0f, 0.5f, 0.0f, 1.0f };
    const auto result = bakeModelLighting(bytes(source), options);
    std::string expected = source;
    replaceOnce(expected, "0.125 0.25 0.5 0.375", "0.5 0.375 0.25 0.375");
    replaceOnce(expected, "0.125 0.25 0.5 0.375", "0 0.125 0.25 0.375");
    replaceOnce(expected, "0.125 0.25 0.5 0.375", "0 0.125 0.25 0.375");
    check(result.text == expected, "IJIM bake updates RGB and preserves source alpha tokens");

    std::string grey = modelText();
    replaceOnce(grey, "0:\t0 0 0  0.125", "0:\t0 0 0  0.125 0.125 0.125 0.625");
    const auto greyResult = bakeModelLighting(bytes(grey), keyLight());
    check(greyResult.text.find("0:\t0 0 0  0.75 0.75 0.75 0.625") != std::string::npos,
        "RED classic RGBA rows stay grey and retain alpha");

    std::string lodSource = modelText("2.1", 2);
    const std::size_t secondLod = lodSource.find("GEOSET 1\n");
    const std::size_t secondNormal = lodSource.find("0: 0 0 1\n", secondLod);
    lodSource.replace(secondNormal, 9, "0: 0 0 -1\n");
    const auto lodResult = bakeModelLighting(bytes(lodSource), keyLight());
    check(lodResult.after.geosets == 2 && lodResult.after.sourceMeshes == 2,
        "all LODs survive scoped lighting output");
    const auto near = loadFrom3DO(bytes(lodResult.text), { 0, false });
    const auto far = loadFrom3DO(bytes(lodResult.text), { 1, false });
    check(near.meshes.size() == 1 && far.meshes.size() == 1
            && near.meshes[0].vertices[0].color.r == 0.75f
            && far.meshes[0].vertices[0].color.r == 0.25f,
        "different LOD samples are evaluated independently instead of merged");
}

void pointAndHierarchyContracts()
{
    std::string source = modelText();
    ModelLightingOptions options;
    ModelLightingLight light;
    light.position = { 0, 0, 1 };
    light.range = 2;
    options.lights.push_back(light);
    const auto result = bakeModelLighting(bytes(source), options);
    const auto model = loadFrom3DO(bytes(result.text));
    check(model.meshes.size() == 1 && close(model.meshes[0].vertices[0].color.r, 0.25)
            && model.meshes[0].vertices[1].color.r == 0,
        "point bake uses squared range falloff and rejects opposing normals");
    replaceOnce(source, "0:\t0 0 0  0.125", "0:\t0 0 -1  0.125");
    const auto cutoff = bakeModelLighting(bytes(source), options);
    const auto cutoffModel = loadFrom3DO(bytes(cutoff.text));
    check(cutoffModel.meshes.size() == 1 && cutoffModel.meshes[0].vertices[0].color.r == 0,
        "point light contributes zero at its exact radius");

    source = modelText();
    replaceOnce(source, "0 0 0 0 0 0 0 0 0 synthetic", "0 0 0 180 0 0 0 0 0 synthetic");
    const auto rotated = bakeModelLighting(bytes(source), keyLight());
    const auto rotatedRaw = loadFrom3DO(bytes(rotated.text), { 0, false });
    check(rotatedRaw.meshes.size() == 1 && rotatedRaw.meshes[0].vertices[0].color.r == 0.25f
            && rotated.text.find("180 0 0 0 0 0 synthetic") != std::string::npos,
        "bind-pose normals rotate through hierarchy without rewriting hierarchy records");

    source = modelText();
    replaceOnce(source, "MESHES 1\n", "MESHES 2\n");
    const std::size_t hierarchyAt = source.find("SECTION: HIERARCHYDEF");
    std::string orphan = meshText();
    replaceOnce(orphan, "MESH 0", "MESH 1");
    source.insert(hierarchyAt, orphan);
    const auto orphanResult = bakeModelLighting(bytes(source), keyLight());
    check(orphanResult.before.orphanMeshes == 1 && orphanResult.changedVertices == 3
            && orphanResult.text.find(orphan) != std::string::npos,
        "unreferenced source meshes retain their lighting bytes unchanged");

    source = modelText();
    replaceOnce(source, "HIERARCHY NODES 1", "HIERARCHY NODES 2");
    replaceOnce(source, "SECTION: CUSTOM", "1: 0x0 0x1 0 -1 -1 -1 0 1 0 0 0 0 0 0 0 0 clone\nSECTION: CUSTOM");
    check(inspectModelLighting(bytes(source)).sharedMeshes == 1,
        "inspector reports multiple hierarchy instances of one source mesh");
    refuses([&] { (void)bakeModelLighting(bytes(source), keyLight()); }, "shared",
        "different shared-mesh placements are refused without lossy averaging");
}

void occlusionContracts()
{
    std::string source = modelText();
    replaceOnce(source, "MESHES 1\n", "MESHES 2\n");
    std::string blocker = meshText();
    replaceOnce(blocker, "MESH 0", "MESH 1");
    replaceOnce(blocker, "0:\t0 0 0", "0:\t-2 -2 1");
    replaceOnce(blocker, "1: 1 0 0", "1: 2 -2 1");
    replaceOnce(blocker, "2: 0 1 0", "2: 0 2 1");
    source.insert(source.find("SECTION: HIERARCHYDEF"), blocker);
    replaceOnce(source, "HIERARCHY NODES 1", "HIERARCHY NODES 2");
    replaceOnce(source, "SECTION: CUSTOM", "1: 0x0 0x1 1 -1 -1 -1 0 0 0 0 0 0 0 0 0 0 blocker\nSECTION: CUSTOM");
    auto options = keyLight();
    options.selfOcclusion = true;
    const auto result = bakeModelLighting(bytes(source), options);
    const auto model = loadFrom3DO(bytes(result.text));
    check(result.occludedSamples >= 1 && model.meshes.size() == 2
            && model.meshes[0].vertices[0].color.r == 0.25f
            && model.meshes[1].vertices[0].color.r == 0.75f,
        "same-LOD blocker shadows sample while surface endpoint does not self-shadow");
    options.selfOcclusion = false;
    const auto clear = bakeModelLighting(bytes(source), options);
    const auto clearModel = loadFrom3DO(bytes(clear.text));
    check(clear.occludedSamples == 0 && clearModel.meshes.size() == 2
            && clearModel.meshes[0].vertices[0].color.r == 0.75f,
        "self-occlusion is explicitly opt-in");

    options.selfOcclusion = true;
    options.ambient = { 0, 0, 0, 1 };
    options.lights[0].kind = ModelLightKind::Point;
    options.lights[0].position = { 0, 0, 0.5 };
    options.lights[0].range = 2;
    options.lights[0].intensity = 1;
    const auto finiteRay = bakeModelLighting(bytes(source), options);
    const auto finiteRayModel = loadFrom3DO(bytes(finiteRay.text));
    check(finiteRayModel.meshes[0].vertices[0].color.r == 0.5625f,
        "occluder beyond a point light cannot shadow that source");

    std::string hidden = source;
    const std::size_t blockerStart = hidden.find("MESH 1\n");
    const std::size_t geometryMode = hidden.find("GEOMETRYMODE 4", blockerStart);
    hidden.replace(geometryMode, 14, "GEOMETRYMODE 0");
    options = keyLight();
    options.selfOcclusion = true;
    const auto hiddenResult = bakeModelLighting(bytes(hidden), options);
    const auto hiddenModel = loadFrom3DO(bytes(hiddenResult.text));
    check(hiddenModel.meshes[0].vertices[0].color.r == 0.75f,
        "nondrawn model faces are excluded from opaque shadow proxies");
}

void refusalContracts()
{
    const std::string source = modelText();
    for (const auto& [old, replacement] : {
        std::pair<std::string_view, std::string_view>{ "3DO 2.1", "3DO 2.2" },
        { "1: 1 0 0 0.125", "0: 1 0 0 0.125" },
        { "VERTICES 3", "VERTICES 4" },
        { "0:\t0 0 0  0.125", "0:\tnan 0 0  0.125" },
        { "0:\t0 0 0  0.125", "0:\t0 0 0  nan" },
        { "0:\t0 0 0  0.125", "0:\t0 0 0  0.125 1" },
        { "0:\t0 0 0  0.125", "0:\t0 0 0  0.125 0.25 0.5 1" },
        { "3 0,0 1,1 2,2", "3 0,0 1,1 9,2" },
        { "0x20 0 -1 -1", "0x20 0 0 -1" },
        { "0x20 0 -1 -1", "0x20 0 9 -1" } })
    {
        std::string malformed = source;
        replaceOnce(malformed, old, replacement);
        refuses([&] { (void)bakeModelLighting(bytes(malformed), keyLight()); }, "",
            "malformed or unrepresentable source refuses atomically");
    }
    auto options = keyLight();
    options.lights[0].color = { 1, 0, 0, 1 };
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "grey",
        "classic profile refuses colored light rather than silently averaging");
    options = keyLight();
    options.profile = ModelLightingProfile::IjimRgb;
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "profile",
        "output profile must match the existing file version");
    options = keyLight();
    options.lights[0].directionToLight = {};
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "direction",
        "zero directional vector is rejected");
    options = keyLight();
    options.shadowBias = 0;
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "bias",
        "shadow bias must be positive finite");
    options = keyLight();
    options.limits.vertices = 2;
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "budget",
        "vertex admission respects caller-lowered budget");
    options = keyLight();
    options.limits.workUnits = 2;
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "budget",
        "aggregate lighting work refuses before publication");
    options = keyLight();
    options.limits.sourceBytes = source.size() - 1;
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "budget",
        "source budget counts exact bytes");
    options = keyLight();
    ++options.limits.vertices;
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "limit",
        "operational hard caps cannot be raised by the caller");
    options = keyLight();
    options.cancelled = [] { return true; };
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "cancelled",
        "cancelled work returns no partial output");

    options = keyLight();
    int cancellationChecks = 0;
    options.cancelled = [&] { return ++cancellationChecks > 10; };
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "cancelled",
        "cancellation during work still returns no partial output");

    ModelLightingLimits exact;
    exact.sourceBytes = source.size();
    check(inspectModelLighting(bytes(source), exact).vertices == 3,
        "exact source-byte budget is admitted");

    for (const std::string value : { "nan", "inf", "1e9999" })
    {
        std::string bad = source;
        replaceOnce(bad, "0:\t0 0 0  0.125", "0:\t0 0 0  " + value);
        refuses([&] { (void)inspectModelLighting(bytes(bad)); }, "numeric",
            "inspector refuses nonfinite or overflowing stored intensity");
    }
    std::string duplicate = source;
    duplicate += "SECTION: GEOMETRYDEF\nGEOSETS 0\n";
    refuses([&] { (void)inspectModelLighting(bytes(duplicate)); }, "duplicate",
        "repeated known sections cannot create ambiguous edit identities");

    options = keyLight();
    options.lights[0].intensity = -1;
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "nonnegative",
        "negative light intensity is refused");
    options = keyLight();
    options.lights[0].intensity = std::numeric_limits<double>::infinity();
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "finite",
        "nonfinite light intensity is refused");
    options = keyLight();
    options.lights[0].kind = ModelLightKind::Point;
    options.lights[0].range = 0;
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "range",
        "zero point-light range is refused");
    options = keyLight();
    options.limits.tokensPerLine = 17;
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "budget",
        "token budget applies before per-line allocation grows");
    options = keyLight();
    options.limits.triangles = 0;
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "budget",
        "face triangle budget applies independently of shadow enablement");
    options = keyLight();
    options.limits.lights = 0;
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "budget",
        "light budget applies before evaluation");
}

void normalAndTopologyContracts()
{
    std::string source = modelText();
    replaceOnce(source, "0: 0 0 1\n1: 0 0 -1", "0: 0 0 5\n1: 0 0 0");
    const auto before = inspectModelLighting(bytes(source));
    check(before.meshes.size() == 1 && before.meshes[0].nonUnitNormals == 1
            && before.meshes[0].zeroNormals == 1,
        "normal quality distinguishes zero and nonunit vectors");
    const auto result = bakeModelLighting(bytes(source), keyLight());
    check(result.zeroNormalVertices == 1 && result.after.meshes[0].maximumVertexLight.r == 0.75f
            && result.text.find("0: 0 0 5\n1: 0 0 0") != std::string::npos,
        "bake normalizes usable normals without rewriting authored normal data");
    source = modelText();
    replaceOnce(source, "VERTEX NORMALS\n0: 0 0 1\n1: 0 0 -1\n2: 1 0 0\n", "");
    const auto missing = bakeModelLighting(bytes(source), keyLight());
    check(missing.before.meshes[0].missingNormals == 3 && missing.zeroNormalVertices == 3
            && missing.after.meshes[0].minimumVertexLight.r == 0.25f
            && missing.after.meshes[0].maximumVertexLight.r == 0.25f,
        "missing normals are diagnosed and only receive the explicit ambient baseline");

    source = modelText();
    replaceOnce(source, "2: 0 1 0 0.125", "2: 2 0 0 0.125");
    auto options = keyLight();
    options.selfOcclusion = true;
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "degenerate",
        "zero-area occluder fan is refused instead of making unreliable shadows");
    options.selfOcclusion = false;
    check(bakeModelLighting(bytes(source), options).changedVertices == 3,
        "vertex authoring without shadows does not require valid polygon triangulation");

    source = modelText();
    replaceOnce(source, "VERTICES 3\n", "VERTICES 5\n");
    replaceOnce(source, "2: 0 1 0 0.125\n", "2: 2 2 0 0.125\n3: 1 0.5 0 0.125\n4: 0 2 0 0.125\n");
    replaceOnce(source, "1: 1 0 0 0.125\n", "1: 2 0 0 0.125\n");
    replaceOnce(source, "2: 1 0 0\nFACES", "2: 1 0 0\n3: 0 0 1\n4: 0 0 1\nFACES");
    replaceOnce(source, "3 0,0 1,1 2,2", "5 0,0 1,1 2,2 3,0 4,1");
    options.selfOcclusion = true;
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "convex",
        "concave polygon cannot silently become an opaque fan proxy");
}

void hierarchyWorkContract()
{
    std::string source = modelText();
    const std::size_t begin = source.find("HIERARCHY NODES 1\n");
    const std::size_t end = source.find("SECTION: CUSTOM", begin);
    std::string chain = "HIERARCHY NODES 64\n";
    for (int node = 0; node < 64; ++node)
    {
        chain += std::to_string(node) + ": 0x0 0x1 "
            + (node == 63 ? std::string("0") : std::string("-1")) + " "
            + std::to_string(node - 1)
            + " -1 -1 0 0 0 0 0 0 0 0 0 0 node" + std::to_string(node) + "\n";
    }
    source.replace(begin, end - begin, chain);
    auto options = keyLight();
    // Source scanning fits, but actual parent transforms exceed the remaining
    // budget. This catches counting only hierarchy validation, not evaluation.
    options.limits.workUnits = 5 * source.size() + 200;
    refuses([&] { (void)bakeModelLighting(bytes(source), options); }, "budget",
        "hierarchy transform vector visits count toward the caller's work budget");
}

void parserAgreementContracts()
{
    const std::string source = modelText();
    const std::size_t geometryBegin = source.find("SECTION: GEOMETRYDEF\n");
    const std::size_t geometryData = geometryBegin + std::string_view("SECTION: GEOMETRYDEF\n").size();
    const std::size_t hierarchy = source.find("SECTION: HIERARCHYDEF\n");
    std::string hidden = source.substr(geometryData, hierarchy - geometryData);
    replaceOnce(hidden, "NAME synthetic", "NAME hidden");
    replaceOnce(hidden, "0: 0 0 1\n", "0: 0 0 -1\n");
    for (const std::string_view prefix : { "SECTION:x", "sEcTiOn:x", "SECTION:CUSTOM" })
    {
        std::string ambiguous = source;
        ambiguous.insert(geometryBegin, "SECTION: CUSTOM\n" + std::string(prefix)
            + " GEOMETRYDEF\n" + hidden);
        refuses([&] { (void)bakeModelLighting(bytes(ambiguous), keyLight()); }, "section",
            "legacy SECTION prefix aliases cannot hide a competing geometry block");
        refuses([&] { (void)inspectModelLighting(bytes(ambiguous)); }, "section",
            "inspector and baker share SECTION-prefix refusal");
    }

    for (const std::string_view reserved : { "GEOSETS 2", "INSERT OFFSET 9 9 9", "HIERARCHY NODES 0" })
    {
        std::string ambiguous = source;
        replaceOnce(ambiguous, "TEXTUREMODE 3\n", "TEXTUREMODE 3\n" + std::string(reserved) + "\n");
        refuses([&] { (void)bakeModelLighting(bytes(ambiguous), keyLight()); }, "reserved",
            "reserved control records cannot masquerade as opaque mesh properties");
    }
}

void storedRangeContract()
{
    std::string source = modelText();
    replaceOnce(source, "0:\t0 0 0  0.125", "0:\t0 0 0  1.5");
    replaceOnce(source, "1: 1 0 0 0.125", "1: 1 0 0 -0.5");
    replaceOnce(source, "0x0 4 3 3 0.25", "0x0 4 3 3 -2.5");
    const auto inspection = inspectModelLighting(bytes(source));
    check(inspection.meshes.size() == 1
            && inspection.meshes[0].minimumVertexLight.r == -0.5f
            && inspection.meshes[0].maximumVertexLight.r == 1.5f,
        "stored scalar light ranges retain authored signed/HDR values before renderer clamps");
    check(inspection.meshes.size() == 1 && inspection.meshes[0].minimumFaceExtra.r == -2.5f
            && inspection.meshes[0].maximumFaceExtra.r == -2.5f,
        "stored scalar face-extra range does not inherit the preview loader's clamp");
}

} // namespace

int main()
{
    try
    {
        basicContracts();
        rgbAndLodContracts();
        pointAndHierarchyContracts();
        occlusionContracts();
        refusalContracts();
        normalAndTopologyContracts();
        hierarchyWorkContract();
        parserAgreementContracts();
        storedRangeContract();
    }
    catch (const std::exception& error)
    {
        check(false, error.what());
    }
    std::cout << "model lighting failures=" << sed::tests::failures << '\n';
    return sed::tests::failures == 0 ? 0 : 1;
}
