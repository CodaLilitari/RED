#include "agent_edit_operations.hpp"

#include "agent_basic_operations.hpp"
#include "agent_workspace.hpp"

#include "sed/level/geometry.hpp"
#include "sed/level/transform.hpp"
#include "sed/level/vertex_position.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace sed_agent
{
namespace
{
constexpr double kCoordinateLimit = 1000000.0;
constexpr std::size_t kVertexEditWorkLimit = 8000000;

[[noreturn]] void invalid(const std::string& message)
{
    throw AgentError{ kExitUsage, message, "editOperationInvalid" };
}

[[noreturn]] void refuse(const std::string& code, const std::string& message)
{
    throw AgentError{ kExitRefusal, message, code };
}

void fields(const Json& operation, const std::initializer_list<std::string_view> allowed)
{
    for ( const auto& [name, value] : operation.object )
    {
        (void)value;
        if ( std::find(allowed.begin(), allowed.end(), name) == allowed.end() )
        {
            invalid("Unknown edit field '" + name + "'");
        }
    }
}

const Json& field(const Json& operation, const std::string_view name, const Json::Type type)
{
    const auto* value = operation.find(name);
    if ( !value || value->type != type )
    {
        invalid("Edit field '" + std::string(name) + "' is missing or has the wrong type");
    }
    return *value;
}

std::int32_t index(const Json& operation, const std::string_view name, const std::size_t count)
{
    const double value = field(operation, name, Json::Type::Number).number;
    if ( !std::isfinite(value) || value < 0 || std::floor(value) != value
        || value > std::numeric_limits<std::int32_t>::max() || value >= static_cast<double>(count) )
    {
        invalid("Edit field '" + std::string(name) + "' must address an existing record");
    }
    return static_cast<std::int32_t>(value);
}

std::int32_t dimension(const Json& operation, const std::string_view name)
{
    const double value = field(operation, name, Json::Type::Number).number;
    if ( !std::isfinite(value) || std::floor(value) != value || value < 1 || value > 16384 )
    {
        invalid("Texture dimension '" + std::string(name) + "' must be an integer in 1..16384");
    }
    return static_cast<std::int32_t>(value);
}

sed::SurfaceRef surfaceRef(const sed::Level& level, const Json& operation,
    const std::string_view sectorField, const std::string_view surfaceField)
{
    const auto sector = index(operation, sectorField, level.sectors.size());
    return { sector, index(operation, surfaceField, level.sectors[sector].surfaces.size()) };
}

sed::TVector position(const sed::Vertex& vertex)
{
    return { vertex.x, vertex.y, vertex.z };
}

bool bounded(const double value)
{
    return std::isfinite(value) && std::abs(value) <= kCoordinateLimit;
}

bool samePosition(const sed::Vertex& first, const sed::Vertex& second)
{
    return first.x == second.x && first.y == second.y && first.z == second.z;
}

sed::Surface checkedFace(const sed::Sector& sector, const sed::Surface& source, const std::string& code)
{
    if ( source.vertices.size() < 3 || source.vertices.size() > 64
        || source.texVertices.size() != source.vertices.size() )
    {
        refuse(code, "Editing requires 3..64 face corners and matching UV/light records");
    }
    std::set<std::int32_t> corners;
    for ( std::size_t c = 0; c < source.vertices.size(); ++c )
    {
        const auto vertex = source.vertices[c];
        if ( vertex < 0 || static_cast<std::size_t>(vertex) >= sector.vertices.size()
            || !corners.insert(vertex).second )
        {
            refuse(code, "A face has invalid or repeated vertex references");
        }
        const auto& point = sector.vertices[vertex];
        if ( !bounded(point.x) || !bounded(point.y) || !bounded(point.z) )
        {
            refuse(code, "Edited geometry must be finite and within +/-1000000");
        }
        if ( !bounded(source.texVertices[c].u) || !bounded(source.texVertices[c].v) )
        {
            refuse(code, "Edited texture coordinates must be finite and within +/-1000000");
        }
    }
    for ( std::size_t c = 0; c < source.vertices.size(); ++c )
    {
        const auto& first = sector.vertices[source.vertices[c]];
        const auto& second = sector.vertices[source.vertices[(c + 1) % source.vertices.size()]];
        if ( (position(first) - position(second)).lengthSqr() <= 1e-18 )
        {
            refuse(code, "Editing refuses zero-length polygon edges");
        }
    }
    auto face = source;
    face.normal = sed::level::surfaceCalcNormalNewell(sector, face);
    if ( face.normal.lengthSqr() < 0.99 || !sed::level::surfaceIsPlanar(sector, face)
        || !sed::level::surfaceIsConvex(sector, face)
        || !sed::level::surfaceHasSimpleBoundary(sector, face) )
    {
        refuse(code, "Editing requires nonzero planar convex faces");
    }
    return face;
}

void setVertexPosition(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "sector", "vertex", "position" });
    const auto sectorIndex = index(operation, "sector", level.sectors.size());
    const auto vertexIndex = index(operation, "vertex", level.sectors[sectorIndex].vertices.size());
    const auto& values = field(operation, "position", Json::Type::Array).array;
    if ( values.size() != 3 )
    {
        invalid("Vertex position must contain exactly three finite coordinates");
    }
    for ( const auto& value : values )
    {
        if ( value.type != Json::Type::Number || !bounded(value.number) )
        {
            invalid("Vertex position coordinates must be finite numbers within +/-1000000");
        }
    }
    const sed::TVector desired{ values[0].number, values[1].number, values[2].number };
    validateGeometryAppendSource(level);

    sed::level::VertexPositionOptions options;
    options.coordinateLimit = kCoordinateLimit;
    options.maxFaceCorners = 64;
    options.workLimit = kVertexEditWorkLimit;
    const auto result = sed::level::levelSetVertexPositionChecked(level,
        { sectorIndex, vertexIndex }, desired, options);
    using Status = sed::level::VertexPositionStatus;
    switch ( result.status )
    {
    case Status::Changed:
    case Status::Unchanged:
        return;
    case Status::InvalidTarget:
    case Status::InvalidPosition:
        invalid(std::string(result.message));
    case Status::WorkLimitExceeded:
        throw AgentError{ kExitInput, std::string(result.message), "editWorkLimitExceeded" };
    case Status::AdjoinUnsupported:
        refuse("editAdjoinUnsupported", std::string(result.message));
    case Status::ReadOnlySector:
    case Status::GeometryUnsupported:
    case Status::AllocationFailed:
        refuse("editGeometryUnsupported", std::string(result.message));
    }
}

void fitTexture(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "sector", "surface", "width", "height" });
    const auto ref = surfaceRef(level, operation, "sector", "surface");
    const auto width = dimension(operation, "width");
    const auto height = dimension(operation, "height");
    if ( level.uvCoordinateSpace != sed::UvCoordinateSpace::TexturePixels )
    {
        refuse("editUvCoordinateSpaceUnsupported", "Dimension-based fitting requires texture-pixel UV coordinates");
    }
    auto& sector = level.sectors[ref.sector];
    auto& target = sector.surfaces[ref.surface];
    auto fitted = checkedFace(sector, target, "editTextureUnsupported");

    // Legacy SED-MotS AutoTextureCommand fits the dominant world-axis
    // projection to explicit texel dimensions. This is distinct from RED's
    // new-face mapping, whose contract preserves a world-space density.
    const double ax = std::abs(fitted.normal.x);
    const double ay = std::abs(fitted.normal.y);
    const double az = std::abs(fitted.normal.z);
    const int plane = az >= ay && az >= ax ? 0 : (ay >= ax ? 1 : 2);
    const auto project = [plane](const sed::Vertex& point)
    {
        return std::pair{ plane == 2 ? point.y : point.x, plane == 0 ? point.y : point.z };
    };
    double minU = std::numeric_limits<double>::infinity();
    double maxU = -minU;
    double minV = minU;
    double maxV = -minU;
    for ( const auto vertex : fitted.vertices )
    {
        const auto [u, v] = project(sector.vertices[vertex]);
        minU = std::min(minU, u);
        maxU = std::max(maxU, u);
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
    }
    const double rangeU = maxU - minU;
    const double rangeV = maxV - minV;
    if ( !(rangeU > 0) || !(rangeV > 0) )
    {
        refuse("editTextureUnsupported", "Texture fitting requires two nonzero projected dimensions");
    }
    for ( std::size_t c = 0; c < fitted.vertices.size(); ++c )
    {
        const auto [u, v] = project(sector.vertices[fitted.vertices[c]]);
        fitted.texVertices[c].u = static_cast<float>((u - minU) / rangeU * width);
        fitted.texVertices[c].v = static_cast<float>((v - minV) / rangeV * height);
    }
    // Only UV values are authored: corner lighting, scales and flags survive.
    target.texVertices = std::move(fitted.texVertices);
}

struct SharedEdge
{
    std::size_t sourceFirst{};
    std::size_t sourceSecond{};
    std::size_t targetFirst{};
    std::size_t targetSecond{};
};

SharedEdge sharedEdge(const sed::Sector& sourceSector, const sed::Surface& source,
    const sed::Sector& targetSector, const sed::Surface& target)
{
    SharedEdge result{};
    std::size_t matches{};
    for ( std::size_t s = 0; s < source.vertices.size(); ++s )
    {
        const auto nextS = (s + 1) % source.vertices.size();
        const auto& a = sourceSector.vertices[source.vertices[s]];
        const auto& b = sourceSector.vertices[source.vertices[nextS]];
        for ( std::size_t t = 0; t < target.vertices.size(); ++t )
        {
            const auto nextT = (t + 1) % target.vertices.size();
            const auto& x = targetSector.vertices[target.vertices[t]];
            const auto& y = targetSector.vertices[target.vertices[nextT]];
            if ( samePosition(a, x) && samePosition(b, y) )
            {
                result = { s, nextS, t, nextT };
                ++matches;
            }
            else if ( samePosition(a, y) && samePosition(b, x) )
            {
                result = { s, nextS, nextT, t };
                ++matches;
            }
        }
    }
    if ( matches != 1 )
    {
        refuse("editTextureUnsupported", "Texture alignment requires one unambiguous complete shared edge with exact endpoints");
    }
    return result;
}

sed::TVector inwardPerpendicular(const sed::Sector& sector, const sed::Surface& face,
    const sed::TVector& along, const sed::TVector& anchor)
{
    auto perpendicular = face.normal.cross(along);
    if ( perpendicular.normalize() == 0 )
    {
        refuse("editTextureUnsupported", "Texture alignment has a degenerate surface frame");
    }
    double distance{};
    for ( const auto vertex : face.vertices )
    {
        distance += (position(sector.vertices[vertex]) - anchor).dot(perpendicular);
    }
    return distance < 0 ? -perpendicular : perpendicular;
}

bool agrees(const double actual, const double expected)
{
    const double tolerance = 0.00001 + 2 * std::numeric_limits<float>::epsilon()
        * std::max(std::abs(actual), std::abs(expected));
    return bounded(actual) && bounded(expected) && std::abs(actual - expected) <= tolerance;
}

std::vector<std::pair<double, double>> alignmentExpectations(const sed::Sector& sourceSector,
    const sed::Surface& source, const sed::Sector& targetSector, const sed::Surface& target,
    const SharedEdge edge)
{
    const auto anchor = position(sourceSector.vertices[source.vertices[edge.sourceFirst]]);
    auto along = position(sourceSector.vertices[source.vertices[edge.sourceSecond]]) - anchor;
    const double length = along.normalize();
    if ( !(length > 1e-9) )
    {
        refuse("editTextureUnsupported", "Texture alignment has a degenerate shared edge");
    }
    const auto referencePerpendicular = inwardPerpendicular(sourceSector, source, along, anchor);
    const auto targetPerpendicular = inwardPerpendicular(targetSector, target, along, anchor);
    if ( std::abs(source.normal.dot(target.normal)) > 1 - 1e-10
        && referencePerpendicular.dot(targetPerpendicular) > 0 )
    {
        refuse("editTextureUnsupported", "Coplanar texture neighbours must lie on opposite sides of their shared edge");
    }
    const auto& uvA = source.texVertices[edge.sourceFirst];
    const auto& uvB = source.texVertices[edge.sourceSecond];
    const double alongU = (static_cast<double>(uvB.u) - uvA.u) / length;
    const double alongV = (static_cast<double>(uvB.v) - uvA.v) / length;
    double distance{};
    std::size_t furthest{};
    for ( std::size_t c = 0; c < source.vertices.size(); ++c )
    {
        const double candidate = (position(sourceSector.vertices[source.vertices[c]]) - anchor).dot(referencePerpendicular);
        if ( candidate > distance )
        {
            distance = candidate;
            furthest = c;
        }
    }
    if ( !(distance > 1e-9) )
    {
        refuse("editTextureUnsupported", "The reference has no usable texture gradient across the shared edge");
    }
    const auto offset = position(sourceSector.vertices[source.vertices[furthest]]) - anchor;
    const double perpendicularU = (static_cast<double>(source.texVertices[furthest].u) - uvA.u - alongU * offset.dot(along)) / distance;
    const double perpendicularV = (static_cast<double>(source.texVertices[furthest].v) - uvA.v - alongV * offset.dot(along)) / distance;
    for ( std::size_t c = 0; c < source.vertices.size(); ++c )
    {
        const auto delta = position(sourceSector.vertices[source.vertices[c]]) - anchor;
        if ( !agrees(source.texVertices[c].u, uvA.u + alongU * delta.dot(along) + perpendicularU * delta.dot(referencePerpendicular))
            || !agrees(source.texVertices[c].v, uvA.v + alongV * delta.dot(along) + perpendicularV * delta.dot(referencePerpendicular)) )
        {
            refuse("editTextureUnsupported", "Texture alignment requires a consistent affine reference mapping");
        }
    }
    std::vector<std::pair<double, double>> result;
    result.reserve(target.vertices.size());
    for ( const auto vertex : target.vertices )
    {
        const auto delta = position(targetSector.vertices[vertex]) - anchor;
        const double u = uvA.u + alongU * delta.dot(along) - perpendicularU * delta.dot(targetPerpendicular);
        const double v = uvA.v + alongV * delta.dot(along) - perpendicularV * delta.dot(targetPerpendicular);
        if ( !bounded(u) || !bounded(v) )
        {
            refuse("editTextureUnsupported", "Aligned coordinates must be finite and within +/-1000000");
        }
        result.emplace_back(u, v);
    }
    return result;
}

sed::Sector isolatedFace(const sed::Sector& source, const sed::Surface& face)
{
    sed::Sector result{};
    result.surfaces.push_back(face);
    auto& isolated = result.surfaces[0];
    isolated.adjoin.reset();
    for ( std::size_t c = 0; c < face.vertices.size(); ++c )
    {
        result.vertices.push_back(source.vertices[face.vertices[c]]);
        isolated.vertices[c] = static_cast<std::int32_t>(c);
    }
    return result;
}

void alignTexture(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "sector", "surface", "otherSector", "otherSurface" });
    const auto targetRef = surfaceRef(level, operation, "sector", "surface");
    const auto sourceRef = surfaceRef(level, operation, "otherSector", "otherSurface");
    if ( targetRef == sourceRef )
    {
        refuse("editTextureUnsupported", "Texture alignment requires two different surfaces");
    }
    if ( level.uvCoordinateSpace != sed::UvCoordinateSpace::TexturePixels
        && level.uvCoordinateSpace != sed::UvCoordinateSpace::Normalized )
    {
        refuse("editUvCoordinateSpaceUnsupported", "Texture alignment requires a declared UV coordinate space");
    }
    if ( !std::isfinite(level.ppunit) || !(level.ppunit > 0) )
    {
        refuse("editTextureUnsupported", "Texture alignment requires an explicit positive texture density");
    }
    const auto& sourceSector = level.sectors[sourceRef.sector];
    const auto& targetSector = level.sectors[targetRef.sector];
    const auto source = checkedFace(sourceSector, sourceSector.surfaces[sourceRef.surface], "editTextureUnsupported");
    const auto target = checkedFace(targetSector, targetSector.surfaces[targetRef.surface], "editTextureUnsupported");
    const auto edge = sharedEdge(sourceSector, source, targetSector, target);
    const auto expected = alignmentExpectations(sourceSector, source, targetSector, target, edge);

    // The portable stitch service owns the actual mapping. A compact candidate
    // holds only the two faces, so neither source metadata nor the caller's
    // target can change when a frame or density is not representable by it.
    sed::Level candidate{};
    candidate.kind = level.kind;
    candidate.ppunit = level.ppunit;
    candidate.uvCoordinateSpace = level.uvCoordinateSpace;
    candidate.sectors.push_back(isolatedFace(sourceSector, source));
    candidate.sectors.push_back(isolatedFace(targetSector, target));
    if ( !sed::level::levelStitchSurfaces(candidate, { 0, 0 }, { 1, 0 }) )
    {
        refuse("editTextureUnsupported", "The shared UV service cannot align these surface mappings");
    }
    auto& stitched = candidate.sectors[1].surfaces[0];
    for ( std::size_t c = 0; c < expected.size(); ++c )
    {
        if ( !agrees(stitched.texVertices[c].u, expected[c].first)
            || !agrees(stitched.texVertices[c].v, expected[c].second) )
        {
            refuse("editTextureUnsupported", "The shared UV service cannot preserve this reference frame and density across the seam");
        }
    }
    auto& published = level.sectors[targetRef.sector].surfaces[targetRef.surface];
    published.texVertices = std::move(stitched.texVertices);
    published.uScale = stitched.uScale;
    published.vScale = stitched.vScale;
    published.faceFlags = stitched.faceFlags;
}

} // namespace

bool tryApplyEditOperation(sed::Level& level, const Json& operation)
{
    const auto& name = field(operation, "op", Json::Type::String).str;
    if ( name != "setVertexPosition" && name != "fitSurfaceTexture" && name != "alignSurfaceTexture" )
    {
        return false;
    }
    if ( level.kind != sed::ProjectType::JKDF2 && level.kind != sed::ProjectType::MOTS )
    {
        refuse("editOperationUnsupported", "Vertex and texture editing currently supports JK and MotS projects");
    }
    if ( name == "setVertexPosition" )
    {
        setVertexPosition(level, operation);
    }
    else if ( name == "fitSurfaceTexture" )
    {
        fitTexture(level, operation);
    }
    else
    {
        alignTexture(level, operation);
    }
    return true;
}

} // namespace sed_agent
