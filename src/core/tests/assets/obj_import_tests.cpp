#include "test_common.hpp"

#include "sed/assets/3do.hpp"
#include "sed/assets/obj_import.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sed::tests
{

namespace
{

using sed::assets::ObjImportError;
using sed::assets::ObjImportErrorKind;
using sed::assets::ObjImportOptions;
using sed::assets::ObjImportResult;

// A 2x2x2 cube in OBJ's y-up convention, every face wound counter-clockwise
// seen from outside. The bottom face's last corner uses vt 5 (0.25, 0.75).
const char* const kCubeObj =
    "# synthetic cube\n"
    "mtllib crate.mtl\n"
    "o Crate\n"
    "v -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\n"
    "v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\n"
    "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvt 0.25 0.75\n"
    "vn 0 0 1\n"
    "usemtl crate\n"
    "s 1\n"
    "f 1/1/1 2/2/1 3/3/1 4/4/1\n"
    "f 6/1 5/2 8/3 7/4\n"
    "f 2/1 6/2 7/3 3/4\n"
    "f 5/1 1/2 4/3 8/4\n"
    "f 4/1 3/2 7/3 8/4\n"
    "f 5/1 6/2 2/3 1/5\n";

ObjImportOptions crateOptions()
{
    ObjImportOptions options{};
    options.materials.push_back({ "crate", "crate.mat", 64, 32 });
    return options;
}

ObjImportResult importOrFail(const std::string_view text, const ObjImportOptions& options, const char* label)
{
    try
    {
        return sed::assets::importObjModel(text, options);
    }
    catch ( const std::exception& error )
    {
        check(false, label);
        std::cerr << "  unexpected exception: " << error.what() << "\n";
    }
    return {};
}

// True when the import throws ObjImportError of `kind` with `code` and a
// message containing `needle`.
bool importRefuses(
    const std::string_view text,
    const ObjImportOptions& options,
    const ObjImportErrorKind kind,
    const std::string_view code,
    const std::string_view needle = {})
{
    try
    {
        (void)sed::assets::importObjModel(text, options);
    }
    catch ( const ObjImportError& error )
    {
        const bool matches = error.kind() == kind && error.code() == code
            && std::string_view(error.what()).find(needle) != std::string_view::npos;
        if ( !matches )
        {
            std::cerr << "  got code " << error.code() << ": " << error.what() << "\n";
        }
        return matches;
    }
    catch ( const std::exception& error )
    {
        std::cerr << "  got a different exception: " << error.what() << "\n";
        return false;
    }
    return false;
}

sed::assets::Model3do reloadRaw(const ObjImportResult& result)
{
    const std::string text = sed::assets::saveTo3DO(result.model);
    sed::assets::Model3doLoadOptions options{};
    options.applyHierarchyTransforms = false;
    return sed::assets::loadFrom3DO(
        std::as_bytes(std::span<const char>(text.data(), text.size())), options);
}

bool hasVertex(const sed::assets::Model3doMesh& mesh, const double x, const double y, const double z)
{
    for ( const sed::assets::Model3doVertex& vertex : mesh.vertices )
    {
        if ( vertex.x == x && vertex.y == y && vertex.z == z )
        {
            return true;
        }
    }
    return false;
}

std::size_t ignoredCount(const ObjImportResult& result, const std::string_view keyword)
{
    for ( const auto& [name, count] : result.ignoredStatements )
    {
        if ( name == keyword )
        {
            return count;
        }
    }
    return 0;
}

double polygonArea(const sed::assets::Model3doMesh& mesh, const sed::assets::Model3doFace& face)
{
    TVector sum{};
    for ( std::size_t i = 0; i < face.vertices.size(); ++i )
    {
        const auto& a = mesh.vertices[static_cast<std::size_t>(face.vertices[i])];
        const auto& b = mesh.vertices[static_cast<std::size_t>(face.vertices[(i + 1) % face.vertices.size()])];
        sum.x += (a.y - b.y) * (a.z + b.z);
        sum.y += (a.z - b.z) * (a.x + b.x);
        sum.z += (a.x - b.x) * (a.y + b.y);
    }
    return sum.Length() / 2.0;
}

std::string triangleFan(const std::size_t triangles)
{
    std::string text = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nusemtl crate\n";
    for ( std::size_t i = 0; i < triangles; ++i )
    {
        text += "f 1/1 2/1 3/1\n";
    }
    return text;
}

void runCubeTests()
{
    ObjImportOptions options = crateOptions();
    options.normalPolicy = sed::assets::ObjNormalPolicy::Recalculate;
    options.origin = sed::assets::ObjOrigin::None;
    const ObjImportResult result = importOrFail(kCubeObj, options, "OBJ: the textured cube imports");
    const sed::assets::Model3do& model = result.model;
    check(model.version == 2.1 && model.geosetCount == 1 && !model.hierarchyTransformsApplied
            && model.materials == std::vector<std::string>{ "crate.mat" },
        "OBJ: the cube becomes a one-geoset 3DO 2.1 listing its MAT");
    check(model.meshes.size() == 1 && model.nodes.size() == 1 && result.meshes.size() == 1,
        "OBJ: one OBJ object is one mesh and one hierarchy node");
    if ( model.meshes.size() != 1 || model.nodes.size() != 1 )
    {
        return;
    }
    const sed::assets::Model3doMesh& mesh = model.meshes[0];
    const sed::assets::Model3doNode& node = model.nodes[0];
    check(mesh.name == "Crate" && node.name == "Crate" && node.type == 0u && node.flags == 0u
            && node.mesh == 0 && node.parent == -1,
        "OBJ: the node is a type-0 root named after the mesh");
    check(mesh.vertices.size() == 8 && mesh.faces.size() == 6 && mesh.texVertices.size() == 5
            && result.faces == 6 && result.vertices == 8 && result.textureVertices == 5
            && result.polygons == 6 && result.triangulatedPolygons == 0,
        "OBJ: planar convex quads are kept and texture vertices are shared");
    check(mesh.geo == 4 && mesh.light == 3 && mesh.tex == 1,
        "OBJ: mesh modes match stock JK props (textured, gouraud, perspective)");
    bool facesLikeStock = true;
    for ( const sed::assets::Model3doFace& face : mesh.faces )
    {
        facesLikeStock = facesLikeStock && face.material == 0 && face.faceFlags == 0u && face.geo == 4
            && face.light == 3 && face.tex == 1 && face.vertices.size() == 4 && face.texVertices.size() == 4
            && face.extraLight == sed::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
    }
    check(facesLikeStock, "OBJ: faces use stock prop modes, no flags and no extra light");
    bool verticesUnlit = true;
    for ( const sed::assets::Model3doVertex& vertex : mesh.vertices )
    {
        verticesUnlit = verticesUnlit && vertex.color == sed::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
    }
    check(verticesUnlit, "OBJ: vertex intensities are zero like stock props");
    // OBJ (x, y, z) -> JK (x, -z, y): OBJ vertex 3 (1, 1, 1) is JK (1, -1, 1)
    // and OBJ vertex 7 (1, 1, -1) is JK (1, 1, 1).
    check(mesh.vertices[2].x == 1.0 && mesh.vertices[2].y == -1.0 && mesh.vertices[2].z == 1.0
            && hasVertex(mesh, 1.0, 1.0, 1.0) && hasVertex(mesh, -1.0, 1.0, -1.0),
        "OBJ: y-up converts to JK z-up as x, -z, y");
    check(mesh.faces[0].vertices == std::vector<std::int32_t>{ 0, 1, 2, 3 }
            && mesh.faces[0].texVertices == std::vector<std::int32_t>{ 0, 1, 2, 3 },
        "OBJ: vertices and texture vertices are numbered in first-use order");
    check(mesh.texVertices[0].u == 0.0f && mesh.texVertices[0].v == 32.0f
            && mesh.texVertices[1].u == 64.0f && mesh.texVertices[1].v == 32.0f
            && mesh.texVertices[2].u == 64.0f && mesh.texVertices[2].v == 0.0f
            && mesh.texVertices[4].u == 16.0f && mesh.texVertices[4].v == 8.0f
            && mesh.faces[5].texVertices[3] == 4,
        "OBJ: texture coordinates become texels u*width, (1-v)*height");
    check(mesh.faceNormals.size() == 6 && closeVec(mesh.faceNormals[0], TVector{ 0.0, -1.0, 0.0 }, 1e-9)
            && closeVec(mesh.faceNormals[1], TVector{ 0.0, 1.0, 0.0 }, 1e-9)
            && closeVec(mesh.faceNormals[4], TVector{ 0.0, 0.0, 1.0 }, 1e-9)
            && closeVec(mesh.faceNormals[5], TVector{ 0.0, 0.0, -1.0 }, 1e-9),
        "OBJ: face normals are outward Newell normals in JK axes");
    const double third = 1.0 / std::sqrt(3.0);
    check(mesh.vertexNormals.size() == 8
            && closeVec(mesh.vertexNormals[2], TVector{ third, -third, third }, 1e-6),
        "OBJ: vertex normals are the area-weighted average of the faces at the vertex");
    check(close(mesh.radius, std::sqrt(3.0), 1e-6) && close(mesh.authoredRadius, std::sqrt(3.0), 1e-6)
            && mesh.authoredRadius >= std::sqrt(3.0) && close(model.radius, std::sqrt(3.0), 1e-6)
            && model.insertOffset.x == 0.0 && model.insertOffset.y == 0.0 && model.insertOffset.z == 0.0,
        "OBJ: mesh and model radius cover every vertex; insert offset is zero");
    check(result.materials.size() == 1 && result.materials[0].name == "crate"
            && result.materials[0].mat == "crate.mat" && result.materials[0].index == 0
            && result.materials[0].faces == 6 && result.unusedMaterials.empty(),
        "OBJ: the receipt lists the used material and its faces");
    check(ignoredCount(result, "s") == 1 && ignoredCount(result, "mtllib") == 1,
        "OBJ: smoothing groups and mtllib are reported as ignored");

    const sed::assets::Model3do reloaded = reloadRaw(result);
    check(reloaded.meshes.size() == 1 && reloaded.nodes.size() == 1
            && reloaded.materials == model.materials
            && reloaded.meshes[0].vertices.size() == 8 && reloaded.meshes[0].faces.size() == 6
            && reloaded.meshes[0].texVertices.size() == 5
            && reloaded.meshes[0].texVertices[4].u == 16.0f && reloaded.meshes[0].texVertices[4].v == 8.0f
            && reloaded.meshes[0].faces[5].vertices == mesh.faces[5].vertices
            && reloaded.meshes[0].vertexNormals.size() == 8 && reloaded.meshes[0].faceNormals.size() == 6
            && reloaded.meshes[0].faces[0].extraLight.r == 0.0f
            && reloaded.meshes[0].vertices[0].color.r == 0.0f,
        "OBJ: the written 3DO reads back with the same counts, texels and lighting");
}

void runPlacementTests()
{
    {
        const ObjImportResult result = importOrFail(kCubeObj, crateOptions(), "OBJ: default origin imports");
        check(result.boundsMin.x == -1.0 && result.boundsMin.y == -1.0 && result.boundsMin.z == 0.0
                && result.boundsMax.x == 1.0 && result.boundsMax.y == 1.0 && result.boundsMax.z == 2.0
                && close(result.model.radius, std::sqrt(6.0), 1e-6),
            "OBJ: origin base puts the bottom centre of the bounds at 0,0,0");
    }
    {
        ObjImportOptions options = crateOptions();
        options.origin = sed::assets::ObjOrigin::Center;
        options.scale = 0.125;
        const ObjImportResult result = importOrFail(kCubeObj, options, "OBJ: centred, scaled cube imports");
        check(result.boundsMin.x == -0.125 && result.boundsMin.z == -0.125 && result.boundsMax.z == 0.125,
            "OBJ: scale applies and origin center centres the bounds");
    }
    {
        ObjImportOptions options = crateOptions();
        options.upAxis = sed::assets::ObjUpAxis::Z;
        options.origin = sed::assets::ObjOrigin::None;
        const ObjImportResult result = importOrFail(
            "v 1 2 3\nv 2 2 3\nv 1 3 3\nvt 0 0\nusemtl crate\nf 1/1 2/1 3/1\n", options, "OBJ: z-up imports");
        check(result.model.meshes.size() == 1 && hasVertex(result.model.meshes[0], 1.0, 2.0, 3.0)
                && closeVec(result.model.meshes[0].faceNormals[0], TVector{ 0.0, 0.0, 1.0 }, 1e-9),
            "OBJ: upAxis z keeps coordinates");
    }
    {
        ObjImportOptions options = crateOptions();
        options.upAxis = sed::assets::ObjUpAxis::Z;
        options.origin = sed::assets::ObjOrigin::None;
        const ObjImportResult result = importOrFail(
            "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nusemtl crate\nf -4/-1 -3/-1 \\\n -2/-1 -1/-1\n",
            options, "OBJ: negative indices and a continued line import");
        check(result.model.meshes.size() == 1 && result.model.meshes[0].faces.size() == 1
                && result.model.meshes[0].faces[0].vertices.size() == 4,
            "OBJ: negative indices count back and a backslash continues the line");
    }
}

void runTriangulationTests()
{
    ObjImportOptions options = crateOptions();
    options.upAxis = sed::assets::ObjUpAxis::Z;
    options.origin = sed::assets::ObjOrigin::None;
    {
        const ObjImportResult result = importOrFail(
            "v 0 0 0\nv 1 0 0\nv 1 1 0.5\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nusemtl crate\n"
            "f 1/1 2/2 3/3 4/4\n",
            options, "OBJ: a non-planar quad imports");
        const auto& faces = result.model.meshes.empty()
            ? std::vector<sed::assets::Model3doFace>{} : result.model.meshes[0].faces;
        check(faces.size() == 2 && faces[0].vertices.size() == 3 && faces[1].vertices.size() == 3
                && result.triangulatedPolygons == 1 && result.polygons == 1,
            "OBJ: a quad that is not planar becomes two triangles");
        check(faces.size() == 2 && result.model.meshes[0].faceNormals[0].z > 0.5
                && result.model.meshes[0].faceNormals[1].z > 0.5,
            "OBJ: both triangles keep the quad's facing");
    }
    {
        const ObjImportResult result = importOrFail(
            "v 0 0 0\nv 2 0 0\nv 2 1 0\nv 1 1 0\nv 1 2 0\nv 0 2 0\nvt 0 0\nusemtl crate\n"
            "f 1/1 2/1 3/1 4/1 5/1 6/1\n",
            options, "OBJ: a non-convex hexagon imports");
        double area = 0.0;
        bool facingUp = true;
        if ( !result.model.meshes.empty() )
        {
            const sed::assets::Model3doMesh& mesh = result.model.meshes[0];
            for ( std::size_t f = 0; f < mesh.faces.size(); ++f )
            {
                area += polygonArea(mesh, mesh.faces[f]);
                facingUp = facingUp && mesh.faceNormals[f].z > 0.999;
            }
        }
        check(result.faces == 4 && result.triangulatedPolygons == 1 && close(area, 3.0, 1e-9) && facingUp,
            "OBJ: a planar L-shaped polygon is ear-clipped into four triangles covering its area");
    }
    {
        const ObjImportResult result = importOrFail(
            "v 0 0 0\nv 2 0 0\nv 3 1.5 0\nv 1 3 0\nv -1 1.5 0\nvt 0 0\nusemtl crate\nf 1/1 2/1 3/1 4/1 5/1\n",
            options, "OBJ: a convex pentagon imports");
        check(result.faces == 1 && result.triangulatedPolygons == 0
                && !result.model.meshes.empty() && result.model.meshes[0].faces[0].vertices.size() == 5,
            "OBJ: a planar convex pentagon is kept as one face");
    }
    {
        check(importRefuses("v 0 0 0\nv 1 0 0\nv 2 0 0\nvt 0 0\nusemtl crate\nf 1/1 2/1 3/1\n", options,
                  ObjImportErrorKind::Refused, "objDegeneratePolygon", "line 6"),
            "OBJ: a polygon without area is refused, naming its line");
        check(importRefuses("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nusemtl crate\nf 1/1 2/1 1/1\n", options,
                  ObjImportErrorKind::Refused, "objDegeneratePolygon"),
            "OBJ: a polygon that repeats a vertex is refused");
        // The last edge crosses the first, so no triangulation covers it.
        check(importRefuses(
                  "v 0 0 0\nv 3 0 0\nv 3 2 0\nv 1 2 0\nv 2 -1 0\nvt 0 0\nusemtl crate\nf 1/1 2/1 3/1 4/1 5/1\n",
                  options, ObjImportErrorKind::Refused, "objTriangulationFailed", "line 8"),
            "OBJ: a self-intersecting polygon is refused instead of guessed");
        check(importRefuses("v 0 0 0\nv 2 0 0\nv 0 2 0\nv 2 2 0\nvt 0 0\nusemtl crate\nf 1/1 2/1 3/1 4/1\n",
                  options, ObjImportErrorKind::Refused, "objDegeneratePolygon"),
            "OBJ: a symmetric bow-tie, whose halves cancel, has no area and is refused");
    }
}

void runMeshNamingTests()
{
    ObjImportOptions options = crateOptions();
    options.upAxis = sed::assets::ObjUpAxis::Z;
    const std::string tri = "vt 0 0\nusemtl crate\nf 1/1 2/1 3/1\n";
    const std::string verts = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
    {
        const ObjImportResult result = importOrFail(
            verts + tri + "o Body Panel\n" + tri + "g Lid\n" + tri + "o Other\ng Lid\n" + tri
                + "o " + std::string(40, 'x') + "\n" + tri,
            options, "OBJ: objects and groups import");
        std::vector<std::string> names{};
        for ( const auto& mesh : result.model.meshes )
        {
            names.push_back(mesh.name);
        }
        check(names == std::vector<std::string>{ "mesh0", "Body_Panel", "Lid", "Lid_2", std::string(31, 'x') },
            "OBJ: each object/group is a mesh with a sanitized, unique, engine-sized name");
        check(result.meshes.size() == 5 && result.meshes[1].source == "Body Panel"
                && result.meshes[2].source == "Lid" && result.meshes[0].source.empty(),
            "OBJ: the receipt keeps each mesh's OBJ source name");
        // The engine draws a model from node 0 down through child and sibling
        // links, so a second root would never be drawn: the first mesh's node
        // is the root and every other mesh's node is its child.
        bool singleTree = result.model.nodes.size() == 5;
        for ( std::size_t i = 0; singleTree && i < result.model.nodes.size(); ++i )
        {
            const sed::assets::Model3doNode& node = result.model.nodes[i];
            singleTree = node.mesh == static_cast<std::int32_t>(i) && node.name == names[i] && node.type == 0u
                && node.parent == (i == 0 ? -1 : 0);
        }
        check(singleTree, "OBJ: one node per mesh, all under the first mesh's node");
        const std::string text = sed::assets::saveTo3DO(result.model);
        check(text.find("HIERARCHY NODES 5\n0: 0x0 0x0 0 -1 1 -1 4 ") != std::string::npos
                && text.find("\n1: 0x0 0x0 1 0 -1 2 0 ") != std::string::npos
                && text.find("\n4: 0x0 0x0 4 0 -1 -1 0 ") != std::string::npos,
            "OBJ: the written hierarchy links every mesh node from node 0 (child and sibling)");
    }
    {
        options.meshNamePrefix = "relay_";
        const ObjImportResult result = importOrFail(verts + tri + "g cap\n" + tri, options,
            "OBJ: prefixed names import");
        check(result.model.meshes.size() == 2 && result.model.meshes[0].name == "relay_0"
                && result.model.meshes[1].name == "relay_cap",
            "OBJ: meshName is a prefix for every mesh name");
    }
    {
        options.meshNamePrefix.clear();
        check(importRefuses(verts + "vt 0 0\nusemtl crate\ng a b\nf 1/1 2/1 3/1\n", options,
                  ObjImportErrorKind::Refused, "objUnsupportedFeature", "line 6"),
            "OBJ: a face in several groups at once is refused");
    }
}

void runMaterialTests()
{
    ObjImportOptions options = crateOptions();
    options.upAxis = sed::assets::ObjUpAxis::Z;
    const std::string verts = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\n";
    check(importRefuses(verts + "usemtl ghost\nf 1/1 2/1 3/1\n", options, ObjImportErrorKind::Refused,
              "objMaterialUnmapped", "'ghost'"),
        "OBJ: a usemtl the request does not map is refused by name");
    check(importRefuses(verts + "f 1/1 2/1 3/1\n", options, ObjImportErrorKind::Refused, "objUntexturedFace",
              "line 5"),
        "OBJ: a face without usemtl is refused unless the request allows untextured faces");
    check(importRefuses(verts + "usemtl crate\nf 1 2 3\n", options, ObjImportErrorKind::Refused,
              "objMissingTextureCoordinates", "'crate'"),
        "OBJ: a textured face without texture coordinates is refused");
    check(importRefuses(verts + "usemtl crate\nf 1/1 2 3/1\n", options, ObjImportErrorKind::Malformed,
              "objMalformed"),
        "OBJ: a face mixing corners with and without texture coordinates is malformed");
    {
        ObjImportOptions untextured = options;
        untextured.allowUntextured = true;
        untextured.materials.push_back({ "spare", "spare.mat", 16, 16 });
        const ObjImportResult result = importOrFail(
            verts + "f 1 2 3\nusemtl crate\nf 1/1 2/1 3/1\n", untextured, "OBJ: allowed untextured faces import");
        const auto& mesh = result.model.meshes.empty() ? sed::assets::Model3doMesh{} : result.model.meshes[0];
        check(mesh.faces.size() == 2 && mesh.faces[0].material == -1 && mesh.faces[0].geo == 0
                && mesh.faces[1].material == 0 && mesh.faces[1].geo == 4 && result.untexturedFaces == 1
                && !mesh.texVertices.empty()
                && mesh.faces[0].texVertices.size() == 3,
            "OBJ: allowed untextured faces have no material and are not drawn");
        check(result.unusedMaterials == std::vector<std::string>{ "spare" }
                && result.model.materials == std::vector<std::string>{ "crate.mat" },
            "OBJ: unused request materials are reported and not listed by the 3DO");
    }
    {
        ObjImportOptions shared = options;
        shared.materials.push_back({ "crate_alt", "CRATE.MAT", 64, 32 });
        shared.materials.push_back({ "panel", "panel.mat", 128, 128 });
        const ObjImportResult result = importOrFail(
            verts + "usemtl panel\nf 1/1 2/1 3/1\nusemtl crate_alt\nf 1/1 2/1 3/1\nusemtl crate\nf 1/1 2/1 3/1\n",
            shared, "OBJ: shared MATs import");
        check(result.model.materials == std::vector<std::string>{ "crate.mat", "panel.mat" }
                && result.materials.size() == 3 && result.materials[1].index == 0
                && !result.model.meshes.empty() && result.model.meshes[0].faces[0].material == 1
                && result.model.meshes[0].faces[1].material == 0,
            "OBJ: materials naming the same MAT share one 3DO slot in request order");
    }
}

void runLimitTests()
{
    ObjImportOptions options = crateOptions();
    options.upAxis = sed::assets::ObjUpAxis::Z;
    options.split = false;
    check(importRefuses(triangleFan(513), options, ObjImportErrorKind::Refused, "objLimitExceeded",
              "reduce the model first"),
        "OBJ: with split off, more than 512 faces in a mesh is refused");
    check(importOrFail(triangleFan(512), options, "OBJ: 512 faces fit").faces == 512,
        "OBJ: exactly 512 faces in a mesh is accepted");
    {
        std::string text = "vt 0 0\nusemtl crate\n";
        for ( int i = 0; i < 171; ++i )
        {
            text += "v " + std::to_string(i) + " 0 0\nv " + std::to_string(i) + " 1 0\nv "
                + std::to_string(i) + ".5 1 1\nf -3/1 -2/1 -1/1\n";
        }
        check(importRefuses(text, options, ObjImportErrorKind::Refused, "objLimitExceeded", "513 vertices"),
            "OBJ: with split off, more than 512 vertices in a mesh is refused");
    }
    {
        std::string text = "v 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl crate\n";
        for ( int i = 0; i < 257; ++i )
        {
            text += "vt " + std::to_string(i * 3) + " 0\nvt " + std::to_string(i * 3 + 1) + " 0\nvt "
                + std::to_string(i * 3 + 2) + " 0\nf 1/-3 2/-2 3/-1\n";
        }
        check(importRefuses(text, options, ObjImportErrorKind::Refused, "objLimitExceeded",
                  "771 texture vertices"),
            "OBJ: with split off, more than 768 texture vertices in a mesh is refused");
    }
    {
        std::string text = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nusemtl crate\n";
        for ( int i = 0; i < 65; ++i )
        {
            text += "g part" + std::to_string(i) + "\nf 1/1 2/1 3/1\n";
        }
        check(importRefuses(text, options, ObjImportErrorKind::Refused, "objLimitExceeded", "65 meshes"),
            "OBJ: more than 64 meshes is refused");
    }
    {
        std::string text = "vt 0 0\nusemtl crate\n";
        std::string face = "f";
        for ( int i = 0; i < 25; ++i )
        {
            const double angle = 6.283185307179586 * i / 25.0;
            text += "v " + std::to_string(std::cos(angle)) + " " + std::to_string(std::sin(angle)) + " 0\n";
            face += " " + std::to_string(i + 1) + "/1";
        }
        check(importRefuses(text + face + "\n", options, ObjImportErrorKind::Refused, "objLimitExceeded",
                  "25 corners"),
            "OBJ: a polygon with more than 24 corners is refused");
    }
}

// A bowl-shaped grid of columns x rows cells, two triangles per cell, z-up,
// with one texture coordinate per vertex. Every coordinate is a float-exact
// value, so positions survive the engine rounding unchanged.
std::string bowlGrid(const int columns, const int rows, const std::string& group)
{
    std::string text = "g " + group + "\nusemtl crate\n";
    for ( int y = 0; y <= rows; ++y )
    {
        for ( int x = 0; x <= columns; ++x )
        {
            text += "v " + std::to_string(x) + " " + std::to_string(y) + " "
                + std::to_string(static_cast<double>(x * x + y * y) / 64.0) + "\n";
            text += "vt " + std::to_string(static_cast<double>(x) / 64.0) + " "
                + std::to_string(static_cast<double>(y) / 64.0) + "\n";
        }
    }
    const int base = -((columns + 1) * (rows + 1));
    const auto corner = [&](const int x, const int y)
    {
        const std::string index = std::to_string(base + y * (columns + 1) + x);
        return index + "/" + index;
    };
    for ( int y = 0; y < rows; ++y )
    {
        for ( int x = 0; x < columns; ++x )
        {
            text += "f " + corner(x, y) + " " + corner(x + 1, y) + " " + corner(x + 1, y + 1) + "\n";
            text += "f " + corner(x, y) + " " + corner(x + 1, y + 1) + " " + corner(x, y + 1) + "\n";
        }
    }
    return text;
}

using FaceKey = std::vector<std::array<double, 3>>;

// Every face of a model as its sorted corner positions, so faces can be
// compared whatever mesh or vertex numbering they ended up with.
std::vector<FaceKey> faceKeys(const sed::assets::Model3do& model)
{
    std::vector<FaceKey> keys{};
    for ( const auto& mesh : model.meshes )
    {
        for ( const auto& face : mesh.faces )
        {
            FaceKey key{};
            for ( const std::int32_t index : face.vertices )
            {
                const auto& v = mesh.vertices[static_cast<std::size_t>(index)];
                key.push_back({ v.x, v.y, v.z });
            }
            std::sort(key.begin(), key.end());
            keys.push_back(std::move(key));
        }
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::vector<FaceKey> bowlFaceKeys(const int columns, const int rows)
{
    const auto at = [](const int x, const int y)
    {
        return std::array<double, 3>{ static_cast<double>(x), static_cast<double>(y),
            static_cast<double>(x * x + y * y) / 64.0 };
    };
    std::vector<FaceKey> keys{};
    for ( int y = 0; y < rows; ++y )
    {
        for ( int x = 0; x < columns; ++x )
        {
            FaceKey a{ at(x, y), at(x + 1, y), at(x + 1, y + 1) };
            FaceKey b{ at(x, y), at(x + 1, y + 1), at(x, y + 1) };
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            keys.push_back(std::move(a));
            keys.push_back(std::move(b));
        }
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

bool meshConnected(const sed::assets::Model3doMesh& mesh)
{
    if ( mesh.faces.empty() )
    {
        return true;
    }
    std::vector<bool> reached(mesh.faces.size(), false);
    std::vector<bool> vertexReached(mesh.vertices.size(), false);
    std::vector<std::size_t> stack{ 0 };
    reached[0] = true;
    while ( !stack.empty() )
    {
        const std::size_t face = stack.back();
        stack.pop_back();
        for ( const std::int32_t v : mesh.faces[face].vertices )
        {
            vertexReached[static_cast<std::size_t>(v)] = true;
        }
        for ( std::size_t other = 0; other < mesh.faces.size(); ++other )
        {
            if ( reached[other] )
            {
                continue;
            }
            for ( const std::int32_t v : mesh.faces[other].vertices )
            {
                if ( vertexReached[static_cast<std::size_t>(v)] )
                {
                    reached[other] = true;
                    stack.push_back(other);
                    break;
                }
            }
        }
    }
    return std::all_of(reached.begin(), reached.end(), [](const bool r) { return r; });
}

bool withinJkLimits(const sed::assets::Model3do& model)
{
    return std::all_of(model.meshes.begin(), model.meshes.end(), [](const sed::assets::Model3doMesh& mesh) {
        return mesh.faces.size() <= 512 && mesh.vertices.size() <= 512 && mesh.texVertices.size() <= 768;
    });
}

void runSplitTests()
{
    ObjImportOptions options = crateOptions();
    options.upAxis = sed::assets::ObjUpAxis::Z;
    options.origin = sed::assets::ObjOrigin::None;
    {
        const ObjImportResult result = importOrFail(triangleFan(513), options, "OBJ split: 513 faces import");
        check(result.model.meshes.size() == 2 && result.meshes.size() == 2 && result.faces == 513
                && result.model.meshes[0].faces.size() == 512 && result.model.meshes[1].faces.size() == 1,
            "OBJ split: a mesh over 512 faces becomes a full mesh and the rest");
        check(result.meshes.size() == 2 && result.meshes[0].part == 1 && result.meshes[0].parts == 2
                && result.meshes[1].part == 2 && result.meshes[1].parts == 2
                && result.meshes[0].name == "mesh0_1" && result.meshes[1].name == "mesh0_2",
            "OBJ split: the receipt numbers the pieces of the split mesh");
        check(result.splits.size() == 1 && result.splits[0].faces == 513 && result.splits[0].vertices == 3
                && result.splits[0].textureVertices == 1
                && result.splits[0].meshes == std::vector<std::string>{ "mesh0_1", "mesh0_2" },
            "OBJ split: the receipt reports what the source mesh needed and what it became");
        check(result.model.nodes.size() == 2 && result.model.nodes[0].parent == -1
                && result.model.nodes[1].parent == 0 && result.model.nodes[1].mesh == 1,
            "OBJ split: every piece hangs under the single root node");
    }
    {
        // 30 x 20 cells: 1200 faces, 651 vertices and 651 texture vertices.
        const std::string grid = bowlGrid(30, 20, "bowl");
        const ObjImportResult result = importOrFail(grid, options, "OBJ split: a large grid imports");
        const sed::assets::Model3do& model = result.model;
        check(model.meshes.size() == 3 && result.faces == 1200 && withinJkLimits(model),
            "OBJ split: a mesh over the face and vertex limits becomes meshes that each fit");
        check(faceKeys(model) == bowlFaceKeys(30, 20),
            "OBJ split: no face is lost or duplicated");
        bool connected = true;
        bool facingUp = true;
        for ( const auto& mesh : model.meshes )
        {
            connected = connected && meshConnected(mesh);
            for ( const TVector& normal : mesh.faceNormals )
            {
                facingUp = facingUp && normal.z > 0.0;
            }
        }
        check(connected && facingUp, "OBJ split: each piece is one connected patch and keeps its winding");
        // A vertex on a seam is written once per piece; its normal must agree.
        std::map<std::array<double, 3>, TVector> normalAt{};
        bool seamsAgree = true;
        std::size_t shared = 0;
        for ( const auto& mesh : model.meshes )
        {
            for ( std::size_t v = 0; v < mesh.vertices.size(); ++v )
            {
                const std::array<double, 3> key{ mesh.vertices[v].x, mesh.vertices[v].y, mesh.vertices[v].z };
                const auto [found, inserted] = normalAt.emplace(key, mesh.vertexNormals[v]);
                if ( !inserted )
                {
                    ++shared;
                    seamsAgree = seamsAgree && found->second.x == mesh.vertexNormals[v].x
                        && found->second.y == mesh.vertexNormals[v].y && found->second.z == mesh.vertexNormals[v].z;
                }
            }
        }
        check(shared > 0 && seamsAgree, "OBJ split: vertices on a seam get the same normal in every piece");
        bool tree = model.nodes.size() == model.meshes.size();
        for ( std::size_t n = 0; tree && n < model.nodes.size(); ++n )
        {
            tree = model.nodes[n].mesh == static_cast<std::int32_t>(n)
                && model.nodes[n].parent == (n == 0 ? -1 : 0);
        }
        check(tree, "OBJ split: one node per piece, all under node 0");
        const ObjImportResult again = importOrFail(grid, options, "OBJ split: the grid imports again");
        check(sed::assets::saveTo3DO(again.model) == sed::assets::saveTo3DO(model),
            "OBJ split: splitting is deterministic");
        const sed::assets::Model3do reloaded = reloadRaw(result);
        check(reloaded.meshes.size() == 3 && reloaded.nodes.size() == 3 && faceKeys(reloaded) == faceKeys(model),
            "OBJ split: the split model reads back whole");
    }
    {
        // Two strips of 300 vertices each: together over the vertex limit,
        // each fits alone, so each stays one piece.
        std::string text = "usemtl crate\n";
        for ( int strip = 0; strip < 2; ++strip )
        {
            const int y = strip * 10;
            for ( int x = 0; x < 150; ++x )
            {
                text += "v " + std::to_string(x) + " " + std::to_string(y) + " 0\nvt " + std::to_string(x) + " 0\n";
                text += "v " + std::to_string(x) + " " + std::to_string(y + 1) + " 0\nvt " + std::to_string(x) + " 1\n";
            }
            const int base = strip * 300 + 1;
            for ( int x = 0; x < 149; ++x )
            {
                const auto b = [&](const int i) { const std::string s = std::to_string(base + 2 * i); return s + "/" + s; };
                const auto t = [&](const int i) { const std::string s = std::to_string(base + 2 * i + 1); return s + "/" + s; };
                text += "f " + b(x) + " " + b(x + 1) + " " + t(x) + "\n";
                text += "f " + b(x + 1) + " " + t(x + 1) + " " + t(x) + "\n";
            }
        }
        const ObjImportResult result = importOrFail(text, options, "OBJ split: two strips import");
        check(result.model.meshes.size() == 2 && result.model.meshes[0].faces.size() == 298
                && result.model.meshes[1].faces.size() == 298 && result.model.meshes[0].vertices.size() == 300
                && result.model.meshes[1].vertices.size() == 300,
            "OBJ split: a connected part that fits a mesh of its own is not cut in two");
    }
    {
        ObjImportOptions shared = options;
        std::string text = "v 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl crate\n";
        for ( int i = 0; i < 257; ++i )
        {
            text += "vt " + std::to_string(i * 3) + " 0\nvt " + std::to_string(i * 3 + 1) + " 0\nvt "
                + std::to_string(i * 3 + 2) + " 0\nf 1/-3 2/-2 3/-1\n";
        }
        const ObjImportResult result = importOrFail(text, shared, "OBJ split: 771 texture vertices import");
        check(result.model.meshes.size() == 2 && withinJkLimits(result.model) && result.faces == 257
                && result.textureVertices == 771,
            "OBJ split: a mesh over the texture-vertex limit is split");
    }
    {
        std::string text = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nusemtl crate\n";
        for ( int g = 0; g < 33; ++g )
        {
            text += "g part" + std::to_string(g) + "\n";
            for ( int i = 0; i < 513; ++i )
            {
                text += "f 1/1 2/1 3/1\n";
            }
        }
        check(importRefuses(text, options, ObjImportErrorKind::Refused, "objLimitExceeded", "66 meshes"),
            "OBJ split: a model that needs more than 64 meshes once split is refused");
    }
}

void runMalformedTests()
{
    ObjImportOptions options = crateOptions();
    const std::string face = "vt 0 0\nusemtl crate\nf 1/1 2/1 3/1\n";
    check(importRefuses("v 0 nan 0\nv 1 0 0\nv 0 1 0\n" + face, options, ObjImportErrorKind::Malformed,
              "objNonFinite", "line 1"),
        "OBJ: NaN is refused");
    check(importRefuses("v 0 0 0\nv 1e999 0 0\nv 0 1 0\n" + face, options, ObjImportErrorKind::Malformed,
              "objNonFinite", "line 2"),
        "OBJ: an overflowing number is refused");
    check(importRefuses("v 0 0 0\nv inf 0 0\nv 0 1 0\n" + face, options, ObjImportErrorKind::Malformed,
              "objNonFinite"),
        "OBJ: infinity is refused");
    check(importRefuses("v 0 0 0\nv 1 0 0\n" + face, options, ObjImportErrorKind::Malformed,
              "objIndexOutOfRange", "line 5"),
        "OBJ: a face naming a missing vertex is refused");
    check(importRefuses("v 0 0\n" + face, options, ObjImportErrorKind::Malformed, "objMalformed", "line 1"),
        "OBJ: a vertex with two coordinates is malformed");
    check(importRefuses("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nusemtl crate\nf 1/1 2/1\n", options,
              ObjImportErrorKind::Malformed, "objMalformed"),
        "OBJ: a face with two corners is malformed");
    check(importRefuses("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nusemtl crate\nf 1/x 2/1 3/1\n", options,
              ObjImportErrorKind::Malformed, "objMalformed"),
        "OBJ: a non-numeric index is malformed");
    check(importRefuses("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nusemtl crate\nf 0/1 2/1 3/1\n", options,
              ObjImportErrorKind::Malformed, "objMalformed"),
        "OBJ: index zero is malformed");
    check(importRefuses("v 0 0 0\nv 1 0 0\nv 0 1 0\ncurv 0 1 1 2\n" + face, options,
              ObjImportErrorKind::Refused, "objUnsupportedFeature", "curv"),
        "OBJ: free-form geometry is refused");
    check(importRefuses("v 0 0 0\nv 1 0 0\nv 0 1 0\nl 1 2\n" + face, options, ObjImportErrorKind::Refused,
              "objUnsupportedFeature", "line 4"),
        "OBJ: line elements are refused");
    check(importRefuses("v 0 0 0 1 0 0\nv 1 0 0\nv 0 1 0\n" + face, options, ObjImportErrorKind::Refused,
              "objUnsupportedFeature", "colour"),
        "OBJ: vertex colours are refused rather than dropped");
    check(importRefuses("v 0 0 0 2\nv 1 0 0\nv 0 1 0\n" + face, options, ObjImportErrorKind::Refused,
              "objUnsupportedFeature"),
        "OBJ: a rational vertex weight other than 1 is refused");
    check(importRefuses("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0 0.5\nusemtl crate\nf 1/1 2/1 3/1\n", options,
              ObjImportErrorKind::Refused, "objUnsupportedFeature"),
        "OBJ: a 3D texture coordinate is refused");
    check(importRefuses("v 0 0 0\nv 1 0 0\nv 0 1 0\n", options, ObjImportErrorKind::Refused, "objNoFaces"),
        "OBJ: a file without faces is refused");
    check(importRefuses(std::string("v 0 0 0\0\n", 10) + face, options, ObjImportErrorKind::Malformed,
              "objMalformed"),
        "OBJ: a NUL byte is malformed");
    check(importRefuses(std::string(sed::assets::kObjImportMaxLines + 1, '\n'), options,
              ObjImportErrorKind::Malformed, "objInputTooLarge"),
        "OBJ: more lines than the line bound are refused");
    check(importRefuses("# " + std::string(sed::assets::kObjImportMaxLineBytes, 'x') + "\n", options,
              ObjImportErrorKind::Malformed, "objInputTooLarge"),
        "OBJ: a line longer than the line bound is refused");
    check(importRefuses(std::string(sed::assets::kObjImportMaxSourceBytes + 1, ' '), options,
              ObjImportErrorKind::Malformed, "objInputTooLarge"),
        "OBJ: a source larger than 16 MiB is refused");
    {
        ObjImportOptions scaled = crateOptions();
        scaled.scale = 1e6;
        check(importRefuses("v 0 0 0\nv 2 0 0\nv 0 2 0\n" + face, scaled, ObjImportErrorKind::Refused,
                  "objCoordinateOutOfRange"),
            "OBJ: coordinates beyond the JK range after scaling are refused");
    }
    {
        const ObjImportResult result = importOrFail(
            "v 0 0 0\nv 1 0 0\nv 0 1 0\nfoo bar\nfoo baz\n" + face, options, "OBJ: unknown statements import");
        check(ignoredCount(result, "foo") == 2, "OBJ: unknown statements are ignored and counted");
    }
}

void runNormalPreservationTests()
{
    ObjImportOptions options = crateOptions();
    options.origin = sed::assets::ObjOrigin::None;
    options.upAxis = sed::assets::ObjUpAxis::Z;
    // Synthetic perpendicular triangles share an edge. Each authored normal
    // deliberately has a non-unit length to exercise normalization.
    const std::string hinge = "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nvt 0 0\nusemtl crate\n";
    const ObjImportResult authored = importOrFail(hinge
        + "vn 0 0 5\nvn 0 7 0\nf 1/1/1 2/1/1 3/1/1\nf 1/1/2 4/1/2 2/1/2\n",
        options, "OBJ normals: authored hard edges import");
    check(authored.model.meshes.size() == 1 && authored.model.meshes[0].vertices.size() == 6,
        "OBJ normals: different authored corner normals split shared geometric vertices");
    if ( authored.model.meshes.size() == 1 )
    {
        const auto& mesh = authored.model.meshes[0];
        bool normalsMatch = mesh.vertexNormals.size() == mesh.vertices.size();
        for ( std::size_t f = 0; f < mesh.faces.size() && normalsMatch; ++f )
        {
            const TVector expected = f == 0 ? TVector{ 0, 0, 1 } : TVector{ 0, 1, 0 };
            for ( const std::int32_t v : mesh.faces[f].vertices )
            {
                normalsMatch = normalsMatch && closeVec(mesh.vertexNormals[static_cast<std::size_t>(v)], expected, 1e-6);
            }
        }
        check(normalsMatch, "OBJ normals: authored directions survive instead of being averaged across a hard edge");
        const auto reloaded = reloadRaw(authored);
        check(reloaded.meshes.size() == 1 && reloaded.meshes[0].vertexNormals == mesh.vertexNormals
                && reloaded.meshes[0].vertices.size() == 6,
            "OBJ normals: hard-edge vertex normals survive 3DO export and readback");
    }
    for ( const std::string& groups : { std::string("s off\n"), std::string("s 0\n"), std::string("s 1\n") } )
    {
        const bool differentGroups = groups == "s 1\n";
        const auto result = importOrFail(hinge + groups + "f 1/1 2/1 3/1\n"
                + (differentGroups ? "s 2\n" : "") + "f 1/1 4/1 2/1\n", options,
            "OBJ normals: explicit smoothing boundaries import");
        check(result.model.meshes.size() == 1 && result.model.meshes[0].vertices.size() == 6,
            "OBJ normals: s off and different smoothing groups preserve the hard edge");
        check(ignoredCount(result, "s") == 0, "OBJ normals: honored smoothing statements are not reported as ignored");
    }
    for ( const std::string& group : { std::string{}, std::string("s on\n"), std::string("s 3\n") } )
    {
        const auto result = importOrFail(hinge + group + "f 1/1 2/1 3/1\nf 1/1 4/1 2/1\n", options,
            "OBJ normals: a smooth hinge imports");
        check(result.model.meshes.size() == 1 && result.model.meshes[0].vertices.size() == 4,
            "OBJ normals: absent hints and one smoothing group retain shared vertices");
    }
    check(authored.normalSplitVertices == 2, "OBJ normals: the receipt counts additional hard-edge vertices");
    {
        ObjImportOptions recalculate = options;
        recalculate.normalPolicy = sed::assets::ObjNormalPolicy::Recalculate;
        const auto result = importOrFail(hinge
                + "s off\nvn 1 0 0\nf 1/1/1 2/1/1 3/1/1\nf 1/1/1 4/1/1 2/1/1\n",
            recalculate, "OBJ normals: explicit geometric recalculation imports");
        check(result.model.meshes.size() == 1 && result.model.meshes[0].vertices.size() == 4
                && result.normalSplitVertices == 0 && ignoredCount(result, "s") == 1
                && closeVec(result.model.meshes[0].vertexNormals[0], TVector{ 0, 1 / std::sqrt(2.0), 1 / std::sqrt(2.0) }, 1e-6),
            "OBJ normals: recalculate ignores authored normals and flat hints and averages face area");
    }
    {
        const std::string square = "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\n"
            "vt 0 0\nvt 0.5 0.5\nusemtl crate\n"
            "vn 0 0 1\nvn 0 0 2\nf 1/1/1 2/1/1 3/1/1\nf 2/2/2 4/2/2 3/2/2\n";
        const auto result = importOrFail(square, options, "OBJ normals: equal normals with a UV seam import");
        check(result.model.meshes.size() == 1 && result.model.meshes[0].vertices.size() == 4
                && result.model.meshes[0].texVertices.size() == 2 && result.normalSplitVertices == 0,
            "OBJ normals: a UV seam and duplicate equal-normal records do not split geometric vertices");
    }
    for ( const char* normal : { "vn 0 0 0\n", "vn 0 0 1e308\n", "vn 0 0 1e-308\n" } )
    {
        const auto result = importOrFail(hinge + normal + "f 1/1/-1 2/1/-1 3/1/-1\n", options,
            "OBJ normals: zero and extreme finite normals import");
        check(result.model.meshes.size() == 1 && result.model.meshes[0].vertexNormals.size() == 3
                && closeVec(result.model.meshes[0].vertexNormals[0], TVector{ 0, 0, 1 }, 1e-6),
            "OBJ normals: zero falls back to geometry and extreme finite magnitudes preserve direction");
    }
    for ( const char* smoothing : { "s -1\n", "s invalid\n", "s 1 2\n", "s 1.5\n", "s 9223372036854775808\n" } )
    {
        check(importRefuses(hinge + smoothing + "f 1/1 2/1 3/1\n", options, ObjImportErrorKind::Malformed,
                  "objMalformed"), "OBJ normals: malformed smoothing records refuse rather than guessing");
    }
    {
        std::string text = hinge + "s off\n";
        for ( std::size_t i = 0; i < 200; ++i )
        {
            text += "f 1/1 2/1 3/1\n";
        }
        ObjImportOptions noSplit = options;
        noSplit.split = false;
        check(importRefuses(text, noSplit, ObjImportErrorKind::Refused, "objLimitExceeded", "600 vertices"),
            "OBJ normals: mesh limits count the vertices introduced by flat shading");
        const auto split = importOrFail(text, options, "OBJ normals: hard-edge vertices partition to engine limits");
        bool withinLimits = split.model.meshes.size() == 2 && split.normalSplitVertices == 597;
        for ( const auto& mesh : split.model.meshes )
        {
            withinLimits = withinLimits && mesh.vertices.size() <= sed::assets::kJkMeshMaxVertices;
            for ( const auto& normal : mesh.vertexNormals )
            {
                withinLimits = withinLimits && closeVec(normal, TVector{ 0, 0, 1 }, 1e-6);
            }
        }
        check(withinLimits, "OBJ normals: partitioning respects vertex limits without losing hard-edge normals");
    }
}

void runOptionTests()
{
    const std::string text = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nusemtl crate\nf 1/1 2/1 3/1\n";
    const auto refusesOptions = [&text](const std::function<void(ObjImportOptions&)>& change)
    {
        ObjImportOptions options = crateOptions();
        change(options);
        return importRefuses(text, options, ObjImportErrorKind::InvalidOptions, "objInvalidOptions");
    };
    check(refusesOptions([](ObjImportOptions& o) { o.scale = 0.0; }), "OBJ: scale 0 is refused");
    check(refusesOptions([](ObjImportOptions& o) { o.scale = -1.0; }), "OBJ: a negative (mirroring) scale is refused");
    check(refusesOptions([](ObjImportOptions& o) { o.scale = std::nan(""); }), "OBJ: a NaN scale is refused");
    check(refusesOptions([](ObjImportOptions& o) { o.normalPolicy = static_cast<sed::assets::ObjNormalPolicy>(99); }),
        "OBJ: an invalid normal policy is refused");
    check(refusesOptions([](ObjImportOptions& o) { o.materials[0].mat = "two words.mat"; }),
        "OBJ: a MAT name with a space is refused");
    check(refusesOptions([](ObjImportOptions& o) { o.materials[0].mat = "mat/crate.mat"; }),
        "OBJ: a MAT path is refused");
    check(refusesOptions([](ObjImportOptions& o) { o.materials[0].mat = "crate.png"; }),
        "OBJ: a MAT name without .mat is refused");
    check(refusesOptions([](ObjImportOptions& o) { o.materials[0].mat = std::string(28, 'a') + ".mat"; }),
        "OBJ: a MAT name longer than the engine's 31 characters is refused");
    check(refusesOptions([](ObjImportOptions& o) { o.materials[0].width = 0; }), "OBJ: a zero width is refused");
    check(refusesOptions([](ObjImportOptions& o) { o.materials.push_back({ "crate", "other.mat", 8, 8 }); }),
        "OBJ: a usemtl name mapped twice is refused");
    check(refusesOptions([](ObjImportOptions& o) { o.materials.push_back({ "b", "Crate.mat", 8, 8 }); }),
        "OBJ: one MAT given two sizes is refused");
    check(refusesOptions([](ObjImportOptions& o) { o.meshNamePrefix = "has space"; }),
        "OBJ: a mesh name prefix the 3DO cannot carry is refused");
    check(refusesOptions([](ObjImportOptions& o) { o.meshNamePrefix = std::string(25, 'p'); }),
        "OBJ: a mesh name prefix longer than 24 characters is refused");
}

} // namespace

void runObjImportTests()
{
    runCubeTests();
    runPlacementTests();
    runTriangulationTests();
    runMeshNamingTests();
    runMaterialTests();
    runLimitTests();
    runSplitTests();
    runMalformedTests();
    runNormalPreservationTests();
    runOptionTests();
}

} // namespace sed::tests
