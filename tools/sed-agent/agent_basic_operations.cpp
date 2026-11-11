#include "agent_basic_operations.hpp"

#include "agent_workspace.hpp"
#include "sed/level/build.hpp"
#include "sed/level/geometry.hpp"
#include "sed/level/transform.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sed_agent
{
namespace
{
constexpr double kCoordinateLimit = 1000000.0;

[[noreturn]] void invalid(const std::string& message)
{
    throw AgentError{ kExitUsage, message, "basicOperationInvalid" };
}

[[noreturn]] void refuse(const std::string& code, const std::string& message)
{
    throw AgentError{ kExitRefusal, message, code };
}

void fields(const Json& operation, const std::initializer_list<std::string_view> allowed)
{
    for ( const auto& [key, value] : operation.object )
    {
        (void)value;
        if ( std::find(allowed.begin(), allowed.end(), key) == allowed.end() )
        {
            invalid("Unknown field '" + key + "' in basic operation");
        }
    }
}

const Json& field(const Json& operation, const char* name, const Json::Type type)
{
    const auto* value = operation.find(name);
    if ( !value || value->type != type )
    {
        invalid(std::string("Missing or invalid '") + name + "'");
    }
    return *value;
}

double boundedNumber(const Json& value, const double minimum, const double maximum,
    const std::string& name)
{
    if ( value.type != Json::Type::Number || !std::isfinite(value.number)
        || value.number < minimum || value.number > maximum )
    {
        invalid(name + " must be finite and within " + std::to_string(minimum)
            + ".." + std::to_string(maximum));
    }
    return value.number;
}

double number(const Json& operation, const char* name, const double minimum, const double maximum)
{
    return boundedNumber(field(operation, name, Json::Type::Number), minimum, maximum, name);
}

std::int32_t index(const Json& operation, const char* name, const std::size_t count)
{
    const double value = number(operation, name, 0.0, std::numeric_limits<std::int32_t>::max());
    if ( std::floor(value) != value || value >= static_cast<double>(count) )
    {
        invalid(std::string(name) + " is outside the current document");
    }
    return static_cast<std::int32_t>(value);
}

sed::TVector vector(const Json& value, const double minimum, const double maximum,
    const std::string& name)
{
    if ( value.type != Json::Type::Array || value.array.size() != 3 )
    {
        invalid(name + " must contain exactly three numbers");
    }
    return { boundedNumber(value.array[0], minimum, maximum, name),
        boundedNumber(value.array[1], minimum, maximum, name),
        boundedNumber(value.array[2], minimum, maximum, name) };
}

sed::TVector position(const sed::Vertex& vertex)
{
    return { vertex.x, vertex.y, vertex.z };
}

sed::SurfaceRef surfaceRef(const sed::Level& level, const Json& operation,
    const char* sectorName = "sector", const char* surfaceName = "surface")
{
    const auto sector = index(operation, sectorName, level.sectors.size());
    return { sector, index(operation, surfaceName, level.sectors[sector].surfaces.size()) };
}

void checkGeometryState(const sed::Level& level)
{
    const bool unknownSectorLine = std::any_of(level.retainedJklSectorUnknownLines.begin(),
        level.retainedJklSectorUnknownLines.end(), [](const auto& lines) { return !lines.empty(); });
    if ( !level.retainedJklUnknownSections.empty() || !level.retainedJklHeaderUnknownLines.empty()
        || unknownSectorLine )
    {
        refuse("basicGeometryUnknownState",
            "Geometry authoring cannot safely update unmodeled JKL sections or records; retain the original and use a typed importer");
    }
    if ( level.hasAmbiguousPendingJklCogSurfaceReferences() )
    {
        refuse("basicGeometryUnknownState",
            "Geometry authoring requires resolved COG bindings when imported surface slots differ from editor order");
    }
}

std::size_t surfaceCount(const sed::Level& level)
{
    std::size_t result = 0;
    for ( const auto& sector : level.sectors )
    {
        if ( sector.surfaces.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) - result )
        {
            refuse("basicGeometryUnsupported", "Surface identity capacity is exhausted");
        }
        result += sector.surfaces.size();
    }
    return result;
}

// Unhydrated COG values have no trustworthy reference/scalar declaration. A
// numeric prefix can be interpreted as an index by legacy integer scans; an
// explicit hexadecimal spelling has an additional possible interpretation.
// Refusal below is conservative ambiguity, never a claim that a raw scalar
// is known to be a reference. Known non-reference CogTypes bypass this path.
std::vector<std::int64_t> unresolvedIndexCandidates(const sed::CogValue& value)
{
    const std::string encoded = value.asString();
    auto text = sed::text::trimView(encoded);
    bool negative = false;
    if ( !text.empty() && (text.front() == '+' || text.front() == '-') )
    {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    std::vector<std::int64_t> result;
    const auto append = [&](const std::string_view digits, const int base)
    {
        std::uint64_t parsed = 0;
        const auto converted = std::from_chars(digits.data(), digits.data() + digits.size(), parsed, base);
        if ( converted.ec == std::errc::invalid_argument )
        {
            return;
        }
        if ( negative && (converted.ec == std::errc::result_out_of_range || parsed != 0) )
        {
            // Negative sentinels cannot be captured by an appended index,
            // including a raw sentinel outside the signed storage range.
            return;
        }
        if ( converted.ec == std::errc::result_out_of_range
            || parsed > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) )
        {
            refuse("basicGeometryUnknownState", "An unresolved COG numeric initializer exceeds the supported index range; resolve its schema before appending geometry");
        }
        const auto signedValue = static_cast<std::int64_t>(parsed);
        result.push_back(negative ? -signedValue : signedValue);
    };
    if ( !text.empty() )
    {
        append(text, 10);
        if ( text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X') )
        {
            append(text.substr(2), 16);
        }
    }
    return result;
}

void checkGeometryAppendReferences(const sed::Level& before, const sed::Level& after)
{
    const auto oldSurfaces = surfaceCount(before);
    const auto newSurfaces = surfaceCount(after);
    const auto oldSectors = before.sectors.size();
    const auto newSectors = after.sectors.size();
    const auto check = [&](const std::int64_t indexValue, const std::size_t first,
        const std::size_t end, const char* space, const bool unresolved,
        const std::size_t cog, const std::size_t parameter)
    {
        if ( indexValue < 0 || static_cast<std::uint64_t>(indexValue) < first
            || static_cast<std::uint64_t>(indexValue) >= end )
        {
            return;
        }
        refuse("basicGeometryReferenceCapture", "Geometry append would make "
            + std::string(unresolved ? "an unresolved numeric COG initializer" : "a typed COG reference")
            + " at cog " + std::to_string(cog) + ", parameter " + std::to_string(parameter)
            + " newly valid in the " + space + " index range [" + std::to_string(first)
            + ", " + std::to_string(end) + "): " + std::to_string(indexValue)
            + ". Resolve or explicitly repair this binding before appending geometry");
    };
    for ( std::size_t cogIndex = 0; cogIndex < before.cogs.size(); ++cogIndex )
    {
        const auto& cog = before.cogs[cogIndex];
        for ( std::size_t parameter = 0; parameter < cog.values.size(); ++parameter )
        {
            const auto& value = cog.values[parameter];
            if ( value.local )
            {
                continue;
            }
            if ( value.cogType == sed::CogType::surface || value.cogType == sed::CogType::sec )
            {
                if ( value.type != sed::ValueType::ptr && value.type != sed::ValueType::integer )
                {
                    refuse("basicGeometryUnknownState", "A typed COG geometry reference has an unsupported value representation");
                }
                if ( value.cogType == sed::CogType::surface )
                {
                    const auto preferred = sed::text::trimView(value.stringValue);
                    if ( preferred.find_first_of(" \t\r\n") != std::string_view::npos )
                    {
                        // JED pair text is the preferred surface binding in
                        // core export. A previously invalid pair must not
                        // acquire a target merely because a room was appended.
                        auto previous = value;
                        auto proposed = value;
                        previous.intValue = -1;
                        proposed.intValue = -1;
                        const bool wasResolved = previous.parseJed(before, preferred);
                        const bool nowResolved = proposed.parseJed(after, preferred);
                        if ( nowResolved != wasResolved
                            || (nowResolved && previous.intValue != proposed.intValue) )
                        {
                            refuse("basicGeometryReferenceCapture", "Geometry append would resolve or retarget preferred COG surface pair '"
                                + std::string(preferred) + "' at cog " + std::to_string(cogIndex)
                                + ", parameter " + std::to_string(parameter)
                                + ". Resolve or explicitly repair this binding before appending geometry");
                        }
                        if ( nowResolved )
                        {
                            // The stable preferred pair takes precedence over
                            // the stored global-index fallback during export.
                            continue;
                        }
                    }
                    check(value.intValue, oldSurfaces, newSurfaces, "surface", false, cogIndex, parameter);
                }
                else
                {
                    check(value.intValue, oldSectors, newSectors, "sector", false, cogIndex, parameter);
                }
            }
            else if ( value.cogType == sed::CogType::unk )
            {
                for ( const auto candidate : unresolvedIndexCandidates(value) )
                {
                    check(candidate, oldSectors, newSectors, "sector", true, cogIndex, parameter);
                    check(candidate, oldSurfaces, newSurfaces, "surface", true, cogIndex, parameter);
                }
            }
        }
    }
}

// A checked copy supplies geometric normals for admission without changing
// authored caches. This bounded route accepts closed convex rooms only; the
// broader GUI geometry tools retain their own precision-aware contracts.
sed::Sector checkedSector(const sed::Sector& source)
{
    if ( source.vertices.size() < 4 || source.surfaces.size() < 4 )
    {
        refuse("basicGeometryUnsupported", "Geometry authoring requires a closed convex sector");
    }
    sed::Sector sector = source;
    sed::TVector center{};
    for ( const auto& vertex : sector.vertices )
    {
        if ( !std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z)
            || std::abs(vertex.x) > kCoordinateLimit || std::abs(vertex.y) > kCoordinateLimit
            || std::abs(vertex.z) > kCoordinateLimit )
        {
            refuse("basicGeometryUnsupported", "Geometry coordinates must be finite and within +/-1000000");
        }
        center = center + position(vertex) * (1.0 / static_cast<double>(sector.vertices.size()));
    }
    std::map<std::pair<std::int32_t, std::int32_t>, std::size_t> edges;
    for ( auto& surface : sector.surfaces )
    {
        if ( surface.vertices.size() < 3 || surface.vertices.size() > 64
            || surface.texVertices.size() != surface.vertices.size() )
        {
            refuse("basicGeometryUnsupported", "Geometry authoring requires 3..64 valid corners and matching UV/light records per face");
        }
        for ( const auto vertex : surface.vertices )
        {
            if ( vertex < 0 || static_cast<std::size_t>(vertex) >= sector.vertices.size() )
            {
                refuse("basicGeometryUnsupported", "A face references an invalid vertex");
            }
        }
        surface.normal = sed::level::surfaceCalcNormalNewell(sector, surface);
        const auto origin = position(sector.vertices[surface.vertices[0]]);
        if ( surface.normal.lengthSqr() < 0.99 || surface.normal.dot(center - origin) <= 1e-8
            || !sed::level::surfaceIsPlanar(sector, surface)
            || !sed::level::surfaceIsConvex(sector, surface) )
        {
            refuse("basicGeometryUnsupported", "Geometry authoring requires planar convex faces wound inward around nonzero volume");
        }
        for ( std::size_t i = 0; i < surface.vertices.size(); ++i )
        {
            const auto a = surface.vertices[i];
            const auto b = surface.vertices[(i + 1) % surface.vertices.size()];
            if ( (position(sector.vertices[a]) - position(sector.vertices[b])).lengthSqr() <= 1e-18 )
            {
                refuse("basicGeometryUnsupported", "Geometry authoring refuses zero-length polygon edges");
            }
            ++edges[{ a, b }];
        }
    }
    for ( const auto& [edge, count] : edges )
    {
        const auto reverse = edges.find({ edge.second, edge.first });
        if ( count != 1 || reverse == edges.end() || reverse->second != 1 )
        {
            refuse("basicGeometryUnsupported", "Geometry authoring requires a closed manifold boundary");
        }
    }
    if ( !sed::level::sectorIsConvex(sector) )
    {
        refuse("basicGeometryUnsupported", "Geometry authoring requires a convex sector");
    }
    return sector;
}

bool separatingFace(const sed::Sector& planes, const sed::Sector& points)
{
    return std::any_of(planes.surfaces.begin(), planes.surfaces.end(), [&](const auto& face)
    {
        const auto origin = position(planes.vertices[face.vertices[0]]);
        return std::all_of(points.vertices.begin(), points.vertices.end(), [&](const auto& point)
        {
            return face.normal.dot(position(point) - origin) <= 0.0;
        });
    });
}

void checkNoOverlap(const sed::Level& level, const sed::Sector& candidate)
{
    const auto checkedCandidate = checkedSector(candidate);
    for ( std::size_t i = 0; i < level.sectors.size(); ++i )
    {
        const auto other = checkedSector(level.sectors[i]);
        if ( !separatingFace(other, checkedCandidate) && !separatingFace(checkedCandidate, other) )
        {
            refuse("basicGeometryOverlap", "New geometry may overlap sector " + std::to_string(i)
                + "; use a free boundary or adjoin exactly matching faces");
        }
    }
}

std::string lowercase(const std::string_view value)
{
    std::string result(value);
    for ( char& c : result )
    {
        if ( c >= 'A' && c <= 'Z' )
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return result;
}

void registerResource(sed::Level& level, const Json& operation, const bool material)
{
    fields(operation, { "op", "name" });
    const auto& name = field(operation, "name", Json::Type::String).str;
    const std::string suffix = material ? ".mat" : ".cmp";
    const auto folded = lowercase(name);
    if ( name.size() <= suffix.size() || name.size() > 127 || !folded.ends_with(suffix)
        || name.find("..") != std::string::npos
        || name.find_first_of("/\\:<>|*?,=\"'#") != std::string::npos
        || std::any_of(name.begin(), name.end(), [](const unsigned char c) { return c < 33 || c > 126; }) )
    {
        invalid("name must be an ASCII resource basename ending in " + suffix + "; paths and traversal are refused");
    }
    auto& table = material ? level.materials : level.colormaps;
    if ( std::any_of(table.begin(), table.end(), [&](const auto& existing) { return lowercase(existing) == folded; }) )
    {
        invalid("Resource is already registered; query and reuse its existing index");
    }
    if ( table.size() >= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) )
    {
        invalid("Resource table is full");
    }
    table.push_back(name);
}

void applyGeometry(sed::Level& level, const Json& operation, const std::string& name)
{
    validateGeometryAppendSource(level);
    if ( name == "addBox" )
    {
        fields(operation, { "op", "center", "halfSize" });
        const auto center = vector(field(operation, "center", Json::Type::Array),
            -kCoordinateLimit, kCoordinateLimit, "center");
        const auto* size = operation.find("halfSize");
        if ( !size )
        {
            invalid("addBox requires halfSize");
        }
        const double scalar = size->type == Json::Type::Number
            ? boundedNumber(*size, 0.0001, 100000.0, "halfSize") : 0.0;
        const auto halfSize = size->type == Json::Type::Number ? sed::TVector{ scalar, scalar, scalar }
            : vector(*size, 0.0001, 100000.0, "halfSize");
        sed::Level candidate = level;
        const auto created = sed::level::levelCreateSectorPrimitiveBox(candidate,
            sed::level::SectorPrimitive::Cube, center - halfSize, center + halfSize);
        if ( created < 0 || candidate.sectors.size() != level.sectors.size() + 1 )
        {
            refuse("basicGeometryUnsupported", "The shared core could not construct the requested box");
        }
        auto& box = candidate.sectors[created];
        // Match the legacy headless box: registration is a separate explicit
        // operation, so adding geometry cannot invent a dflt.mat resource slot.
        for ( auto& surface : box.surfaces )
        {
            surface.material.clear();
            surface.nMat = -1;
        }
        box.colormap = level.colormaps.empty() ? std::string{} : level.colormaps.front();
        box.colormapIndex = level.colormaps.empty() ? -1 : 0;
        checkGeometryAppendReferences(level, candidate);
        checkNoOverlap(level, box);
        level = std::move(candidate);
        return;
    }
    fields(operation, name == "extrudeSurface"
        ? std::initializer_list<std::string_view>{ "op", "sector", "surface", "distance" }
        : std::initializer_list<std::string_view>{ "op", "sector", "surface", "otherSector", "otherSurface" });
    const auto source = surfaceRef(level, operation);
    const auto checkedSource = checkedSector(level.sectors[source.sector]);
    if ( level.sectors[source.sector].surfaces[source.surface].adjoin )
    {
        refuse("basicGeometryUnsupported", "The source surface is already adjoined");
    }
    if ( name == "extrudeSurface" )
    {
        const double distance = number(operation, "distance", 0.0001, 100000.0);
        sed::level::SurfaceExtrudeReport report{};
        const auto preview = sed::level::levelBuildSurfaceExtrudeSectorPreview(level, source, distance, 0.0, &report);
        if ( !preview )
        {
            refuse("basicGeometryUnsupported", "The shared core refused the requested extrusion (reason "
                + std::to_string(static_cast<int>(report.error)) + ")");
        }
        checkNoOverlap(level, *preview);
        sed::Level candidate = level;
        if ( !sed::level::levelExtrudeSurface(candidate, source, distance, 0.0, &report) )
        {
            refuse("basicGeometryUnsupported", "The shared core could not commit the validated extrusion");
        }
        checkGeometryAppendReferences(level, candidate);
        level = std::move(candidate);
        return;
    }
    const auto other = surfaceRef(level, operation, "otherSector", "otherSurface");
    if ( source.sector == other.sector || level.sectors[other.sector].surfaces[other.surface].adjoin )
    {
        refuse("basicGeometryUnsupported", "Adjoin requires unadjoined faces in two different sectors");
    }
    const auto checkedOther = checkedSector(level.sectors[other.sector]);
    if ( !sed::level::surfaceDoOverlap(checkedSource, checkedSource.surfaces[source.surface],
        checkedOther, checkedOther.surfaces[other.surface]) )
    {
        refuse("basicGeometryUnsupported", "Adjoin requires matching complete opposite boundaries; partial overlaps are unsupported");
    }
    if ( !sed::level::levelSysAdjoinSurfaces(level, source, other, true) )
    {
        refuse("basicGeometryUnsupported", "The shared core refused the adjoin");
    }
}

// Sector-local translation: only the named sectors' own vertices move.
// Coincident corners in unlisted sectors do not follow, so adjoins across
// the moved set become seams for validate to report; Things, Lights and
// frames keep their positions. Duplicate sector entries move once.
void applyTranslateSectors(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "sectors", "delta" });
    checkGeometryState(level);
    const auto& sectorsJson = field(operation, "sectors", Json::Type::Array).array;
    if ( sectorsJson.empty() )
    {
        invalid("sectors must be a non-empty array of sector indices");
    }
    std::vector<std::int32_t> sectors{};
    for ( const Json& entry : sectorsJson )
    {
        const double value = boundedNumber(entry, 0.0,
            static_cast<double>(std::numeric_limits<std::int32_t>::max()), "sectors");
        if ( std::floor(value) != value || value >= static_cast<double>(level.sectors.size()) )
        {
            invalid("sectors entries must be sector indices in the current document");
        }
        sectors.push_back(static_cast<std::int32_t>(value));
    }
    std::sort(sectors.begin(), sectors.end());
    sectors.erase(std::unique(sectors.begin(), sectors.end()), sectors.end());
    const auto delta = vector(field(operation, "delta", Json::Type::Array),
        -kCoordinateLimit, kCoordinateLimit, "delta");
    sed::Level candidate = level;
    if ( !sed::level::levelTranslateSectorsChecked(candidate, sectors, delta.x, delta.y, delta.z) )
    {
        refuse("basicGeometryUnsupported", "The shared core refused the requested sector translation");
    }
    level = std::move(candidate);
}

void applyVertexLight(sed::Level& level, const Json& operation)
{
    fields(operation, { "op", "sector", "vertex", "channel", "value" });
    auto& sector = level.sectors[index(operation, "sector", level.sectors.size())];
    const auto vertex = index(operation, "vertex", sector.vertices.size());
    const auto& channel = field(operation, "channel", Json::Type::String).str;
    const float value = static_cast<float>(number(operation, "value", 0.0, 1000.0));
    if ( channel != "mono" && channel != "red" && channel != "green" && channel != "blue" )
    {
        invalid("channel must be mono, red, green, or blue");
    }
    if ( level.kind == sed::ProjectType::JKDF2 && channel != "mono" )
    {
        refuse("basicOperationUnsupported", "JK engine surfaces persist monochrome vertex light only");
    }
    for ( auto& surface : sector.surfaces )
    {
        if ( surface.texVertices.size() != surface.vertices.size() )
        {
            refuse("basicOperationUnsupported", "Surface corner UV/light records are malformed");
        }
        for ( std::size_t i = 0; i < surface.vertices.size(); ++i )
        {
            if ( surface.vertices[i] == vertex )
            {
                auto& color = surface.texVertices[i].color;
                if ( channel == "mono" )
                {
                    color.a = value;
                }
                else if ( channel == "red" )
                {
                    color.r = value;
                }
                else if ( channel == "green" )
                {
                    color.g = value;
                }
                else
                {
                    color.b = value;
                }
            }
        }
    }
}
} // namespace

void validateGeometryAppendSource(const sed::Level& level)
{
    checkGeometryState(level);
}

void validateGeometryAppendReferences(const sed::Level& before, const sed::Level& after)
{
    checkGeometryAppendReferences(before, after);
}

bool tryApplyBasicOperation(sed::Level& level, const Json& operation)
{
    const auto* nameValue = operation.find("op");
    if ( !nameValue || nameValue->type != Json::Type::String )
    {
        return false;
    }
    const auto& name = nameValue->str;
    constexpr std::string_view names[] = { "addBox", "extrudeSurface", "adjoinSurfaces",
        "translateSectors", "registerMaterial", "registerColormap", "setSurfaceMaterial", "setSectorColormap",
        "setSurfaceRenderModes", "setSurfaceUv", "setVertexLight" };
    if ( std::find(std::begin(names), std::end(names), name) == std::end(names) )
    {
        return false;
    }
    if ( level.kind != sed::ProjectType::JKDF2 && level.kind != sed::ProjectType::MOTS )
    {
        refuse("basicOperationUnsupported", "Basic design operations currently support JK and MotS projects");
    }
    if ( name == "addBox" || name == "extrudeSurface" || name == "adjoinSurfaces" )
    {
        applyGeometry(level, operation, name);
    }
    else if ( name == "registerMaterial" || name == "registerColormap" )
    {
        registerResource(level, operation, name == "registerMaterial");
    }
    else if ( name == "setSectorColormap" )
    {
        fields(operation, { "op", "sector", "colormapIndex" });
        auto& sector = level.sectors[index(operation, "sector", level.sectors.size())];
        const auto slot = index(operation, "colormapIndex", level.colormaps.size());
        sector.colormap = level.colormaps[slot];
        sector.colormapIndex = slot;
    }
    else if ( name == "setVertexLight" )
    {
        applyVertexLight(level, operation);
    }
    else if ( name == "translateSectors" )
    {
        applyTranslateSectors(level, operation);
    }
    else
    {
        const auto ref = surfaceRef(level, operation);
        auto& surface = level.sectors[ref.sector].surfaces[ref.surface];
        if ( name == "setSurfaceMaterial" )
        {
            fields(operation, { "op", "sector", "surface", "materialIndex" });
            const auto slot = index(operation, "materialIndex", level.materials.size());
            surface.material = level.materials[slot];
            surface.nMat = slot;
        }
        else if ( name == "setSurfaceRenderModes" )
        {
            fields(operation, { "op", "sector", "surface", "geometryMode", "lightMode", "textureMode" });
            surface.geo = index(operation, "geometryMode", 5);
            surface.light = index(operation, "lightMode", 4);
            surface.tex = index(operation, "textureMode", 4);
        }
        else
        {
            fields(operation, { "op", "sector", "surface", "uv" });
            if ( level.uvCoordinateSpace != sed::UvCoordinateSpace::TexturePixels )
            {
                refuse("basicUvCoordinateSpaceUnsupported",
                    "setSurfaceUv uses texel coordinates and requires a pixel-space document; normalized UVs require texture dimensions");
            }
            const auto& pairs = field(operation, "uv", Json::Type::Array).array;
            if ( pairs.size() < 3 || pairs.size() > 64 || pairs.size() != surface.vertices.size()
                || surface.texVertices.size() != surface.vertices.size() )
            {
                invalid("uv must provide one [u,v] texel pair per surface corner, in query corner order (3..64)");
            }
            for ( std::size_t i = 0; i < pairs.size(); ++i )
            {
                if ( pairs[i].type != Json::Type::Array || pairs[i].array.size() != 2 )
                {
                    invalid("Every uv entry must be a two-number texel pair");
                }
                surface.texVertices[i].u = static_cast<float>(boundedNumber(pairs[i].array[0],
                    -kCoordinateLimit, kCoordinateLimit, "u"));
                surface.texVertices[i].v = static_cast<float>(boundedNumber(pairs[i].array[1],
                    -kCoordinateLimit, kCoordinateLimit, "v"));
            }
        }
    }
    return true;
}
} // namespace sed_agent
