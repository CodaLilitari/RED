#pragma once

#include "sed/assets/3do.hpp"
#include "sed/assets/key.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sed::assets
{

/**
 * 3DO hierarchy and KEY authoring.
 *
 * A 3DO's hierarchy is a tree of nodes, each with an optional mesh, a parent,
 * and a position, rotation and pivot relative to its parent. A KEY animates
 * that tree: its NODES name hierarchy nodes by index (the engine's joint
 * number) and give each node's position and rotation over time, with per-frame
 * deltas the engine interpolates with.
 *
 * Edits work on a model loaded with `applyHierarchyTransforms = false`, so it
 * saves back unchanged apart from the edit. Each edit refuses, leaving the
 * model unchanged, with a code and message. The delta rule follows the
 * Blender-Sith add-on's KEY exporter (GPL-3.0): the motion of a segment is
 * stored on its first entry, as change per frame, and the entry's flags say
 * whether position (1), orientation (2) or both change.
 */

inline constexpr std::size_t kMaxModelNodeNameLength = 31;

struct HierarchyIssue
{
    /** "error" or "warning". */
    std::string severity{};
    std::string code{};
    std::string message{};
    std::int32_t node{ -1 };
};

/** Problems the engine would trip over: parents, cycles, meshes, the root, names. */
std::vector<HierarchyIssue> validateModelHierarchy(const Model3do& model);

/** Each node's depth-first position and depth, for listing the tree. */
struct HierarchyRow
{
    std::int32_t node{};
    std::int32_t depth{};
};
std::vector<HierarchyRow> modelHierarchyRows(const Model3do& model);

std::optional<HierarchyIssue> renameModelNode(Model3do& model, std::int32_t node, std::string_view name);
/** Moves a node under another; refuses the root, a cycle and an unknown parent. */
std::optional<HierarchyIssue> reparentModelNode(Model3do& model, std::int32_t node, std::int32_t parent);
std::optional<HierarchyIssue> setModelNodeTransform(Model3do& model, std::int32_t node, const std::optional<TVector>& position,
    const std::optional<TVector>& rotation, const std::optional<TVector>& pivot);
/** Appends a node under `parent` showing `mesh` (-1 for none); returns its index. */
std::optional<HierarchyIssue> addModelNode(Model3do& model, std::int32_t parent, std::int32_t mesh, std::string_view name,
    std::int32_t* added = nullptr);
/** Removes a node without children (not the root) and renumbers the rest. */
std::optional<HierarchyIssue> removeModelNode(Model3do& model, std::int32_t node);

/**
 * A KEY for the model: one track per hierarchy node, named after it, with a
 * frame-0 entry at its rest pose. Add entries to animate it, then compute the
 * deltas.
 */
KeyFile keyFromModel(const Model3do& model, std::int32_t frames, double fps, std::uint32_t flags = 0);

/**
 * Recomputes every entry's deltas and flags from the next entry in its track;
 * the last entry of a track holds still. Returns how many entries changed.
 */
std::size_t keyComputeDeltas(KeyFile& key);

/** A KEY against the model it animates: joints, names, frames and stale deltas. */
std::vector<HierarchyIssue> validateKeyForModel(const KeyFile& key, const Model3do& model);

} // namespace sed::assets
