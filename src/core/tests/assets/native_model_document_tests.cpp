#include "sed/assets/native_model_document.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace sed::assets;

namespace
{
void require(bool condition, const char* message)
{
    if ( !condition )
    {
        throw std::runtime_error(message);
    }
}

template <class Fn>
void refuses(Fn attempt, std::string_view diagnostic = {})
{
    try
    {
        attempt();
    }
    catch ( const std::runtime_error& error )
    {
        require(std::string_view(error.what()).find(diagnostic) != std::string_view::npos, "Unexpected refusal diagnostic.");
        return;
    }
    throw std::runtime_error("Expected admission or edit refusal: " + std::string(diagnostic));
}

void replace(std::string& text, std::string_view old, std::string_view next)
{
    const auto at = text.find(old);
    require(at != text.npos, "Fixture anchor missing.");
    text.replace(at, old.size(), next);
}

// Construct only classic mesh records. The relay fixture supplies the real sections and hierarchy.
std::string meshRecords(const std::vector<std::string>& rows, const std::vector<std::string>& faces)
{
    std::string records = "VERTICES " + std::to_string(rows.size()) + "\n";
    for ( std::size_t i = 0; i < rows.size(); ++i )
    {
        records += std::to_string(i) + ": " + rows[i] + "\n";
    }
    records += "TEXTURE VERTICES 0\nVERTEX NORMALS\n";
    for ( std::size_t i = 0; i < rows.size(); ++i )
    {
        records += std::to_string(i) + (i % 2 == 0 ? ": 0 0 0\n" : ": -2 0 0\n");
    }
    records += "FACES " + std::to_string(faces.size()) + "\n";
    for ( std::size_t i = 0; i < faces.size(); ++i )
    {
        records += std::to_string(i) + ": -1 0x12 0 3 3 0.375 " + faces[i] + "\n";
    }
    records += "FACE NORMALS\n";
    for ( std::size_t i = 0; i < faces.size(); ++i )
    {
        records += std::to_string(i) + ": 0 7 0\n";
    }
    return records;
}

std::string withMeshRecords(std::string input, std::string_view records, bool bothMeshes = false)
{
    const auto last = input.rfind("MESH 1\n");
    const auto begin = input.find("VERTICES ", last);
    input.replace(begin, input.find("SECTION: HIERARCHYDEF", begin) - begin, records);
    if ( bothMeshes )
    {
        const auto first = input.rfind("MESH 0\n", last);
        const auto start = input.find("VERTICES ", first);
        input.replace(start, last - start, records);
    }
    return input;
}

void checkGeometryAdmission(const std::string& input)
{
    const std::string tri = "3 0,-1 1,-1 2,-1";
    const auto makeDocument = [&](const std::vector<std::string>& positions,
                                  const std::vector<std::string>& faces = {})
    {
        return NativeModelDocument::open(withMeshRecords(input, meshRecords(positions, faces)));
    };
    const auto transform = [](const NativeModelDocument& doc, double scale, const sed::TVector& shift)
    {
        const NativeMeshTransform request[]{ { 1, 1, scale, shift } };
        return doc.applyMeshTransforms(request);
    };
    const auto empty = makeDocument({});
    require(transform(empty, 12, { -2, 3, 1 }).source() == empty.source(), "Empty mesh changed.");
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for ( const auto& bad : std::vector<NativeMeshTransform>{
            { 2, 1 }, { 1, 2 }, { 1, 1, 0 }, { 1, 1, -1 }, { 1, 1, nan }, { 1, 1, inf },
            { 1, 1, 1, { inf, 0, 0 } }, { 1, 1, 1, { 0, nan, 0 } }, { 1, 1, 1, { 0, 0, -inf } } } )
    {
        const NativeMeshTransform batch[]{ { 0, 0, 2 }, bad };
        refuses([&] { empty.applyMeshTransforms(batch); });
    }
    const NativeMeshTransform duplicates[]{ { 1, 1 }, { 1, 1, 2 } };
    refuses([&] { empty.applyMeshTransforms(duplicates); }, "duplicate");
    refuses([&] { empty.applyMeshTransforms(std::vector<NativeMeshTransform>(4097)); }, "count");
    const auto fused = makeDocument({ "0.999999992549419403076171875 -0.000 0 0.5" });
    const auto cancelled = transform(fused, 1 + 0x1p-27, { -1, 0, 0 });
    require(cancelled.geosets()[1].meshes[1].vertices[0].x == -0x1p-54, "Product rounded before addition.");
    require(cancelled.source().find(" -0.000 0 0.5") != std::string_view::npos, "Signed zero was respelled.");
    const auto overflow = makeDocument({ "2 0 0 1" });
    refuses([&] { transform(overflow, std::numeric_limits<double>::max(), {}); }, "nonfinite");
    const auto asymmetric = makeDocument({ "0 0 0 1", "1.5 0 0 1", "0 1 0 1" }, { tri });
    const auto denorm = std::numeric_limits<double>::denorm_min();
    // A relative edge of 1.5 * denorm rounds to 2 * denorm in both the candidate
    // and a naively scaled reference, hiding a one-third change in proportions.
    refuses([&] { transform(asymmetric, denorm, {}); }, "face extent");
    const auto tiny = makeDocument({ "0 0 0 1", "3.88549e-318 0 0 1", "0 2.590327e-318 0 1" }, { tri });
    const auto original = std::string(tiny.source());
    const NativeMeshTransform mixedScale[]{ { 0, 0, 2 }, { 1, 1, 0x1p-19 } };
    refuses([&] { tiny.applyMeshTransforms(mixedScale); }, "face extent");
    require(tiny.source() == original && tiny.geosets()[0].meshes[0].vertices[1].x == 0.06,
        "Failed subnormal transform mutated the source batch.");
    require(transform(tiny, 1, {}).source() == original, "Unchanged subnormal face should keep its source bytes.");

    const auto unitFace = makeDocument({ "0 0 0 1", "1 0 0 1", "0 1 0 1" }, { tri });
    const auto minNormal = std::numeric_limits<double>::min();
    refuses([&] { transform(unitFace, std::nextafter(minNormal, 0.0), {}); }, "face extent");
    // The face extent is valid as a double, but the game reads positions as floats.
    refuses([&] { transform(unitFace, minNormal, {}); }, "read as zero");
    const auto floatMin = static_cast<double>(std::numeric_limits<float>::min());
    const auto atBoundary = transform(unitFace, floatMin, {});
    const auto& boundaryRows = atBoundary.geosets()[1].meshes[1].vertices;
    require(boundaryRows[1].x == floatMin && boundaryRows[2].y == floatMin,
        "Smallest normal float face extent failed readback.");
    refuses([&] { transform(unitFace, 1e40, {}); }, "float range");
    refuses([&] { transform(overflow, 1, { 4e38, 0, 0 }); }, "float range");
    const auto largest = static_cast<double>(std::numeric_limits<float>::max());
    require(transform(overflow, 1, { largest - 2, 0, 0 }).geosets()[1].meshes[1].vertices[0].x
            == std::fma(1.0, 2.0, largest - 2),
        "A coordinate inside the float range was refused.");
    // Distinct floats near 1 stay distinct doubles after a 2^25 shift, but both round to 2^25 as floats.
    const auto close = makeDocument({ "1 0 0 1", "1.0000002 0 0 1" });
    require(transform(close, 1, { 1, 0, 0 }).geosets()[1].meshes[1].vertices[1].x == std::fma(1.0, 1.0000002, 1.0),
        "A small shift of distinct float positions failed readback.");
    refuses([&] { transform(close, 1, { 0x1p25, 0, 0 }); }, "coincident in the game");
    const auto loose = makeDocument({ "0 0 0 1", "1 0 0 0" });
    refuses([&] { transform(loose, 1, { 0x1p54, 0, 0 }); }, "coincident");

    const std::vector<std::string> pieces{ "0 0 0 0", "0 1 0 1", "0 0 1 1",
                                           "1 0 0 1", "1 1 0 1", "1 0 1 1" };
    const auto disconnected = makeDocument(pieces, { tri, "3 3,-1 4,-1 5,-1" });
    refuses([&] { transform(disconnected, 1, { 0x1p54, 0, 0 }); }, "coincident");

    const auto collinear = makeDocument({ "1 1 1 1", "2 2 2 1", "3 3 3 1" }, { tri });
    require(transform(collinear, 1, {}).source() == collinear.source(), "Identity rejected an unedited face.");
    require(transform(collinear, 1, { 1e-30, 0, 0 }).source() == collinear.source(), "Rounded no-op rejected.");
    refuses([&] { transform(collinear, 2, {}); }, "degenerate");
    const auto warp = makeDocument({ "0 0 0 1", "0.1 0 0 1", "0 1 0 1" }, { tri });
    refuses([&] { transform(warp, 1, { 1e8, 0, 0 }); }, "relative");
    // Candidate remains a triangle with three distinct rows, but rounding puts the
    // short edge below the strict polygon threshold while relative error is < 1e-10.
    const auto narrow = makeDocument({ "0 0 0 1", "1 0 0 1", "0 1.0001e-10 0 1" }, { tri });
    require(transform(narrow, 2, {}).geosets()[1].meshes[1].vertices[1].x == 2, "Valid narrow baseline refused.");
    refuses([&] { transform(narrow, 1, { 0, 131072, 0 }); });
    const NativeMeshTransform mixedChange[]{ { 1, 1 }, { 0, 1, 2 } };
    require(collinear.applyMeshTransforms(mixedChange).geosets()[0].meshes[1].vertices[1].x == 0.06,
        "Identity target with invalid source faces blocked another mesh edit.");
    const auto normal = makeDocument({ "0 0 0 1", "1 0 0 1", "0 0.0001 0.00001 1" }, { tri });
    refuses([&] { transform(normal, 1, { 0, 0, 8192 }); }, "normal");
    const auto crossing = makeDocument({ "0 0 0 1", "1 1 0 1", "0 1 0 1", "1 0 0 1" },
        { "4 0,-1 1,-1 2,-1 3,-1" });
    refuses([&] { transform(crossing, 2, {}); }, "convex");

    // A loose duplicate shares a position but carries its own color and normal.
    const auto seams = makeDocument({ "-0.000 0 0 0.25 # red\r", "0 0 0 0.75", "0 1 0 0.5", "1 0 0 1" },
        { "3 0,-1 2,-1 3,-1" });
    auto expected = std::string(seams.source());
    replace(expected, "-0.000 0 0 0.25 # red", "2 0 0 0.25 # red");
    replace(expected, "1: 0 0 0 0.75", "1: 2 0 0 0.75");
    replace(expected, "2: 0 1 0 0.5", "2: 2 1 0 0.5");
    replace(expected, "3: 1 0 0 1", "3: 3 0 0 1");
    require(transform(seams, 1, { 2, 0, 0 }).source() == expected, "Seam rows merged or attributes changed.");
}

void checkSharedWorkBudget(const std::string& input)
{
    constexpr int corners = 1000;
    std::vector<std::string> rows;
    std::string face = std::to_string(corners);
    for ( int i = 0; i < corners; ++i )
    {
        const auto angle = i * (2 * std::numbers::pi / corners);
        std::ostringstream row;
        row << std::setprecision(17) << std::cos(angle) << ' ' << std::sin(angle) << " 0 1";
        rows.push_back(row.str());
        face += " " + std::to_string(i) + ",-1";
    }
    const auto records = meshRecords(rows, std::vector<std::string>(13, face));
    const auto doc = NativeModelDocument::open(withMeshRecords(input, records, true));
    const NativeMeshTransform batch[]{ { 1, 0, 2 }, { 1, 1, 2 } };
    require(doc.applyMeshTransforms(std::span(batch).first(1)).geosets()[1].meshes[0].vertices[0].x == 2,
        "Single mesh exceeded the shared work budget.");
    refuses([&] { doc.applyMeshTransforms(batch); }, "work budget");
}

}

int main()
{
    try
    {
        const auto path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
            .parent_path().parent_path() / "tests/fixtures/relay-sentry/relay_sentry.3do";
        std::ifstream file(path, std::ios::binary);
        require(file.good(), "Fixture missing.");
        std::string input{std::istreambuf_iterator<char>(file), {}};
        replace(input, "1: 0x0000 0x00001 1 0", "1: 0x0000 0x00001 0 0");
        replace(input, "0: 0 0\n1: 1 0", "0: -0.000 0.00 # origin\r\n1: 1 0");
        replace(input, "3 1,1 3,3 2,2", "3 1,0 3,3 2,2");
        const auto doc = NativeModelDocument::open(input);
        require(doc.source() == input && doc.geosets().size() == 2, "Source or LOD lost.");
        for ( const auto& model : doc.geosets() )
        {
            require(!model.hierarchyTransformsApplied && model.meshes.size() == 2 &&
                model.nodes[1].mesh == 0 && model.meshes[1].name == "boom", "Raw mesh identity lost.");
        }
        const NativeMeshTransform boomTransform[]{ { 1, 1, 1.25, { 0.125, -0.25, 0.5 } } };
        const auto transformed = doc.applyMeshTransforms(boomTransform);
        auto expectedTransform = input;
        const auto positions = expectedTransform.rfind("VERTICES 3\n0: 0.000000");
        require(positions != expectedTransform.npos, "Missing far LOD boom.");
        expectedTransform.replace(positions, expectedTransform.find("TEXTURE VERTICES", positions) - positions,
            "VERTICES 3\n0: 0.125 -0.25 0.5 1.000000\n1: 0.1625 -0.25 0.5 1.000000\n"
            "2: 0.125 -0.2125 0.5 1.000000\n");
        require(transformed.source() == expectedTransform, "Transform changed bytes outside far boom XYZ tokens.");
        const auto& rows = transformed.geosets()[1].meshes[1].vertices;
        require(rows[0].x == 0.125 && rows[0].y == -0.25 && rows[0].z == 0.5 &&
            rows[1].x == 0.1625 && rows[1].y == -0.25 && rows[1].z == 0.5 &&
            rows[2].x == 0.125 && rows[2].y == -0.2125 && rows[2].z == 0.5, "Incorrect local XYZ readback.");

        const NativeTextureVertexEdit edits[]{{0, 0, 0, 0, -32.5f}};
        auto expected = input;
        replace(expected, "-0.000 0.00 # origin", "-0.000 -32.5 # origin");
        const auto changed = doc.applyTextureVertexEdits(edits);
        require(changed.source() == expected && doc.source() == input, "Changed unrelated source bytes.");
        require(changed.geosets()[0].meshes[0].texVertices[0].v == -32.5f, "Changed UV did not reopen.");
        const auto& faces = changed.geosets()[0].meshes[0].faces;
        require(faces[0].vertices[0] == 0 && faces[1].vertices[0] == 1 &&
            faces[0].texVertices[0] == 0 && faces[1].texVertices[0] == 0, "UV seam changed geometry binding.");
        require(changed.applyTextureVertexEdits(edits).source() == expected, "Repeat edit changed spelling.");
        const NativeTextureVertexEdit extremes[]{
            { 1, 1, 2, std::numeric_limits<float>::max(), std::numeric_limits<float>::denorm_min() },
            { 0, 1, 1, -0.125f, 2048.0f }
        };
        const auto variant = doc.applyTextureVertexEdits(extremes);
        const auto uv = variant.geosets()[1].meshes[1].texVertices[2];
        require(uv.u == extremes[0].u && uv.v == extremes[0].v, "Extreme finite UV lost precision.");
        require(variant.geosets()[0].meshes[1].texVertices[1].u == -0.125f, "Second batch row missing.");
        require(doc.applyTextureVertexEdits({}).source() == input, "Empty edit changed bytes.");
        const NativeTextureVertexEdit duplicates[]{edits[0], edits[0]};
        refuses([&] { doc.applyTextureVertexEdits(duplicates); });
        const NativeTextureVertexEdit invalidEdits[]{
            { 2, 0, 0, 0, 0 }, { 0, 2, 0, 0, 0 }, { 0, 0, 8, 0, 0 },
            { 0, 0, 0, std::numeric_limits<float>::infinity(), 0 }
        };
        for ( const auto& bad : invalidEdits )
        {
            const NativeTextureVertexEdit batch[]{edits[0], bad};
            refuses([&] { doc.applyTextureVertexEdits(batch); });
        }
        refuses([&] { NativeModelDocument::open(input + "SECTION: FUTURE\n"); });
        auto unknown = input;
        replace(unknown, "NAME base", "NAME base\nFUTURE 3");
        refuses([&] { NativeModelDocument::open(unknown); });
        for ( const auto& [original, invalid] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
                {"3DO 2.1", "3DO 2.3"}, {"3DO 2.1", "3DO 2.1\nOTHER 7"},
                {"1.000000\n", "1.000000 1 1 1\n"}, {"0: -0.000", "0: 1e99"}} )
        {
            auto malformed = input;
            replace(malformed, original, invalid);
            refuses([&] { NativeModelDocument::open(malformed); });
        }
        auto missing = input;
        replace(missing, "FACE NORMALS\n0: 0 0 1\n1: 0 0 1\n", "");
        refuses([&] { NativeModelDocument::open(missing); });

        // A material rename changes only the entry's bytes, through the native document or the ordinary reader.
        // The second also takes a 2.1 model whose vertex rows carry RGBA light, as RED's own writer and the Blender
        // Sith add-on write it, which native editing refuses.
        const NativeMaterialRename dark[]{ { 0, "relay_dark.mat" } };
        auto renamedInput = input;
        replace(renamedInput, "0: relay_skin.mat", "0: relay_dark.mat");
        require(doc.applyMaterialRenames(dark).source() == renamedInput && renameModelMaterials(input, dark) == renamedInput,
            "A rename changed bytes outside the table entry.");
        auto rgba = input;
        replace(rgba, "1.000000\n", "1.000000 1 1 1\n");
        refuses([&] { NativeModelDocument::open(rgba); }, "vertex lighting");
        auto renamedRgba = rgba;
        replace(renamedRgba, "0: relay_skin.mat", "0: relay_dark.mat");
        require(renameModelMaterials(rgba, dark) == renamedRgba, "An RGBA-lit rename changed bytes outside the table entry.");
        const auto reread = readModelGeosets(renamedRgba);
        require(reread.size() == 2 && reread[0].materials == std::vector<std::string>{ "relay_dark.mat" }
                && reread[1].materials == reread[0].materials, "Renamed table did not read back in every geoset.");
        const NativeMaterialRename same[]{ { 0, "relay_skin.mat" } };
        require(renameModelMaterials(rgba, same) == rgba && doc.applyMaterialRenames(same).source() == input,
            "Renaming an entry to its own name changed bytes.");
        const NativeMaterialRename spaced[]{ { 0, "relay dark.mat" } };
        const NativeMaterialRename outside[]{ { 1, "x.mat" } };
        const NativeMaterialRename twice[]{ { 0, "a.mat" }, { 0, "b.mat" } };
        refuses([&] { renameModelMaterials(rgba, spaced); }, "printable ASCII");
        refuses([&] { renameModelMaterials(rgba, outside); }, "outside the table");
        refuses([&] { doc.applyMaterialRenames(twice); }, "duplicate material ordinal");
        refuses([&] { renameModelMaterials("bad model", dark); });
        refuses([] { NativeModelDocument::open(std::string(16 * 1024 * 1024 + 1, ' ')); });
        auto missingVertexNormals = input;
        replace(missingVertexNormals, "VERTEX NORMALS\n0: 0 0 1\n1: 0 0 1\n2: 0 0 1\n3: 0 0 1\n", "");
        refuses([&] { NativeModelDocument::open(missingVertexNormals); }, "vertex normals");
        const std::vector<NativeTextureVertexEdit> oversizedBatch(262145);
        refuses([&] { doc.applyTextureVertexEdits(oversizedBatch); }, "edit count");

        std::ifstream singleFile(path.parent_path() / "relay_single.3do", std::ios::binary);
        require(singleFile.good(), "Single-LOD fixture missing.");
        std::string full{std::istreambuf_iterator<char>(singleFile), {}};
        full.push_back('#');
        full.resize(16 * 1024 * 1024, 'x');
        const auto fullDocument = NativeModelDocument::open(full);
        const NativeTextureVertexEdit larger[]{ { 0, 0, 0, 0, 32 } };
        refuses([&] { fullDocument.applyTextureVertexEdits(larger); }, "edited source exceeds byte limit");
        require(fullDocument.source() == full, "Output overflow mutated the document.");

        const NativeMeshTransform grow[]{ { 0, 0, 1, { 0.12345678901234567, 0, 0 } } };
        refuses([&] { fullDocument.applyMeshTransforms(grow); }, "edited source exceeds byte limit");
        require(fullDocument.source() == full, "Transform output overflow mutated the document.");
        checkGeometryAdmission(input);
        checkSharedWorkBudget(input);
        require(doc.applyMeshTransforms({}).source() == input, "Empty transform batch changed source.");
        auto chained = expectedTransform;
        replace(chained, "-0.000 0.00 # origin", "-0.000 -32.5 # origin");
        require(changed.applyMeshTransforms(boomTransform).source() == chained, "Chained edit used stale spans.");

        auto shared = input;
        replace(shared, "NAME boom", "NAME base");
        const auto sharedDoc = NativeModelDocument::open(shared);
        const NativeMeshTransform sharedTransform[]{ { 0, 0, 2 }, { 0, 1, 1, { 0.25, 0, 0 } } };
        const auto sharedResult = sharedDoc.applyMeshTransforms(sharedTransform);
        const auto& sharedModel = sharedResult.geosets()[0];
        require(sharedModel.nodes[0].mesh == 0 && sharedModel.nodes[1].mesh == 0 && sharedModel.nodes[1].pivot.x == 0.01 &&
            sharedModel.meshes[0].vertices[1].x == 0.12 && sharedModel.meshes[1].vertices[0].x == 0.25 &&
            sharedModel.meshes[0].name == sharedModel.meshes[1].name, "Shared or orphan source mesh lost identity.");

        auto positive = input;
        replace(positive, "0: -0.000", "0: +1.5e+1");
        require(NativeModelDocument::open(positive).geosets()[0].meshes[0].texVertices[0].u == 15,
            "Valid leading plus or exponent sign refused.");
        auto material = input;
        replace(material, "relay_skin.mat", "ns:relay_skin.mat");
        refuses([&] { NativeModelDocument::open(material); }, "material name");
        auto doubleSign = input;
        replace(doubleSign, "0: -0.000", "0: +-1");
        refuses([&] { NativeModelDocument::open(doubleSign); }, "numeric");
        require(doc.source() == input, "Failure mutated the original document.");
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
