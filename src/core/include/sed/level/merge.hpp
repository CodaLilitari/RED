#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sed/level.hpp"

namespace sed::level
{

/** Merges two same-sector surfaces that share one reversed edge and returns the kept surface reference. */
std::optional<SurfaceRef> levelMergeSurfaces(Level& level, SurfaceRef surf1, SurfaceRef surf2);

struct SurfaceMergeBatchResult
{
    std::vector<SurfaceRef> selection{};
    std::size_t mergeCount{};
};

// Coordinates belong to the before/after snapshots of one merge step, never to a persistent ID.
struct SurfaceMergeAlias
{
    SurfaceRef source{};
    SurfaceRef survivor{};
    std::int32_t sourceGlobal{};
    std::int32_t survivorGlobalBefore{};
    std::int32_t survivorGlobalAfter{};
};

struct SurfaceMergeStep
{
    SurfaceMergeAlias primary{};
    std::optional<SurfaceMergeAlias> reciprocal{};
};

struct SurfaceMergeBinding
{
    std::size_t cog{};
    std::size_t argument{};
    SurfaceRef before{};
    SurfaceRef after{};
    std::int32_t globalBefore{};
    std::int32_t globalAfter{};
};

struct SurfaceMergeReferenceMap
{
    std::vector<SurfaceMergeStep> steps{};
    std::vector<SurfaceMergeBinding> surfaceBindings{};
};

/** Explains the first reference whose surface correspondence cannot be proved. */
std::optional<std::string> surfaceMergeReferenceBlocker(const Level& level);

enum class SurfaceMergePolicy
{
    keepSurvivorMapping,
    preserveAuthoredSeams,
};

/**
 * Deterministically merges valid adjacent same-sector surfaces to a fixed
 * point. Newly merged polygons are reconsidered, while invalid/cross-sector
 * pairs are skipped. Unmerged surfaces remain selected. Publication is
 * transactional: the input Level is unchanged when no pair can merge. Optional
 * tracked face references follow survivors and shifted face ordinals.
 */
std::optional<SurfaceMergeBatchResult> levelMergeSelectedSurfaces(
    Level& level,
    std::span<const SurfaceRef> surfaces,
    SurfaceMergePolicy policy = SurfaceMergePolicy::keepSurvivorMapping,
    std::span<SurfaceRef> trackedSurfaces = {});

/** Same transaction with an explicit per-step correspondence for this exact operation. */
std::optional<SurfaceMergeBatchResult> levelMergeSelectedSurfaces(
    Level& level, std::span<const SurfaceRef> surfaces, SurfaceMergePolicy policy,
    std::span<SurfaceRef> trackedSurfaces, SurfaceMergeReferenceMap& referenceMap,
    std::string* blocker = nullptr);

/** Finds the unique neighboring surface across an edge and merges it into the selected surface. */
bool levelMergeSurfacesAt(Level& level, SurfaceRef surf, std::int32_t edge);

/** Merges two adjoined sectors, preserving valid external adjoins and returning the kept sector index. */
std::optional<std::int32_t> levelMergeSectors(Level& level, std::int32_t sector, std::int32_t otherSector);

struct SectorMergeBatchResult
{
    std::vector<std::int32_t> selection{};
    std::size_t mergeCount{};
};

/**
 * Deterministically merges valid adjacent selected sectors to a fixed point.
 * Invalid/nonadjacent pairs are skipped and surviving indices are remapped
 * after every deletion. The input Level is unchanged when no pair can merge.
 */
std::optional<SectorMergeBatchResult> levelMergeSelectedSectors(
    Level& level,
    std::span<const std::int32_t> sectors);

/** Merges selected vertices inside one sector and rewrites all affected surface vertex references. */
std::int32_t levelMergeVertices(Level& level, std::span<const VertexRef> vertexSelection);

} // namespace sed::level
