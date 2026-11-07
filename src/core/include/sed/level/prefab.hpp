#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "sed/level.hpp"
#include "sed/level/clipboard.hpp"
#include "sed/level/uv.hpp"
#include "sed/level/workbench.hpp"

namespace sed::level
{

/** Prefab format version written by `makePrefab`. */
inline constexpr std::uint32_t kPrefabVersion = 1;

/** One boundary face of a prefab: a surface, in payload-local
 * (sector, surface) indices, that has no adjoin inside the prefab —
 * either it never had one or its adjoin led outside the captured
 * sectors and was severed into a solid face on capture. An insertion
 * host can offer these faces as attachment points. */
struct PrefabAttachment
{
    std::int32_t sector{ -1 };
    std::int32_t surface{ -1 };

    constexpr bool operator==(const PrefabAttachment&) const noexcept = default;
};

/** A versioned, hash-bound level fragment (E-04 typed prefab).
 *
 * The payload is the sector clipboard form: internal adjoins are
 * payload-local, layers travel by stable name, and the anchor is the
 * primary sector's centre. `contentSha256Hex` is the lowercase hex
 * SHA-256 of the canonical clipboard encoding of `payload`, so any
 * edit to the captured geometry, layers, or anchor is detected
 * before insertion. */
struct Prefab
{
    std::uint32_t version{ kPrefabVersion };
    ProjectType sourceProject{ ProjectType::JKDF2 };
    SectorClipboardPayload payload{};
    std::vector<PrefabAttachment> attachments{};
    std::string contentSha256Hex{};
};

/** Captures the referenced sectors as a prefab.
 *
 * Sectors are taken valid, sorted, and unique, exactly as
 * `copySectorsToClipboard` takes them; an empty or invalid selection
 * produces no prefab. Every payload face without an internal adjoin
 * is recorded in `attachments` in (sector, surface) order. */
[[nodiscard]] std::optional<Prefab> makePrefab(
    const Level& level,
    std::span<const std::int32_t> sectors,
    std::int32_t primarySector = -1);

/** Whether a prefab is internally consistent: a supported version, a
 * non-empty payload from the recorded project, attachment faces that
 * address real payload surfaces, and a content hash that matches the
 * payload's canonical encoding. */
[[nodiscard]] bool prefabHasValidHash(const Prefab& prefab);

/** Serializes a prefab as portable text (E-04 file format).
 *
 * The text carries the format version, source project, content hash,
 * boundary-face list, and the canonical clipboard encoding of the
 * payload as hex, so a prefab survives across editor and agent
 * processes. Empty when the prefab fails `prefabHasValidHash` or its
 * payload cannot be encoded. */
[[nodiscard]] std::optional<std::string> savePrefabText(const Prefab& prefab);

/** Parses text written by `savePrefabText`.
 *
 * Refuses (empty result) on a wrong header, an unsupported version or
 * project, malformed hex, an undecodable payload, or any prefab that
 * fails `prefabHasValidHash` — a tampered file never loads. */
[[nodiscard]] std::optional<Prefab> loadPrefabFromText(std::string_view text);

/** Inserts a prefab into `level`, translating its anchor to `target`
 * when given (otherwise authored coordinates are kept).
 *
 * Refuses, leaving the Level unchanged, when the prefab fails
 * `prefabHasValidHash` or its source project differs from the
 * Level's project: a fragment never crosses projects silently.
 * Returns the paste result of the underlying clipboard insertion. */
[[nodiscard]] std::optional<EntityPasteResult> insertPrefab(
    Level& level,
    const Prefab& prefab,
    std::optional<TVector> target = std::nullopt,
    const UvTextureDimensionsResolver& resolveDimensions = {});

/** Builds a geometry array (E-03): captures `sectors` as a prefab once
 * and inserts one copy per `computePlacementPositions` position, with
 * each copy's anchor (the primary sector's centre) landing on its
 * layout position. The source sectors stay in place; copies are new
 * sectors appended after them. Refuses, leaving the Level unchanged,
 * for an empty or invalid selection or a layout the placement domain
 * rejects (duplicate positions, counts above 256, degenerate radial
 * shapes). Returns every new sector index in layout order. */
[[nodiscard]] std::optional<std::vector<std::int32_t>> levelBuildSectorArray(
    Level& level,
    std::span<const std::int32_t> sectors,
    const PlacementLayout& layout);

} // namespace sed::level
