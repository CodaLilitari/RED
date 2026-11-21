#include "sed/assets/native_model_document.hpp"

#include <initializer_list>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

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
void refuses(Fn attempt)
{
    try
    {
        attempt();
    }
    catch ( const std::runtime_error& )
    {
        return;
    }
    throw std::runtime_error("Expected admission or edit refusal.");
}

void replace(std::string& text, std::string_view old, std::string_view next)
{
    const auto at = text.find(old);
    require(at != text.npos, "Fixture anchor missing.");
    text.replace(at, old.size(), next);
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
        refuses([] { NativeModelDocument::open(std::string(16 * 1024 * 1024 + 1, ' ')); });
        require(doc.source() == input, "Failure mutated the original document.");
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
