#include "test_common.hpp"

#include "sed/assets/3do.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sed::tests
{

namespace
{

std::string readFixture3do(const char* name)
{
    const std::string path = std::string("src/core/tests/assets/fixtures/") + name;
    std::ifstream in(path, std::ios::binary);
    check(in.good(), "3DO.writer: fixture file opens");
    std::ostringstream buffer{};
    buffer << in.rdbuf();
    return buffer.str();
}

sed::assets::Model3do loadRaw3do(const std::string& text)
{
    const auto bytes = std::as_bytes(
        std::span<const char>(text.data(), text.size()));
    sed::assets::Model3doLoadOptions options{};
    options.applyHierarchyTransforms = false;
    return sed::assets::loadFrom3DO(bytes, options);
}

bool sameVec(const sed::TVector& a, const sed::TVector& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool sameColour(const sed::ColorF& a, const sed::ColorF& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

bool sameGeometry(const sed::assets::Model3do& a, const sed::assets::Model3do& b)
{
    if (a.materials != b.materials || a.meshes.size() != b.meshes.size()
        || a.nodes.size() != b.nodes.size() || a.radius != b.radius
        || !sameVec(a.insertOffset, b.insertOffset))
    {
        return false;
    }
    for (std::size_t m = 0; m < a.meshes.size(); ++m)
    {
        const auto& x = a.meshes[m];
        const auto& y = b.meshes[m];
        if (x.name != y.name || x.authoredRadius != y.authoredRadius
            || x.geo != y.geo || x.light != y.light || x.tex != y.tex
            || x.vertices.size() != y.vertices.size()
            || x.texVertices.size() != y.texVertices.size()
            || x.vertexNormals.size() != y.vertexNormals.size()
            || x.faces.size() != y.faces.size()
            || x.faceNormals.size() != y.faceNormals.size())
        {
            return false;
        }
        for (std::size_t v = 0; v < x.vertices.size(); ++v)
        {
            if (x.vertices[v].x != y.vertices[v].x
                || x.vertices[v].y != y.vertices[v].y
                || x.vertices[v].z != y.vertices[v].z
                || !sameColour(x.vertices[v].color, y.vertices[v].color))
            {
                return false;
            }
        }
        for (std::size_t v = 0; v < x.texVertices.size(); ++v)
        {
            if (x.texVertices[v].u != y.texVertices[v].u
                || x.texVertices[v].v != y.texVertices[v].v)
            {
                return false;
            }
        }
        for (std::size_t v = 0; v < x.vertexNormals.size(); ++v)
        {
            if (!sameVec(x.vertexNormals[v], y.vertexNormals[v]))
            {
                return false;
            }
        }
        for (std::size_t f = 0; f < x.faces.size(); ++f)
        {
            if (x.faces[f].material != y.faces[f].material
                || x.faces[f].faceFlags != y.faces[f].faceFlags
                || x.faces[f].geo != y.faces[f].geo
                || x.faces[f].light != y.faces[f].light
                || x.faces[f].tex != y.faces[f].tex
                || !sameColour(x.faces[f].extraLight, y.faces[f].extraLight)
                || x.faces[f].vertices != y.faces[f].vertices
                || x.faces[f].texVertices != y.faces[f].texVertices)
            {
                return false;
            }
        }
        for (std::size_t f = 0; f < x.faceNormals.size(); ++f)
        {
            if (!sameVec(x.faceNormals[f], y.faceNormals[f]))
            {
                return false;
            }
        }
    }
    for (std::size_t n = 0; n < a.nodes.size(); ++n)
    {
        const auto& x = a.nodes[n];
        const auto& y = b.nodes[n];
        if (x.id != y.id || x.type != y.type || x.flags != y.flags
            || x.mesh != y.mesh || x.parent != y.parent || x.name != y.name
            || !sameVec(x.position, y.position)
            || !sameVec(x.rotation, y.rotation)
            || !sameVec(x.pivot, y.pivot))
        {
            return false;
        }
    }
    return true;
}

bool saveThrowsWith(const sed::assets::Model3do& model, const std::string_view needle)
{
    try
    {
        (void)sed::assets::saveTo3DO(model);
    }
    catch (const std::runtime_error& error)
    {
        return std::string_view(error.what()).find(needle) != std::string_view::npos;
    }
    catch (...)
    {
        return false;
    }
    return false;
}

void checkRoundTrip(const char* fixture, const char* label)
{
    const sed::assets::Model3do original = loadRaw3do(readFixture3do(fixture));
    const std::string written = sed::assets::saveTo3DO(original);
    const sed::assets::Model3do reloaded = loadRaw3do(written);
    check(sameGeometry(original, reloaded), label);
    // The transform flag lives in the load options, not the file: the raw
    // original and the reloaded model both carry it unset, so comparing the
    // full model also proves hierarchy metadata survived.
    check(!reloaded.hierarchyTransformsApplied
            && reloaded.geosetCount == original.geosetCount,
        "3DO.writer: reloaded model keeps raw-hierarchy state and geoset count");
}

} // namespace

void runModel3doWriterTests()
{
    {
        checkRoundTrip("f04_diamond.3do",
            "3DO.writer: diamond fixture round-trips geometry, hierarchy, and normals");
        const sed::assets::Model3do model = loadRaw3do(readFixture3do("f04_diamond.3do"));
        check(model.meshes.size() == 1 && model.nodes.size() == 1
                && model.meshes[0].faces.size() == 2
                && model.meshes[0].faces[0].faceFlags == 0x2u
                && model.meshes[0].faces[1].faceFlags == 0x1u,
            "3DO.writer: diamond fixture keeps per-face flags");
        check(model.nodes.size() == 1 && model.nodes[0].flags == 0x2u
                && model.nodes[0].type == 1u && model.nodes[0].mesh == 0,
            "3DO.writer: diamond fixture keeps node flags, type, and mesh link");
    }

    {
        checkRoundTrip("f04_hierarchy.3do",
            "3DO.writer: hierarchy fixture round-trips multi-node hierarchy");
        const sed::assets::Model3do model = loadRaw3do(readFixture3do("f04_hierarchy.3do"));
        check(model.nodes.size() == 5 && model.meshes.size() == 2
                && model.nodes[0].parent == -1 && model.nodes[3].parent == -1
                && model.nodes[1].parent == 0 && model.nodes[2].parent == 1
                && model.nodes[4].parent == 1
                && model.nodes[2].mesh == -1 && model.nodes[4].mesh == 1
                && model.nodes[1].position.x == 1.0
                && model.nodes[4].rotation.z == 90.0,
            "3DO.writer: hierarchy fixture is a five-node two-root model sharing parents");
    }

    {
        checkRoundTrip("f04_marker.3do",
            "3DO.writer: 2.1 marker fixture round-trips grey colours and empty UV list");
        const sed::assets::Model3do model = loadRaw3do(readFixture3do("f04_marker.3do"));
        check(model.version == 2.1 && model.meshes.size() == 1
                && model.meshes[0].texVertices.empty()
                && model.meshes[0].faces.size() == 1,
            "3DO.writer: marker fixture is a 2.1 model with an empty texture-vertex list");
        const std::string written = sed::assets::saveTo3DO(model);
        const sed::assets::Model3do reloaded = loadRaw3do(written);
        check(reloaded.meshes.size() == 1
                && reloaded.meshes[0].vertices.size() == 3
                && reloaded.meshes[0].vertices[1].color.r == 0.5f
                && reloaded.meshes[0].faces[0].extraLight.r == 0.5f,
            "3DO.writer: 2.1 grey vertex and face intensities survive exactly");
    }

    {
        // Empty mesh edge: no vertices, faces, texture vertices, or normals.
        sed::assets::Model3do edge{};
        edge.version = 2.3;
        edge.hierarchyTransformsApplied = false;
        edge.materials = { "edge.mat" };
        sed::assets::Model3doMesh empty{};
        empty.name = "empty_mesh";
        edge.meshes.push_back(empty);
        sed::assets::Model3doNode node{};
        node.id = 0;
        node.type = 1;
        node.mesh = 0;
        node.name = "empty_mesh";
        edge.nodes.push_back(node);
        const std::string written = sed::assets::saveTo3DO(edge);
        check(written.find("VERTICES 0") != std::string::npos
                && written.find("FACES 0") != std::string::npos,
            "3DO.writer: empty mesh writes explicit zero counts");
        const sed::assets::Model3do reloaded = loadRaw3do(written);
        check(reloaded.meshes.size() == 1 && reloaded.meshes[0].vertices.empty()
                && reloaded.meshes[0].faces.empty() && reloaded.nodes.size() == 1,
            "3DO.writer: empty mesh round-trips");
    }

    {
        // Double and float text round-trips exactly, including signed zero.
        sed::assets::Model3do precise{};
        precise.version = 2.3;
        precise.hierarchyTransformsApplied = false;
        sed::assets::Model3doMesh mesh{};
        mesh.name = "precise_mesh";
        sed::assets::Model3doVertex vertex{};
        vertex.x = 0.1;
        vertex.y = -0.0;
        vertex.z = 1.0 / 3.0;
        vertex.color = sed::ColorF{ 0.1f, 0.2f, 0.3f, 1.0f };
        mesh.vertices.push_back(vertex);
        mesh.vertices.push_back(vertex);
        mesh.vertices.push_back(vertex);
        sed::assets::Model3doTexVertex uv{};
        uv.u = 0.1f;
        uv.v = -0.0f;
        mesh.texVertices = { uv, uv, uv };
        sed::assets::Model3doFace face{};
        face.material = -1;
        face.faceFlags = 0x2u;
        face.extraLight = sed::ColorF{ 0.1f, 0.2f, 0.3f, 0.4f };
        face.vertices = { 0, 1, 2 };
        face.texVertices = { 0, 1, 2 };
        mesh.faces.push_back(face);
        precise.meshes.push_back(mesh);
        const sed::assets::Model3do reloaded =
            loadRaw3do(sed::assets::saveTo3DO(precise));
        check(reloaded.meshes.size() == 1 && reloaded.meshes[0].vertices.size() == 3
                && reloaded.meshes[0].vertices[0].x == 0.1
                && std::signbit(reloaded.meshes[0].vertices[0].y)
                && reloaded.meshes[0].vertices[0].z == 1.0 / 3.0
                && reloaded.meshes[0].texVertices[0].u == 0.1f
                && std::signbit(reloaded.meshes[0].texVertices[0].v)
                && reloaded.meshes[0].faces[0].extraLight.a == 0.4f,
            "3DO.writer: double/float values round-trip exactly through text");
    }

    {
        const std::string text = readFixture3do("f04_diamond.3do");
        const auto bytes = std::as_bytes(std::span<const char>(text.data(), text.size()));
        const sed::assets::Model3do transformed = sed::assets::loadFrom3DO(bytes);
        check(transformed.hierarchyTransformsApplied && !transformed.nodes.empty(),
            "3DO.writer: default load applies hierarchy transforms");
        check(saveThrowsWith(transformed, "hierarchy-transformed geometry"),
            "3DO.writer: hierarchy-transformed geometry is refused by name");
    }

    {
        sed::assets::Model3do badType = loadRaw3do(readFixture3do("f04_hierarchy.3do"));
        badType.nodes[0].type = 7;
        check(saveThrowsWith(badType, "unsupported hierarchy node type 7"),
            "3DO.writer: unsupported hierarchy node type is refused by name");
        sed::assets::Model3do badVersion = loadRaw3do(readFixture3do("f04_hierarchy.3do"));
        badVersion.version = 2.2;
        check(saveThrowsWith(badVersion, "unsupported 3DO version"),
            "3DO.writer: unsupported write version is refused by name");
        sed::assets::Model3do badGeosets = loadRaw3do(readFixture3do("f04_hierarchy.3do"));
        badGeosets.geosetCount = 2;
        check(saveThrowsWith(badGeosets, "more than one geoset"),
            "3DO.writer: multi-geoset model is refused instead of dropping geosets");
        sed::assets::Model3do badMesh = loadRaw3do(readFixture3do("f04_hierarchy.3do"));
        badMesh.nodes[0].mesh = 9;
        check(saveThrowsWith(badMesh, "not a retained mesh"),
            "3DO.writer: hierarchy-expanded mesh reference is refused by name");
        sed::assets::Model3do badParent = loadRaw3do(readFixture3do("f04_hierarchy.3do"));
        badParent.nodes[0].parent = 9;
        check(saveThrowsWith(badParent, "invalid parent"),
            "3DO.writer: invalid hierarchy parent is refused by name");
        sed::assets::Model3do spaced = loadRaw3do(readFixture3do("f04_hierarchy.3do"));
        spaced.meshes[0].name = "two words";
        check(saveThrowsWith(spaced, "cannot represent"),
            "3DO.writer: unrepresentable mesh name token is refused by name");
        sed::assets::Model3do nanModel = loadRaw3do(readFixture3do("f04_hierarchy.3do"));
        nanModel.meshes[0].vertices[0].x =
            std::numeric_limits<double>::quiet_NaN();
        check(saveThrowsWith(nanModel, "non-finite"),
            "3DO.writer: non-finite vertex is refused by name");
    }

    {
        sed::assets::Model3do nonGrey = loadRaw3do(readFixture3do("f04_marker.3do"));
        nonGrey.meshes[0].vertices[0].color = sed::ColorF{ 1.0f, 0.0f, 0.0f, 1.0f };
        check(saveThrowsWith(nonGrey, "vertex colours must be grey"),
            "3DO.writer: 2.1 non-grey vertex colour is refused instead of approximated");
        sed::assets::Model3do nonGreyFace = loadRaw3do(readFixture3do("f04_marker.3do"));
        nonGreyFace.meshes[0].faces[0].extraLight =
            sed::ColorF{ 1.0f, 0.0f, 0.0f, 1.0f };
        check(saveThrowsWith(nonGreyFace, "face extra light must be grey"),
            "3DO.writer: 2.1 non-grey face extra light is refused instead of approximated");
    }

    {
        // Stream overload writes identical text.
        const sed::assets::Model3do model = loadRaw3do(readFixture3do("f04_diamond.3do"));
        std::ostringstream out{};
        sed::assets::saveTo3DO(model, out);
        check(out.str() == sed::assets::saveTo3DO(model),
            "3DO.writer: stream overload writes the same text as the string form");
    }

}

} // namespace sed::tests
