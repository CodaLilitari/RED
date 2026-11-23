#include "sed/level/ray_lighting_resources.hpp"

#include "sed/assets/3do.hpp"
#include "sed/assets/current_archive_snapshot.hpp"
#include "sed/assets/tpl.hpp"
#include "sed/binary_reader.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace sed::level
{
namespace
{
constexpr std::size_t kItemBytes = 16u << 20;
constexpr std::size_t kTotalBytes = 128u << 20;
constexpr std::size_t kWorkLimit = 64'000'000;
constexpr std::int32_t kLevelGeometry = 0x8;
// The ordinary Thing pass with distant Gouraud shading kept: how RED's JO/JA importer draws static world models
// when asked to keep nearer models in front in JK's software renderer. No stock JK level template sets it.
constexpr std::int32_t kIgnoreGouraudDistance = 0x04000000;

[[noreturn]] void refuse(const std::string& reason)
{
    throw std::runtime_error("Ray lighting: " + reason);
}

void checkCancel(const std::function<bool()>& cancelled)
{
    if ( cancelled && cancelled() )
    {
        refuse("cancelled");
    }
}

double linear(std::uint8_t component)
{
    const double value = component / 255.0;
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

struct Bounds
{
    std::array<float, 3> lo{INFINITY, INFINITY, INFINITY};
    std::array<float, 3> hi{-INFINITY, -INFINITY, -INFINITY};

    void include(RayLightVec point)
    {
        const std::array<float, 3> coords{point.x, point.y, point.z};
        for ( std::size_t i = 0; i < coords.size(); ++i )
        {
            if ( !std::isfinite(coords[i]) )
            {
                refuse("nonfinite world geometry");
            }
            lo[i] = std::min(lo[i], coords[i]);
            hi[i] = std::max(hi[i], coords[i]);
        }
    }

    bool overlaps(const Bounds& box) const
    {
        for ( std::size_t i = 0; i < lo.size(); ++i )
        {
            if ( hi[i] < box.lo[i] || lo[i] > box.hi[i] )
            {
                return false;
            }
        }
        return true;
    }
};

RayLightVec narrow(double x, double y, double z)
{
    const double limit = std::numeric_limits<float>::max();
    if ( !(std::abs(x) <= limit && std::abs(y) <= limit && std::abs(z) <= limit) )
    {
        refuse("invalid world coordinates");
    }
    return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}

struct Resolver
{
    const EmissiveResourceReader& reader;
    const std::function<bool()>& cancelled;
    RayLightingResources result;
    using Key = std::pair<std::string, std::string>;
    std::map<Key, std::size_t> resources;
    std::map<Key, ColorF> colors;
    std::map<std::string, assets::CmpPalette> palettes;
    std::vector<Bounds> sectorBounds;
    std::vector<std::size_t> components;
    std::set<std::string> checkedTemplates;
    Level emptyLevel;

    void verifyTemplate(const assets::Templates& templates, const Thing& thing)
    {
        const auto& original = thing.templateName.empty() ? thing.name : thing.templateName;
        auto current = text::lowerAscii(original);
        std::set<std::string> chain;
        while ( !current.empty() && current != "none" && !checkedTemplates.contains(current) )
        {
            charge(1);
            if ( chain.size() >= 1000 || !chain.insert(current).second )
            {
                refuse("cyclic or excessive template inheritance at " + current);
            }
            const auto* item = templates.find(current);
            if ( !item )
            {
                refuse("missing Thing template " + current + " (supply the project template source)");
            }
            current = text::lowerAscii(item->parent);
        }
        checkedTemplates.insert(chain.begin(), chain.end());
    }

    std::size_t component(std::size_t sector)
    {
        while ( components[sector] != sector )
        {
            components[sector] = components[components[sector]];
            sector = components[sector];
        }
        return sector;
    }

    void connect(const Level& level, std::size_t sector, std::size_t faceIndex, const Surface& face)
    {
        const auto ref = *face.adjoin;
        if ( ref.sector < 0 || ref.surface < 0 || static_cast<std::size_t>(ref.sector) >= level.sectors.size() )
        {
            refuse("invalid adjoin in dependency geometry");
        }
        const auto destination = static_cast<std::size_t>(ref.sector);
        const auto& faces = level.sectors[destination].surfaces;
        if ( static_cast<std::size_t>(ref.surface) >= faces.size() )
        {
            refuse("invalid adjoin destination");
        }
        const auto& other = faces[static_cast<std::size_t>(ref.surface)];
        if ( other.adjoin != SurfaceRef{static_cast<std::int32_t>(sector), static_cast<std::int32_t>(faceIndex)} )
        {
            refuse("nonreciprocal adjoin in dependency geometry");
        }
        if ( ((face.adjoinFlags | other.adjoinFlags) & SAF_BlockLight) == 0 )
        {
            components[component(destination)] = component(sector);
        }
    }

    std::size_t bytes{}, work{};

    void charge(std::size_t amount)
    {
        if ( amount > kWorkLimit - work )
        {
            refuse("resource processing work budget exceeded");
        }
        work += amount;
        checkCancel(cancelled);
    }

    const std::vector<std::byte>& read(const std::string& name, const std::string& ext)
    {
        const Key key{name, ext};
        if ( const auto found = resources.find(key); found != resources.end() )
        {
            return *result.dependencies[found->second].bytes;
        }
        if ( resources.size() >= 4096 )
        {
            refuse("resource count exceeds 4096");
        }
        checkCancel(cancelled);
        auto data = reader(name, ext);
        if ( !data )
        {
            refuse("missing " + name + " (required " + ext + ")");
        }
        if ( data->size() > kItemBytes || data->size() > kTotalBytes - bytes )
        {
            refuse("resource byte limit exceeded by " + name);
        }
        bytes += data->size();
        resources.emplace(key, result.dependencies.size());
        result.dependencies.push_back({name, ext, std::make_shared<const std::vector<std::byte>>(std::move(*data))});
        return *result.dependencies.back().bytes;
    }

    ColorF color(const std::string& name, const std::string& cmp)
    {
        Key key{name, cmp};
        if ( const auto found = colors.find(key); found != colors.end() )
        {
            return found->second;
        }
        const auto& data = read(name, ".mat");
        charge(data.size() / 4 + 1);
        // The compatibility reader displays the first color cell. A static
        // transport snapshot cannot silently make that choice for animations.
        if ( data.size() < 16 )
        {
            refuse("truncated MAT " + name);
        }
        ByteReader header(data);
        header.seek(12);
        if ( header.readLE<std::int32_t>() != 1 )
        {
            refuse("animated or invalid cel count in MAT " + name);
        }
        auto parse = [&](const assets::CmpPalette* palette)
        {
            try
            {
                return assets::MatFile::parse(data, palette);
            }
            catch ( const std::exception& e )
            {
                refuse("invalid MAT " + name + ": " + e.what());
            }
        };
        auto mat = parse(nullptr);
        if ( mat.celCount() != 1 || mat.hasColorKey() || mat.colorInfo().alphaBPP != 0 )
        {
            refuse("animated, alpha-keyed or translucent MAT " + name);
        }
        if ( !mat.isTrueColor() )
        {
            auto palette = palettes.find(cmp);
            if ( palette == palettes.end() )
            {
                auto parsed = assets::parseCmpPalette(read(cmp, ".cmp"));
                if ( !parsed )
                {
                    refuse("invalid CMP " + cmp + " for " + name);
                }
                palette = palettes.emplace(cmp, *parsed).first;
            }
            mat = parse(&palette->second);
        }
        const auto& cel = mat.cel(0);
        ColorF value{static_cast<float>(linear(cel.solidColor.r)), static_cast<float>(linear(cel.solidColor.g)),
                     static_cast<float>(linear(cel.solidColor.b)), 1};
        if ( !mat.isColor() )
        {
            const auto image = mat.decodeCelRgba8(0);
            const auto count = image.rgba.size() / 4;
            if ( count == 0 )
            {
                refuse("empty MAT " + name);
            }
            charge(count);
            static const auto lookup = []
            {
                std::array<double, 256> table{};
                for ( std::size_t i = 0; i < table.size(); ++i )
                {
                    table[i] = linear(static_cast<std::uint8_t>(i));
                }
                return table;
            }();
            std::array<double, 3> total{};
            for ( std::size_t pixel = 0; pixel < count; ++pixel )
            {
                if ( (pixel & 65535u) == 0 )
                {
                    checkCancel(cancelled);
                }
                const auto* p = image.rgba.data() + pixel * 4;
                if ( p[3] != 255 )
                {
                    refuse("partial coverage in MAT " + name);
                }
                for ( std::size_t c = 0; c < 3; ++c )
                {
                    total[c] += lookup[p[c]];
                }
            }
            value = {static_cast<float>(total[0] / count), static_cast<float>(total[1] / count),
                     static_cast<float>(total[2] / count), 1};
        }
        colors.emplace(std::move(key), value);
        return value;
    }

    void registerTriangle(const RayLightTriangle& triangle, std::int32_t owner)
    {
        Bounds box;
        box.include(triangle.a);
        box.include(triangle.b);
        box.include(triangle.c);
        charge(sectorBounds.size());
        // A static placement can straddle a portal. Its surface must be
        // visible to rays in every overlapped sector. The nearest sector
        // boundary still clips the ray during the ordinary portal walk.
        for ( std::size_t i = 0; i < sectorBounds.size(); ++i )
        {
            if ( static_cast<std::int32_t>(i) == owner ||
                 (components[i] == components[static_cast<std::size_t>(owner)] && box.overlaps(sectorBounds[i])) )
            {
                if ( result.blockers.size() >= kRayLightingMaxTriangles )
                {
                    refuse("static model blocker triangle limit exceeded");
                }
                result.blockers.push_back({static_cast<std::int32_t>(i), triangle});
            }
        }
    }

    void model(const Level& level, const assets::Templates& templates, const Thing& thing)
    {
        verifyTemplate(templates, thing);
        const auto flags = thingFlags(emptyLevel, &templates, thing);
        // Static world architecture is drawn either in the level-geometry pass or, when an import moved it to the
        // ordinary Thing pass, with the Gouraud distance cutoff disabled. Changing how a building is drawn must not
        // change whether it casts shadows in the bake.
        const bool levelGeometry = (flags & kLevelGeometry) != 0;
        const bool ordinaryArchitecture = !levelGeometry && (flags & kIgnoreGouraudDistance) != 0;
        if ( (!levelGeometry && !ordinaryArchitecture) || (flags & (TF_INVISIBLE | TF_DISABLED)) != 0 )
        {
            return;
        }
        auto field = [&](std::string_view name)
        {
            const auto* v = thingFindValue(emptyLevel, &templates, thing, name);
            return v ? text::trim(v->asString()) : std::string{};
        };
        auto name = field("model3d");
        if ( name.empty() )
        {
            name = field("model3do");
        }
        if ( name.empty() || text::iequalsAscii(name, "none") )
        {
            return;
        }
        const auto move = field("move");
        const auto puppet = field("puppet");
        // A path mover with no frames has nowhere to go: JO/JA world models use move=path without frames.
        const auto frames = field("numframes");
        const bool framed = thingFindValue(emptyLevel, &templates, thing, "frame") != nullptr ||
                            (!frames.empty() && frames != "0");
        const bool moves = !move.empty() && !text::iequalsAscii(move, "none") &&
                           !(text::iequalsAscii(move, "path") && !framed);
        if ( moves || (!puppet.empty() && !text::iequalsAscii(puppet, "none")) )
        {
            // Stock healthpacks, batteries, keys and pushable crates carry LEVELGEO but move under
            // physics: they are not architecture. A level-geometry door, lift or animated fan is, and
            // its shadow would be wrong once it moves, so the bake refuses it instead of guessing.
            if ( ordinaryArchitecture || text::iequalsAscii(move, "physics") )
            {
                return; // only static models are architecture; a moving one stays out of the blocker set
            }
            refuse("moving/animated LEVELGEO model " + name);
        }
        if ( thing.sector < 0 || static_cast<std::size_t>(thing.sector) >= level.sectors.size() )
        {
            refuse("invalid owning sector for " + name);
        }
        const auto& data = read(name, ".3do");
        charge(data.size() / 4 + 1);
        assets::Model3do model;
        try
        {
            model = assets::loadFrom3DO(data);
        }
        catch ( const std::exception& e )
        {
            refuse("invalid 3DO " + name + ": " + e.what());
        }
        const auto xf = TMatrix43::Create({thing.x, thing.y, thing.z}, {thing.pch, thing.yaw, thing.rol});
        const auto& sector = level.sectors[static_cast<std::size_t>(thing.sector)];
        const std::string cmp =
            sector.colormap.empty() || sector.colormap == "-" ? level.getMasterCMP() : sector.colormap;
        for ( const auto& mesh : model.meshes )
        {
            for ( const auto& face : mesh.faces )
            {
                charge(1);
                if ( std::min(mesh.geo, face.geo) < 3 )
                {
                    continue;
                }
                if ( (face.faceFlags & FF_Transluent) != 0 || face.vertices.size() < 3 ||
                     face.vertices.size() > assets::kModel3doMaxFaceVertices )
                {
                    refuse("unsupported translucent or invalid polygon in " + name);
                }
                if ( face.material >= 0 && static_cast<std::size_t>(face.material) < model.materials.size() )
                {
                    color(model.materials[static_cast<std::size_t>(face.material)], cmp);
                }
                else
                {
                    refuse("missing material on drawn face in " + name);
                }
                auto world = [&](std::int32_t index)
                {
                    if ( index < 0 || static_cast<std::size_t>(index) >= mesh.vertices.size() )
                    {
                        refuse("bad vertex index in " + name);
                    }
                    const auto& v = mesh.vertices[static_cast<std::size_t>(index)];
                    const auto p = xf.TransformPoint({v.x, v.y, v.z});
                    return narrow(p.x, p.y, p.z);
                };
                if ( face.vertices.size() > 3 )
                {
                    // Validate after the same world transform and float narrowing
                    // used for intersections. A fan of a concave, crossing or
                    // nonplanar polygon can create a nonexistent shadow caster.
                    std::vector<TVector> points;
                    for ( const auto corner : face.vertices )
                    {
                        const auto q = world(corner);
                        points.emplace_back(q.x, q.y, q.z);
                    }
                    const auto anchor = points.front();
                    double scale = 0;
                    for ( auto& q : points )
                    {
                        q = q - anchor;
                        scale = std::max(scale, std::sqrt(q.Dot(q)));
                    }
                    if ( !(scale > 0) )
                    {
                        refuse("collapsed blocker in " + name);
                    }
                    for ( auto& q : points )
                    {
                        q = {q.x / scale, q.y / scale, q.z / scale};
                    }
                    const auto normal = points[1].Cross(points[2]);
                    const double normalLength = std::sqrt(normal.Dot(normal));
                    charge(points.size() * points.size());
                    for ( std::size_t j = 0; j < points.size(); ++j )
                    {
                        if ( std::abs(points[j].Dot(normal)) > 1.e-5 * normalLength )
                        {
                            refuse("nonplanar blocker in " + name);
                        }
                        const auto next = (j + 1) % points.size();
                        const auto edge = points[next] - points[j];
                        for ( std::size_t k = 0; k < points.size(); ++k )
                        {
                            if ( k != j && k != next &&
                                 !(edge.Cross(points[k] - points[j]).Dot(normal) > 1.e-12 * normalLength) )
                            {
                                refuse("nonconvex or degenerate blocker in " + name);
                            }
                        }
                    }
                }
                const auto a = world(face.vertices.front());
                auto b = world(face.vertices[1]);
                for ( std::size_t i = 2; i < face.vertices.size(); ++i )
                {
                    const auto c = world(face.vertices[i]);
                    registerTriangle({a, b, c}, thing.sector);
                    b = c;
                }
            }
        }
        ++result.modelInstances;
    }
};
} // namespace

EmissiveResourceReader makeRayLightingResourceReader(assets::ResourceLocator locator)
{
    // A copied file-backed archive retains the old directory index. Reopen
    // those directories so a replacement archive cannot replay stale offsets.
    // Preserve captured/override payloads and original mount priority.
    locator = assets::currentArchiveSnapshot(locator);
    return [locator = std::move(locator)](std::string_view name, std::string_view ext)
    {
        bool over{};
        auto bytes = locator.tryReadBinary(name, kItemBytes, over);
        if ( !bytes && !over && !text::iendsWithAscii(name, ext) )
        {
            bytes = locator.tryReadBinary(std::string(name) + std::string(ext), kItemBytes, over);
        }
        if ( over )
        {
            refuse("resource exceeds 16 MiB: " + std::string(name));
        }
        return bytes;
    };
}

RayLightingResources resolveRayLightingResources(const Level& level, const EmissiveResourceReader& reader,
                                                 const std::function<bool()>& cancelled,
                                                 const assets::Templates* templates)
{
    Resolver resolver{reader, cancelled, {}, {}, {}, {}, {}, {}, {}, {}};
    if ( level.sectors.size() > 65'536 || level.things.size() > 65'536 )
    {
        refuse("world sector/Thing budget exceeded");
    }
    resolver.components.resize(level.sectors.size());
    std::iota(resolver.components.begin(), resolver.components.end(), std::size_t{0});
    checkCancel(cancelled);
    resolver.sectorBounds.resize(level.sectors.size());
    for ( std::size_t s = 0; s < level.sectors.size(); ++s )
    {
        const auto& sector = level.sectors[s];
        resolver.charge(sector.vertices.size());
        for ( const auto& v : sector.vertices )
        {
            resolver.sectorBounds[s].include(narrow(v.x, v.y, v.z));
        }
        const auto cmp = sector.colormap.empty() || sector.colormap == "-" ? level.getMasterCMP() : sector.colormap;
        for ( std::size_t f = 0; f < sector.surfaces.size(); ++f )
        {
            resolver.charge(1);
            const auto& face = sector.surfaces[f];
            if ( face.adjoin )
            {
                resolver.connect(level, s, f, face);
                continue;
            }
            RayLightingMaterial material;
            material.sector = static_cast<std::int32_t>(s);
            material.surface = static_cast<std::int32_t>(f);
            material.sky = (face.surfaceFlags & (SF_SkyHorizon | SF_SkyCeiling)) != 0;
            if ( (face.faceFlags & FF_Transluent) != 0 )
            {
                refuse("unsupported translucent world surface " + std::to_string(s) + "/" + std::to_string(f));
            }
            if ( !face.material.empty() && face.material != "-" )
            {
                material.albedo = resolver.color(face.material, cmp);
                if ( face.light == 0 && !material.sky )
                {
                    material.emission = material.albedo;
                }
            }
            if ( resolver.result.materials.size() >= kRayLightingMaxCorners )
            {
                refuse("material surface budget exceeded");
            }
            resolver.result.materials.push_back(material);
        }
    }
    for ( std::size_t i = 0; i < resolver.components.size(); ++i )
    {
        resolver.components[i] = resolver.component(i);
    }
    if ( !level.things.empty() )
    {
        // Embedded definitions win and may derive from a project template.
        // Resolve through one indexed inheritance table rather than scanning
        // the whole embedded table for every property of every placement.
        assets::Templates combined = templates ? *templates : assets::Templates{};
        for ( const auto& item : level.templates )
        {
            resolver.charge(1 + item.values.size());
            combined.addOrUpdate(item);
        }
        for ( const auto& thing : level.things )
        {
            resolver.charge(1 + thing.values.size());
            resolver.model(level, combined, thing);
        }
    }
    return std::move(resolver.result);
}

bool rayLightingResourcesCurrent(const RayLightingResources& snapshot, const EmissiveResourceReader& reader,
                                 const std::function<bool()>& cancelled)
{
    checkCancel(cancelled);
    for ( const auto& item : snapshot.dependencies )
    {
        checkCancel(cancelled);
        const auto fresh = reader(item.name, item.extension);
        if ( !fresh || !item.bytes || *fresh != *item.bytes )
        {
            return false;
        }
    }
    return true;
}

std::uint64_t rayLightingResourceInputsFingerprint(const Level& level, const assets::Templates* templates)
{
    std::uint64_t hash = 14695981039346656037ull;
    auto integer = [&](std::uint64_t value)
    {
        for ( unsigned i = 0; i < 8; ++i )
        {
            hash = (hash ^ (value & 255u)) * 1099511628211ull;
            value >>= 8;
        }
    };
    auto string = [&](std::string_view value)
    {
        integer(value.size());
        for ( unsigned char c : value )
        {
            hash = (hash ^ c) * 1099511628211ull;
        }
    };
    auto values = [&](const auto& items)
    {
        integer(items.size());
        for ( const auto& v : items )
        {
            string(v.name);
            integer(static_cast<std::uint64_t>(v.type));
            integer(static_cast<std::uint64_t>(v.intValue));
            integer(std::bit_cast<std::uint64_t>(v.floatValue));
            string(v.stringValue);
        }
    };
    auto record = [&](const ThingTemplate& item)
    {
        string(item.name);
        string(item.parent);
        values(item.values);
    };
    integer(static_cast<std::uint64_t>(level.kind));
    string(level.getMasterCMP());
    string(level.masterCMP);
    integer(level.colormaps.size());
    for ( const auto& name : level.colormaps )
    {
        string(name);
    }
    integer(level.sectors.size());
    for ( const auto& s : level.sectors )
    {
        string(s.colormap);
        // Water absorption reads tint even when an in-progress property edit
        // has not yet produced an Undo entry or source-generation change.
        integer(std::bit_cast<std::uint32_t>(s.tint.r));
        integer(std::bit_cast<std::uint32_t>(s.tint.g));
        integer(std::bit_cast<std::uint32_t>(s.tint.b));
        integer(s.surfaces.size());
        for ( const auto& f : s.surfaces )
        {
            string(f.material);
            integer(f.surfaceFlags);
            integer(f.faceFlags);
            integer(f.light);
            integer(f.adjoin.has_value());
            integer(f.texVertices.size());
            if ( level.kind == ProjectType::MOTS || level.kind == ProjectType::IJIM )
            {
                for ( const auto& corner : f.texVertices )
                {
                    integer(std::bit_cast<std::uint32_t>(corner.color.a));
                }
            }
        }
    }
    integer(level.templates.size());
    for ( const auto& item : level.templates )
    {
        record(item);
    }
    integer(templates ? templates->size() : 0);
    for ( std::size_t i = 0; templates && i < templates->size(); ++i )
    {
        record(*templates->get(i));
    }
    integer(level.things.size());
    for ( const auto& t : level.things )
    {
        integer(t.id);
        integer(t.sector);
        string(t.templateName);
        string(t.name);
        for ( double p : {t.x, t.y, t.z, t.pch, t.yaw, t.rol} )
        {
            integer(std::bit_cast<std::uint64_t>(p));
        }
        values(t.values);
    }
    return hash;
}
} // namespace sed::level
