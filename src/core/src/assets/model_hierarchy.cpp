#include "sed/assets/model_hierarchy.hpp"

#include "sed/text.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace sed::assets
{
namespace
{

HierarchyIssue issue(std::string severity, std::string code, std::string message, const std::int32_t node = -1)
{
    return HierarchyIssue{ std::move(severity), std::move(code), std::move(message), node };
}

std::optional<HierarchyIssue> refuse(std::string code, std::string message, const std::int32_t node = -1)
{
    return issue("error", std::move(code), std::move(message), node);
}

bool validNode(const Model3do& model, const std::int32_t node)
{
    return node >= 0 && static_cast<std::size_t>(node) < model.nodes.size();
}

std::string nodeLabel(const Model3do& model, const std::int32_t node)
{
    return "node " + std::to_string(node) + " (" + model.nodes[static_cast<std::size_t>(node)].name + ")";
}

bool finite(const TVector& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::abs(v.x) < 1e6 && std::abs(v.y) < 1e6
        && std::abs(v.z) < 1e6;
}

/** True when walking up from `node` reaches `ancestor`. */
bool isUnder(const Model3do& model, std::int32_t node, const std::int32_t ancestor)
{
    for (std::size_t steps = 0; validNode(model, node) && steps <= model.nodes.size(); ++steps)
    {
        if (node == ancestor) return true;
        node = model.nodes[static_cast<std::size_t>(node)].parent;
    }
    return false;
}

std::optional<HierarchyIssue> checkName(const std::string_view name)
{
    if (name.empty() || name.size() > kMaxModelNodeNameLength)
        return refuse("nodeNameLength", "A node name has 1 to 31 characters.");
    if (std::any_of(name.begin(), name.end(), [](const unsigned char c) { return c <= ' ' || c >= 127; }))
        return refuse("nodeNameCharacters", "A node name can't contain spaces or other non-printing characters.");
    return std::nullopt;
}

bool near(const double a, const double b)
{
    return std::abs(a - b) <= 1e-4 * std::max(1.0, std::max(std::abs(a), std::abs(b)));
}

bool near(const TVector& a, const TVector& b)
{
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

bool zero(const TVector& v)
{
    return v.x == 0.0 && v.y == 0.0 && v.z == 0.0;
}

TVector slope(const TVector& from, const TVector& to, const double frames)
{
    return TVector{ (to.x - from.x) / frames, (to.y - from.y) / frames, (to.z - from.z) / frames };
}

/** Angles turn the short way round, as in retail KEYs: 343 to 3 degrees is +20. */
double turn(const double from, const double to)
{
    double d = std::fmod(to - from, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d < -180.0) d += 360.0;
    return d;
}

TVector angularSlope(const TVector& from, const TVector& to, const double frames)
{
    return TVector{ turn(from.x, to.x) / frames, turn(from.y, to.y) / frames, turn(from.z, to.z) / frames };
}

std::size_t expectedFirst(const KeyNode& node)
{
    std::size_t first = 0;
    for (std::size_t e = 1; e < node.entries.size(); ++e)
    {
        if (node.entries[e].frame < node.entries[first].frame) first = e;
    }
    return first;
}

/** What the engine applies: a delta counts only when its flag is set. */
TVector effective(const TVector& delta, const bool flagged)
{
    return flagged ? delta : TVector{};
}

/**
 * The deltas and flags each entry of a track should have, in entry order.
 * Each entry moves toward the next. The last one moves back toward the first
 * over the frames left before the KEY loops, or holds when it is at the end:
 * that is what most retail KEYs do (5,417 of 5,417 segments between entries,
 * and the last entries of 60 Kyle KEYs, either way).
 */
std::vector<KeyEntry> expectedDeltas(const KeyNode& node, const double lastFrame)
{
    std::vector<std::size_t> order(node.entries.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
        [&](const std::size_t a, const std::size_t b) { return node.entries[a].frame < node.entries[b].frame; });
    std::vector<KeyEntry> out = node.entries;
    for (std::size_t k = 0; k < order.size(); ++k)
    {
        KeyEntry& entry = out[order[k]];
        entry.dPosition = TVector{};
        entry.dRotation = TVector{};
        const bool last = k + 1 == order.size();
        const KeyEntry& next = node.entries[order[last ? 0 : k + 1]];
        const double frames = last ? lastFrame - entry.frame + next.frame : next.frame - entry.frame;
        const bool holds = last && (order.size() < 2 || entry.frame >= lastFrame - 1.0);
        if (frames > 0.0 && !holds)
        {
            entry.dPosition = slope(entry.position, next.position, frames);
            entry.dRotation = angularSlope(entry.rotation, next.rotation, frames);
        }
        entry.flags = (zero(entry.dPosition) ? 0u : 1u) | (zero(entry.dRotation) ? 0u : 2u);
    }
    return out;
}

} // namespace

std::vector<HierarchyIssue> validateModelHierarchy(const Model3do& model)
{
    std::vector<HierarchyIssue> out;
    if (model.nodes.empty())
    {
        out.push_back(issue("error", "noHierarchy", "The model has no hierarchy; the engine needs at least one node."));
        return out;
    }
    std::int32_t roots = 0;
    std::set<std::string> names;
    std::vector<bool> meshUsed(model.meshes.size(), false);
    for (std::size_t i = 0; i < model.nodes.size(); ++i)
    {
        const Model3doNode& node = model.nodes[i];
        const auto index = static_cast<std::int32_t>(i);
        if (node.parent == -1)
        {
            ++roots;
            if (i != 0)
                out.push_back(issue("error", "rootNotFirst", nodeLabel(model, index) + " has no parent, but only node 0 may be the root.", index));
        }
        else if (!validNode(model, node.parent) || node.parent == index)
            out.push_back(issue("error", "badParent", nodeLabel(model, index) + " has parent " + std::to_string(node.parent)
                + ", which isn't another node.", index));
        else if (isUnder(model, node.parent, index))
            out.push_back(issue("error", "cycle", nodeLabel(model, index) + " is its own ancestor.", index));
        if (node.mesh < -1 || node.mesh >= static_cast<std::int32_t>(model.meshes.size()))
            out.push_back(issue("error", "badMesh", nodeLabel(model, index) + " shows mesh " + std::to_string(node.mesh)
                + ", which the model doesn't have.", index));
        else if (node.mesh >= 0)
            meshUsed[static_cast<std::size_t>(node.mesh)] = true;
        if (node.name.empty() || node.name.size() > kMaxModelNodeNameLength)
            out.push_back(issue("error", "nodeNameLength", "Node " + std::to_string(i) + "'s name must have 1 to 31 characters.", index));
        std::string lower = node.name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!node.name.empty() && lower.rfind("$$$", 0) != 0 && !names.insert(lower).second)
            out.push_back(issue("warning", "duplicateName", nodeLabel(model, index)
                + " has the same name as another node, so a KEY that names it is ambiguous.", index));
        if (!finite(node.position) || !finite(node.rotation) || !finite(node.pivot))
            out.push_back(issue("error", "badTransform", nodeLabel(model, index) + " has a position, rotation or pivot that isn't a usable number.", index));
    }
    if (model.nodes.front().parent != -1)
        out.push_back(issue("error", "noRoot", "Node 0 must be the root (parent -1).", 0));
    for (std::size_t m = 0; m < meshUsed.size(); ++m)
    {
        if (!meshUsed[m])
            out.push_back(issue("warning", "unusedMesh", "Mesh " + std::to_string(m) + " (" + model.meshes[m].name
                + ") isn't shown by any node, so the engine never draws it."));
    }
    (void)roots;
    return out;
}

std::vector<HierarchyRow> modelHierarchyRows(const Model3do& model)
{
    std::vector<HierarchyRow> rows;
    std::vector<bool> seen(model.nodes.size(), false);
    const std::function<void(std::int32_t, std::int32_t)> visit = [&](const std::int32_t node, const std::int32_t depth) {
        if (!validNode(model, node) || seen[static_cast<std::size_t>(node)]) return;
        seen[static_cast<std::size_t>(node)] = true;
        rows.push_back({ node, depth });
        for (std::size_t i = 0; i < model.nodes.size(); ++i)
        {
            if (model.nodes[i].parent == node) visit(static_cast<std::int32_t>(i), depth + 1);
        }
    };
    for (std::size_t i = 0; i < model.nodes.size(); ++i)
    {
        if (!validNode(model, model.nodes[i].parent)) visit(static_cast<std::int32_t>(i), 0);
    }
    for (std::size_t i = 0; i < model.nodes.size(); ++i)
    {
        if (!seen[i]) rows.push_back({ static_cast<std::int32_t>(i), 0 });   // in a cycle
    }
    return rows;
}

std::optional<HierarchyIssue> renameModelNode(Model3do& model, const std::int32_t node, const std::string_view name)
{
    if (!validNode(model, node)) return refuse("nodeOutOfRange", "Node " + std::to_string(node) + " doesn't exist.", node);
    if (auto problem = checkName(name)) return problem;
    model.nodes[static_cast<std::size_t>(node)].name = std::string(name);
    return std::nullopt;
}

std::optional<HierarchyIssue> reparentModelNode(Model3do& model, const std::int32_t node, const std::int32_t parent)
{
    if (!validNode(model, node)) return refuse("nodeOutOfRange", "Node " + std::to_string(node) + " doesn't exist.", node);
    if (node == 0) return refuse("rootFixed", "Node 0 is the root and stays the root.", node);
    if (!validNode(model, parent)) return refuse("nodeOutOfRange", "Parent " + std::to_string(parent) + " doesn't exist.", node);
    if (isUnder(model, parent, node))
        return refuse("cycle", nodeLabel(model, node) + " can't go under " + nodeLabel(model, parent) + ", which is below it.", node);
    model.nodes[static_cast<std::size_t>(node)].parent = parent;
    return std::nullopt;
}

std::optional<HierarchyIssue> setModelNodeTransform(Model3do& model, const std::int32_t node, const std::optional<TVector>& position,
    const std::optional<TVector>& rotation, const std::optional<TVector>& pivot)
{
    if (!validNode(model, node)) return refuse("nodeOutOfRange", "Node " + std::to_string(node) + " doesn't exist.", node);
    for (const auto* v : { &position, &rotation, &pivot })
    {
        if (*v && !finite(**v)) return refuse("badTransform", "Positions, rotations and pivots must be finite and under a million.", node);
    }
    Model3doNode& n = model.nodes[static_cast<std::size_t>(node)];
    if (position) n.position = *position;
    if (rotation) n.rotation = *rotation;
    if (pivot) n.pivot = *pivot;
    return std::nullopt;
}

std::optional<HierarchyIssue> addModelNode(Model3do& model, const std::int32_t parent, const std::int32_t mesh,
    const std::string_view name, std::int32_t* added)
{
    if (!validNode(model, parent) && !(parent == -1 && model.nodes.empty()))
        return refuse("nodeOutOfRange", "Parent " + std::to_string(parent) + " doesn't exist.");
    if (mesh < -1 || mesh >= static_cast<std::int32_t>(model.meshes.size()))
        return refuse("badMesh", "Mesh " + std::to_string(mesh) + " doesn't exist.");
    if (auto problem = checkName(name)) return problem;
    Model3doNode node;
    node.id = static_cast<std::int32_t>(model.nodes.size());
    node.parent = parent;
    node.mesh = mesh;
    node.name = std::string(name);
    model.nodes.push_back(node);
    if (added) *added = node.id;
    return std::nullopt;
}

std::optional<HierarchyIssue> removeModelNode(Model3do& model, const std::int32_t node)
{
    if (!validNode(model, node)) return refuse("nodeOutOfRange", "Node " + std::to_string(node) + " doesn't exist.", node);
    if (node == 0) return refuse("rootFixed", "Node 0 is the root and can't be removed.", node);
    if (std::any_of(model.nodes.begin(), model.nodes.end(), [&](const Model3doNode& n) { return n.parent == node; }))
        return refuse("hasChildren", nodeLabel(model, node) + " has children; move or remove them first.", node);
    model.nodes.erase(model.nodes.begin() + node);
    for (std::size_t i = 0; i < model.nodes.size(); ++i)
    {
        Model3doNode& n = model.nodes[i];
        n.id = static_cast<std::int32_t>(i);
        if (n.parent > node) --n.parent;
    }
    return std::nullopt;
}

KeyFile keyFromModel(const Model3do& model, const std::int32_t frames, const double fps, const std::uint32_t flags)
{
    KeyFile key;
    key.flags = flags;
    key.frames = frames;
    key.fps = fps;
    key.type = 0xffff;
    key.joints = static_cast<std::int32_t>(model.nodes.size());
    for (std::size_t i = 0; i < model.nodes.size(); ++i)
    {
        KeyNode node;
        node.meshName = model.nodes[i].name;
        node.nodeIndex = static_cast<std::int32_t>(i);
        KeyEntry rest;
        rest.frame = 0.0;
        rest.position = model.nodes[i].position;
        rest.rotation = model.nodes[i].rotation;
        node.entries.push_back(rest);
        key.nodes.push_back(std::move(node));
    }
    return key;
}

std::size_t keyComputeDeltas(KeyFile& key)
{
    std::size_t changed = 0;
    for (KeyNode& node : key.nodes)
    {
        const std::vector<KeyEntry> expected = expectedDeltas(node, static_cast<double>(key.frames));
        for (std::size_t i = 0; i < node.entries.size(); ++i)
        {
            KeyEntry& entry = node.entries[i];
            if (entry.flags != expected[i].flags || !near(entry.dPosition, expected[i].dPosition)
                || !near(entry.dRotation, expected[i].dRotation))
                ++changed;
            entry.flags = expected[i].flags;
            entry.dPosition = expected[i].dPosition;
            entry.dRotation = expected[i].dRotation;
        }
    }
    return changed;
}

std::vector<HierarchyIssue> validateKeyForModel(const KeyFile& key, const Model3do& model)
{
    std::vector<HierarchyIssue> out;
    if (key.joints >= 0 && key.joints < static_cast<std::int32_t>(model.nodes.size()))
        out.push_back(issue("warning", "jointsShort", "The KEY declares " + std::to_string(key.joints) + " joints, fewer than the model's "
            + std::to_string(model.nodes.size()) + " nodes."));
    std::set<std::int32_t> seen;
    for (std::size_t n = 0; n < key.nodes.size(); ++n)
    {
        const KeyNode& node = key.nodes[n];
        const std::int32_t joint = node.nodeIndex >= 0 ? node.nodeIndex : static_cast<std::int32_t>(n);
        const std::string track = "track " + std::to_string(n) + " (" + node.meshName + ")";
        if (!validNode(model, joint))
        {
            out.push_back(issue("error", "badJoint", track + " animates joint " + std::to_string(joint) + ", but the model has only "
                + std::to_string(model.nodes.size()) + " nodes.", joint));
            continue;
        }
        if (!seen.insert(joint).second)
            out.push_back(issue("error", "duplicateJoint", track + " animates joint " + std::to_string(joint) + " a second time.", joint));
        if (!sed::text::iequalsAscii(node.meshName, model.nodes[static_cast<std::size_t>(joint)].name))
            out.push_back(issue("warning", "nameMismatch", track + " animates joint " + std::to_string(joint) + ", which the model calls "
                + model.nodes[static_cast<std::size_t>(joint)].name + "; check it is the same part.", joint));
        const std::vector<KeyEntry> expected = expectedDeltas(node, static_cast<double>(key.frames));
        std::size_t lastEntry = 0;
        for (std::size_t e = 1; e < node.entries.size(); ++e)
        {
            if (node.entries[e].frame >= node.entries[lastEntry].frame) lastEntry = e;
        }
        bool stale = false;
        for (std::size_t e = 0; e < node.entries.size(); ++e)
        {
            const KeyEntry& entry = node.entries[e];
            if (key.frames > 0 && entry.frame > key.frames)
                out.push_back(issue("error", "beyondEnd", track + " has an entry at frame " + std::to_string(entry.frame)
                    + ", after the KEY's last frame " + std::to_string(key.frames) + ".", joint));
            const TVector moves = effective(entry.dPosition, (entry.flags & 1u) != 0);
            const TVector turns = effective(entry.dRotation, (entry.flags & 2u) != 0);
            // The last entry may hold or loop back; retail KEYs do both.
            if (e == lastEntry && zero(moves) && zero(turns)) continue;
            if (e == lastEntry && node.entries.size() > 1)
            {
                const KeyEntry& first = node.entries[expectedFirst(node)];
                const double span = static_cast<double>(key.frames) - entry.frame + first.frame;
                if (span > 0.0 && near(moves, slope(entry.position, first.position, span))
                    && near(turns, angularSlope(entry.rotation, first.rotation, span)))
                    continue;
            }
            stale = stale || !near(moves, expected[e].dPosition) || !near(turns, expected[e].dRotation);
        }
        if (stale)
            out.push_back(issue("warning", "staleDeltas", track + "'s deltas don't lead to its next entries, so the engine won't move it "
                "as the entries say; recompute them.", joint));
    }
    return out;
}

ModelKeyPlayback::ModelKeyPlayback(const Model3do &model, KeyFile key) : nodes_(model.nodes), key_(std::move(key))
{
    if (model.hierarchyTransformsApplied || !key_.serializationSafe || key_.frames < 1 || !std::isfinite(key_.fps) || key_.fps <= 0.0)
        throw std::runtime_error("Playback requires raw model meshes and a complete KEY with positive timing.");
    const auto hierarchy = validateModelHierarchy(model);
    if (const auto issue = std::ranges::find(hierarchy, std::string("error"), &HierarchyIssue::severity); issue != hierarchy.end())
        throw std::runtime_error(issue->message);
    for (const auto &issue : validateKeyForModel(key_, model))
        if (issue.severity == "error" || issue.code == "nameMismatch" || issue.code == "beyondEnd") throw std::runtime_error(issue.message);
    order_ = modelHierarchyRows(model);
    for (auto &track : key_.nodes)
        std::stable_sort(track.entries.begin(), track.entries.end(), [](const auto &a, const auto &b) { return a.frame < b.frame; });
}

std::vector<ModelPoseJoint> ModelKeyPlayback::sample(double frame, const bool loop) const
{
    if (!std::isfinite(frame)) throw std::runtime_error("A preview frame must be finite.");
    const double length = key_.frames;
    if (loop)
    {
        frame = std::fmod(frame, length);
        if (frame < 0.0) frame += length;
    }
    else
        frame = std::clamp(frame, 0.0, length);
    std::vector<TVector> positions, rotations;
    for (const auto &node : nodes_)
    {
        positions.push_back(node.position);
        rotations.push_back(node.rotation);
    }
    for (std::size_t i = 0; i < key_.nodes.size(); ++i)
    {
        const auto &track = key_.nodes[i];
        const auto after = std::ranges::upper_bound(track.entries, frame, {}, &KeyEntry::frame);
        if (after == track.entries.begin()) continue;
        const auto &entry = *std::prev(after);
        const auto joint = track.nodeIndex < 0 ? i : static_cast<std::size_t>(track.nodeIndex);
        const double delta = frame - entry.frame;
        positions.at(joint) = entry.position + ((entry.flags & 1u) ? entry.dPosition * delta : TVector{});
        rotations.at(joint) = entry.rotation + ((entry.flags & 2u) ? entry.dRotation * delta : TVector{});
    }
    std::vector<TMatrix43> matrices(nodes_.size());
    std::vector<ModelPoseJoint> pose(nodes_.size());
    for (const auto &row : order_)
    {
        const auto i = static_cast<std::size_t>(row.node);
        const auto &node = nodes_[i];
        auto matrix = TMatrix43::Create(positions[i], rotations[i]);
        if (node.parent >= 0)
        {
            const auto &parent = matrices.at(static_cast<std::size_t>(node.parent));
            matrix.rvec = parent.TransformVector(matrix.rvec);
            matrix.lvec = parent.TransformVector(matrix.lvec);
            matrix.uvec = parent.TransformVector(matrix.uvec);
            matrix.dvec = parent.TransformPoint(matrix.dvec);
        }
        matrices[i] = matrix;
        auto mesh = matrix;
        mesh.dvec = matrix.TransformPoint(node.pivot);
        pose[i] = {matrix.dvec, rotations[i], mesh};
    }
    return pose;
}

} // namespace sed::assets
