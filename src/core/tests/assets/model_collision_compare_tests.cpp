#include "sed/assets/model_collision_compare.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

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

Model3doMesh mesh(std::initializer_list<int> heights, bool hidden = false)
{
    Model3doMesh result;
    result.name = "synthetic";
    for ( const int height : heights )
    {
        const auto index = static_cast<std::int32_t>(result.vertices.size());
        const double z = height;
        result.vertices.insert(result.vertices.end(), { { 0, 0, z }, { 1, 0, z }, { 0, 1, z } });
        Model3doFace face;
        face.material = hidden ? -1 : 0;
        face.geo = hidden ? 0 : 4;
        face.vertices = { index, index + 1, index + 2 };
        face.texVertices = { -1, -1, -1 };
        result.faces.push_back(face);
        result.faceNormals.push_back({ 0, 0, 1 });
    }
    return result;
}

std::string source(std::vector<Model3doMesh> meshes)
{
    Model3do model;
    model.version = 2.1;
    model.materials = { "synthetic.mat" };
    model.meshes = std::move(meshes);
    return saveTo3DO(model, { .scalarVersion21Vertices = true });
}

NativeModelDocument doc(std::vector<Model3doMesh> meshes)
{
    return NativeModelDocument::open(source(std::move(meshes)));
}

void checkScopeAndAdmission()
{
    const auto first = source({ mesh({ 1 }) });
    const auto triangle = NativeModelDocument::open(first);
    auto twoLods = first;
    const auto second = source({ mesh({ 7, 8 }) });
    const auto geoset = second.find("GEOSET 0\n");
    const auto hierarchy = second.find("SECTION: HIERARCHYDEF");
    auto other = second.substr(geoset, hierarchy - geoset);
    other.replace(0, 8, "GEOSET 1");
    twoLods.insert(twoLods.find("SECTION: HIERARCHYDEF"), other);
    twoLods.replace(twoLods.find("GEOSETS 1"), 9, "GEOSETS 2");
    const auto lods = compareModelCollision(triangle, NativeModelDocument::open(twoLods));
    require(lods.sameFaceSequence && lods.candidate.faces == 1, "Other visual LODs must stay out of the comparison.");

    auto withInstances = first;
    withInstances.replace(withInstances.find("HIERARCHY NODES 0"), 17,
        "HIERARCHY NODES 2\n0: 0x0 0x1 0 -1 1 -1 1 0 0 9 0 0 0 0 0 0 base\n"
        "1: 0x0 0x1 0 0 -1 -1 0 1 0 0 0 0 0 0 0 0 copy");
    const auto instances = compareModelCollision(triangle, NativeModelDocument::open(withInstances));
    require(instances.sameFaceSequence && instances.matchedFaces == 1,
        "Draw-node translation and repeated mesh instances must not move or duplicate raw faces.");

    auto expectRefusal = [](const std::string& text, std::string_view diagnostic)
    {
        try
        {
            (void)NativeModelDocument::open(text);
        }
        catch ( const std::runtime_error& error )
        {
            require(std::string_view(error.what()).find(diagnostic) != std::string_view::npos, "Wrong admission error.");
            return;
        }
        throw std::runtime_error("Expected strict input refusal.");
    };
    auto mislabelled = first;
    mislabelled.replace(mislabelled.find("GEOSET 0\n"), 8, "GEOSET 5");
    expectRefusal(mislabelled, "geoset");
    auto absentNormals = first;
    absentNormals.erase(absentNormals.find("FACE NORMALS"), absentNormals.find("SECTION: HIERARCHYDEF")
        - absentNormals.find("FACE NORMALS"));
    expectRefusal(absentNormals, "face normals");
    expectRefusal(std::string(16 * 1024 * 1024 + 1, ' '), "source");
}

void checkComparison()
{
    const auto original = doc({ mesh({ 1, 2 }), mesh({ 1 }) });
    const auto reordered = compareModelCollision(original, doc({ mesh({ 2, 1, 1 }, true) }));
    require(reordered.matchedFaces == 3 && reordered.missingFaces == 0 && reordered.extraFaces == 0,
        "Moving duplicate faces across meshes must preserve all three occurrences.");
    require(!reordered.sameFaceSequence && !reordered.sameMeshFaceCounts, "Changed traversal order was missed.");
    require(reordered.source.meshes == 2 && reordered.source.faces == 3
            && reordered.source.untexturedModeZeroFaces == 0 && reordered.candidate.untexturedModeZeroFaces == 3,
        "Face material -1 and geometry mode 0 must be measured separately from equivalence.");

    const auto repartitioned = compareModelCollision(original, doc({ mesh({ 1 }), mesh({ 2, 1 }) }));
    require(repartitioned.sameFaceSequence && !repartitioned.sameMeshFaceCounts,
        "Moving a boundary between faces must be visible even when flattened traversal order is identical.");
    const auto changed = compareModelCollision(original, doc({ mesh({ 1, 2 }), mesh({ 3 }) }));
    require(changed.matchedFaces == 2 && changed.missingFaces == 1 && changed.extraFaces == 1
            && !changed.sameFaceSequence && changed.sameMeshFaceCounts, "One duplicate was replaced by an extra face.");

    const auto triangle = doc({ mesh({ 2 }) });
    auto variant = mesh({ 2 });
    std::swap(variant.vertices[0], variant.vertices[1]);
    variant.faces[0].vertices = { 1, 0, 2 };
    require(compareModelCollision(triangle, doc({ variant })).sameFaceSequence, "Physical coordinates resolve vertex IDs.");
    variant.faces[0].vertices = { 0, 2, 1 };
    require(compareModelCollision(triangle, doc({ variant })).matchedFaces == 0, "First corner is the native plane anchor.");
    variant.faces[0].vertices = { 1, 2, 0 };
    require(compareModelCollision(triangle, doc({ variant })).matchedFaces == 0, "Reversed winding must differ.");
    variant = mesh({ 2 });
    variant.faceNormals[0].z = 0.75;
    require(compareModelCollision(triangle, doc({ variant })).matchedFaces == 0, "Stored normals must differ.");
    variant = mesh({ 2 });
    variant.vertices[2].y = 1.000001;
    require(compareModelCollision(triangle, doc({ variant })).matchedFaces == 0, "Coordinates must compare exactly.");
    variant = mesh({ 2 });
    variant.faces[0].geo = 0;
    require(compareModelCollision(triangle, doc({ variant })).candidate.untexturedModeZeroFaces == 0,
        "Mode zero alone does not establish an untextured mode-zero face.");

    variant = mesh({ 2 });
    variant.vertices[0].x = -0.0;
    require(compareModelCollision(triangle, doc({ variant })).matchedFaces == 1, "Signed zeros are numerically equal.");
    variant.faceNormals[0] = { 1e200, -1e200, 0 };
    auto different = variant;
    different.faceNormals[0].x = 2e200;
    require(compareModelCollision(doc({ variant }), doc({ different })).matchedFaces == 0,
        "Large finite normals must be compared directly without normalization or subtraction.");

    const auto empty = compareModelCollision(doc({ mesh({}) }), doc({ mesh({}) }));
    require(empty.source.faces == 0 && empty.matchedFaces == 0 && empty.sameFaceSequence,
        "An empty comparison must report an explicit zero count.");
}
} // namespace

int main()
{
    try
    {
        checkComparison();
        checkScopeAndAdmission();
        return 0;
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
