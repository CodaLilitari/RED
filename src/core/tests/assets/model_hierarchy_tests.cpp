#include "test_common.hpp"

#include "sed/assets/model_hierarchy.hpp"

#include <algorithm>
#include <sstream>
#include <string>

namespace sed::tests
{
namespace
{

using namespace sed::assets;

/** A small arm: base (mesh 0) > upper (mesh 1) > hand (mesh 2), with an empty helper under the base. */
Model3do arm()
{
    Model3do model;
    model.version = 2.1;
    model.geosetCount = 1;
    model.hierarchyTransformsApplied = false;
    for (const char* name : { "base", "upper", "hand" })
    {
        Model3doMesh mesh;
        mesh.name = name;
        model.meshes.push_back(mesh);
    }
    const auto node = [&](const char* name, const std::int32_t mesh, const std::int32_t parent, const TVector position) {
        Model3doNode n;
        n.id = static_cast<std::int32_t>(model.nodes.size());
        n.name = name;
        n.mesh = mesh;
        n.parent = parent;
        n.position = position;
        model.nodes.push_back(n);
    };
    node("base", 0, -1, { 0, 0, 0 });
    node("upper", 1, 0, { 0, 0, 0.1 });
    node("hand", 2, 1, { 0, 0.05, 0.1 });
    node("helper", -1, 0, { 0.02, 0, 0 });
    return model;
}

bool has(const std::vector<HierarchyIssue>& issues, const std::string& code)
{
    return std::any_of(issues.begin(), issues.end(), [&](const HierarchyIssue& i) { return i.code == code; });
}

void testValidation()
{
    Model3do model = arm();
    check(validateModelHierarchy(model).empty(), "3DO hierarchy: a clean tree has no issues");
    const auto rows = modelHierarchyRows(model);
    check(rows.size() == 4 && rows[0].node == 0 && rows[1].node == 1 && rows[2].node == 2 && rows[2].depth == 2
            && rows[3].node == 3 && rows[3].depth == 1,
        "3DO hierarchy: rows list the tree depth first");
    model.nodes[2].parent = 2;
    model.nodes[3].mesh = 7;
    model.nodes[1].name = "hand";
    const auto issues = validateModelHierarchy(model);
    check(has(issues, "badParent") && has(issues, "badMesh") && has(issues, "duplicateName"),
        "3DO hierarchy: a bad parent, a missing mesh and a repeated name are reported");
    Model3do cyclic = arm();
    cyclic.nodes[1].parent = 2;
    check(has(validateModelHierarchy(cyclic), "cycle"), "3DO hierarchy: a cycle is reported");
    Model3do orphan = arm();
    orphan.nodes[2].mesh = -1;
    check(has(validateModelHierarchy(orphan), "unusedMesh"), "3DO hierarchy: a mesh no node shows is reported");
}

void testEdits()
{
    Model3do model = arm();
    check(!renameModelNode(model, 3, "wrist") && model.nodes[3].name == "wrist", "3DO hierarchy: rename");
    check(renameModelNode(model, 3, "two words")->code == "nodeNameCharacters" && model.nodes[3].name == "wrist",
        "3DO hierarchy: a name with a space is refused");
    check(reparentModelNode(model, 1, 2)->code == "cycle" && model.nodes[1].parent == 0,
        "3DO hierarchy: moving a node under its own child is refused");
    check(reparentModelNode(model, 0, 1)->code == "rootFixed", "3DO hierarchy: the root stays the root");
    check(!reparentModelNode(model, 3, 2) && model.nodes[3].parent == 2, "3DO hierarchy: reparent");
    check(!setModelNodeTransform(model, 3, TVector{ 0, 0, 0.02 }, std::nullopt, TVector{ 0.01, 0, 0 })
            && model.nodes[3].position.z == 0.02 && model.nodes[3].pivot.x == 0.01,
        "3DO hierarchy: set position and pivot");
    std::int32_t added = -1;
    check(!addModelNode(model, 2, 2, "finger", &added) && added == 4 && model.nodes[4].parent == 2,
        "3DO hierarchy: add a node");
    check(removeModelNode(model, 2)->code == "hasChildren", "3DO hierarchy: a node with children isn't removed");
    check(!removeModelNode(model, 3) && model.nodes.size() == 4 && model.nodes[3].name == "finger" && model.nodes[3].parent == 2
            && model.nodes[3].id == 3,
        "3DO hierarchy: removing a leaf renumbers the nodes after it");
    check(validateModelHierarchy(model).empty(), "3DO hierarchy: the edited tree is still clean");

    std::istringstream in(saveTo3DO(model));
    Model3doLoadOptions raw;
    raw.applyHierarchyTransforms = false;
    const Model3do back = loadFrom3DO(in, raw);
    check(back.nodes.size() == 4 && back.nodes[3].name == "finger" && back.nodes[3].parent == 2 && back.nodes[1].position.z == 0.1,
        "3DO hierarchy: the edited model saves and reads back");
}

void testKey()
{
    Model3do model = arm();
    KeyFile key = keyFromModel(model, 30, 15.0);
    check(key.nodes.size() == 4 && key.joints == 4 && key.nodes[2].meshName == "hand" && key.nodes[2].nodeIndex == 2
            && key.nodes[2].entries.size() == 1 && key.nodes[2].entries[0].position.y == 0.05,
        "KEY from a model: a track per node at its rest pose");
    check(validateKeyForModel(key, model).empty(), "KEY from a model: matches its model");

    KeyEntry raised;
    raised.frame = 10;
    raised.position = model.nodes[1].position;
    raised.rotation = TVector{ 90, 0, 0 };
    check(keyInsertEntry(key, 1, raised), "KEY from a model: an entry can be added");
    check(!validateKeyForModel(key, model).empty() && validateKeyForModel(key, model).front().code == "staleDeltas",
        "KEY: a new entry leaves the track's deltas stale");
    check(keyComputeDeltas(key) == 2, "KEY deltas: both entries of the edited track change");
    const KeyEntry& first = key.nodes[1].entries[0];
    const KeyEntry& last = key.nodes[1].entries[1];
    check(first.dRotation.x == 9.0 && first.dPosition.x == 0.0 && first.flags == 2,
        "KEY deltas: 90 degrees over 10 frames is 9 a frame, flagged as an orientation change");
    check(last.dRotation.x == -4.5 && last.flags == 2,
        "KEY deltas: the last entry turns back to the first over the 20 frames left, as retail KEYs do");
    KeyFile wrap = keyFromModel(model, 20, 15.0);
    wrap.nodes[0].entries[0].rotation = TVector{ 343, 0, 0 };
    KeyEntry over;
    over.frame = 10;
    over.rotation = TVector{ 3, 0, 0 };
    (void)keyInsertEntry(wrap, 0, over);
    (void)keyComputeDeltas(wrap);
    check(wrap.nodes[0].entries[0].dRotation.x == 2.0, "KEY deltas: 343 to 3 degrees turns +20 the short way, as retail KEYs do");
    check(validateKeyForModel(key, model).empty() && keyComputeDeltas(key) == 0, "KEY deltas: recomputing again changes nothing");

    std::istringstream in(saveToKEYText(key));
    const KeyFile back = loadFromKEY(in);
    check(back.nodes.size() == 4 && back.nodes[1].entries.size() == 2 && back.nodes[1].entries[0].dRotation.x == 9.0,
        "KEY: the authored KEY saves and reads back");

    key.nodes[3].nodeIndex = 9;
    key.nodes[0].meshName = "torso";
    key.nodes[1].entries[1].frame = 40;
    const auto issues = validateKeyForModel(key, model);
    check(has(issues, "badJoint") && has(issues, "nameMismatch") && has(issues, "beyondEnd"),
        "KEY against a model: a missing joint, a renamed part and an entry past the end are reported");
}

} // namespace

void runModelHierarchyTests()
{
    testValidation();
    testEdits();
    testKey();
}

} // namespace sed::tests
