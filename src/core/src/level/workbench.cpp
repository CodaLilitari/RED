#include "sed/level/workbench.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <unordered_set>
#include <utility>
#include <variant>

#include "sed/level/spatial_selection.hpp"
#include "sed/text.hpp"

namespace sed::level
{
namespace
{

WorkbenchFailure failure(std::string code, std::string message)
{
    return WorkbenchFailure{ std::move(code), std::move(message) };
}

bool finiteVector(const TVector& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

bool boundedVector(const TVector& value) noexcept
{
    return finiteVector(value)
        && std::abs(value.x) <= kMaxWorkbenchCoordinate
        && std::abs(value.y) <= kMaxWorkbenchCoordinate
        && std::abs(value.z) <= kMaxWorkbenchCoordinate;
}

bool samePoint(const TVector& a, const TVector& b) noexcept
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool blankText(const std::string_view text) noexcept
{
    return std::all_of(text.begin(), text.end(), [](const char c) noexcept {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    });
}

bool containsSubstringIgnoreCase(
    const std::string_view haystack,
    const std::string_view needle) noexcept
{
    return text::lowerAscii(haystack).find(text::lowerAscii(needle))
        != std::string::npos;
}

// ------------------------------------------------------------- queries

QueryResult queryFailure(WorkbenchFailure refusal)
{
    QueryResult result{};
    result.failure = std::move(refusal);
    return result;
}

std::optional<WorkbenchFailure> validateFilter(
    const WorkbenchCollection collection,
    const ExactQueryFilter& filter)
{
    if (filter.empty())
    {
        return failure(
            "emptyQueryFilter", "filter must contain at least one predicate");
    }
    const auto checkMask = [](const std::optional<FlagMaskPredicate>& mask,
        const char* const name) -> std::optional<WorkbenchFailure> {
        if (mask.has_value() && mask->empty())
        {
            return failure("invalidQueryFilterValue",
                std::string("filter.") + name + " must contain a mask");
        }
        return std::nullopt;
    };
    if (const auto refusal = checkMask(filter.flags, "flags"))
    {
        return refusal;
    }
    if (const auto refusal = checkMask(filter.surfFlags, "surfFlags"))
    {
        return refusal;
    }
    if (const auto refusal = checkMask(filter.faceFlags, "faceFlags"))
    {
        return refusal;
    }
    if (const auto refusal = checkMask(filter.adjoinFlags, "adjoinFlags"))
    {
        return refusal;
    }
    if (filter.layer.has_value() && *filter.layer < 0)
    {
        return failure(
            "invalidQueryFilterValue", "filter.layer must be at least 0");
    }
    if (filter.materialIndex.has_value() && *filter.materialIndex < -1)
    {
        return failure("invalidQueryFilterValue",
            "filter.materialIndex must be at least -1");
    }
    const auto checkText = [](const std::optional<std::string>& text,
        const char* const name) -> std::optional<WorkbenchFailure> {
        if (text.has_value()
            && (text->empty() || text->size() > 127 || blankText(*text)))
        {
            return failure("invalidQueryFilterValue",
                std::string("filter.") + name
                    + " must contain 1 to 127 non-blank characters");
        }
        return std::nullopt;
    };
    if (const auto refusal = checkText(filter.name, "name"))
    {
        return refusal;
    }
    if (const auto refusal = checkText(filter.templateName, "template"))
    {
        return refusal;
    }

    const bool surfaces = collection == WorkbenchCollection::surfaces;
    const bool things = collection == WorkbenchCollection::things
        || collection == WorkbenchCollection::thingContainment;
    if (surfaces
        && (filter.name.has_value() || filter.templateName.has_value()
            || filter.flags.has_value()))
    {
        return failure("unsupportedQueryFilterField",
            "surface filters accept materialIndex, layer, surfFlags, "
            "faceFlags and adjoinFlags only");
    }
    if (things
        && (filter.materialIndex.has_value() || filter.flags.has_value()
            || filter.surfFlags.has_value() || filter.faceFlags.has_value()
            || filter.adjoinFlags.has_value()))
    {
        return failure("unsupportedQueryFilterField",
            "thing filters accept name, template and layer only");
    }
    if (!surfaces && !things
        && (filter.materialIndex.has_value() || filter.name.has_value()
            || filter.templateName.has_value()
            || filter.surfFlags.has_value() || filter.faceFlags.has_value()
            || filter.adjoinFlags.has_value()))
    {
        return failure("unsupportedQueryFilterField",
            "sector and light filters accept layer and flags only");
    }
    return std::nullopt;
}

bool flagsMatch(
    const std::optional<FlagMaskPredicate>& predicate,
    const std::uint32_t bits) noexcept
{
    return !predicate.has_value() || predicate->matches(bits);
}

// ------------------------------------------------------------ batch UV

struct UvPair
{
    double u{};
    double v{};
};

UvPair projectPair(const TVector& relative, const UvProjectionDirection direction)
{
    switch (direction)
    {
    case UvProjectionDirection::positiveX: return { relative.y, relative.z };
    case UvProjectionDirection::negativeX: return { -relative.y, relative.z };
    case UvProjectionDirection::positiveY: return { -relative.x, relative.z };
    case UvProjectionDirection::negativeY: return { relative.x, relative.z };
    case UvProjectionDirection::positiveZ: return { relative.x, relative.y };
    case UvProjectionDirection::negativeZ: return { -relative.x, relative.y };
    }
    return {};
}

TVector newellNormal(const std::vector<TVector>& positions) noexcept
{
    TVector normal{};
    const std::size_t count = positions.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        const TVector& a = positions[i];
        const TVector& b = positions[(i + 1) % count];
        normal.x += (a.y - b.y) * (a.z + b.z);
        normal.y += (a.z - b.z) * (a.x + b.x);
        normal.z += (a.x - b.x) * (a.y + b.y);
    }
    return normal;
}

double cross2(const UvPair a, const UvPair b, const UvPair p) noexcept
{
    return (b.u - a.u) * (p.v - a.v) - (b.v - a.v) * (p.u - a.u);
}

bool onSegment2(const UvPair a, const UvPair b, const UvPair p, const double cross) noexcept
{
    return std::abs(cross) <= 1e-12
        && p.u >= std::min(a.u, b.u) - 1e-12
        && p.u <= std::max(a.u, b.u) + 1e-12
        && p.v >= std::min(a.v, b.v) - 1e-12
        && p.v <= std::max(a.v, b.v) + 1e-12;
}

bool segmentsIntersect2(const UvPair a, const UvPair b, const UvPair c, const UvPair d) noexcept
{
    const double abC = cross2(a, b, c);
    const double abD = cross2(a, b, d);
    const double cdA = cross2(c, d, a);
    const double cdB = cross2(c, d, b);
    if (((abC > 0.0 && abD < 0.0) || (abC < 0.0 && abD > 0.0))
        && ((cdA > 0.0 && cdB < 0.0) || (cdA < 0.0 && cdB > 0.0)))
    {
        return true;
    }
    return onSegment2(a, b, c, abC) || onSegment2(a, b, d, abD)
        || onSegment2(c, d, a, cdA) || onSegment2(c, d, b, cdB);
}

// Simple-polygon check on normalized coordinates: distinct consecutive
// corners, no crossing edges and nonzero signed area.
bool simplePolygon2(const std::vector<UvPair>& points) noexcept
{
    const std::size_t count = points.size();
    if (count < 3)
    {
        return false;
    }
    std::vector<UvPair> local(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        local[i] = { points[i].u - points[0].u, points[i].v - points[0].v };
    }
    double extent = 0.0;
    for (const UvPair& point : local)
    {
        extent = std::max(
            extent, std::max(std::abs(point.u), std::abs(point.v)));
    }
    if (!std::isfinite(extent) || extent <= 0.0)
    {
        return false;
    }
    for (UvPair& point : local)
    {
        point.u /= extent;
        point.v /= extent;
    }
    double area = 0.0;
    for (std::size_t i = 0; i < count; ++i)
    {
        const UvPair a = local[i];
        const UvPair b = local[(i + 1) % count];
        if (std::abs(a.u - b.u) + std::abs(a.v - b.v) <= 1e-12)
        {
            return false;
        }
        area += a.u * b.v - a.v * b.u;
        for (std::size_t j = i + 2; j < count; ++j)
        {
            if (i == 0 && j == count - 1)
            {
                continue;
            }
            if (segmentsIntersect2(a, b, local[j], local[(j + 1) % count]))
            {
                return false;
            }
        }
    }
    return std::isfinite(area) && std::abs(area) > 1e-12;
}

// Validates planarity and polygon simplicity; returns the unit normal.
std::optional<TVector> checkedUnitNormal(
    const std::vector<TVector>& positions,
    std::string& reason) noexcept
{
    const std::size_t count = positions.size();
    std::vector<TVector> relative(count);
    double extent = 0.0;
    for (std::size_t i = 0; i < count; ++i)
    {
        relative[i] = positions[i].Sub(positions[0]);
        extent = std::max(extent,
            std::max({ std::abs(relative[i].x), std::abs(relative[i].y),
                std::abs(relative[i].z) }));
    }
    if (!std::isfinite(extent) || extent <= 0.0)
    {
        reason = "surface has a degenerate extent";
        return std::nullopt;
    }
    for (TVector& point : relative)
    {
        point.Scale(1.0 / extent);
    }
    TVector normal = newellNormal(relative);
    const double length = normal.Length();
    if (!std::isfinite(length) || length <= 1e-12)
    {
        reason = "surface polygon has no usable area";
        return std::nullopt;
    }
    normal.Scale(1.0 / length);
    for (const TVector& point : relative)
    {
        if (std::abs(normal.Dot(point)) > 1e-8)
        {
            reason = "surface is not planar";
            return std::nullopt;
        }
    }
    const double ax = std::abs(normal.x);
    const double ay = std::abs(normal.y);
    const double az = std::abs(normal.z);
    std::vector<UvPair> projected(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (ax >= ay && ax >= az)
        {
            projected[i] = { relative[i].y, relative[i].z };
        }
        else if (ay >= az)
        {
            projected[i] = { relative[i].x, relative[i].z };
        }
        else
        {
            projected[i] = { relative[i].x, relative[i].y };
        }
    }
    if (!simplePolygon2(projected))
    {
        reason = "surface polygon is degenerate or self-intersecting";
        return std::nullopt;
    }
    return normal;
}

PreparedBatchUv batchUvFailure(WorkbenchFailure refusal)
{
    PreparedBatchUv prepared{};
    prepared.failure = std::move(refusal);
    return prepared;
}

template <typename Calculate>
PreparedBatchUv prepareBatchUv(
    const Level& level,
    const std::span<const SurfaceRef> surfaces,
    Calculate&& calculate)
{
    if (surfaces.empty())
    {
        return batchUvFailure(failure(
            "emptyBatchUvSelection", "select at least one surface"));
    }
    if (surfaces.size() > kMaxBatchUvSurfaces)
    {
        return batchUvFailure(failure("batchUvSurfaceLimit",
            "a batch UV operation addresses at most 256 surfaces"));
    }
    for (std::size_t i = 0; i < surfaces.size(); ++i)
    {
        for (std::size_t j = i + 1; j < surfaces.size(); ++j)
        {
            if (surfaces[i] == surfaces[j])
            {
                return batchUvFailure(failure("duplicateBatchUvSurface",
                    "batch UV surfaces must be unique"));
            }
        }
    }

    PreparedBatchUv prepared{};
    std::size_t corners = 0;
    for (const SurfaceRef& ref : surfaces)
    {
        if (ref.sector < 0
            || static_cast<std::size_t>(ref.sector) >= level.sectors.size()
            || ref.surface < 0
            || static_cast<std::size_t>(ref.surface)
                >= level.sectors[static_cast<std::size_t>(ref.sector)]
                       .surfaces.size())
        {
            return batchUvFailure(failure("batchUvSurfaceOutOfRange",
                "batch UV surface address is outside the level"));
        }
        const Sector& sector =
            level.sectors[static_cast<std::size_t>(ref.sector)];
        const Surface& surface =
            sector.surfaces[static_cast<std::size_t>(ref.surface)];
        if (surface.vertices.size() < 3
            || surface.vertices.size() != surface.texVertices.size())
        {
            return batchUvFailure(failure("invalidBatchUvSurface",
                "batch UV surfaces need matching corners, at least three"));
        }
        corners += surface.vertices.size();
        if (corners > kMaxBatchUvCorners)
        {
            return batchUvFailure(failure("batchUvCornerLimit",
                "a batch UV operation addresses at most 4096 corners"));
        }

        BatchUvSurfacePlan plan{};
        plan.ref = ref;
        plan.vertexIndices = surface.vertices;
        plan.positions.reserve(surface.vertices.size());
        for (const std::int32_t vertexIndex : surface.vertices)
        {
            if (vertexIndex < 0
                || static_cast<std::size_t>(vertexIndex)
                    >= sector.vertices.size())
            {
                return batchUvFailure(failure("invalidBatchUvSurface",
                    "batch UV surface references a missing vertex"));
            }
            const Vertex& vertex =
                sector.vertices[static_cast<std::size_t>(vertexIndex)];
            const TVector position{ vertex.x, vertex.y, vertex.z };
            if (!finiteVector(position))
            {
                return batchUvFailure(failure("invalidBatchUvInput",
                    "batch UV vertex positions must be finite"));
            }
            plan.positions.push_back(position);
        }
        plan.before = surface.texVertices;
        for (const TexVertex& texVertex : plan.before)
        {
            if (!std::isfinite(texVertex.u) || !std::isfinite(texVertex.v))
            {
                return batchUvFailure(failure("invalidBatchUvInput",
                    "existing surface UVs must be finite"));
            }
        }
        std::string reason{};
        const std::optional<TVector> normal =
            checkedUnitNormal(plan.positions, reason);
        if (!normal.has_value())
        {
            return batchUvFailure(
                failure("batchUvInvalidGeometry", std::move(reason)));
        }

        plan.after.reserve(plan.before.size());
        for (std::size_t i = 0; i < plan.before.size(); ++i)
        {
            const UvPair computed = calculate(
                plan.positions[i], *normal,
                UvPair{ plan.before[i].u, plan.before[i].v });
            if (!std::isfinite(computed.u) || !std::isfinite(computed.v)
                || std::abs(computed.u) > kMaxWorkbenchCoordinate
                || std::abs(computed.v) > kMaxWorkbenchCoordinate)
            {
                return batchUvFailure(failure("batchUvOutputOutOfRange",
                    "batch UV results must be finite within +/-1000000"));
            }
            TexVertex after = plan.before[i];
            after.u = static_cast<float>(computed.u);
            after.v = static_cast<float>(computed.v);
            if (!std::isfinite(after.u) || !std::isfinite(after.v))
            {
                return batchUvFailure(failure("batchUvOutputOutOfRange",
                    "batch UV results must be finite within +/-1000000"));
            }
            plan.after.push_back(after);
        }
        prepared.surfaces.push_back(std::move(plan));
    }
    prepared.cornerCount = corners;
    return prepared;
}

bool sameUvBits(const float a, const float b) noexcept
{
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

// ---------------------------------------------------------- placements

PlacementPositions placementFailure(WorkbenchFailure refusal)
{
    PlacementPositions result{};
    result.failure = std::move(refusal);
    return result;
}

std::optional<WorkbenchFailure> checkArrayCounts(
    const std::size_t count, const std::size_t patchOutputsBefore)
{
    if (count == 0)
    {
        return failure(
            "emptyPlacementArray", "an array needs at least one output");
    }
    if (count > kMaxPlacementArrayOutputs)
    {
        return failure("placementArrayLimit",
            "an array creates at most 256 outputs");
    }
    if (patchOutputsBefore > kMaxPlacementPatchOutputs
        || count > kMaxPlacementPatchOutputs - patchOutputsBefore)
    {
        return failure("placementPatchLimit",
            "a patch creates at most 1024 array outputs");
    }
    return std::nullopt;
}

std::optional<WorkbenchFailure> checkDistinctBounded(
    const std::span<const TVector> positions)
{
    for (const TVector& position : positions)
    {
        if (!boundedVector(position))
        {
            return failure("invalidPlacementInput",
                "array positions must be finite within +/-1000000");
        }
    }
    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        for (std::size_t j = i + 1; j < positions.size(); ++j)
        {
            if (samePoint(positions[i], positions[j]))
            {
                return failure("duplicatePlacementPosition",
                    "array positions must be distinct");
            }
        }
    }
    return std::nullopt;
}

bool validLayer(const Level& level, const std::int32_t layer) noexcept
{
    const std::size_t count = std::max<std::size_t>(level.layers.size(), 1);
    return layer >= 0 && static_cast<std::size_t>(layer) < count;
}

bool validPlacementGame(const Level& level) noexcept
{
    return level.kind == ProjectType::JKDF2
        || level.kind == ProjectType::MOTS;
}

// Smallest signed inward distance to the sector's face planes, or no
// value when a face cannot define one. Stored normals point inward.
std::optional<double> inwardDistance(
    const Sector& sector, const TVector& point) noexcept
{
    if (sector.surfaces.empty())
    {
        return std::nullopt;
    }
    double smallest = 0.0;
    bool first = true;
    for (const Surface& surface : sector.surfaces)
    {
        if (surface.vertices.empty())
        {
            return std::nullopt;
        }
        const std::int32_t index = surface.vertices.front();
        if (index < 0
            || static_cast<std::size_t>(index) >= sector.vertices.size())
        {
            return std::nullopt;
        }
        TVector normal = surface.normal;
        if (!finiteVector(normal))
        {
            return std::nullopt;
        }
        const double length = normal.Length();
        if (!(length > 0.0) || !std::isfinite(length))
        {
            return std::nullopt;
        }
        normal.Scale(1.0 / length);
        const Vertex& vertex = sector.vertices[static_cast<std::size_t>(index)];
        const double distance =
            normal.Dot(point.Sub(TVector{ vertex.x, vertex.y, vertex.z }));
        if (!std::isfinite(distance))
        {
            return std::nullopt;
        }
        if (first || distance < smallest)
        {
            smallest = distance;
            first = false;
        }
    }
    return smallest;
}

// Resolves one point to its strictly containing sector index.
std::variant<std::int32_t, WorkbenchFailure> resolvePlacementSector(
    const Level& level,
    const TVector& point,
    const std::optional<std::int32_t> explicitSector)
{
    if (explicitSector.has_value())
    {
        const Sector& sector =
            level.sectors[static_cast<std::size_t>(*explicitSector)];
        const std::optional<double> distance = inwardDistance(sector, point);
        if (!distance.has_value())
        {
            return failure("placementSectorNotClassifiable",
                "the explicit sector cannot classify placements safely");
        }
        if (*distance < -kSpatialDistanceTolerance)
        {
            return failure("placementOutsideSector",
                "array position lies outside the explicit sector");
        }
        if (*distance <= kSpatialDistanceTolerance)
        {
            return failure("placementOnSectorBoundary",
                "array position lies on the explicit sector boundary");
        }
        return *explicitSector;
    }

    const SelectionContext context{};
    const SpatialSelection selection = selectSectorAtPoint(level, context, point);
    if (selection.match == SpatialMatch::cannotClassify)
    {
        return failure("placementSectorNotClassifiable",
            selection.diagnostic.empty()
                ? "a sector cannot classify placements safely"
                : selection.diagnostic);
    }
    if (selection.match == SpatialMatch::none)
    {
        return failure("placementOutsideSector",
            "array position lies outside every sector");
    }
    if (selection.match == SpatialMatch::ambiguous)
    {
        return failure("placementAmbiguousSector",
            "array position lies in more than one sector; "
            "supply an explicit sector");
    }
    const Sector& sector =
        level.sectors[static_cast<std::size_t>(selection.target.sector)];
    const std::optional<double> distance = inwardDistance(sector, point);
    if (!distance.has_value() || *distance <= kSpatialDistanceTolerance)
    {
        return failure("placementOnSectorBoundary",
            "array position lies on a sector boundary");
    }
    return selection.target.sector;
}

bool tokenTextValid(const std::string_view text, const std::size_t maximum) noexcept
{
    if (text.empty() || text.size() > maximum)
    {
        return false;
    }
    for (const char c : text)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (byte <= 32 || byte >= 127 || c == '#' || c == '"')
        {
            return false;
        }
    }
    return true;
}

std::string generatedThingName(const std::string_view prefix, const std::size_t index)
{
    std::string name{ prefix };
    name += '_';
    const std::string digits = std::to_string(index);
    if (digits.size() < 3)
    {
        name.append(3 - digits.size(), '0');
    }
    name += digits;
    return name;
}

} // namespace

std::optional<WorkbenchFailure> validatePlacementPointInsideSector(
    const Level& level, const TVector& point, const std::int32_t sectorIndex)
{
    if ( !boundedVector(point) )
    {
        return failure("invalidPlacementInput",
            "position must be finite within +/-1000000");
    }
    if ( sectorIndex < 0 || static_cast<std::size_t>(sectorIndex) >= level.sectors.size() )
    {
        return failure("placementSectorOutOfRange",
            "the explicit sector is outside the level");
    }
    const auto result = resolvePlacementSector(level, point, sectorIndex);
    if ( const auto* refused = std::get_if<WorkbenchFailure>(&result) )
    {
        return *refused;
    }
    return std::nullopt;
}

std::int32_t surfaceMaterialIndex(
    const Level& level, const Surface& surface) noexcept
{
    const std::string_view name = text::trimView(surface.material);
    if (name.empty() || text::iequalsAscii(name, "none"))
    {
        // No material name is the untextured state, whatever stale slot a
        // default-constructed surface happens to carry.
        return -1;
    }
    for (std::size_t i = 0; i < level.materials.size(); ++i)
    {
        if (text::iequalsAscii(text::trimView(level.materials[i]), name))
        {
            return static_cast<std::int32_t>(i);
        }
    }
    return -2;
}

QueryResult queryWorkbench(
    const Level& level,
    const WorkbenchCollection collection,
    const std::optional<std::int32_t> sectorIndex,
    const std::optional<ExactQueryFilter>& filter,
    const std::string_view search,
    const QueryPageRequest& page)
{
    if (page.limit == 0)
    {
        return queryFailure(failure(
            "invalidQueryPage", "query limit must be at least 1"));
    }
    const bool surfaces = collection == WorkbenchCollection::surfaces;
    if (surfaces && !sectorIndex.has_value())
    {
        return queryFailure(failure("querySectorRequired",
            "surface queries require one explicit sector"));
    }
    if (!surfaces && sectorIndex.has_value())
    {
        return queryFailure(failure("querySectorNotApplicable",
            "only surface queries take a sector"));
    }
    if (surfaces
        && (*sectorIndex < 0
            || static_cast<std::size_t>(*sectorIndex) >= level.sectors.size()))
    {
        return queryFailure(failure("querySectorOutOfRange",
            "query sector is outside the level"));
    }
    if (filter.has_value())
    {
        if (const auto refusal = validateFilter(collection, *filter))
        {
            return queryFailure(*refusal);
        }
    }
    const bool things = collection == WorkbenchCollection::things
        || collection == WorkbenchCollection::thingContainment;
    if (!search.empty())
    {
        if (!things)
        {
            return queryFailure(failure("unsupportedQuerySearch",
                "search is only valid for thing queries"));
        }
        if (search.size() > 127 || blankText(search))
        {
            return queryFailure(failure("invalidQueryFilterValue",
                "search must contain 1 to 127 non-blank characters"));
        }
    }

    const ExactQueryFilter empty{};
    const ExactQueryFilter& active = filter.value_or(empty);
    const bool hasFilter = filter.has_value();
    const auto layerMatches = [&active, hasFilter](
        const std::int32_t layer) noexcept {
        return !hasFilter || !active.layer.has_value()
            || *active.layer == layer;
    };

    std::vector<std::int32_t> matches{};
    if (surfaces)
    {
        const Sector& sector =
            level.sectors[static_cast<std::size_t>(*sectorIndex)];
        for (std::size_t i = 0; i < sector.surfaces.size(); ++i)
        {
            const Surface& surface = sector.surfaces[i];
            if (hasFilter && active.materialIndex.has_value()
                && surfaceMaterialIndex(level, surface) != *active.materialIndex)
            {
                continue;
            }
            if (!layerMatches(sector.layer))
            {
                continue;
            }
            if (hasFilter
                && (!flagsMatch(active.surfFlags,
                        static_cast<std::uint32_t>(surface.surfaceFlags))
                    || !flagsMatch(active.faceFlags, surface.faceFlags)
                    || (active.adjoinFlags.has_value()
                        && (!surface.adjoin.has_value()
                            || !active.adjoinFlags->matches(
                                static_cast<std::uint32_t>(
                                    surface.adjoinFlags))))))
            {
                continue;
            }
            matches.push_back(static_cast<std::int32_t>(i));
        }
    }
    else if (things)
    {
        for (std::size_t i = 0; i < level.things.size(); ++i)
        {
            const Thing& thing = level.things[i];
            if (!layerMatches(thing.layer))
            {
                continue;
            }
            if (hasFilter && active.name.has_value()
                && !text::iequalsAscii(thing.name, *active.name))
            {
                continue;
            }
            if (hasFilter && active.templateName.has_value()
                && !text::iequalsAscii(thing.templateName, *active.templateName))
            {
                continue;
            }
            if (!search.empty()
                && !containsSubstringIgnoreCase(thing.name, search)
                && !containsSubstringIgnoreCase(thing.templateName, search))
            {
                continue;
            }
            matches.push_back(static_cast<std::int32_t>(i));
        }
    }
    else if (collection == WorkbenchCollection::sectors)
    {
        for (std::size_t i = 0; i < level.sectors.size(); ++i)
        {
            const Sector& sector = level.sectors[i];
            if (!layerMatches(sector.layer))
            {
                continue;
            }
            if (hasFilter
                && !flagsMatch(active.flags,
                    static_cast<std::uint32_t>(sector.flags)))
            {
                continue;
            }
            matches.push_back(static_cast<std::int32_t>(i));
        }
    }
    else
    {
        for (std::size_t i = 0; i < level.lights.size(); ++i)
        {
            const SedLight& light = level.lights[i];
            if (!layerMatches(light.layer))
            {
                continue;
            }
            if (hasFilter
                && !flagsMatch(active.flags,
                    static_cast<std::uint32_t>(light.flags)))
            {
                continue;
            }
            matches.push_back(static_cast<std::int32_t>(i));
        }
    }

    QueryResult result{};
    result.totalMatches = matches.size();
    const std::size_t limit = std::min<std::size_t>(page.limit, 200);
    if (page.offset < matches.size())
    {
        const std::size_t count =
            std::min(limit, matches.size() - page.offset);
        for (std::size_t i = 0; i < count; ++i)
        {
            result.indices.push_back(matches[page.offset + i]);
        }
        if (page.offset + count < matches.size())
        {
            result.nextOffset = page.offset + count;
        }
    }
    return result;
}

bool PreparedBatchUv::apply(Level& level) const
{
    if (!ok())
    {
        return false;
    }
    for (const BatchUvSurfacePlan& plan : surfaces)
    {
        if (plan.ref.sector < 0
            || static_cast<std::size_t>(plan.ref.sector) >= level.sectors.size()
            || plan.ref.surface < 0
            || static_cast<std::size_t>(plan.ref.surface)
                >= level.sectors[static_cast<std::size_t>(plan.ref.sector)]
                       .surfaces.size())
        {
            return false;
        }
        const Sector& sector =
            level.sectors[static_cast<std::size_t>(plan.ref.sector)];
        const Surface& surface =
            sector.surfaces[static_cast<std::size_t>(plan.ref.surface)];
        if (surface.vertices != plan.vertexIndices
            || surface.texVertices.size() != plan.before.size()
            || plan.positions.size() != plan.before.size()
            || plan.after.size() != plan.before.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < plan.before.size(); ++i)
        {
            if (surface.vertices[i] < 0
                || static_cast<std::size_t>(surface.vertices[i])
                    >= sector.vertices.size())
            {
                return false;
            }
            const Vertex& vertex = sector.vertices[static_cast<std::size_t>(
                surface.vertices[i])];
            if (vertex.x != plan.positions[i].x
                || vertex.y != plan.positions[i].y
                || vertex.z != plan.positions[i].z
                || !sameUvBits(surface.texVertices[i].u, plan.before[i].u)
                || !sameUvBits(surface.texVertices[i].v, plan.before[i].v))
            {
                return false;
            }
        }
    }
    for (const BatchUvSurfacePlan& plan : surfaces)
    {
        Surface& surface = level
            .sectors[static_cast<std::size_t>(plan.ref.sector)]
            .surfaces[static_cast<std::size_t>(plan.ref.surface)];
        for (std::size_t i = 0; i < plan.after.size(); ++i)
        {
            surface.texVertices[i].u = plan.after[i].u;
            surface.texVertices[i].v = plan.after[i].v;
        }
    }
    return true;
}

PreparedBatchUv prepareProjectSurfaceUv(
    const Level& level,
    const std::span<const SurfaceRef> surfaces,
    const UvProjectionDirection direction,
    const TVector& origin,
    const double texelsPerUnit)
{
    const int directionValue = static_cast<int>(direction);
    if (directionValue < 0 || directionValue > 5)
    {
        return batchUvFailure(failure("invalidBatchUvInput",
            "unknown UV projection direction"));
    }
    if (!boundedVector(origin))
    {
        return batchUvFailure(failure("invalidBatchUvInput",
            "projection origin must be finite within +/-1000000"));
    }
    if (!std::isfinite(texelsPerUnit) || texelsPerUnit <= 0.0
        || texelsPerUnit > kMaxWorkbenchCoordinate)
    {
        return batchUvFailure(failure("invalidBatchUvInput",
            "texels per unit must be finite and in (0, 1000000]"));
    }
    PreparedBatchUv prepared = prepareBatchUv(level, surfaces,
        [direction, origin, texelsPerUnit](
            const TVector& position, const TVector&, const UvPair) {
            return UvPair{
                projectPair(position.Sub(origin), direction).u * texelsPerUnit,
                projectPair(position.Sub(origin), direction).v * texelsPerUnit,
            };
        });
    if (!prepared.ok())
    {
        return prepared;
    }
    // Edge-on faces and collapsed projections refuse after the shared
    // geometry preflight, against each surface's own unit normal.
    for (const BatchUvSurfacePlan& plan : prepared.surfaces)
    {
        std::string reason{};
        const std::optional<TVector> normal =
            checkedUnitNormal(plan.positions, reason);
        if (!normal.has_value())
        {
            return batchUvFailure(
                failure("batchUvInvalidGeometry", std::move(reason)));
        }
        const double facing = direction == UvProjectionDirection::positiveX
                || direction == UvProjectionDirection::negativeX
            ? normal->x
            : direction == UvProjectionDirection::positiveY
                    || direction == UvProjectionDirection::negativeY
                ? normal->y
                : normal->z;
        if (std::abs(facing) <= 1e-10)
        {
            return batchUvFailure(failure("batchUvEdgeOnSurface",
                "a selected surface is edge-on to the projection"));
        }
        std::vector<UvPair> projected(plan.after.size());
        for (std::size_t i = 0; i < plan.after.size(); ++i)
        {
            projected[i] = { plan.after[i].u, plan.after[i].v };
        }
        if (!simplePolygon2(projected))
        {
            return batchUvFailure(failure("batchUvInvalidGeometry",
                "the projected UV polygon collapses or intersects"));
        }
    }
    return prepared;
}

PreparedBatchUv prepareCylindricalSurfaceUv(
    const Level& level,
    const std::span<const SurfaceRef> surfaces,
    const TVector& axisOrigin,
    const TVector& axisDirection,
    const double texelsPerUnit)
{
    if (!boundedVector(axisOrigin) || !finiteVector(axisDirection)
        || axisDirection.Length() < 1e-12
        || !std::isfinite(texelsPerUnit) || texelsPerUnit <= 0.0
        || texelsPerUnit > kMaxWorkbenchCoordinate)
    {
        return batchUvFailure(failure("invalidBatchUvInput",
            "cylindrical axis origin/direction must be finite (direction non-zero) and "
            "texels per unit must be finite and in (0, 1000000]"));
    }
    const TVector axis = axisDirection.Scaled(1.0 / axisDirection.Length());
    const TVector reference = std::abs(axis.x) < 0.9
        ? TVector{ 1.0, 0.0, 0.0 } : TVector{ 0.0, 1.0, 0.0 };
    const TVector e1Raw = reference.Sub(axis.Scaled(reference.Dot(axis)));
    const TVector e1 = e1Raw.Scaled(1.0 / e1Raw.Length());
    const TVector e2 = axis.Cross(e1);
    PreparedBatchUv prepared = prepareBatchUv(level, surfaces,
        [axisOrigin, axis, e1, e2, texelsPerUnit](
            const TVector& position, const TVector&, const UvPair) {
            const TVector relative = position.Sub(axisOrigin);
            const double height = relative.Dot(axis);
            const TVector radial = relative.Sub(axis.Scaled(height));
            const double radius = radial.Length();
            const double theta = radius > 0.0
                ? std::atan2(radial.Dot(e2), radial.Dot(e1)) : 0.0;
            return UvPair{ theta * radius * texelsPerUnit, height * texelsPerUnit };
        });
    if (!prepared.ok())
    {
        return prepared;
    }
    for (const BatchUvSurfacePlan& plan : prepared.surfaces)
    {
        std::vector<UvPair> projected(plan.after.size());
        for (std::size_t i = 0; i < plan.after.size(); ++i)
        {
            projected[i] = { plan.after[i].u, plan.after[i].v };
        }
        if (!simplePolygon2(projected))
        {
            return batchUvFailure(failure("batchUvInvalidGeometry",
                "the cylindrical UV polygon collapses or intersects"));
        }
    }
    return prepared;
}

PreparedBatchUv prepareSphericalSurfaceUv(
    const Level& level,
    const std::span<const SurfaceRef> surfaces,
    const TVector& center,
    const TVector& poleDirection,
    const double texelsPerUnit)
{
    if (!boundedVector(center) || !finiteVector(poleDirection)
        || poleDirection.Length() < 1e-12
        || !std::isfinite(texelsPerUnit) || texelsPerUnit <= 0.0
        || texelsPerUnit > kMaxWorkbenchCoordinate)
    {
        return batchUvFailure(failure("invalidBatchUvInput",
            "spherical center/pole must be finite (pole non-zero) and "
            "texels per unit must be finite and in (0, 1000000]"));
    }
    const TVector pole = poleDirection.Scaled(1.0 / poleDirection.Length());
    const TVector reference = std::abs(pole.x) < 0.9
        ? TVector{ 1.0, 0.0, 0.0 } : TVector{ 0.0, 1.0, 0.0 };
    const TVector e1Raw = reference.Sub(pole.Scaled(reference.Dot(pole)));
    const TVector e1 = e1Raw.Scaled(1.0 / e1Raw.Length());
    const TVector e2 = pole.Cross(e1);
    PreparedBatchUv prepared = prepareBatchUv(level, surfaces,
        [center, pole, e1, e2, texelsPerUnit](
            const TVector& position, const TVector&, const UvPair) {
            const TVector relative = position.Sub(center);
            const double radius = relative.Length();
            if (radius <= 0.0)
            {
                return UvPair{ 0.0, 0.0 };
            }
            const double cosPhi = std::clamp(relative.Dot(pole) / radius, -1.0, 1.0);
            const double phi = std::acos(cosPhi);
            const double sinPhi = std::sin(phi);
            const double theta = sinPhi > 1e-12
                ? std::atan2(relative.Dot(e2), relative.Dot(e1)) : 0.0;
            return UvPair{ theta * radius * texelsPerUnit, phi * radius * texelsPerUnit };
        });
    if (!prepared.ok())
    {
        return prepared;
    }
    for (const BatchUvSurfacePlan& plan : prepared.surfaces)
    {
        std::vector<UvPair> projected(plan.after.size());
        for (std::size_t i = 0; i < plan.after.size(); ++i)
        {
            projected[i] = { plan.after[i].u, plan.after[i].v };
        }
        if (!simplePolygon2(projected))
        {
            return batchUvFailure(failure("batchUvInvalidGeometry",
                "the spherical UV polygon collapses or intersects"));
        }
    }
    return prepared;
}

PreparedBatchUv prepareTransformSurfaceUv(
    const Level& level,
    const std::span<const SurfaceRef> surfaces,
    const WorkbenchUvPoint& pivot,
    const WorkbenchUvPoint& scale,
    const double rotationDegrees,
    const WorkbenchUvPoint& offset)
{
    const auto finitePoint = [](const WorkbenchUvPoint& point) noexcept {
        return std::isfinite(point.u) && std::isfinite(point.v);
    };
    if (!finitePoint(pivot) || !finitePoint(scale) || !finitePoint(offset)
        || !std::isfinite(rotationDegrees))
    {
        return batchUvFailure(failure("invalidBatchUvInput",
            "UV pivot, scale, rotation and offset must be finite"));
    }
    if (scale.u == 0.0 || scale.v == 0.0)
    {
        return batchUvFailure(failure(
            "invalidBatchUvInput", "UV scales must be nonzero"));
    }
    const double radians = std::fmod(rotationDegrees, 360.0)
        * (std::acos(-1.0) / 180.0);
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    return prepareBatchUv(level, surfaces,
        [pivot, scale, offset, cosine, sine](
            const TVector&, const TVector&, const UvPair old) {
            const double u = (old.u - pivot.u) * scale.u;
            const double v = (old.v - pivot.v) * scale.v;
            return UvPair{ pivot.u + u * cosine - v * sine + offset.u,
                pivot.v + u * sine + v * cosine + offset.v };
        });
}

PlacementPositions computePlacementPositions(const PlacementLayout& layout)
{
    const auto countValid = [](const std::int32_t count) noexcept {
        return count >= 1
            && static_cast<std::size_t>(count) <= kMaxPlacementArrayOutputs;
    };
    std::vector<TVector> positions{};
    switch (layout.kind)
    {
    case PlacementLayoutKind::linear:
    {
        if (!countValid(layout.count))
        {
            return placementFailure(failure(
                "placementArrayLimit", "an array creates 1 to 256 outputs"));
        }
        if (!boundedVector(layout.origin) || !boundedVector(layout.step))
        {
            return placementFailure(failure("invalidPlacementInput",
                "layout origin and step must be finite within +/-1000000"));
        }
        positions.reserve(static_cast<std::size_t>(layout.count));
        for (std::int32_t i = 0; i < layout.count; ++i)
        {
            positions.push_back(
                layout.origin.Add(layout.step.Scaled(static_cast<double>(i))));
        }
        break;
    }
    case PlacementLayoutKind::grid:
    {
        if (!countValid(layout.countU) || !countValid(layout.countV)
            || static_cast<std::int64_t>(layout.countU) * layout.countV
                > static_cast<std::int64_t>(kMaxPlacementArrayOutputs))
        {
            return placementFailure(failure(
                "placementArrayLimit", "an array creates 1 to 256 outputs"));
        }
        if (!boundedVector(layout.origin) || !boundedVector(layout.stepU)
            || !boundedVector(layout.stepV))
        {
            return placementFailure(failure("invalidPlacementInput",
                "layout origin and steps must be finite within +/-1000000"));
        }
        positions.reserve(static_cast<std::size_t>(
            static_cast<std::int64_t>(layout.countU) * layout.countV));
        for (std::int32_t v = 0; v < layout.countV; ++v)
        {
            for (std::int32_t u = 0; u < layout.countU; ++u)
            {
                positions.push_back(layout.origin
                    .Add(layout.stepU.Scaled(static_cast<double>(u)))
                    .Add(layout.stepV.Scaled(static_cast<double>(v))));
            }
        }
        break;
    }
    case PlacementLayoutKind::radial:
    {
        if (!countValid(layout.count))
        {
            return placementFailure(failure(
                "placementArrayLimit", "an array creates 1 to 256 outputs"));
        }
        if (!boundedVector(layout.origin) || !boundedVector(layout.axis))
        {
            return placementFailure(failure("invalidPlacementInput",
                "layout origin and axis must be finite within +/-1000000"));
        }
        if (!std::isfinite(layout.radius) || layout.radius <= 0.0
            || layout.radius > kMaxWorkbenchCoordinate
            || !std::isfinite(layout.startDegrees)
            || std::abs(layout.startDegrees) > kMaxWorkbenchCoordinate
            || !std::isfinite(layout.sweepDegrees)
            || layout.sweepDegrees == 0.0
            || std::abs(layout.sweepDegrees) > 360.0)
        {
            return placementFailure(failure("invalidPlacementInput",
                "radial radius, start and sweep are out of bounds"));
        }
        const double largest = std::max({ std::abs(layout.axis.x),
            std::abs(layout.axis.y), std::abs(layout.axis.z) });
        if (!(largest > 0.0))
        {
            return placementFailure(failure(
                "invalidPlacementInput", "radial axis must be nonzero"));
        }
        TVector axis = layout.axis.Scaled(1.0 / largest);
        if (axis.Normalize() == 0.0)
        {
            return placementFailure(failure(
                "invalidPlacementInput", "radial axis must be nonzero"));
        }
        const TVector helper = std::abs(axis.x) <= std::abs(axis.y)
                && std::abs(axis.x) <= std::abs(axis.z)
            ? TVector{ 1.0, 0.0, 0.0 }
            : std::abs(axis.y) <= std::abs(axis.z)
                ? TVector{ 0.0, 1.0, 0.0 }
                : TVector{ 0.0, 0.0, 1.0 };
        TVector u = helper.Sub(axis.Scaled(helper.Dot(axis)));
        if (u.Normalize() == 0.0)
        {
            return placementFailure(failure(
                "invalidPlacementInput", "radial axis has no usable basis"));
        }
        const TVector v = axis.Cross(u);
        const double toRadians = std::acos(-1.0) / 180.0;
        positions.reserve(static_cast<std::size_t>(layout.count));
        for (std::int32_t i = 0; i < layout.count; ++i)
        {
            const double angle = (layout.startDegrees
                + layout.sweepDegrees * static_cast<double>(i)
                    / static_cast<double>(layout.count))
                * toRadians;
            positions.push_back(layout.origin
                .Add(u.Scaled(layout.radius * std::cos(angle)))
                .Add(v.Scaled(layout.radius * std::sin(angle))));
        }
        break;
    }
    default:
        return placementFailure(failure(
            "invalidPlacementInput", "unknown placement layout kind"));
    }

    if (const auto refusal = checkDistinctBounded(positions))
    {
        return placementFailure(*refusal);
    }
    PlacementPositions result{};
    result.positions = std::move(positions);
    return result;
}

namespace
{

PreparedThingArray prepareNamedThingPlacements(
    const Level& level,
    const std::span<const TVector> positions,
    const std::string_view templateName,
    const std::span<const std::string> requestedNames,
    const TVector& orientation,
    const std::int32_t layer,
    const std::optional<std::int32_t> explicitSector,
    const std::size_t patchOutputsBefore,
    std::optional<CogSchemaResolver> schemaResolver)
{
    PreparedThingArray prepared{};
    const auto refuse = [&prepared](WorkbenchFailure refusal)
    {
        prepared.failure = std::move(refusal);
        return prepared;
    };
    if ( const auto refusal = checkArrayCounts(positions.size(), patchOutputsBefore) )
    {
        return refuse(*refusal);
    }
    if ( const auto refusal = checkDistinctBounded(positions) )
    {
        return refuse(*refusal);
    }
    if ( requestedNames.size() != positions.size() )
    {
        return refuse(failure("invalidPlacementInput", "each placement needs exactly one name"));
    }
    if ( !validPlacementGame(level) )
    {
        return refuse(failure("unsupportedPlacementGame",
            "array authoring supports JK and MotS levels"));
    }
    if ( !tokenTextValid(templateName, 127) )
    {
        return refuse(failure("invalidPlacementInput",
            "template must be a nonempty ASCII token of at most "
            "127 characters"));
    }
    bool templateFound = false;
    for ( const ThingTemplate& thingTemplate : level.templates )
    {
        if ( text::iequalsAscii(thingTemplate.name, templateName) )
        {
            templateFound = true;
            break;
        }
    }
    if ( !templateFound )
    {
        return refuse(failure(
            "placementTemplateNotFound", "template is not defined in this level"));
    }
    if ( !boundedVector(orientation) )
    {
        return refuse(failure("invalidPlacementInput",
            "orientation must be finite within +/-1000000"));
    }
    if ( !validLayer(level, layer) )
    {
        return refuse(failure(
            "invalidPlacementLayer", "layer must name an existing layer"));
    }
    if ( explicitSector.has_value()
        && (*explicitSector < 0
            || static_cast<std::size_t>(*explicitSector) >= level.sectors.size()) )
    {
        return refuse(failure("placementSectorOutOfRange",
            "the explicit sector is outside the level"));
    }

    std::unordered_set<std::string> names{};
    for ( const Thing& thing : level.things )
    {
        names.insert(text::lowerAscii(thing.name));
    }
    std::vector<std::string> generated{};
    generated.reserve(positions.size());
    for ( const auto& name : requestedNames )
    {
        if ( !tokenTextValid(name, 127) )
        {
            return refuse(failure("invalidPlacementInput",
                "name must be a nonempty ASCII token of at most 127 characters"));
        }
        if ( !names.insert(text::lowerAscii(name)).second )
        {
            return refuse(failure("placementNameCollision",
                "thing name '" + name
                    + "' already exists (case-insensitive)"));
        }
        generated.push_back(name);
    }

    if ( !level.cogs.empty() )
    {
        if ( !schemaResolver.has_value() )
        {
            return refuse(failure("cogAppendGuardRequired",
                "appending things to a level with placed COGs requires "
                "complete COG schemas to verify existing thing references"));
        }
        if ( const auto guard = validateThingAppend(level, *schemaResolver) )
        {
            return refuse(failure(guard->code, guard->message));
        }
    }
    if ( !level.canAllocateIds(positions.size()) )
    {
        return refuse(failure(
            "placementIdExhausted", "the level ID space cannot hold the array"));
    }

    prepared.positions.assign(positions.begin(), positions.end());
    prepared.names = std::move(generated);
    prepared.templateName = std::string(templateName);
    prepared.orientation = orientation;
    prepared.layer = layer;
    prepared.schemaResolver = std::move(schemaResolver);
    prepared.sectorIndices.reserve(positions.size());
    for ( const TVector& position : positions )
    {
        auto resolved = resolvePlacementSector(level, position, explicitSector);
        if ( const auto* refusal = std::get_if<WorkbenchFailure>(&resolved) )
        {
            prepared.failure = *refusal;
            prepared.positions.clear();
            prepared.names.clear();
            prepared.sectorIndices.clear();
            return prepared;
        }
        prepared.sectorIndices.push_back(std::get<std::int32_t>(resolved));
    }
    return prepared;
}

} // namespace

PreparedThingArray prepareThingArray(
    const Level& level,
    const std::span<const TVector> positions,
    const std::string_view templateName,
    const std::string_view namePrefix,
    const TVector& orientation,
    const std::int32_t layer,
    const std::optional<std::int32_t> explicitSector,
    const std::size_t patchOutputsBefore,
    std::optional<CogSchemaResolver> schemaResolver)
{
    PreparedThingArray refused{};
    if ( const auto problem = checkArrayCounts(positions.size(), patchOutputsBefore) )
    {
        refused.failure = *problem;
        return refused;
    }
    if ( !tokenTextValid(namePrefix, 123) )
    {
        refused.failure = failure("invalidPlacementInput",
            "namePrefix must be a nonempty ASCII token of at most 123 characters");
        return refused;
    }
    std::vector<std::string> names;
    names.reserve(positions.size());
    for ( std::size_t i = 0; i < positions.size(); ++i )
    {
        names.push_back(generatedThingName(namePrefix, i));
    }
    return prepareNamedThingPlacements(level, positions, templateName, names,
        orientation, layer, explicitSector, patchOutputsBefore, std::move(schemaResolver));
}

PreparedThingArray prepareThingPlacement(
    const Level& level,
    const TVector& position,
    const std::string_view templateName,
    const std::string_view name,
    const TVector& orientation,
    const std::int32_t layer,
    const std::optional<std::int32_t> explicitSector,
    const std::size_t patchOutputsBefore,
    std::optional<CogSchemaResolver> schemaResolver)
{
    const TVector positions[] = { position };
    const std::string names[] = { std::string(name) };
    return prepareNamedThingPlacements(level, positions, templateName, names,
        orientation, layer, explicitSector, patchOutputsBefore, std::move(schemaResolver));
}

bool PreparedThingArray::apply(Level& level) const
{
    if (!ok() || positions.size() != names.size()
        || positions.size() != sectorIndices.size())
    {
        return false;
    }
    if (!validPlacementGame(level) || !validLayer(level, layer)
        || !level.canAllocateIds(positions.size()))
    {
        return false;
    }
    bool templateFound = false;
    for (const ThingTemplate& thingTemplate : level.templates)
    {
        if (text::iequalsAscii(thingTemplate.name, templateName))
        {
            templateFound = true;
            break;
        }
    }
    if (!templateFound)
    {
        return false;
    }
    std::unordered_set<std::string> existingNames{};
    for (const Thing& thing : level.things)
    {
        existingNames.insert(text::lowerAscii(thing.name));
    }
    for (const std::string& name : names)
    {
        if (!existingNames.insert(text::lowerAscii(name)).second)
        {
            return false;
        }
    }
    if (!level.cogs.empty())
    {
        if (!schemaResolver.has_value()
            || validateThingAppend(level, *schemaResolver).has_value())
        {
            return false;
        }
    }
    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        if (sectorIndices[i] < 0
            || static_cast<std::size_t>(sectorIndices[i])
                >= level.sectors.size())
        {
            return false;
        }
        // Re-resolving against the prepared sector proves the point is
        // still strictly inside the sector it was prepared for.
        auto resolved = resolvePlacementSector(
            level, positions[i],
            std::optional<std::int32_t>(sectorIndices[i]));
        if (std::holds_alternative<WorkbenchFailure>(resolved)
            || std::get<std::int32_t>(resolved) != sectorIndices[i])
        {
            return false;
        }
    }

    firstCreatedIndex = level.things.size();
    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        Thing& thing = level.newThing();
        thing.templateName = templateName;
        thing.name = names[i];
        thing.sector = sectorIndices[i];
        thing.x = positions[i].x;
        thing.y = positions[i].y;
        thing.z = positions[i].z;
        thing.pch = orientation.x;
        thing.yaw = orientation.y;
        thing.rol = orientation.z;
        thing.layer = layer;
    }
    return true;
}

PreparedLightArray prepareLightArray(
    const Level& level,
    const std::span<const TVector> positions,
    const ColorF& color,
    const double intensity,
    const double range,
    const std::int32_t flags,
    const std::int32_t layer,
    const std::size_t patchOutputsBefore)
{
    PreparedLightArray prepared{};
    const auto refuse = [&prepared](WorkbenchFailure refusal) {
        prepared.failure = std::move(refusal);
        return prepared;
    };
    if (const auto refusal = checkArrayCounts(positions.size(), patchOutputsBefore))
    {
        return refuse(*refusal);
    }
    if (const auto refusal = checkDistinctBounded(positions))
    {
        return refuse(*refusal);
    }
    if (!validPlacementGame(level))
    {
        return refuse(failure("unsupportedPlacementGame",
            "array authoring supports JK and MotS levels"));
    }
    const auto channelValid = [](const float value) noexcept {
        return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
    };
    if (!channelValid(color.r) || !channelValid(color.g)
        || !channelValid(color.b) || !std::isfinite(color.a))
    {
        return refuse(failure("invalidPlacementInput",
            "light RGB components must be finite and in [0, 1]"));
    }
    if (!std::isfinite(intensity) || intensity < 0.0 || intensity > 1000.0)
    {
        return refuse(failure(
            "invalidPlacementInput", "light intensity must be in [0, 1000]"));
    }
    if (!std::isfinite(range) || range < 0.0001 || range > 100000.0)
    {
        return refuse(failure("invalidPlacementInput",
            "light range must be in [0.0001, 100000]"));
    }
    if (flags < 0)
    {
        return refuse(failure(
            "invalidPlacementInput", "light flags must not be negative"));
    }
    if (!validLayer(level, layer))
    {
        return refuse(failure(
            "invalidPlacementLayer", "layer must name an existing layer"));
    }
    if (!level.canAllocateIds(positions.size()))
    {
        return refuse(failure(
            "placementIdExhausted", "the level ID space cannot hold the array"));
    }
    prepared.positions.assign(positions.begin(), positions.end());
    prepared.color = color;
    prepared.intensity = intensity;
    prepared.range = range;
    prepared.flags = flags;
    prepared.layer = layer;
    return prepared;
}

bool PreparedLightArray::apply(Level& level) const
{
    if (!ok())
    {
        return false;
    }
    if (!validPlacementGame(level) || !validLayer(level, layer)
        || !level.canAllocateIds(positions.size()))
    {
        return false;
    }
    firstCreatedIndex = level.lights.size();
    for (const TVector& position : positions)
    {
        SedLight& light = level.newLight();
        light.position = position;
        light.color = color;
        light.intensity = intensity;
        light.range = range;
        light.flags = flags;
        light.layer = layer;
    }
    return true;
}

} // namespace sed::level
