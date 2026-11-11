#include "test_common.hpp"

#include "sed/assets/3do.hpp"
#include "sed/assets/obj_import.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
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

void put32(std::string& out, const std::uint32_t value)
{
    for ( int shift = 0; shift < 32; shift += 8 )
    {
        out.push_back(static_cast<char>((value >> shift) & 0xFFu));
    }
}

std::string floatBytes(const std::vector<float>& values)
{
    std::string out{};
    for ( const float value : values )
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        put32(out, bits);
    }
    return out;
}

std::string indexBytes(const std::vector<std::uint32_t>& values, const std::uint32_t type)
{
    std::string out{};
    for ( const std::uint32_t value : values )
    {
        out.push_back(static_cast<char>(value & 0xFFu));
        if ( type != 5121 )
        {
            out.push_back(static_cast<char>((value >> 8u) & 0xFFu));
        }
        if ( type == 5125 )
        {
            out.push_back(static_cast<char>((value >> 16u) & 0xFFu));
            out.push_back(static_cast<char>((value >> 24u) & 0xFFu));
        }
    }
    return out;
}

std::string base64(const std::string& bytes)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out{};
    std::size_t i = 0;
    for ( ; i + 2 < bytes.size(); i += 3 )
    {
        const std::uint32_t n = (static_cast<std::uint8_t>(bytes[i]) << 16u)
            | (static_cast<std::uint8_t>(bytes[i + 1]) << 8u) | static_cast<std::uint8_t>(bytes[i + 2]);
        out += kAlphabet[(n >> 18u) & 63u];
        out += kAlphabet[(n >> 12u) & 63u];
        out += kAlphabet[(n >> 6u) & 63u];
        out += kAlphabet[n & 63u];
    }
    if ( i + 1 == bytes.size() )
    {
        const std::uint32_t n = static_cast<std::uint8_t>(bytes[i]) << 16u;
        out += kAlphabet[(n >> 18u) & 63u];
        out += kAlphabet[(n >> 12u) & 63u];
        out += "==";
    }
    else if ( i + 2 == bytes.size() )
    {
        const std::uint32_t n = (static_cast<std::uint8_t>(bytes[i]) << 16u) | (static_cast<std::uint8_t>(bytes[i + 1]) << 8u);
        out += kAlphabet[(n >> 18u) & 63u];
        out += kAlphabet[(n >> 12u) & 63u];
        out += kAlphabet[(n >> 6u) & 63u];
        out += "=";
    }
    return out;
}

// A GLB file: the JSON chunk padded with spaces, the BIN chunk (when given)
// padded with zeros.
std::string glbContainer(std::string json, const std::string& bin, const std::uint32_t version = 2)
{
    while ( json.size() % 4 != 0 )
    {
        json.push_back(' ');
    }
    std::string paddedBin = bin;
    while ( paddedBin.size() % 4 != 0 )
    {
        paddedBin.push_back('\0');
    }
    std::string out = "glTF";
    put32(out, version);
    put32(out, static_cast<std::uint32_t>(12 + 8 + json.size() + (bin.empty() ? 0 : 8 + paddedBin.size())));
    put32(out, static_cast<std::uint32_t>(json.size()));
    out += "JSON";
    out += json;
    if ( !bin.empty() )
    {
        put32(out, static_cast<std::uint32_t>(paddedBin.size()));
        out += std::string("BIN\0", 4);
        out += paddedBin;
    }
    return out;
}

struct Primitive
{
    std::vector<float> positions{};
    std::vector<float> normals{};
    std::vector<float> uvs{};
    std::vector<std::uint32_t> indices{};
    std::uint32_t indexType{ 5123 };
    int mode{ 4 };
    std::string material{ "0" };    // the primitive's material member, empty for none
    std::string positionExtra{};    // more members of the POSITION accessor
    std::string positionViewExtra{}; // more members of the POSITION buffer view
    std::uint32_t positionComponentType{ 5126 }; // as declared; the data stays float
    std::string uvExtra{};          // more members of the TEXCOORD_0 accessor
    std::uint32_t uvComponentType{ 5126 };
    std::string extra{};            // more members of the primitive
};

struct Gltf
{
    std::vector<Primitive> primitives{};
    std::string nodes{ R"([{"name":"crate","mesh":0}])" };
    std::string scenes{ R"("scene":0,"scenes":[{"nodes":[0]}],)" };
    std::string materials{ R"([{"name":"crate"}])" };
    std::string extra{};      // more root members, each followed by ','
    std::string images{};     // extra raw bytes appended to the buffer for images, as a view
    bool bufferAsDataUri{};   // the buffer as a base64 data: URI instead of the BIN chunk
    std::string bufferUri{};  // an explicit buffer uri
};

// A glTF with one mesh of the given primitives. When `images` is set its
// bytes get the last buffer view, whose index is returned through imageView.
std::string buildGlb(const Gltf& gltf, std::size_t* imageView = nullptr)
{
    std::string bin{};
    std::string views{};
    std::string accessors{};
    std::size_t viewCount = 0;
    std::size_t accessorCount = 0;
    const auto addView = [&](const std::string& bytes, const std::string& extra)
    {
        while ( bin.size() % 4 != 0 )
        {
            bin.push_back('\0');
        }
        views += std::string(views.empty() ? "" : ",") + "{\"buffer\":0,\"byteOffset\":" + std::to_string(bin.size())
            + ",\"byteLength\":" + std::to_string(bytes.size()) + extra + "}";
        bin += bytes;
        return viewCount++;
    };
    const auto addAccessor = [&](const std::size_t view, const std::uint32_t componentType, const std::size_t count,
                                 const char* type, const std::string& extra)
    {
        accessors += std::string(accessors.empty() ? "" : ",") + "{\"bufferView\":" + std::to_string(view)
            + ",\"componentType\":" + std::to_string(componentType) + ",\"count\":" + std::to_string(count)
            + ",\"type\":\"" + type + "\"" + extra + "}";
        return accessorCount++;
    };
    std::string primitives{};
    for ( const Primitive& p : gltf.primitives )
    {
        std::string attributes = "\"POSITION\":"
            + std::to_string(addAccessor(addView(floatBytes(p.positions), p.positionViewExtra),
                p.positionComponentType, p.positions.size() / 3, "VEC3", p.positionExtra));
        if ( !p.normals.empty() )
        {
            attributes += ",\"NORMAL\":"
                + std::to_string(addAccessor(addView(floatBytes(p.normals), ""), 5126, p.normals.size() / 3, "VEC3", ""));
        }
        if ( !p.uvs.empty() )
        {
            attributes += ",\"TEXCOORD_0\":"
                + std::to_string(addAccessor(addView(floatBytes(p.uvs), ""), p.uvComponentType, p.uvs.size() / 2,
                    "VEC2", p.uvExtra));
        }
        std::string primitive = "{\"attributes\":{" + attributes + "}";
        if ( !p.indices.empty() )
        {
            primitive += ",\"indices\":"
                + std::to_string(addAccessor(addView(indexBytes(p.indices, p.indexType), ""), p.indexType,
                    p.indices.size(), "SCALAR", ""));
        }
        if ( !p.material.empty() )
        {
            primitive += ",\"material\":" + p.material;
        }
        primitive += ",\"mode\":" + std::to_string(p.mode) + p.extra + "}";
        primitives += (primitives.empty() ? "" : ",") + primitive;
    }
    if ( !gltf.images.empty() )
    {
        const std::size_t view = addView(gltf.images, "");
        if ( imageView )
        {
            *imageView = view;
        }
    }
    std::string buffer = "{\"byteLength\":" + std::to_string(bin.size());
    if ( !gltf.bufferUri.empty() )
    {
        buffer += ",\"uri\":\"" + gltf.bufferUri + "\"";
    }
    else if ( gltf.bufferAsDataUri )
    {
        buffer += ",\"uri\":\"data:application/octet-stream;base64," + base64(bin) + "\"";
    }
    buffer += "}";
    const std::string json = "{\"asset\":{\"version\":\"2.0\"}," + gltf.extra + gltf.scenes + "\"nodes\":" + gltf.nodes
        + ",\"meshes\":[{\"name\":\"box\",\"primitives\":[" + primitives + "]}],\"materials\":" + gltf.materials
        + ",\"buffers\":[" + buffer + "],\"bufferViews\":[" + views + "],\"accessors\":[" + accessors + "]}";
    return glbContainer(json, gltf.bufferAsDataUri || !gltf.bufferUri.empty() ? std::string{} : bin);
}

// A 2x2x2 cube centred on the origin, glTF y-up: 24 vertices (each face has
// its own four, with the face's normal and the full texture), 12 triangles
// wound counter-clockwise seen from outside.
Primitive cube()
{
    Primitive p{};
    for ( int axis = 0; axis < 3; ++axis )
    {
        for ( const int sign : { 1, -1 } )
        {
            std::array<float, 3> centre{};
            centre[static_cast<std::size_t>(axis)] = static_cast<float>(sign);
            std::array<float, 3> u{};
            std::array<float, 3> v{};
            u[static_cast<std::size_t>((axis + 1) % 3)] = 1.0f;
            v[static_cast<std::size_t>((axis + 2) % 3)] = 1.0f;
            if ( sign < 0 )
            {
                std::swap(u, v);
            }
            const std::uint32_t base = static_cast<std::uint32_t>(p.positions.size() / 3);
            const std::array<std::array<float, 2>, 4> corners{ { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } } };
            for ( const auto& corner : corners )
            {
                for ( std::size_t k = 0; k < 3; ++k )
                {
                    p.positions.push_back(centre[k] + corner[0] * u[k] + corner[1] * v[k]);
                    p.normals.push_back(centre[k]);
                }
                p.uvs.push_back((corner[0] + 1.0f) / 2.0f);
                p.uvs.push_back((corner[1] + 1.0f) / 2.0f);
            }
            for ( const std::uint32_t index : { 0u, 1u, 2u, 0u, 2u, 3u } )
            {
                p.indices.push_back(base + index);
            }
        }
    }
    p.uvs[0] = 0.25f; // vertex 0 (on the +x face) maps to texel (16, 24) of 64x32
    p.uvs[1] = 0.75f;
    return p;
}

// One triangle in the glTF ground plane (y = 0), facing up (+y).
Primitive groundTriangle()
{
    Primitive p{};
    p.positions = { 0, 0, 0, 1, 0, 0, 0, 0, -1 };
    p.uvs = { 0, 0, 1, 0, 0, 1 };
    p.indices = { 0, 1, 2 };
    return p;
}

ObjImportOptions crateOptions()
{
    ObjImportOptions options{};
    options.materials.push_back({ "crate", "crate.mat", 64, 32 });
    options.origin = sed::assets::ObjOrigin::None;
    return options;
}

std::span<const std::byte> bytesOf(const std::string& s)
{
    return std::as_bytes(std::span<const char>(s.data(), s.size()));
}

ObjImportResult importOrFail(const std::string& glb, const ObjImportOptions& options, const char* label)
{
    try
    {
        return sed::assets::importGlbModel(bytesOf(glb), options);
    }
    catch ( const std::exception& error )
    {
        check(false, label);
        std::cerr << "  unexpected exception: " << error.what() << "\n";
    }
    return {};
}

bool refuses(
    const std::string& glb,
    const ObjImportOptions& options,
    const ObjImportErrorKind kind,
    const std::string_view code,
    const std::string_view needle = {})
{
    try
    {
        (void)sed::assets::importGlbModel(bytesOf(glb), options);
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
    std::cerr << "  the import did not throw\n";
    return false;
}

bool hasVertexNear(const sed::assets::Model3doMesh& mesh, const double x, const double y, const double z)
{
    return std::any_of(mesh.vertices.begin(), mesh.vertices.end(), [&](const sed::assets::Model3doVertex& v) {
        return std::fabs(v.x - x) < 1e-6 && std::fabs(v.y - y) < 1e-6 && std::fabs(v.z - z) < 1e-6;
    });
}

bool hasTexel(const sed::assets::Model3doMesh& mesh, const float u, const float v)
{
    return std::any_of(mesh.texVertices.begin(), mesh.texVertices.end(),
        [&](const sed::assets::Model3doTexVertex& t) { return t.u == u && t.v == v; });
}

// Every face's normal points away from the origin (for shapes centred on it).
bool facesPointOut(const sed::assets::Model3doMesh& mesh)
{
    for ( std::size_t f = 0; f < mesh.faces.size(); ++f )
    {
        TVector centre{};
        for ( const std::int32_t index : mesh.faces[f].vertices )
        {
            const auto& v = mesh.vertices[static_cast<std::size_t>(index)];
            centre.x += v.x;
            centre.y += v.y;
            centre.z += v.z;
        }
        const TVector& n = mesh.faceNormals[f];
        if ( n.x * centre.x + n.y * centre.y + n.z * centre.z <= 0.0 )
        {
            return false;
        }
    }
    return true;
}

void runCubeTests()
{
    Gltf gltf{};
    gltf.primitives.push_back(cube());
    const std::string glb = buildGlb(gltf);
    const ObjImportResult result = importOrFail(glb, crateOptions(), "glTF: the cube imports");
    check(result.format == sed::assets::ModelSourceFormat::Glb && result.model.meshes.size() == 1
            && result.model.nodes.size() == 1 && result.model.materials == std::vector<std::string>{ "crate.mat" },
        "glTF: one node with a mesh becomes one 3DO mesh listing the mapped MAT");
    if ( result.model.meshes.size() != 1 )
    {
        return;
    }
    const sed::assets::Model3doMesh& mesh = result.model.meshes[0];
    check(mesh.vertices.size() == 8 && result.gltf.sourceVertices == 24 && result.gltf.uniquePositions == 8,
        "glTF: vertices that differ only in normal or texture coordinate share one 3DO vertex");
    check(mesh.faces.size() == 12 && result.gltf.sourceTriangles == 12 && result.gltf.primitives == 1
            && result.gltf.nodes == 1 && result.degenerateTriangles == 0,
        "glTF: every triangle becomes one JK face");
    check(mesh.name == "crate" && result.meshes[0].source == "crate" && result.model.nodes[0].parent == -1,
        "glTF: the mesh is named after its node");
    // glTF (x, y, z) -> JK (x, -z, y): glTF (1, 1, 1) is JK (1, -1, 1).
    check(hasVertexNear(mesh, 1, -1, 1) && hasVertexNear(mesh, -1, 1, -1) && hasVertexNear(mesh, 1, 1, -1),
        "glTF: y-up converts to JK z-up as x, -z, y");
    check(facesPointOut(mesh), "glTF: counter-clockwise triangles keep their outward facing");
    check(hasTexel(mesh, 16.0f, 24.0f) && hasTexel(mesh, 64.0f, 32.0f) && hasTexel(mesh, 0.0f, 0.0f)
            && !hasTexel(mesh, 16.0f, 8.0f),
        "glTF: texture coordinates become texels u*width, v*height (glTF v runs down the image)");
    const double third = 1.0 / std::sqrt(3.0);
    bool cornerNormals = mesh.vertexNormals.size() == 8;
    for ( std::size_t v = 0; cornerNormals && v < 8; ++v )
    {
        const auto& p = mesh.vertices[v];
        const TVector& n = mesh.vertexNormals[v];
        cornerNormals = std::fabs(n.x - p.x * third) < 1e-6 && std::fabs(n.y - p.y * third) < 1e-6
            && std::fabs(n.z - p.z * third) < 1e-6;
    }
    check(cornerNormals, "glTF: a corner's normal averages the authored normals merged there");
    check(result.materials.size() == 1 && result.materials[0].faces == 12,
        "glTF: the receipt counts the faces of each mapped material");

    for ( const std::uint32_t type : { 5121u, 5125u } )
    {
        Gltf other = gltf;
        other.primitives[0].indexType = type;
        const ObjImportResult same = importOrFail(buildGlb(other), crateOptions(), "glTF: other index types import");
        check(sed::assets::saveTo3DO(same.model) == sed::assets::saveTo3DO(result.model),
            "glTF: 8- and 32-bit indices read like 16-bit ones");
    }
    {
        Gltf flat{};
        Primitive expanded{};
        const Primitive source = cube();
        for ( const std::uint32_t index : source.indices )
        {
            for ( std::size_t k = 0; k < 3; ++k )
            {
                expanded.positions.push_back(source.positions[index * 3 + k]);
                expanded.normals.push_back(source.normals[index * 3 + k]);
            }
            expanded.uvs.push_back(source.uvs[index * 2]);
            expanded.uvs.push_back(source.uvs[index * 2 + 1]);
        }
        flat.primitives.push_back(expanded);
        const ObjImportResult same = importOrFail(buildGlb(flat), crateOptions(), "glTF: a non-indexed cube imports");
        check(sed::assets::saveTo3DO(same.model) == sed::assets::saveTo3DO(result.model)
                && same.gltf.sourceVertices == 36 && same.gltf.uniquePositions == 8,
            "glTF: a primitive without indices reads its vertices in order");
    }
    {
        const ObjImportResult viaDispatch = sed::assets::importModel(glb, crateOptions());
        check(sed::assets::isGlbModel(bytesOf(glb)) && viaDispatch.format == sed::assets::ModelSourceFormat::Glb
                && sed::assets::saveTo3DO(viaDispatch.model) == sed::assets::saveTo3DO(result.model),
            "glTF: importModel recognises a GLB by its magic");
        check(!sed::assets::isGlbModel(bytesOf(std::string("v 0 0 0\n"))),
            "glTF: OBJ text is not taken for a GLB");
    }
    {
        Gltf mirrored = gltf;
        mirrored.nodes = R"([{"name":"crate","mesh":0,"scale":[-1,1,1]}])";
        const ObjImportResult flipped = importOrFail(buildGlb(mirrored), crateOptions(), "glTF: a mirrored cube imports");
        check(flipped.gltf.mirroredNodes == 1 && !flipped.model.meshes.empty() && facesPointOut(flipped.model.meshes[0]),
            "glTF: a mirroring node reverses the winding so faces still point out");
    }
    {
        Gltf scaled = gltf;
        scaled.nodes = R"([{"name":"crate","mesh":0,"scale":[0,1,1]}])";
        check(refuses(buildGlb(scaled), crateOptions(), ObjImportErrorKind::Refused, "gltfUnsupportedFeature",
                  "scales its mesh to nothing"),
            "glTF: a node that scales its mesh flat is refused");
    }
}

void runTransformTests()
{
    const ObjImportOptions options = crateOptions();
    {
        // translation (1, 2, 3), 90 degrees about +y, scale 2: glTF (1, 0, 0)
        // goes to (2, 0, 0), then (0, 0, -2), then (1, 2, 1): JK (1, -1, 2).
        Gltf trs{};
        trs.primitives.push_back(groundTriangle());
        trs.nodes = R"([{"name":"tri","mesh":0,"translation":[1,2,3],"rotation":[0,0.7071067811865476,0,0.7071067811865476],"scale":[2,2,2]}])";
        const ObjImportResult result = importOrFail(buildGlb(trs), options, "glTF: a TRS node imports");
        check(!result.model.meshes.empty() && hasVertexNear(result.model.meshes[0], 1, -3, 2)
                && hasVertexNear(result.model.meshes[0], 1, -1, 2) && hasVertexNear(result.model.meshes[0], -1, -3, 2),
            "glTF: translation, rotation and scale apply in TRS order");
        Gltf matrix{};
        matrix.primitives.push_back(groundTriangle());
        matrix.nodes = R"([{"name":"tri","mesh":0,"matrix":[0,0,-2,0, 0,2,0,0, 2,0,0,0, 1,2,3,1]}])";
        const ObjImportResult same = importOrFail(buildGlb(matrix), options, "glTF: a matrix node imports");
        check(!same.model.meshes.empty() && hasVertexNear(same.model.meshes[0], 1, -3, 2)
                && hasVertexNear(same.model.meshes[0], 1, -1, 2) && hasVertexNear(same.model.meshes[0], -1, -3, 2),
            "glTF: a column-major node matrix gives the same placement");
        Gltf both = matrix;
        both.nodes = R"([{"name":"tri","mesh":0,"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"scale":[2,2,2]}])";
        check(refuses(buildGlb(both), options, ObjImportErrorKind::Malformed, "gltfMalformed", "both a matrix"),
            "glTF: a node with both a matrix and TRS is malformed");
    }
    {
        Gltf nested{};
        nested.primitives.push_back(groundTriangle());
        nested.nodes = R"([{"name":"base","translation":[10,0,0],"children":[1]},{"name":"tri","mesh":0,"translation":[0,5,0]}])";
        const ObjImportResult result = importOrFail(buildGlb(nested), options, "glTF: nested nodes import");
        check(!result.model.meshes.empty() && hasVertexNear(result.model.meshes[0], 10, 0, 5)
                && hasVertexNear(result.model.meshes[0], 11, 0, 5) && result.meshes[0].source == "tri",
            "glTF: transforms compose from the scene root down");
        Gltf cycle = nested;
        cycle.nodes = R"([{"name":"base","children":[1]},{"name":"tri","mesh":0,"children":[0]}])";
        check(refuses(buildGlb(cycle), options, ObjImportErrorKind::Malformed, "gltfMalformed", "reached twice"),
            "glTF: a node hierarchy with a cycle is malformed");
        Gltf noScene = nested;
        noScene.scenes.clear();
        const ObjImportResult roots = importOrFail(buildGlb(noScene), options, "glTF: a file without scenes imports");
        check(!roots.model.meshes.empty() && hasVertexNear(roots.model.meshes[0], 10, 0, 5),
            "glTF: without a scene every root node is walked");
    }
    {
        // Authored normals turn with the inverse transpose: (1, 1, 0) under
        // scale (1, 2, 1) becomes (1, 0.5, 0), normalised; JK (x, -z, y).
        Gltf tilted{};
        Primitive p = groundTriangle();
        p.normals = { 1, 1, 0, 1, 1, 0, 1, 1, 0 };
        tilted.primitives.push_back(p);
        tilted.nodes = R"([{"name":"tri","mesh":0,"scale":[1,2,1]}])";
        const ObjImportResult result = importOrFail(buildGlb(tilted), options, "glTF: a tilted normal imports");
        const double x = 1.0 / std::sqrt(1.25);
        const double z = 0.5 / std::sqrt(1.25);
        bool authored = !result.model.meshes.empty() && result.model.meshes[0].vertexNormals.size() == 3;
        for ( std::size_t v = 0; authored && v < 3; ++v )
        {
            const TVector& n = result.model.meshes[0].vertexNormals[v];
            authored = std::fabs(n.x - x) < 1e-6 && std::fabs(n.y) < 1e-6 && std::fabs(n.z - z) < 1e-6;
        }
        check(authored, "glTF: vertex normals are the authored normals under the node's inverse transpose");
        check(!result.model.meshes.empty() && closeVec(result.model.meshes[0].faceNormals[0], TVector{ 0.0, 0.0, 1.0 }, 1e-9),
            "glTF: face normals stay geometric");
    }
}

void runPrimitiveTests()
{
    const ObjImportOptions options = crateOptions();
    {
        Primitive quad{};
        quad.positions = { 0, 0, 0, 1, 0, 0, 0, 0, -1, 1, 0, -1 };
        quad.uvs = { 0, 0, 1, 0, 0, 1, 1, 1 };
        for ( const int mode : { 5, 6 } )
        {
            Primitive p = quad;
            p.mode = mode;
            p.indices = mode == 5 ? std::vector<std::uint32_t>{ 0, 1, 2, 3 } : std::vector<std::uint32_t>{ 0, 1, 3, 2 };
            Gltf gltf{};
            gltf.primitives.push_back(p);
            const ObjImportResult result = importOrFail(buildGlb(gltf), options, "glTF: a strip or fan imports");
            bool up = !result.model.meshes.empty() && result.model.meshes[0].faces.size() == 2;
            for ( std::size_t f = 0; up && f < 2; ++f )
            {
                up = result.model.meshes[0].faceNormals[f].z > 0.999;
            }
            check(up, mode == 5 ? "glTF: a triangle strip alternates its winding correctly"
                                : "glTF: a triangle fan keeps its winding");
        }
    }
    {
        Gltf gltf{};
        Primitive p{};
        p.positions = { 0, 0, 0, 1, 0, 0, 0, 0, -1, 1, 0, 0, 2, 0, 0 };
        p.uvs = { 0, 0, 1, 0, 0, 1, 1, 0, 1, 1 };
        // a good triangle; a repeated index; two vertices on one position;
        // three corners on a line
        p.indices = { 0, 1, 2, 0, 0, 2, 1, 3, 2, 0, 1, 4 };
        gltf.primitives.push_back(p);
        const ObjImportResult result = importOrFail(buildGlb(gltf), options, "glTF: degenerate triangles import");
        check(result.faces == 1 && result.degenerateTriangles == 3 && result.gltf.sourceTriangles == 4,
            "glTF: triangles without area are dropped and counted");
    }
    {
        Gltf gltf{};
        gltf.primitives.push_back(groundTriangle());
        gltf.primitives.push_back(groundTriangle());
        gltf.primitives[1].material = "1";
        gltf.materials = R"([{"name":"crate"},{}])";
        ObjImportOptions both = options;
        both.materials.push_back({ "#1", "plain.mat", 32, 32 });
        const ObjImportResult result = importOrFail(buildGlb(gltf), both, "glTF: an unnamed material imports");
        check(result.model.materials == std::vector<std::string>{ "crate.mat", "plain.mat" } && result.faces == 2
                && result.model.meshes.size() == 1 && result.model.meshes[0].vertices.size() == 3,
            "glTF: an unnamed material maps as #<index>; primitives of one mesh share its vertices");
        check(refuses(buildGlb(gltf), options, ObjImportErrorKind::Refused, "objMaterialUnmapped",
                  "material '#1' is not mapped")
                && refuses(buildGlb(gltf), options, ObjImportErrorKind::Refused, "objMaterialUnmapped",
                    "the model's materials: 'crate', '#1'"),
            "glTF: an unmapped material is refused by name, listing the model's materials");
    }
    {
        Gltf gltf{};
        gltf.primitives.push_back(groundTriangle());
        gltf.primitives[0].material.clear();
        check(refuses(buildGlb(gltf), options, ObjImportErrorKind::Refused, "objUntexturedFace", "no material"),
            "glTF: a primitive without a material is refused unless untextured faces are allowed");
        ObjImportOptions untextured = options;
        untextured.allowUntextured = true;
        const ObjImportResult result = importOrFail(buildGlb(gltf), untextured, "glTF: allowed untextured imports");
        check(!result.model.meshes.empty() && result.model.meshes[0].faces[0].material == -1
                && result.model.meshes[0].faces[0].geo == 0 && result.untexturedFaces == 1,
            "glTF: allowed untextured faces have no material and are not drawn");
        Gltf noUv{};
        noUv.primitives.push_back(groundTriangle());
        noUv.primitives[0].uvs.clear();
        check(refuses(buildGlb(noUv), options, ObjImportErrorKind::Refused, "objMissingTextureCoordinates",
                  "TEXCOORD_0"),
            "glTF: a textured primitive without TEXCOORD_0 is refused");
    }
    {
        // Two nodes share one mesh: each is its own source mesh.
        Gltf twice{};
        twice.primitives.push_back(groundTriangle());
        twice.nodes = R"([{"name":"a","mesh":0},{"name":"b","mesh":0,"translation":[0,0,-5]}])";
        twice.scenes = R"("scene":0,"scenes":[{"nodes":[0,1]}],)";
        const ObjImportResult result = importOrFail(buildGlb(twice), options, "glTF: instanced meshes import");
        check(result.model.meshes.size() == 2 && result.model.meshes[0].name == "a" && result.model.meshes[1].name == "b"
                && result.model.nodes[1].parent == 0 && hasVertexNear(result.model.meshes[1], 0, 5, 0),
            "glTF: each node with a mesh becomes a mesh under the root");
    }
}

void runRefusalTests()
{
    const ObjImportOptions options = crateOptions();
    const auto withPrimitive = [](const auto& change)
    {
        Gltf gltf{};
        gltf.primitives.push_back(groundTriangle());
        change(gltf);
        return buildGlb(gltf);
    };
    check(refuses(withPrimitive([](Gltf& g) { g.extra = R"("extensionsRequired":["KHR_draco_mesh_compression"],)"; }),
              options, ObjImportErrorKind::Refused, "gltfUnsupportedFeature", "KHR_draco_mesh_compression"),
        "glTF: a required extension is refused");
    check(refuses(withPrimitive([](Gltf& g) {
                  g.primitives[0].extra = R"(,"extensions":{"KHR_draco_mesh_compression":{"bufferView":0,"attributes":{}}})";
              }),
              options, ObjImportErrorKind::Refused, "gltfUnsupportedFeature", "Draco"),
        "glTF: a Draco-compressed primitive is refused even when the extension is optional");
    check(refuses(withPrimitive([](Gltf& g) {
                  g.primitives[0].positionViewExtra = R"(,"extensions":{"EXT_meshopt_compression":{"buffer":0,"byteLength":36,"byteStride":12,"count":3,"mode":"ATTRIBUTES"}})";
              }),
              options, ObjImportErrorKind::Refused, "gltfUnsupportedFeature", "meshopt"),
        "glTF: a meshopt-compressed buffer view is refused");
    check(refuses(withPrimitive([](Gltf& g) {
                  g.primitives[0].positionExtra = R"(,"sparse":{"count":1,"indices":{"bufferView":0,"componentType":5123},"values":{"bufferView":0}})";
              }),
              options, ObjImportErrorKind::Refused, "gltfUnsupportedFeature", "sparse"),
        "glTF: a sparse accessor is refused");
    check(refuses(withPrimitive([](Gltf& g) { g.primitives[0].mode = 1; }), options, ObjImportErrorKind::Refused,
              "gltfUnsupportedFeature", "lines"),
        "glTF: a primitive of lines is refused");
    check(refuses(withPrimitive([](Gltf& g) { g.primitives[0].mode = 0; }), options, ObjImportErrorKind::Refused,
              "gltfUnsupportedFeature", "points"),
        "glTF: a primitive of points is refused");
    {
        // POSITION and TEXCOORD_0 declared as (normalized) integers.
        check(refuses(withPrimitive([](Gltf& g) { g.primitives[0].positionComponentType = 5123; }), options,
                  ObjImportErrorKind::Refused, "gltfUnsupportedFeature", "POSITION must be 32-bit float"),
            "glTF: a non-float POSITION is refused");
        check(refuses(withPrimitive([](Gltf& g) {
                  g.primitives[0].uvComponentType = 5123;
                  g.primitives[0].uvExtra = R"(,"normalized":true)";
              }),
                  options, ObjImportErrorKind::Refused, "gltfUnsupportedFeature", "TEXCOORD_0 must be 32-bit float"),
            "glTF: a non-float TEXCOORD_0 is refused");
        // Renaming the attribute keeps the file's length.
        std::string noPosition = withPrimitive([](Gltf&) {});
        noPosition.replace(noPosition.find("\"POSITION\""), 10, "\"POSITIOX\"");
        check(refuses(noPosition, options, ObjImportErrorKind::Malformed, "gltfMalformed", "no POSITION"),
            "glTF: a primitive without POSITION is malformed");
    }
    check(refuses(withPrimitive([](Gltf& g) { g.bufferUri = "crate.bin"; }), options, ObjImportErrorKind::Refused,
              "gltfUnsupportedFeature", "external file"),
        "glTF: an external buffer is refused");
    {
        const ObjImportResult inline_ = importOrFail(withPrimitive([](Gltf& g) { g.bufferAsDataUri = true; }), options,
            "glTF: a buffer in a data: URI imports");
        check(inline_.faces == 1, "glTF: a small base64 data: URI buffer is read");
        Gltf big{};
        big.primitives.push_back(groundTriangle());
        big.bufferUri = "data:application/octet-stream;base64," + std::string(5600000, 'A');
        check(refuses(buildGlb(big), options, ObjImportErrorKind::Malformed, "gltfInputTooLarge", "data: URI"),
            "glTF: a data: URI larger than the bound is refused before it is decoded");
    }
    {
        Gltf gltf{};
        Primitive p = groundTriangle();
        p.indices = { 0, 1, 7 };
        gltf.primitives.push_back(p);
        check(refuses(buildGlb(gltf), options, ObjImportErrorKind::Malformed, "gltfMalformed", "past its 3 vertices"),
            "glTF: an index past the vertex count is malformed");
        std::string glb = withPrimitive([](Gltf&) {});
        const std::string count = R"("componentType":5126,"count":3,"type":"VEC3")";
        glb.replace(glb.find(count), count.size(), R"("componentType":5126,"count":4,"type":"VEC3")");
        check(refuses(glb, options, ObjImportErrorKind::Malformed, "gltfMalformed", "reaches past"),
            "glTF: an accessor that reaches past its buffer view is malformed");
    }
    {
        const std::string glb = withPrimitive([](Gltf&) {});
        std::string badMagic = glb;
        badMagic[3] = 'X';
        check(refuses(badMagic, options, ObjImportErrorKind::Malformed, "gltfMalformed", "magic"),
            "glTF: a file without the glTF magic is malformed");
        std::string version1 = glb;
        version1[4] = 1;
        check(refuses(version1, options, ObjImportErrorKind::Refused, "gltfUnsupportedFeature", "version 1"),
            "glTF: a GLB container other than version 2 is refused");
        check(refuses(glb.substr(0, glb.size() - 4), options, ObjImportErrorKind::Malformed, "gltfMalformed", "length"),
            "glTF: a truncated GLB is malformed");
        std::string badJson = glbContainer("{\"asset\":", "");
        check(refuses(badJson, options, ObjImportErrorKind::Malformed, "gltfMalformed", "not valid JSON"),
            "glTF: a JSON chunk that is not JSON is malformed");
        std::string duplicate = glbContainer(R"({"asset":{"version":"2.0"},"asset":{"version":"2.0"}})", "");
        check(refuses(duplicate, options, ObjImportErrorKind::Malformed, "gltfMalformed", "duplicate"),
            "glTF: a JSON object with a duplicate key is malformed");
        check(refuses(std::string(sed::assets::kGlbImportMaxSourceBytes + 1, 'g'), options,
                  ObjImportErrorKind::Malformed, "gltfInputTooLarge"),
            "glTF: a file larger than the bound is refused");
    }
}

void runBaseColorTests()
{
    const ObjImportOptions options = crateOptions();
    const std::string jpeg = std::string("\xFF\xD8\xFF\xE0", 4) + "a stand-in for JPEG data";
    {
        Gltf gltf{};
        gltf.primitives.push_back(groundTriangle());
        gltf.images = jpeg;
        std::size_t view = 0;
        (void)buildGlb(gltf, &view);
        gltf.materials = R"([{"name":"crate","pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":0}},"normalTexture":{"index":0},"doubleSided":true}])";
        gltf.extra = R"("textures":[{"source":0}],"images":[{"name":"crate.jpg","mimeType":"image/jpeg","bufferView":)"
            + std::to_string(view) + "}],";
        const ObjImportResult result = importOrFail(buildGlb(gltf), options, "glTF: a textured material imports");
        check(result.gltf.baseColorImages.size() == 1 && result.gltf.baseColorImages[0].problem.empty()
                && result.gltf.baseColorImages[0].mimeType == "image/jpeg"
                && result.gltf.baseColorImages[0].name == "crate.jpg"
                && result.gltf.baseColorImages[0].materials == std::vector<std::string>{ "crate" }
                && std::string(result.gltf.baseColorImages[0].bytes.begin(), result.gltf.baseColorImages[0].bytes.end())
                    == jpeg,
            "glTF: the base colour image comes back exactly as stored");
        bool reported = false;
        for ( const auto& [what, count] : result.gltf.ignored )
        {
            reported = reported || (what == "material normalTexture" && count == 1);
        }
        check(reported, "glTF: material maps JK cannot use are reported as ignored");

        Gltf transformed = gltf;
        transformed.materials = R"([{"name":"crate","pbrMetallicRoughness":{"baseColorTexture":{"index":0,"extensions":{"KHR_texture_transform":{"scale":[2,2]}}}}}])";
        check(refuses(buildGlb(transformed), options, ObjImportErrorKind::Refused, "gltfUnsupportedFeature",
                  "KHR_texture_transform"),
            "glTF: a base colour mapped through KHR_texture_transform is refused");
        Gltf secondSet = gltf;
        secondSet.materials = R"([{"name":"crate","pbrMetallicRoughness":{"baseColorTexture":{"index":0,"texCoord":1}}}])";
        check(refuses(buildGlb(secondSet), options, ObjImportErrorKind::Refused, "gltfUnsupportedFeature",
                  "TEXCOORD_1"),
            "glTF: a base colour mapped through another texture coordinate set is refused");
    }
    {
        const std::string png = std::string("\x89PNG\r\n\x1a\n", 8) + "a stand-in for PNG data";
        Gltf gltf{};
        gltf.primitives.push_back(groundTriangle());
        gltf.materials = R"([{"name":"crate","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}])";
        gltf.extra = R"("textures":[{"source":0}],"images":[{"uri":"data:image/png;base64,)" + base64(png) + "\"}],";
        const ObjImportResult result = importOrFail(buildGlb(gltf), options, "glTF: a data: URI image imports");
        check(result.gltf.baseColorImages.size() == 1 && result.gltf.baseColorImages[0].mimeType == "image/png"
                && std::string(result.gltf.baseColorImages[0].bytes.begin(), result.gltf.baseColorImages[0].bytes.end())
                    == png,
            "glTF: a base colour image in a data: URI is decoded");
        Gltf external = gltf;
        external.extra = R"("textures":[{"source":0}],"images":[{"uri":"crate.png"}],)";
        const ObjImportResult outside = importOrFail(buildGlb(external), options, "glTF: an external image imports");
        check(outside.gltf.baseColorImages.size() == 1 && outside.gltf.baseColorImages[0].bytes.empty()
                && outside.gltf.baseColorImages[0].problem.find("external file") != std::string::npos,
            "glTF: an external base colour image is reported, not read");
    }
}

} // namespace

void runGltfImportTests()
{
    runCubeTests();
    runTransformTests();
    runPrimitiveTests();
    runRefusalTests();
    runBaseColorTests();
}

} // namespace sed::tests
