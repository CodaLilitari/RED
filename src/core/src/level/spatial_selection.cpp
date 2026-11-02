#include "sed/level/spatial_selection.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>

#include "sed/assets/jkl.hpp"
#include "sed/level/geometry.hpp"

namespace sed::level
{
namespace
{

bool finitePoint(const TVector& point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

TVector vertexPoint(const Vertex& vertex) noexcept
{
    return TVector{ vertex.x, vertex.y, vertex.z };
}

bool unitNormal(const Surface& surface, TVector& normal) noexcept
{
    normal = surface.normal;
    if (!finitePoint(normal))
    {
        return false;
    }
    const double length = normal.Length();
    if (!(length > 0.0) || !std::isfinite(length))
    {
        return false;
    }
    normal.Scale(1.0 / length);
    return finitePoint(normal);
}

// A sector can classify points only when every face is a usable planar
// convex polygon, the planes bound one convex volume, and the face edges
// pair up into a closed shell. Anything else is reported, never guessed.
bool sectorClassifiable(const Sector& sector, std::string& reason)
{
    if (sector.vertices.empty() || sector.surfaces.empty())
    {
        reason = "sector has no closed geometry";
        return false;
    }
    for (const Vertex& vertex : sector.vertices)
    {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
        {
            reason = "sector vertex is not finite";
            return false;
        }
    }
    std::map<std::pair<std::int32_t, std::int32_t>, int> directedEdges;
    for (const Surface& surface : sector.surfaces)
    {
        if (surface.vertices.size() < 3)
        {
            reason = "sector surface has fewer than three vertices";
            return false;
        }
        TVector normal{};
        if (!unitNormal(surface, normal))
        {
            reason = "sector surface has no usable normal";
            return false;
        }
        for (const std::int32_t index : surface.vertices)
        {
            if (index < 0 || static_cast<std::size_t>(index) >= sector.vertices.size())
            {
                reason = "sector surface references a missing vertex";
                return false;
            }
        }
        if (!surfaceIsPlanar(sector, surface) || !surfaceIsConvex(sector, surface))
        {
            reason = "sector surface is not planar and convex";
            return false;
        }
        const std::size_t count = surface.vertices.size();
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::int32_t a = surface.vertices[i];
            const std::int32_t b = surface.vertices[(i + 1) % count];
            if (a == b)
            {
                reason = "sector surface has a degenerate edge";
                return false;
            }
            ++directedEdges[{ a, b }];
        }
    }
    if (!sectorIsConvex(sector))
    {
        reason = "sector is not convex";
        return false;
    }
    for (const auto& [edge, count] : directedEdges)
    {
        const auto reverse = directedEdges.find({ edge.second, edge.first });
        if (reverse == directedEdges.end() || reverse->second < count)
        {
            reason = "sector shell is not closed";
            return false;
        }
    }
    return true;
}

bool sectorBoundsContain(
    const Sector& sector,
    const TVector& point,
    const double padding) noexcept
{
    if (sector.vertices.empty())
    {
        return false;
    }
    TVector low = vertexPoint(sector.vertices.front());
    TVector high = low;
    for (const Vertex& vertex : sector.vertices)
    {
        low.x = std::min(low.x, vertex.x);
        low.y = std::min(low.y, vertex.y);
        low.z = std::min(low.z, vertex.z);
        high.x = std::max(high.x, vertex.x);
        high.y = std::max(high.y, vertex.y);
        high.z = std::max(high.z, vertex.z);
    }
    return point.x >= low.x - padding && point.x <= high.x + padding
        && point.y >= low.y - padding && point.y <= high.y + padding
        && point.z >= low.z - padding && point.z <= high.z + padding;
}

// Interior points sit on the positive (inward-normal) side of every face.
bool sectorContains(const Sector& sector, const TVector& point, const double tolerance) noexcept
{
    for (const Surface& surface : sector.surfaces)
    {
        TVector normal{};
        if (!unitNormal(surface, normal))
        {
            return false;
        }
        const TVector origin = vertexPoint(
            sector.vertices[static_cast<std::size_t>(surface.vertices.front())]);
        if (normal.Dot(point.Sub(origin)) < -tolerance)
        {
            return false;
        }
    }
    return true;
}

bool pointOnSurface(
    const Sector& sector,
    const Surface& surface,
    const TVector& point,
    const double tolerance) noexcept
{
    TVector normal{};
    if (!unitNormal(surface, normal))
    {
        return false;
    }
    const TVector origin = vertexPoint(
        sector.vertices[static_cast<std::size_t>(surface.vertices.front())]);
    if (std::abs(normal.Dot(point.Sub(origin))) > tolerance)
    {
        return false;
    }
    const std::size_t count = surface.vertices.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        const TVector a = vertexPoint(
            sector.vertices[static_cast<std::size_t>(surface.vertices[i])]);
        const TVector b = vertexPoint(
            sector.vertices[static_cast<std::size_t>(surface.vertices[(i + 1) % count])]);
        const TVector edge = b.Sub(a);
        const double edgeLength = edge.Length();
        if (!(edgeLength > 0.0))
        {
            continue;
        }
        const double side = edge.Cross(point.Sub(a)).Dot(normal) / edgeLength;
        if (side < -tolerance)
        {
            return false;
        }
    }
    return true;
}

double angleDegrees(const TVector& a, const TVector& b) noexcept
{
    const double dot = std::clamp(a.Dot(b), -1.0, 1.0);
    return RadToDeg(std::acos(dot));
}

std::string formatDouble(const double value)
{
    const double normalized = value == 0.0 ? 0.0 : value;
    std::array<char, 64> buffer{};
    const int written = std::snprintf(
        buffer.data(), buffer.size(), "%.17g", normalized);
    if (written <= 0)
    {
        return "0";
    }
    return std::string(buffer.data(), static_cast<std::size_t>(written));
}

// Canonical geometry text for one sector: vertex positions, face vertex
// order and stored face normals. Material, UV and light data are excluded
// on purpose, so metadata edits keep a token valid within a batch.
std::string sectorGeometryDigest(const Level& level, const std::int32_t sectorIndex)
{
    const Sector& sector = level.sectors[static_cast<std::size_t>(sectorIndex)];
    std::string canonical = "sector-geometry-v1\n";
    for (const Vertex& vertex : sector.vertices)
    {
        canonical += formatDouble(vertex.x);
        canonical += ',';
        canonical += formatDouble(vertex.y);
        canonical += ',';
        canonical += formatDouble(vertex.z);
        canonical += ';';
    }
    canonical += '\n';
    for (const Surface& surface : sector.surfaces)
    {
        for (const std::int32_t index : surface.vertices)
        {
            canonical += std::to_string(index);
            canonical += ',';
        }
        canonical += '|';
        canonical += formatDouble(surface.normal.x);
        canonical += ',';
        canonical += formatDouble(surface.normal.y);
        canonical += ',';
        canonical += formatDouble(surface.normal.z);
        canonical += ';';
    }
    return sed::assets::jklSourceSha256Hex(canonical);
}

std::string thingDigest(const Level& level, const std::int32_t thingIndex)
{
    const Thing& thing = level.things[static_cast<std::size_t>(thingIndex)];
    std::string canonical = "thing-v1\n";
    canonical += std::to_string(thingIndex);
    canonical += ';';
    canonical += std::to_string(thing.num);
    canonical += ';';
    canonical += std::to_string(thing.id);
    canonical += ';';
    canonical += std::to_string(thing.sector);
    canonical += ';';
    canonical += formatDouble(thing.x);
    canonical += ',';
    canonical += formatDouble(thing.y);
    canonical += ',';
    canonical += formatDouble(thing.z);
    return sed::assets::jklSourceSha256Hex(canonical);
}

std::string bindingDigest(const SelectionContext& context)
{
    return sed::assets::jklSourceSha256Hex(
        context.inputSha256 + "\n" + context.sourceRevision);
}

SelectionToken mintToken(
    const SelectionContext& context,
    const SelectionTarget& target,
    const std::string& geometryDigest)
{
    const std::int32_t first = target.kind == SelectionTargetKind::thing
        ? target.thing
        : target.sector;
    const std::int32_t second = target.kind == SelectionTargetKind::surface
        ? target.surface
        : target.kind == SelectionTargetKind::vertex ? target.vertex : -1;
    SelectionToken token;
    token.text = "sedsp1.";
    token.text += selectionTargetKindName(target.kind);
    token.text += '.';
    token.text += std::to_string(first);
    token.text += '.';
    token.text += std::to_string(second);
    token.text += '.';
    token.text += geometryDigest;
    token.text += '.';
    token.text += bindingDigest(context);
    return token;
}

bool parseIndex(const std::string_view text, std::int32_t& value) noexcept
{
    if (text.empty())
    {
        return false;
    }
    const char* const first = text.data();
    const char* const last = first + text.size();
    const auto result = std::from_chars(first, last, value);
    return result.ec == std::errc{} && result.ptr == last;
}

bool isDigestText(const std::string_view text) noexcept
{
    if (text.size() != 64)
    {
        return false;
    }
    for (const char c : text)
    {
        const bool digit = c >= '0' && c <= '9';
        const bool lower = c >= 'a' && c <= 'f';
        if (!digit && !lower)
        {
            return false;
        }
    }
    return true;
}

SpatialSelection cannotClassify(std::string diagnostic)
{
    SpatialSelection result;
    result.match = SpatialMatch::cannotClassify;
    result.code = std::string(kCannotClassifySpatialCode);
    result.diagnostic = std::move(diagnostic);
    return result;
}

SpatialSelection finishSelection(
    const Level& level,
    const SelectionContext& context,
    const SpatialMatch match,
    const SelectionTarget& target,
    const std::size_t candidateCount)
{
    SpatialSelection result;
    result.match = match;
    result.candidateCount = candidateCount;
    if (match == SpatialMatch::unique)
    {
        result.target = target;
        const std::string digest = target.kind == SelectionTargetKind::thing
            ? thingDigest(level, target.thing)
            : sectorGeometryDigest(level, target.sector);
        result.token = mintToken(context, target, digest);
    }
    return result;
}

} // namespace

std::string_view selectionTargetKindName(const SelectionTargetKind kind) noexcept
{
    switch (kind)
    {
    case SelectionTargetKind::sector: return "sector";
    case SelectionTargetKind::surface: return "surface";
    case SelectionTargetKind::vertex: return "vertex";
    case SelectionTargetKind::thing: return "thing";
    }
    return "sector";
}

std::string_view spatialMatchName(const SpatialMatch match) noexcept
{
    switch (match)
    {
    case SpatialMatch::unique: return "unique";
    case SpatialMatch::none: return "none";
    case SpatialMatch::ambiguous: return "ambiguous";
    case SpatialMatch::cannotClassify: return "cannotClassify";
    }
    return "none";
}

std::string_view selectionTokenStatusName(const SelectionTokenStatus status) noexcept
{
    switch (status)
    {
    case SelectionTokenStatus::valid: return "valid";
    case SelectionTokenStatus::stale: return "stale";
    case SelectionTokenStatus::malformed: return "malformed";
    }
    return "malformed";
}

SpatialSelection selectSectorAtPoint(
    const Level& level,
    const SelectionContext& context,
    const TVector& point,
    const SpatialTolerances& tolerances)
{
    if (!finitePoint(point))
    {
        return cannotClassify("query point is not finite");
    }
    std::vector<std::int32_t> candidates;
    for (std::size_t i = 0; i < level.sectors.size(); ++i)
    {
        const Sector& sector = level.sectors[i];
        std::string reason;
        if (!sectorClassifiable(sector, reason))
        {
            if (sectorBoundsContain(sector, point, tolerances.distance))
            {
                return cannotClassify(
                    "sector " + std::to_string(i) + " cannot classify safely: " + reason);
            }
            continue;
        }
        if (sectorContains(sector, point, tolerances.distance))
        {
            candidates.push_back(static_cast<std::int32_t>(i));
        }
    }
    if (candidates.empty())
    {
        SpatialSelection result;
        result.match = SpatialMatch::none;
        return result;
    }
    if (candidates.size() > 1)
    {
        SpatialSelection result;
        result.match = SpatialMatch::ambiguous;
        result.candidateCount = candidates.size();
        return result;
    }
    SelectionTarget target{};
    target.kind = SelectionTargetKind::sector;
    target.sector = candidates.front();
    return finishSelection(level, context, SpatialMatch::unique, target, 1);
}

SpatialSelection selectSurfaceByPointAndDirection(
    const Level& level,
    const SelectionContext& context,
    const TVector& point,
    const TVector& outwardDirection,
    const SpatialTolerances& tolerances)
{
    if (!finitePoint(point) || !finitePoint(outwardDirection))
    {
        return cannotClassify("query point or direction is not finite");
    }
    TVector direction = outwardDirection;
    if (direction.Length() == 0.0 || direction.Normalize() == 0.0)
    {
        return cannotClassify("query direction is empty");
    }
    std::vector<SelectionTarget> candidates;
    for (std::size_t i = 0; i < level.sectors.size(); ++i)
    {
        const Sector& sector = level.sectors[i];
        std::string reason;
        if (!sectorClassifiable(sector, reason))
        {
            if (sectorBoundsContain(sector, point, tolerances.distance))
            {
                return cannotClassify(
                    "sector " + std::to_string(i) + " cannot classify safely: " + reason);
            }
            continue;
        }
        for (std::size_t j = 0; j < sector.surfaces.size(); ++j)
        {
            const Surface& surface = sector.surfaces[j];
            if (!pointOnSurface(sector, surface, point, tolerances.distance))
            {
                continue;
            }
            TVector inward{};
            if (!unitNormal(surface, inward))
            {
                continue;
            }
            const TVector outward = inward.Scaled(-1.0);
            if (angleDegrees(outward, direction) > tolerances.angularDegrees)
            {
                continue;
            }
            SelectionTarget target{};
            target.kind = SelectionTargetKind::surface;
            target.sector = static_cast<std::int32_t>(i);
            target.surface = static_cast<std::int32_t>(j);
            candidates.push_back(target);
        }
    }
    if (candidates.empty())
    {
        SpatialSelection result;
        result.match = SpatialMatch::none;
        return result;
    }
    if (candidates.size() > 1)
    {
        SpatialSelection result;
        result.match = SpatialMatch::ambiguous;
        result.candidateCount = candidates.size();
        return result;
    }
    return finishSelection(level, context, SpatialMatch::unique, candidates.front(), 1);
}

SpatialSelection selectVertexAtPoint(
    const Level& level,
    const SelectionContext& context,
    const TVector& point,
    const SpatialTolerances& tolerances)
{
    if (!finitePoint(point))
    {
        return cannotClassify("query point is not finite");
    }
    std::vector<SelectionTarget> candidates;
    for (std::size_t i = 0; i < level.sectors.size(); ++i)
    {
        const Sector& sector = level.sectors[i];
        for (std::size_t j = 0; j < sector.vertices.size(); ++j)
        {
            if (vertexPoint(sector.vertices[j]).Distance(point) <= tolerances.distance)
            {
                SelectionTarget target{};
                target.kind = SelectionTargetKind::vertex;
                target.sector = static_cast<std::int32_t>(i);
                target.vertex = static_cast<std::int32_t>(j);
                candidates.push_back(target);
            }
        }
    }
    if (candidates.empty())
    {
        SpatialSelection result;
        result.match = SpatialMatch::none;
        return result;
    }
    if (candidates.size() > 1)
    {
        SpatialSelection result;
        result.match = SpatialMatch::ambiguous;
        result.candidateCount = candidates.size();
        return result;
    }
    return finishSelection(level, context, SpatialMatch::unique, candidates.front(), 1);
}

SpatialSelection selectThingAtPoint(
    const Level& level,
    const SelectionContext& context,
    const TVector& point,
    const SpatialTolerances& tolerances)
{
    if (!finitePoint(point))
    {
        return cannotClassify("query point is not finite");
    }
    std::vector<SelectionTarget> candidates;
    for (std::size_t i = 0; i < level.things.size(); ++i)
    {
        const Thing& thing = level.things[i];
        const TVector position{ thing.x, thing.y, thing.z };
        if (finitePoint(position) && position.Distance(point) <= tolerances.distance)
        {
            SelectionTarget target{};
            target.kind = SelectionTargetKind::thing;
            target.thing = static_cast<std::int32_t>(i);
            candidates.push_back(target);
        }
    }
    if (candidates.empty())
    {
        SpatialSelection result;
        result.match = SpatialMatch::none;
        return result;
    }
    if (candidates.size() > 1)
    {
        SpatialSelection result;
        result.match = SpatialMatch::ambiguous;
        result.candidateCount = candidates.size();
        return result;
    }
    return finishSelection(level, context, SpatialMatch::unique, candidates.front(), 1);
}

SelectionResolution resolveSelectionToken(
    const Level& level,
    const SelectionContext& context,
    const std::string_view tokenText)
{
    SelectionResolution malformed;
    malformed.status = SelectionTokenStatus::malformed;
    malformed.code = std::string(kMalformedSelectionTokenCode);

    std::array<std::string_view, 6> parts{};
    std::size_t partCount = 0;
    std::size_t begin = 0;
    for (std::size_t i = 0; i <= tokenText.size(); ++i)
    {
        if (i == tokenText.size() || tokenText[i] == '.')
        {
            if (partCount >= parts.size())
            {
                return malformed;
            }
            parts[partCount++] = tokenText.substr(begin, i - begin);
            begin = i + 1;
        }
    }
    if (partCount != parts.size() || parts[0] != "sedsp1"
        || !isDigestText(parts[4]) || !isDigestText(parts[5]))
    {
        return malformed;
    }

    SelectionTarget target{};
    bool kindKnown = true;
    if (parts[1] == "sector") target.kind = SelectionTargetKind::sector;
    else if (parts[1] == "surface") target.kind = SelectionTargetKind::surface;
    else if (parts[1] == "vertex") target.kind = SelectionTargetKind::vertex;
    else if (parts[1] == "thing") target.kind = SelectionTargetKind::thing;
    else kindKnown = false;

    std::int32_t first = -1;
    std::int32_t second = -1;
    if (!kindKnown || !parseIndex(parts[2], first) || !parseIndex(parts[3], second))
    {
        return malformed;
    }
    if (target.kind == SelectionTargetKind::sector)
    {
        target.sector = first;
        if (second != -1) return malformed;
    }
    else if (target.kind == SelectionTargetKind::surface)
    {
        target.sector = first;
        target.surface = second;
    }
    else if (target.kind == SelectionTargetKind::vertex)
    {
        target.sector = first;
        target.vertex = second;
    }
    else
    {
        target.thing = first;
        if (second != -1) return malformed;
    }

    SelectionResolution stale;
    stale.status = SelectionTokenStatus::stale;
    stale.code = std::string(kStaleSelectionTokenCode);
    stale.target = target;

    std::string expectedDigest;
    if (target.kind == SelectionTargetKind::thing)
    {
        if (target.thing < 0
            || static_cast<std::size_t>(target.thing) >= level.things.size())
        {
            return stale;
        }
        expectedDigest = thingDigest(level, target.thing);
    }
    else
    {
        if (target.sector < 0
            || static_cast<std::size_t>(target.sector) >= level.sectors.size())
        {
            return stale;
        }
        const Sector& sector = level.sectors[static_cast<std::size_t>(target.sector)];
        if (target.kind == SelectionTargetKind::surface
            && (target.surface < 0
                || static_cast<std::size_t>(target.surface) >= sector.surfaces.size()))
        {
            return stale;
        }
        if (target.kind == SelectionTargetKind::vertex
            && (target.vertex < 0
                || static_cast<std::size_t>(target.vertex) >= sector.vertices.size()))
        {
            return stale;
        }
        expectedDigest = sectorGeometryDigest(level, target.sector);
    }

    if (parts[4] != expectedDigest || parts[5] != bindingDigest(context))
    {
        return stale;
    }

    SelectionResolution valid;
    valid.status = SelectionTokenStatus::valid;
    valid.target = target;
    return valid;
}

} // namespace sed::level
