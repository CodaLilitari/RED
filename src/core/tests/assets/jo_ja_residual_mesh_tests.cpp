#include "sed/assets/jo_ja_residual_mesh.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
using namespace sed;
using namespace sed::assets;

void require(bool condition, const char* message)
{
    if ( !condition )
    {
        throw std::runtime_error(message);
    }
}

JoJaResidualCorner corner(double x, double y, float u, float v)
{
    return {{x, y, 2.0, ColorWhite}, {0, 0, 1}, {u, v}};
}

JoJaResidualTriangle tri(double x = 0.0)
{
    return {{{corner(x, 0, 0, 0), corner(x + 1, 0, 1, 0), corner(x, 1, 0, 1)}}, 7};
}

void checkSeams()
{
    const auto a = tri();
    JoJaResidualTriangle b{{a.corners[2], a.corners[1], corner(1, 1, 1, 1)}, 9};
    b.corners[0].uv = {8, 9};
    std::vector<JoJaResidualTriangle> input{a, b};
    for ( std::size_t seam = 0; seam < 5; ++seam )
    {
        auto c = a;
        auto& v = c.corners[0];
        switch ( seam )
        {
        case 0:
            v.normal = {0, 1, 0};
            break;
        case 1:
            v.vertex.color.r = 0.5f;
            break;
        case 2:
            v.vertex.color.g = 0.5f;
            break;
        case 3:
            v.vertex.color.b = 0.5f;
            break;
        default:
            v.vertex.color.a = 0.5f;
            break;
        }
        input.push_back(c);
    }
    const auto packed = packJoJaResidualMeshes(input, 140);
    require(packed.size() == 1 && packed[0].firstTriangle == 0, "one contiguous packet");
    const auto& packet = packed[0];
    const auto& mesh = packet.mesh;
    require(mesh.vertices.size() == 9 && mesh.texVertices.size() == 5, "independent exact position/UV keys");
    require(mesh.vertexNormals.size() == mesh.vertices.size(), "normal table follows shared positions");
    require(mesh.faces[1].vertices == std::vector<std::int32_t>({2, 1, 3}), "winding and first occurrence indices");
    require(mesh.faces[1].texVertices == std::vector<std::int32_t>({3, 1, 4}), "UV seam survives shared position");
    require(packet.center.x > 0 && packet.center.x < 1 && packet.center.y > 0 && packet.center.y < 1 &&
                packet.center.z == 2.0,
            "packet pivot");
    require(mesh.radius >= std::sqrt(0.5) && mesh.radius == mesh.authoredRadius, "bounding radius");
    for ( std::size_t f = 0; f < input.size(); ++f )
    {
        const auto& face = mesh.faces[f];
        require(face.material == input[f].material, "material remains face-local");
        for ( std::size_t c = 0; c < 3; ++c )
        {
            const auto& expected = input[f].corners[c];
            const auto& v = mesh.vertices.at(face.vertices[c]);
            const auto& n = mesh.vertexNormals.at(face.vertices[c]);
            const auto& uv = mesh.texVertices.at(face.texVertices[c]);
            require(v.x + packet.center.x == expected.vertex.x && v.y + packet.center.y == expected.vertex.y &&
                        v.z + packet.center.z == expected.vertex.z && v.color.r == expected.vertex.color.r &&
                        v.color.g == expected.vertex.color.g && v.color.b == expected.vertex.color.b &&
                        v.color.a == expected.vertex.color.a && n.x == expected.normal.x && n.y == expected.normal.y &&
                        n.z == expected.normal.z && uv.u == expected.uv.u && uv.v == expected.uv.v,
                    "every ordered face corner reproduces authored channels");
        }
    }
}

void checkWireRoundTrip()
{
    auto source = tri();
    source.material = 0;
    auto reversed = source;
    std::swap(reversed.corners[0], reversed.corners[2]);
    reversed.corners[0].normal = {1, 0, 0};
    const std::array input{source, reversed};
    auto packets = packJoJaResidualMeshes(input, 479);
    Model3do model;
    model.version = 2.1;
    model.geosetCount = 1;
    model.hierarchyTransformsApplied = false;
    model.materials = {"stone.mat"};
    model.radius = packets.front().mesh.radius;
    model.meshes.push_back(std::move(packets.front().mesh));
    Model3doNode root;
    root.mesh = 0;
    root.name = "world";
    model.nodes.push_back(root);
    require(model3doEngineLimitIssues(model).empty(), "packet satisfies target 3DO writer limits");
    std::istringstream wire(saveTo3DO(model, {.scalarVersion21Vertices = true}));
    const auto loaded = loadFrom3DO(wire, {.applyHierarchyTransforms = false});
    const auto& mesh = loaded.meshes.at(0);
    require(mesh.vertices.size() == 4 && mesh.vertexNormals.at(mesh.faces.at(1).vertices.at(0)).x == 1,
            "wire round trip preserves a corner normal discontinuity");
    require(mesh.faceNormals.at(0).z == 1 && mesh.faceNormals.at(1).z == -1,
            "wire face normals derive from the retained geometric winding rather than corner normals");
}

void checkSectorOwnership()
{
    auto face = tri();
    face.corners[0].vertex = {0, 0, 1, ColorWhite};
    face.corners[1].vertex = {1, 0, 0, ColorWhite};
    face.corners[2].vertex = {0, 1, 0, ColorWhite};
    auto inside = [](const TVector& p)
    {
        return p.x > 0 && p.y > 0 && p.z > 0 && p.x + p.y + p.z < 1;
    };
    const std::array triangles{face};
    const TVector anchor{0.125, 0.125, 0.125};
    const auto chunks = packJoJaResidualMeshes(triangles, 140, anchor, inside);
    const auto& p = chunks.front().center;
    require(inside(p) && p.x > 0.3, "boundary geometry keeps a close, strictly in-sector float32 pivot");
    const auto fallback = packJoJaResidualMeshes(triangles, 140, anchor,
                                                 [&](const TVector& v)
                                                 {
                                                     return v.x == anchor.x && v.y == anchor.y && v.z == anchor.z;
                                                 });
    require(fallback.front().center.x == anchor.x, "invalid rounded pivot falls back to the proven sector interior");
    bool refused = false;
    try
    {
        (void)packJoJaResidualMeshes(triangles, 140, anchor,
                                     [](const TVector&)
                                     {
                                         return false;
                                     });
    }
    catch ( const std::runtime_error& )
    {
        refused = true;
    }
    require(refused, "invalid float32 interior anchor also refused");
}

void checkBudgets()
{
    std::vector<JoJaResidualTriangle> input(480, tri());
    auto packed = packJoJaResidualMeshes(input, 512);
    require(packed.size() == 2 && packed[0].mesh.faces.size() == 479 && packed[1].firstTriangle == 479,
            "aggregate measured triangle ceiling");
    packed = packJoJaResidualMeshes(input, 140);
    require(packed.size() == 4 && packed[3].firstTriangle == 420, "smaller configured triangle ceiling");
    input.clear();
    for ( int i = 0; i < 171; ++i )
    {
        input.push_back(tri(i * 2.0));
    }
    packed = packJoJaResidualMeshes(input, 479);
    require(packed.size() == 2 && packed[1].firstTriangle == 170 && packed[0].mesh.vertices.size() == 510 &&
                packed[1].mesh.vertices.size() == 3 && packed[0].mesh.texVertices.size() == 3,
            "position budget splits before an entire triangle and retries it");
    input.resize(170);
    auto edge = tri(1000);
    edge.corners[0] = input.front().corners[0];
    input.push_back(edge);
    input.push_back(input.front());
    input.push_back(tri(1200));
    packed = packJoJaResidualMeshes(input, 479);
    require(packed.size() == 2 && packed[1].firstTriangle == 172 && packed[0].mesh.vertices.size() == 512,
            "all 512 position slots and subsequent faces using existing slots are admitted");
    input.assign(257, tri());
    for ( std::size_t i = 0; i < input.size(); ++i )
    {
        for ( std::size_t c = 0; c < 3; ++c )
        {
            input[i].corners[c].uv.u = static_cast<float>(3 * i + c);
        }
    }
    packed = packJoJaResidualMeshes(input, 479);
    require(packed.size() == 2 && packed[1].firstTriangle == 256 && packed[0].mesh.texVertices.size() == 768 &&
                packed[0].mesh.vertices.size() == 3 && packed[1].mesh.texVertices.size() == 3,
            "independent UV budget allows all 768 slots");
}

void checkPolygons()
{
    const JoJaResidualFace quad{{corner(0, 0, 0, 0), corner(2, 0, 4, 0), corner(2, 1, 4, 2), corner(0, 1, 0, 2)}, 7};
    auto donor = quad;
    donor.corners.insert(donor.corners.begin() + 1, corner(1, 0, 2, 0));
    donor.corners[1].vertex.color = {.25f, .25f, .25f, 1.f};
    donor.corners[1].normal = {0, 1, 0};
    const std::array input{quad, donor};
    auto packets = packJoJaResidualFaces(input, 479);
    require(packets.size() == 1 && packets[0].firstFace == 0, "two clipped polygons share one packet");
    const auto& mesh = packets[0].mesh;
    require(mesh.faces.size() == 2 && mesh.faces[0].vertices.size() == 4 && mesh.faces[1].vertices.size() == 5,
            "one wire 3DO face per source polygon, including a collinear edge donor");
    require(mesh.vertices.size() == 5 && mesh.texVertices.size() == 5, "quad indices shared exactly");
    const auto donorId = mesh.faces[1].vertices[1];
    require(mesh.vertexNormals[donorId].y == 1 && mesh.vertices[donorId].color.r == .25f &&
                mesh.texVertices[mesh.faces[1].texVertices[1]].u == 2,
            "collinear corner channels survive");
    require(std::abs(packets[0].center.x - 1) < 1e-7 && std::abs(packets[0].center.y - 4. / 9) < 1e-7,
            "pivot is weighted by actual polygon corner count");
    Model3do model;
    model.version = 2.1;
    model.geosetCount = 1;
    model.hierarchyTransformsApplied = false;
    model.materials.resize(8, "stone.mat");
    model.meshes.push_back(mesh);
    model.radius = mesh.radius;
    Model3doNode root;
    root.mesh = 0;
    root.name = "world";
    model.nodes.push_back(root);
    require(model3doEngineLimitIssues(model).empty(), "retail polygon model satisfies loader limits");
    std::istringstream wire(saveTo3DO(model, {.scalarVersion21Vertices = true}));
    const auto decoded = loadFrom3DO(wire, {.applyHierarchyTransforms = false});
    require(decoded.meshes.front().faces[1].vertices.size() == 5 && decoded.meshes.front().faceNormals[1].z == 1,
            "scalar 2.1 wire retains full polygon and its geometric normal despite three collinear leading corners");

    std::vector<JoJaResidualFace> repeated(480, quad);
    packets = packJoJaResidualFaces(repeated, 512);
    require(packets.size() == 2 && packets[1].firstFace == 479, "479 polygon faces per packet");

    const auto thinTriangle = tri(), distantTriangle = tri(100);
    JoJaResidualFace thin{{thinTriangle.corners.begin(), thinTriangle.corners.end()}, 0};
    thin.corners[1].vertex.x = 1e-10;
    JoJaResidualFace distant{{distantTriangle.corners.begin(), distantTriangle.corners.end()}, 0};
    const std::array precisionGroup{thin, distant};
    auto localPackets = packJoJaResidualFaces(precisionGroup, 479);
    require(localPackets.size() == 2 && localPackets[1].firstFace == 1 &&
                localPackets[0].mesh.faces[0].vertices.size() == 3,
            "retry with a local packet pivot when a distant face destroys tiny source detail");

    JoJaResidualFace narrow{
        {corner(1 + 3e-10, 0, 0, 0), corner(1.1, 0, 1, 0), corner(1.1, .2, 1, 1), corner(1 + 1e-10, 0, .001f, .002f)},
        4};
    narrow.corners[1].vertex.y = .1;
    auto single = packJoJaResidualFaces(std::span{&narrow, 1}, 479);
    require(single.size() == 1 && single.front().mesh.faces.front().vertices.size() == 4,
            "recover both distinct endpoint corners with a verified singleton pivot");
    const auto& first = single.front().mesh;
    require(float(first.vertices[0].x) != float(first.vertices[3].x) && first.texVertices.size() == 4,
            "close authored endpoints and distinct UVs both survive float32");

    auto rejects = [](JoJaResidualFace f)
    {
        try
        {
            (void)packJoJaResidualFaces(std::span{&f, 1}, 1);
        }
        catch ( const std::runtime_error& )
        {
            return true;
        }
        return false;
    };
    auto bad = quad;
    bad.corners[2].vertex.z += 1e-3;
    require(rejects(bad), "non-planar source polygon refused");
    bad = quad;
    bad.corners[2] = corner(.2, .2, 1, 1);
    require(rejects(bad), "concave source polygon refused");
    bad = quad;
    bad.corners.resize(2);
    require(rejects(bad), "underfull polygon refused");
    bad.corners.resize(25);
    require(rejects(bad), "overfull polygon refused");
}

void checkRefusals()
{
    auto rejects = [](const std::vector<JoJaResidualTriangle>& input, std::size_t budget)
    {
        try
        {
            (void)packJoJaResidualMeshes(input, budget);
        }
        catch ( const std::runtime_error& )
        {
            return true;
        }
        return false;
    };
    require(packJoJaResidualMeshes({}, 1).empty(), "empty group");
    require(rejects({tri()}, 0), "zero budget refused");
    auto bad = tri();
    bad.corners[1].normal.y = std::numeric_limits<double>::quiet_NaN();
    require(rejects({bad}, 1), "nonfinite key refused");
    auto tiny = tri();
    tiny.corners[1].vertex.x = 1e-10;
    require(rejects({tiny, tri(100)}, 479), "target float32 collapse after centering refused");
    const double step = std::ldexp(1.0, -18);
    JoJaResidualTriangle sliver{
        {corner(0, 0, 0, 0), corner(1, 1 + 0.55 * step, 1, 0), corner(2, 2 + 1.12 * step, 0, 1)}, 0};
    JoJaResidualTriangle distant{{corner(100, 100, 0, 0), corner(101, 100, 1, 0), corner(100, 101, 0, 1)}, 0};
    require(rejects({sliver, distant}, 479), "target float32 winding reversal after centering refused");
}

} // namespace

int main()
{
    try
    {
        checkSeams();
        checkWireRoundTrip();
        checkSectorOwnership();
        checkBudgets();
        checkRefusals();
        checkPolygons();
        return 0;
    }
    catch ( const std::exception& e )
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
