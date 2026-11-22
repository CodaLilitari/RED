#include "sed/assets/jo_ja_import.hpp"
#include "jo_ja_player_clip.hpp"
#include "jo_ja_stock_pickups.hpp"
#include "sed/assets/jo_ja_projectile_audit.hpp"

#include "sed/assets/3do.hpp"
#include "sed/assets/cmp.hpp"
#include "sed/assets/episode.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/jkl.hpp"
#include "sed/assets/jo_ja_hybrid_geometry.hpp"
#include "sed/assets/jo_ja_residual_mesh.hpp"
#include "sed/assets/mat.hpp"
#include "sed/assets/mat_write.hpp"
#include "sed/assets/raven_bsp.hpp"
#include "sed/assets/tpl.hpp"
#include "sed/float_chars.hpp"
#include "sed/level/build.hpp"
#include "sed/level/project_defaults.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace sed::assets
{
namespace
{
// Raven's flags differ from Quake III's. See OpenJK game/surfaceflags.h.
constexpr std::uint32_t kRavenSky = 0x00002000u;
constexpr std::uint32_t kRavenNoDraw = 0x00200000u;
constexpr std::size_t kMaxGeneratedBytes = 256u * 1024u * 1024u;

[[noreturn]] void refuse(const std::string &message)
{
    throw std::runtime_error("joJaImport: " + message);
}

std::span<const std::byte> bytesOf(const std::string &value)
{
    return std::as_bytes(std::span(value.data(), value.size()));
}

std::vector<std::byte> copyBytes(const std::string &value)
{
    std::vector<std::byte> bytes(value.size());
    std::memcpy(bytes.data(), value.data(), value.size());
    return bytes;
}

std::string numbered(const char *prefix, std::size_t number, const char *suffix)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << prefix << std::setfill('0') << std::setw(4) << number << suffix;
    return out.str();
}

std::string scalarText(double value)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return out.str();
}

std::string worldModelThingFlags(JoJaWorldModelRenderPass pass, std::string_view footstep = {})
{
    const unsigned standable = 0x40;
    // Ordinary Things normally lose Gouraud shading beyond the engine's distance threshold.
    constexpr unsigned levelGeometry = 0x8, ignoreGouraudDistance = 0x04000000;
    const unsigned renderFlags = pass == JoJaWorldModelRenderPass::LevelGeometry ? levelGeometry : ignoreGouraudDistance;
    const unsigned cue = footstep == "metal" ? 0x400000 : footstep == "earth" ? 0x800000 : 0;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "0x" << std::hex << (standable | renderFlags | cue);
    return out.str();
}

bool safeStem(const std::string &value, std::size_t maximum)
{
    return !value.empty() && value.size() <= maximum &&
           std::all_of(value.begin(), value.end(), [](unsigned char c)
                       { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
}

bool safeMatName(const std::string &value)
{
    return value.size() > 4 && value.ends_with(".mat") && safeStem(value.substr(0, value.size() - 4), 27);
}

void validateOptions(const JoJaImportOptions &options)
{
    (void)joJaSourceProfileName(options.sourceProfile);
    if ( options.target != ProjectType::JKDF2 && options.target != ProjectType::MOTS )
    {
        refuse("target must be JK or MotS");
    }
    if ( options.worldModelRenderPass != JoJaWorldModelRenderPass::LevelGeometry &&
         options.worldModelRenderPass != JoJaWorldModelRenderPass::OrdinaryThing )
    {
        refuse("unknown worldModelRenderPass");
    }
    if ( options.target == ProjectType::MOTS &&
         options.worldModelRenderPass == JoJaWorldModelRenderPass::OrdinaryThing )
    {
        refuse("ordinaryThing is supported only for JK. MotS world models retain level geometry and ARCH lighting.");
    }
    if (options.sourcePlayerClip != JoJaPlayerClipMode::Omit &&
        (options.target != ProjectType::JKDF2 || options.geometryMode != JoJaGeometryMode::ModelScene))
        refuse("source player clips require JK model-scene");
    if ( !safeStem(options.levelName, 24) )
    {
        refuse("levelName must be 1..24 lowercase ASCII letters, digits, underscores or hyphens");
    }
    if ( !std::isfinite(options.scale) || options.scale <= 0 || options.scale > 1 )
    {
        refuse("scale must be finite, greater than zero and at most 1");
    }
    if ( options.patchSubdivisions < 1 || options.patchSubdivisions > 16 )
    {
        refuse("patchSubdivisions must be 1..16");
    }
    if ( options.geometryMode != JoJaGeometryMode::ModelScene && options.geometryMode != JoJaGeometryMode::HybridSectors )
    {
        refuse("unknown geometry mode");
    }
    const bool hybrid = options.geometryMode == JoJaGeometryMode::HybridSectors;
    if ( options.maxSectors < 1 || options.maxSectors > 96 || options.maxFragments < 1 || options.maxFragments > 1000000 )
    {
        refuse("budgets must be 1..96 sectors and 1..1000000 geometry fragments");
    }
    if ( options.trianglesPerModel < 1 || options.trianglesPerModel > (hybrid ? 479u : 170u) )
    {
        refuse(hybrid ? "hybrid trianglesPerModel must be 1..479, subject to independent vertex/UV limits"
                      : "trianglesPerModel must be 1..170 (at most 510 flat corners)");
    }
    if ( options.maxTriangles < 1 || options.maxTriangles > (hybrid ? 250000u : 100000u) ||
         options.maxModels < 1 || options.maxModels > 512 )
    {
        refuse("triangle budget exceeds the geometry mode's limit, or model budget is outside 1..512");
    }
    if ( options.materials.size() > kJoJaMaxMaterials || options.skipShaders.size() > kJoJaMaxMaterials ||
         options.palette.size() > kJoJaMaxPaletteBytes )
    {
        refuse("at most 256 material mappings and 1 MiB of CMP data are admitted");
    }
    if ( options.audioPolicy != JoJaAudioPolicy::StockEquivalents &&
         options.audioPolicy != JoJaAudioPolicy::SourceImport )
    {
        refuse("unknown audio policy");
    }
    if ( options.audioPolicy == JoJaAudioPolicy::SourceImport )
    {
        throw std::runtime_error("joJaSourceAudioUnavailable: importing source sounds requires an explicit verified "
                                 "playback mapping; use stock-equivalents for this scene scaffold");
    }
    std::size_t remaining = kJoJaMaxCapturedMaterialBytes;
    std::set<std::string> shaders;
    std::set<std::string> skipped;
    for ( const auto &name : options.skipShaders )
    {
        if ( name.empty() || name.size() > 63 || !skipped.insert(name).second )
        {
            refuse("skipShaders must have unique nonempty source names");
        }
    }
    std::map<std::string, std::string> names;
    for ( const auto &material : options.materials )
    {
        if ( material.shader.empty() || material.shader.size() > 63 || !shaders.insert(material.shader).second )
        {
            refuse("material mappings require unique, nonempty source shader names");
        }
        if ( skipped.contains(material.shader) )
        {
            refuse("a source shader cannot be both skipped and mapped");
        }
        if ( !safeMatName(material.name) )
        {
            refuse("target MAT name is not a portable engine basename: " + material.name);
        }
        if ( material.bytes.empty() || material.bytes.size() > kJoJaMaxMaterialBytes ||
             material.bytes.size() > remaining )
        {
            refuse("material bytes exceed the per-file or aggregate capture budget");
        }
        remaining -= material.bytes.size();
        const std::string hash = jklSourceSha256Hex(material.bytes);
        const auto [it, inserted] = names.emplace(material.name, hash);
        if ( !inserted && it->second != hash )
        {
            refuse("different material bytes use the same target basename");
        }
    }
}

std::pair<std::string, bool> legacyStockFootstep(const RavenBspShader &shader)
{
    // Preserve the original combined rules and reviewed hashes for old recipes.
    const auto material = shader.surfaceFlags & 0x1fu;
    const bool water = (shader.contents & 0x00000004u) != 0;
    if ( shader.surfaceFlags & 0x00400000u )
    {
        return {"hard", true}; // NOSTEPS has no model-Thing equivalent.
    }
    if ( (shader.surfaceFlags & 0x00008000u) || material == 3 || material == 4 )
    {
        return {"metal", water};
    }
    if ( material == 7 || material == 8 || material == 9 || material == 14 || material == 17 )
    {
        return {"earth", water};
    }
    const bool missing = water || material == 1 || material == 2 || material == 5 || material == 6 || material == 13 ||
                         material == 19 || material == 20 || material == 21 || material == 22 || material == 24 ||
                         material == 25 || material == 27;
    return {"hard", missing};
}

void assignStockFootstep(JoJaMaterialReport &material, const RavenBspShader &shader, JoJaSourceProfile profile)
{
    if ( profile == JoJaSourceProfile::Legacy )
    {
        const auto [cue, review] = legacyStockFootstep(shader);
        material.footstep = cue;
        material.footstepNeedsReview = review;
        return;
    }
    material.noStepsFlag = (shader.surfaceFlags & 0x00400000u) != 0;
    const auto assign = [&](const char *source, const char *target, bool approximate = false)
    {
        material.sourceFootstep = source;
        material.footstep = target;
        material.footstepNeedsReview = approximate || material.noStepsFlag || (shader.contents & 0x4u) != 0;
    };
    if ( profile == JoJaSourceProfile::JediOutcastMultiplayer )
    {
        if ( material.noStepsFlag )
        {
            assign("no steps", "hard", true);
        }
        else if ( shader.surfaceFlags & 0x00008000u )
        {
            assign("metal", "metal");
        }
        else
        {
            assign("generic", "hard");
        }
        return;
    }
    // JA MP _PlayerFootStep selects sound families by MATERIAL_MASK.
    // A map can carry JO's metal bit as well, but it has no role here.
    switch ( material.sourceMaterial )
    {
    case 1: case 2: assign("wood", "hard", true); break;
    case 3: assign("metal", "metal"); break;
    case 4: assign("pipe", "metal", true); break;
    case 5: case 6: assign("grass", "hard", true); break;
    case 7: assign("dirt", "earth"); break;
    case 8: assign("sand", "earth", true); break;
    case 9: assign("gravel", "earth", true); break;
    case 13: assign("stone (water material)", "hard", true); break;
    case 14: assign("snow", "earth", true); break;
    case 17: assign("mud", "earth", true); break;
    case 21: case 22: case 24: case 25: case 27: assign("rug", "hard", true); break;
    default: assign("stone", "hard"); break;
    }
}

float cornerLight(const RavenBsp &bsp, const RavenBspSurface &surface, const RavenBspVertex &vertex,
                  JoJaImportResult &result)
{
    if ( surface.lightmapIndex < 0 )
    {
        ++result.vertexLitCorners;
        return std::clamp(0.2126f * vertex.color.r + 0.7152f * vertex.color.g + 0.0722f * vertex.color.b, 0.0f, 1.0f);
    }
    ++result.lightmapCorners;
    // Clamp in floating point before converting. Valid finite source UVs may
    // be far outside 0..1 and cannot safely be cast to integer first.
    const double x = std::clamp(vertex.lightmapU * 128.0 - 0.5, 0.0, 127.0);
    const double y = std::clamp(vertex.lightmapV * 128.0 - 0.5, 0.0, 127.0);
    const auto x0 = static_cast<std::size_t>(std::floor(x));
    const auto y0 = static_cast<std::size_t>(std::floor(y));
    const auto x1 = std::min<std::size_t>(127, x0 + 1);
    const auto y1 = std::min<std::size_t>(127, y0 + 1);
    const double fx = x - static_cast<double>(x0), fy = y - static_cast<double>(y0);
    const std::size_t base = static_cast<std::size_t>(surface.lightmapIndex) * 128u * 128u * 3u;
    const auto luminance = [&](std::size_t px, std::size_t py)
    {
        const auto at = base + (py * 128u + px) * 3u;
        return (0.2126 * bsp.lightmaps.at(at) + 0.7152 * bsp.lightmaps.at(at + 1) + 0.0722 * bsp.lightmaps.at(at + 2)) /
               255.0;
    };
    return static_cast<float>(luminance(x0, y0) * (1 - fx) * (1 - fy) + luminance(x1, y0) * fx * (1 - fy) +
                              luminance(x0, y1) * (1 - fx) * fy + luminance(x1, y1) * fx * fy);
}

std::string placeholderMat(std::size_t index, const CmpPalette *palette)
{
    RgbaImage image{16, 16, std::vector<std::uint8_t>(16u * 16u * 4u)};
    const std::array<std::uint8_t, 3> color{static_cast<std::uint8_t>(80 + index * 73 % 144),
                                            static_cast<std::uint8_t>(80 + index * 47 % 144),
                                            static_cast<std::uint8_t>(80 + index * 109 % 144)};
    for ( std::size_t y = 0; y < 16; ++y )
    {
        for ( std::size_t x = 0; x < 16; ++x )
        {
            const bool bright = ((x / 4 + y / 4) % 2) == 0;
            const auto at = (y * 16 + x) * 4;
            for ( std::size_t c = 0; c < 3; ++c )
            {
                image.rgba[at + c] = bright ? color[c] : 24;
            }
            image.rgba[at + 3] = 255;
        }
    }
    return palette ? buildIndexedMat({image}, *palette).bytes : buildRgb565Mat({image}).bytes;
}

TVector scaled(const TVector &value, double scale)
{
    const TVector result{value.x * scale, value.y * scale, value.z * scale};
    if ( !std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z) ||
         std::abs(result.x) > 100000 || std::abs(result.y) > 100000 || std::abs(result.z) > 100000 )
    {
        refuse("scaled geometry must remain within +/-100000 Jedi Knight units");
    }
    return result;
}

void extend(TVector &low, TVector &high, const TVector &point)
{
    low.x = std::min(low.x, point.x);
    low.y = std::min(low.y, point.y);
    low.z = std::min(low.z, point.z);
    high.x = std::max(high.x, point.x);
    high.y = std::max(high.y, point.y);
    high.z = std::max(high.z, point.z);
}

std::string_view field(const RavenBspEntity &entity, std::string_view name)
{
    std::string_view value;
    bool found = false;
    for ( const auto &item : entity.fields )
    {
        if ( item.first != name )
        {
            continue;
        }
        if ( found )
        {
            refuse("an entity repeats the field needed for conversion: " + std::string(name));
        }
        value = item.second;
        found = true;
    }
    return value;
}

bool parseNumber(std::string_view value, double &result)
{
    const auto read = text::fromChars(value.data(), value.data() + value.size(), result);
    return read.ec == std::errc{} && read.ptr == value.data() + value.size() && std::isfinite(result);
}

std::vector<Thing> playerStarts(const RavenBsp &bsp, double scale, JoJaImportResult &result)
{
    std::vector<Thing> out;
    std::size_t omitted = 0;
    const bool hasDeathmatch = std::any_of(bsp.entities.begin(), bsp.entities.end(), [](const auto &entity)
                                           { return field(entity, "classname") == "info_player_deathmatch"; });
    for ( const auto &entity : bsp.entities )
    {
        const auto kind = field(entity, "classname");
        if ( kind != "info_player_deathmatch" && kind != "info_player_start" )
        {
            continue;
        }
        if ( hasDeathmatch && kind == "info_player_start" )
        {
            result.warnings.push_back(
                "Single-player start retained in entity dump; deathmatch starts take precedence in this FFA scaffold.");
            continue;
        }
        const std::string origin(field(entity, "origin"));
        std::istringstream stream(origin);
        stream.imbue(std::locale::classic());
        TVector position;
        std::string tail;
        if ( !(stream >> position.x >> position.y >> position.z) || stream >> tail )
        {
            refuse("a source player start has no valid three-number origin");
        }
        position = scaled(position, scale);
        double yaw = 0;
        const auto angle = field(entity, "angle");
        if ( !angle.empty() && !parseNumber(angle, yaw) )
        {
            refuse("a source player start has invalid yaw");
        }
        const auto angles = field(entity, "angles");
        if ( !angles.empty() )
        {
            double pitch = 0, roll = 0;
            std::istringstream rotation{std::string(angles)};
            rotation.imbue(std::locale::classic());
            if ( !(rotation >> pitch >> yaw >> roll) || rotation >> tail || !std::isfinite(pitch) ||
                 !std::isfinite(yaw) || !std::isfinite(roll) )
            {
                refuse("a source player start has invalid angles");
            }
        }
        if ( out.size() >= 32 )
        {
            ++omitted;
            continue;
        }
        Thing start;
        start.templateName = "walkplayer";
        start.name = numbered("spawn", out.size(), "");
        start.sector = 0;
        start.x = position.x;
        start.y = position.y;
        start.z = position.z;
        start.yaw = std::fmod(std::fmod(yaw - 90, 360) + 360, 360);
        out.push_back(std::move(start));
    }
    if ( omitted )
    {
        result.warnings.push_back(std::to_string(omitted) +
                                  " player starts beyond the 32-start import limit remain only in the entity dump.");
    }
    return out;
}

void addPlayerTemplates(Level &level)
{
    // Same target defaults as RED's data/master.tpl and data/mots.tpl. These
    // are resource references only; no stock game bytes are included.
    std::string source =
        "_actor none orient=(0/0/0) type=actor collide=1 move=physics thingflags=0x20000000 mass=150 physflags=0x4a4f "
        "maxrotvel=200 maxvel=1 health=40 maxhealth=40 maxrotthrust=180 jumpspeed=1.5 eyeoffset=(0/0/.037) "
        "minheadpitch=-80 maxheadpitch=80 lightoffset=(0/.07/.04) lightintensity=.8\n";
    source += level.kind == ProjectType::MOTS
                  ? "walkplayer _actor type=player thingflags=0x20000401 light=.2 model3d=kk.3do size=.065 "
                    "movesize=.065 puppet=ky.pup soundclass=ky.snd cog=kyle_m.cog surfdrag=3 airdrag=.5 staticdrag=.3 "
                    "health=100 maxhealth=100 maxthrust=2 typeflags=0x1 error=.5 fov=.71 chance=1\n"
                  : "walkplayer _actor type=player thingflags=0x20000401 light=.2 model3d=ky.3do size=.065 "
                    "movesize=.065 puppet=ky.pup soundclass=ky.snd cog=kyle.cog surfdrag=3 airdrag=.5 staticdrag=.3 "
                    "health=100 maxhealth=100 maxthrust=2 typeflags=0x1 error=.5 fov=.71 chance=1\n";
    std::istringstream in(source);
    const auto templates = loadTemplates(in);
    for ( std::size_t i = 0; i < templates.size(); ++i )
    {
        level.templates.push_back(*templates.get(i));
    }
}

void addHybridRooms(Level& level, const JoJaHybridGeometry& geometry,
                    const std::vector<JoJaMaterialReport>& materials)
{
    for ( const auto& cell : geometry.cells )
    {
        auto& sector = level.newSector();
        sector.ambient = ColorBlack;
        sector.colormap = level.masterCMP;
        sector.colormapIndex = 0;
        std::map<std::tuple<double, double, double>, std::int32_t> vertices;
        for ( const auto& face : cell.faces )
        {
            Surface surface;
            surface.nMat = -1;
            surface.nAdj = -1;
            surface.normal = face.normal;
            surface.light = 3;
            surface.tex = 1;
            surface.extraLight = ColorBlack;
            if ( face.neighbor >= 0 )
            {
                surface.adjoin = SurfaceRef{face.neighbor, face.mirror};
                surface.adjoinFlags = SAF_Visible | SAF_Move | SAF_AllowSoundPass;
            }
            else
            {
                surface.surfaceFlags = SF_Collision;
                if ( face.normal.z > 0.5 )
                {
                    surface.surfaceFlags |= SF_Floor;
                }
            }
            const JoJaMaterialReport* material = face.material ? &materials.at(*face.material) : nullptr;
            if ( material )
            {
                surface.geo = 4;
                surface.material = material->name;
                surface.nMat = static_cast<std::int32_t>(std::find(level.materials.begin(), level.materials.end(),
                    material->name) - level.materials.begin());
                if ( material->footstep == "metal" )
                {
                    surface.surfaceFlags |= 0x10000;
                }
                else if ( material->footstep == "earth" )
                {
                    surface.surfaceFlags |= 0x80000;
                }
            }
            for ( const auto& corner : face.corners )
            {
                const auto& p = corner.position;
                const auto [at, added] = vertices.emplace(std::tuple{p.x, p.y, p.z},
                    static_cast<std::int32_t>(sector.vertices.size()));
                if ( added )
                {
                    sector.vertices.push_back({p.x, p.y, p.z, 0});
                }
                surface.vertices.push_back(at->second);
                const double u = material ? corner.u * material->width : 0;
                const double v = material ? corner.v * material->height : 0;
                if ( !std::isfinite(u) || !std::isfinite(v) || std::abs(u) > std::numeric_limits<float>::max() ||
                     std::abs(v) > std::numeric_limits<float>::max() )
                {
                    refuse("native texture coordinates exceed target precision");
                }
                // Native TexVertex color.a is the classic JKL intensity, not
                // model opacity. cornerLight already supplies scalar RGB;
                // MotS also writes those RGB channels beside the mono value.
                const ColorF nativeLight{corner.color.r, corner.color.g, corner.color.b, corner.color.r};
                surface.texVertices.push_back({static_cast<float>(u), static_cast<float>(v),
                    material ? nativeLight : ColorBlack});
            }
            sector.surfaces.push_back(std::move(surface));
        }
    }
}

void optimizeGeneratedMesh(Model3doMesh& mesh, const JoJaImportOptions& options, JoJaImportResult& result)
{
    if ( options.meshOptimization == MeshOptimization::Off )
    {
        return;
    }
    const auto stats = optimizeModel3doMesh(mesh, meshOptimizationLimits(options.meshOptimization));
    result.optimizedVerticesIn += stats.verticesIn;
    result.optimizedVerticesOut += stats.verticesOut;
    result.optimizedFacesIn += stats.facesIn;
    result.optimizedFacesOut += stats.facesOut;
    result.worstNormalTurnDegrees = std::max(result.worstNormalTurnDegrees, stats.worstNormalTurnDegrees);
}

/** A generated world model whose file is written once every model exists. */
struct PendingWorldModel
{
    std::string path;
    Model3do model;
    TVector offset;
    std::size_t thing{};
};

/**
 * Loose optimization removes straight corners across every generated world mesh at once, so that a
 * corner leaves a position only when no face anywhere keeps it. Then the 3DOs are written and MotS
 * ARCH light rows are taken from the final vertices.
 */
void finishWorldModels(Level& level, std::vector<PendingWorldModel>& pending, const JoJaImportOptions& options,
                       JoJaImportResult& result, const std::function<void(std::string, std::string)>& addFile)
{
    if ( options.meshOptimization == MeshOptimization::Loose )
    {
        std::vector<MeshPlacement> placements;
        for ( auto& world : pending )
        {
            for ( auto& mesh : world.model.meshes )
            {
                placements.push_back({&mesh, world.offset});
            }
        }
        std::vector<TVector> fixed;
        for ( const auto& sector : level.sectors )
        {
            for ( const auto& vertex : sector.vertices )
            {
                fixed.push_back({vertex.x, vertex.y, vertex.z});
            }
        }
        const auto stats =
            pruneStraightCorners(placements, fixed, meshOptimizationLimits(options.meshOptimization));
        result.straightCornersRemoved = stats.cornersRemoved;
        result.optimizedFanTrianglesIn = stats.fanTrianglesIn;
        result.optimizedFanTrianglesOut = stats.fanTrianglesOut;
        result.optimizedVerticesOut -= stats.verticesIn - stats.verticesOut;
    }
    for ( auto& world : pending )
    {
        addFile(world.path, saveTo3DO(world.model, {.scalarVersion21Vertices = true}));
        if ( options.target == ProjectType::MOTS )
        {
            MotsArchLightObject architecture;
            architecture.wireIndex = static_cast<std::int32_t>(level.motsArchLighting.size());
            MotsArchLightMesh lighting;
            lighting.wireIndex = 0;
            for ( const auto& vertex : world.model.meshes[0].vertices )
            {
                const float light = vertex.color.r;
                lighting.vertices.push_back({light, light, light, light});
            }
            architecture.meshes.push_back(std::move(lighting));
            level.things.at(world.thing).archLightingIndex = architecture.wireIndex;
            level.motsArchLighting.push_back(std::move(architecture));
        }
    }
    pending.clear();
}

void addHybridModels(Level& level, const JoJaHybridGeometry& geometry, const JoJaImportOptions& options,
                     JoJaImportResult& result, std::vector<PendingWorldModel>& pending)
{
    using Group = std::pair<std::size_t, std::string>;
    std::map<Group, std::vector<const JoJaHybridModelFace*>> groups;
    for ( const auto& face : geometry.residualPolygons )
    {
        groups[{face.sector, result.materials.at(face.material).footstep}].push_back(&face);
    }
    for ( const auto& [group, source] : groups )
    {
        std::vector<JoJaResidualFace> faces;
        faces.reserve(source.size());
        for ( const auto* input : source )
        {
            JoJaResidualFace face;
            face.material = static_cast<std::int32_t>(input->material);
            const auto& material = result.materials.at(input->material);
            for ( const auto& corner : input->corners )
            {
                const auto& p = corner.position;
                const double u = corner.u * material.width, v = corner.v * material.height;
                if ( !std::isfinite(u) || !std::isfinite(v) || std::abs(u) > std::numeric_limits<float>::max() ||
                     std::abs(v) > std::numeric_limits<float>::max() )
                {
                    refuse("residual texture coordinates exceed target precision");
                }
                face.corners.push_back({{p.x, p.y, p.z, corner.color}, corner.normal,
                                        {static_cast<float>(u), static_cast<float>(v)}});
            }
            faces.push_back(std::move(face));
        }
        const auto& cell = geometry.cells.at(group.first);
        auto packets = packJoJaResidualFaces(faces, options.trianglesPerModel, cell.interior,
            [&](const TVector& point) { return joJaHybridContains(cell, point); });
        if ( packets.size() > options.maxModels - result.models.size() )
        {
            refuse("hybrid residual models exceed maxModels; reduce sector count or raise the chunk face budget");
        }
        for ( auto& packet : packets )
        {
            JoJaModelReport report;
            report.name = numbered("jjm", result.models.size(), ".3do");
            for ( const auto& face : packet.mesh.faces )
            {
                report.triangles += face.vertices.size() - 2;
            }
            report.position = packet.center;
            report.radius = packet.mesh.radius;
            report.footstep = group.second;
            report.sector = group.first;
            std::set<std::size_t> sourceSurfaces;
            for ( std::size_t i = packet.firstFace; i < packet.firstFace + packet.mesh.faces.size(); ++i )
            {
                sourceSurfaces.insert(source[i]->sourceSurface);
            }
            report.sourceSurfaces.assign(sourceSurfaces.begin(), sourceSurfaces.end());
            Model3do model;
            model.version = 2.1;
            model.geosetCount = 1;
            model.hierarchyTransformsApplied = false;
            model.radius = packet.mesh.radius;
            std::map<std::int32_t, std::int32_t> localMaterials;
            for ( auto& face : packet.mesh.faces )
            {
                const auto [at, added] = localMaterials.emplace(face.material, static_cast<std::int32_t>(model.materials.size()));
                if ( added )
                {
                    model.materials.push_back(result.materials.at(static_cast<std::size_t>(face.material)).name);
                }
                face.material = at->second;
            }
            optimizeGeneratedMesh(packet.mesh, options, result);
            model.meshes.push_back(std::move(packet.mesh));
            Model3doNode node;
            node.id = 0;
            node.mesh = 0;
            node.parent = -1;
            node.name = "world";
            model.nodes.push_back(std::move(node));
            if ( !model3doEngineLimitIssues(model).empty() )
            {
                refuse("hybrid model exceeds the classic loader limits");
            }
            ThingTemplate object;
            object.name = numbered("jjm", result.models.size(), "");
            object.parent = "_jjworld";
            object.values = {deserializeTplValue("model3d", report.name),
                deserializeTplValue("thingflags", worldModelThingFlags(options.worldModelRenderPass, report.footstep)),
                deserializeTplValue("size", scalarText(model.radius + 0.001)),
                deserializeTplValue("movesize", scalarText(model.radius + 0.001))};
            level.templates.push_back(object);
            auto& thing = level.newThing();
            report.thing = level.things.size() - 1;
            thing.templateName = object.name;
            thing.name = object.name;
            thing.sector = static_cast<std::int32_t>(report.sector);
            thing.x = report.position.x;
            thing.y = report.position.y;
            thing.z = report.position.z;
            pending.push_back({"3do/" + report.name, std::move(model), report.position, report.thing});
            result.models.push_back(std::move(report));
        }
    }
}

JoJaVisibilityReport describeVisibility(const RavenBsp& bsp, const RavenVisibilityQuery& query, const Level& level,
                                       const JoJaHybridGeometry* hybrid, const std::vector<JoJaModelReport>& models)
{
    const auto view = ravenPvsView(bsp, query.viewCluster);
    JoJaVisibilityReport report;
    report.rowUsable = view.rowUsable;
    const auto target = [&](std::size_t sector, std::size_t index, std::optional<std::size_t> model,
                            std::span<const std::size_t> sources)
    {
        const auto state = ravenMeshVisibility(view, sources);
        report.outside += state == RavenPvsState::Outside;
        report.unknown += state == RavenPvsState::Unknown;
        if ( report.total >= query.offset && report.total - query.offset < kRavenVisibilityPage )
        {
            report.page.push_back({sector, index, model, {sources.begin(), sources.end()}, state});
        }
        ++report.total;
    };
    for ( std::size_t s = 0; s < level.sectors.size(); ++s )
    {
        for ( std::size_t f = 0; f < level.sectors[s].surfaces.size(); ++f )
        {
            std::span<const std::size_t> sources;
            if ( hybrid )
            {
                const auto& face = hybrid->cells.at(s).faces.at(f);
                if ( face.material )
                {
                    sources = {&face.sourceSurface, 1};
                }
            }
            target(s, f, std::nullopt, sources);
        }
    }
    // Both generators emit a single mesh per model. Its placed Thing was recorded during emission.
    for ( std::size_t i = 0; i < models.size(); ++i )
    {
        target(models[i].sector, models[i].thing, i, models[i].sourceSurfaces);
    }
    return report;
}

} // namespace

JoJaImportResult prepareJoJaImport(const std::span<const std::byte> source, const JoJaImportOptions &options)
{
    validateOptions(options);
    const auto bsp = parseRavenBsp(source);
    if ( bsp.models.empty() )
    {
        refuse("the BSP has no world model");
    }
    JoJaImportResult result;
    result.sourceSha256 = jklSourceSha256Hex({reinterpret_cast<const char *>(source.data()), source.size()});
    result.sourceEntities = bsp.entities.size();
    result.sourceBrushes = bsp.brushCount;
    result.sourceSurfaces = bsp.surfaces.size();
    result.sourceShaders = bsp.shaders.size();
    result.omittedSubmodels = bsp.models.size() - 1;
    const auto palette = options.palette.empty() ? std::optional<CmpFile>{} : parseCmp(bytesOf(options.palette));
    if ( !options.palette.empty() && !palette )
    {
        refuse("the captured CMP is incomplete or invalid");
    }

    std::map<std::string, const JoJaMaterialInput *> requested;
    for ( const auto &material : options.materials )
    {
        requested.emplace(material.shader, &material);
    }
    const std::set<std::string> explicitSkips(options.skipShaders.begin(), options.skipShaders.end());
    for ( const auto &name : options.skipShaders )
    {
        if ( std::none_of(bsp.shaders.begin(), bsp.shaders.end(),
                          [&](const auto &shader) { return shader.name == name; }) )
        {
            refuse("skipShaders names an absent source shader: " + name);
        }
    }
    for ( const auto &material : options.materials )
    {
        if ( std::none_of(bsp.shaders.begin(), bsp.shaders.end(),
                          [&](const auto &shader) { return shader.name == material.shader; }) )
        {
            refuse("material mapping names an absent source shader: " + material.shader);
        }
    }
    std::map<std::int32_t, std::size_t> shaderMaterials;
    std::map<std::string, std::string> files;
    std::size_t generatedBytes = 0;
    const auto addFile = [&](std::string name, std::string data)
    {
        const auto found = files.find(name);
        if ( found != files.end() )
        {
            if ( found->second != data )
            {
                refuse("two generated resources collide: " + name);
            }
            return;
        }
        if ( data.size() > kMaxGeneratedBytes - generatedBytes )
        {
            refuse("generated resources exceed 256 MiB");
        }
        generatedBytes += data.size();
        files.emplace(std::move(name), std::move(data));
    };
    const auto materialFor = [&](std::int32_t shaderIndex)
    {
        if ( const auto found = shaderMaterials.find(shaderIndex); found != shaderMaterials.end() )
        {
            return found->second;
        }
        if ( result.materials.size() >= 256 )
        {
            refuse("more than 256 visible shaders require a smaller import");
        }
        const auto &shader = bsp.shaders.at(static_cast<std::size_t>(shaderIndex));
        JoJaMaterialReport material;
        material.shader = shader.name;
        material.sourceMaterial = shader.surfaceFlags & 0x1fu;
        assignStockFootstep(material, shader, options.sourceProfile);
        std::string data;
        if ( const auto mapping = requested.find(shader.name); mapping != requested.end() )
        {
            material.name = mapping->second->name;
            data = mapping->second->bytes;
        }
        else
        {
            if ( !options.allowPlaceholders )
            {
                refuse("unmapped visible shader: " + shader.name);
            }
            material.placeholder = true;
            material.name = numbered("jjp", static_cast<std::size_t>(shaderIndex), ".mat");
            data = placeholderMat(static_cast<std::size_t>(shaderIndex), palette ? &palette->palette : nullptr);
        }
        const auto parsed = MatFile::parse(copyBytes(data), palette ? &palette->palette : nullptr);
        if ( parsed.isColor() || parsed.celCount() != 1 )
        {
            refuse("scene materials require one texture cel: " + material.name);
        }
        if ( parsed.bitsPerPixel() != 8 && parsed.bitsPerPixel() != 16 )
        {
            refuse("scene materials require classic 8-bit indexed or 16-bit RGB565 MATs");
        }
        if ( parsed.bitsPerPixel() == 8 && !palette )
        {
            refuse("indexed MATs require the explicit matching CMP");
        }
        const auto &channels = parsed.colorInfo();
        if ( parsed.bitsPerPixel() == 16 &&
             (channels.mode != ColorMode::RGB || channels.redBPP != 5 || channels.greenBPP != 6 ||
              channels.blueBPP != 5 || channels.redShl != 11 || channels.greenShl != 5 || channels.blueShl != 0 ||
              channels.redShr != 3 || channels.greenShr != 2 || channels.blueShr != 3 || channels.alphaBPP != 0) )
        {
            refuse("16-bit scene materials require the explicit opaque RGB565 channel layout");
        }
        if ( parsed.bitsPerPixel() == 8 && channels.mode != ColorMode::Indexed &&
             channels.mode != ColorMode::IndexedCols )
        {
            refuse("8-bit scene materials require an indexed channel layout");
        }
        if ( options.geometryMode == JoJaGeometryMode::HybridSectors &&
             (resolveMatTransparencyMode(channels, 0) != MatTransparencyMode::Opaque || parsed.hasColorKey()) )
        {
            refuse("hybrid sectors currently require opaque materials; " + material.name +
                   " uses transparency. Choose model-scene for this mapping");
        }
        material.width = parsed.cel(0).width;
        material.height = parsed.cel(0).height;
        if ( material.width < 8 || material.height < 8 || material.width > 256 || material.height > 256 ||
             (material.width & (material.width - 1)) || (material.height & (material.height - 1)) )
        {
            refuse("retail scene materials must have power-of-two dimensions from 8 to 256");
        }
        if ( !parsed.mipFallbackNote().empty() )
        {
            refuse("a material has an incomplete mip chain");
        }
        material.sha256 = jklSourceSha256Hex(data);
        const std::size_t index = result.materials.size();
        shaderMaterials.emplace(shaderIndex, index);
        addFile("mat/" + material.name, std::move(data));
        result.materials.push_back(std::move(material));
        return index;
    };

    struct Triangle
    {
        RavenBspTriangle source;
        std::size_t material{};
    };
    using Cell = std::tuple<int, int, int, std::string>;
    std::map<Cell, std::vector<Triangle>> cells;
    const bool useHybrid = options.geometryMode == JoJaGeometryMode::HybridSectors;
    std::vector<JoJaHybridTriangle> hybridTriangles;
    TVector low{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity()};
    TVector high{-low.x, -low.y, -low.z};
    std::vector<std::string> skippedShaders;
    std::uint64_t attemptedTriangles = 0;
    const auto &world = bsp.models[0];
    for ( std::int32_t offset = 0; offset < world.surfaceCount; ++offset )
    {
        const auto index = static_cast<std::size_t>(world.firstSurface + offset);
        const auto &surface = bsp.surfaces[index];
        const auto &shader = bsp.shaders[static_cast<std::size_t>(surface.shader)];
        if ( surface.type == 4 || (shader.surfaceFlags & (kRavenSky | kRavenNoDraw)) ||
             explicitSkips.contains(shader.name) )
        {
            ++result.skippedSurfaces;
            skippedShaders.push_back(shader.name);
            continue;
        }
        const std::uint64_t work = surface.type == 2 ? static_cast<std::uint64_t>((surface.patchWidth - 1) / 2) *
                                                           static_cast<std::uint64_t>((surface.patchHeight - 1) / 2) *
                                                           2u * options.patchSubdivisions * options.patchSubdivisions
                                                     : static_cast<std::uint64_t>(surface.indexCount) / 3u;
        if ( work > 2u * options.maxTriangles - attemptedTriangles )
        {
            refuse("tessellation work exceeds twice maxTriangles, including degenerate candidates");
        }
        attemptedTriangles += work;
        auto triangles = ravenBspSurfaceTriangles(bsp, index, options.patchSubdivisions);
        if ( triangles.size() > options.maxTriangles - result.triangles )
        {
            refuse("tessellated triangles exceed maxTriangles");
        }
        if ( triangles.empty() )
        {
            ++result.skippedSurfaces;
            continue;
        }
        if ( surface.type == 2 )
        {
            ++result.patches;
        }
        const auto material = materialFor(surface.shader);
        result.triangles += triangles.size();
        for ( auto &triangle : triangles )
        {
            TVector center{};
            for ( const auto &corner : triangle.corners )
            {
                const auto point = scaled(corner.position, options.scale);
                extend(low, high, point);
                center.x += point.x / 3;
                center.y += point.y / 3;
                center.z += point.z / 3;
            }
            if ( useHybrid )
            {
                for ( auto& corner : triangle.corners )
                {
                    const float light = cornerLight(bsp, surface, corner, result);
                    corner.color = {light, light, light, 1.0f};
                }
                hybridTriangles.push_back({std::move(triangle), material, 0});
            }
            else
            {
                const Cell cell{static_cast<int>(std::floor(center.x / 1.6)), static_cast<int>(std::floor(center.y / 1.6)),
                                static_cast<int>(std::floor(center.z / 1.6)), result.materials[material].footstep};
                cells[cell].push_back({std::move(triangle), material});
            }
        }
    }
    if ( !result.triangles )
    {
        refuse("the world model has no convertible visible triangles");
    }
    std::optional<JoJaHybridGeometry> hybrid;
    if ( useHybrid )
    {
        hybrid = buildJoJaHybridGeometry(bsp, hybridTriangles, {options.scale, options.maxSectors, options.maxFragments});
        hybridTriangles.clear();
        hybridTriangles.shrink_to_fit();
        result.sectors = hybrid->cells.size();
        result.portalPairs = hybrid->portalPairs;
        result.nativeFaces = hybrid->nativeFaces;
        result.residualTriangles = hybrid->residual.size();
        result.residualFaces = hybrid->residualPolygons.size();
        result.splitSourceTriangles = hybrid->splitSourceTriangles;
        result.untrimmedBoundaryPlanes = hybrid->untrimmedBoundaryPlanes;
        result.nativePromotionFallbacks = hybrid->nativePromotionFallbacks;
        result.targetCollapsedFragments = hybrid->targetCollapsedFragments;
        result.targetCollapsedArea = hybrid->targetCollapsedArea;
        result.sourceArea = hybrid->sourceArea;
        result.outputArea = hybrid->outputArea;
        result.maximumRelativeCoverageError = hybrid->maximumRelativeCoverageError;
    }
    else
    {
        result.residualTriangles = result.triangles;
        result.residualFaces = result.triangles;
    }
    std::size_t modelCount = 0;
    for ( const auto &[cell, triangles] : cells )
    {
        (void)cell;
        modelCount += (triangles.size() + options.trianglesPerModel - 1) / options.trianglesPerModel;
        if ( modelCount > options.maxModels )
        {
            refuse("spatial model chunks exceed maxModels");
        }
    }

    auto clips = buildJoJaPlayerClips(bsp, options, options.maxModels - modelCount, result.materials.front().name);
    Level level;
    level::applyNewProjectHeaderDefaults(level, options.target);
    level.exportBufferExtras = level::defaultNewProjectExportBufferExtras(options.target);
    // Model MATs and stock player-class dependencies are resolved dynamically
    // by the game. With no mounted stock archive the JKL writer cannot count
    // them. A model-only world otherwise emits zero material/sound capacity.
    level.exportBufferExtras.materials = static_cast<std::uint32_t>(result.materials.size() + 256);
    level.exportBufferExtras.sounds = 512;
    level.exportBufferExtras.cogs = 128;
    level.uvCoordinateSpace = UvCoordinateSpace::TexturePixels;
    level.masterCMP = palette ? "joja.cmp" : "dflt.cmp";
    level.colormaps = {level.masterCMP};
    level.exportBufferExtras.things = 64;
    auto starts = playerStarts(bsp, options.scale, result);
    for ( const auto &start : starts )
    {
        extend(low, high, {start.x, start.y, start.z});
    }
    if ( starts.empty() )
    {
        Thing start;
        start.templateName = "walkplayer";
        start.name = "review_start";
        start.sector = 0;
        start.x = (low.x + high.x) / 2;
        start.y = (low.y + high.y) / 2;
        start.z = high.z + 0.1;
        if ( hybrid )
        {
            const auto& point = hybrid->cells.front().interior;
            start.x = point.x;
            start.y = point.y;
            start.z = point.z;
        }
        extend(low, high, {start.x, start.y, start.z});
        starts.push_back(start);
        result.warnings.push_back(hybrid
            ? "No source player start: an untested review start was placed inside a generated cell."
            : "No source player start: an untested review start was placed above the scene.");
    }
    for (const auto& clip : clips)
    {
        extend(low, high, clip.low);
        extend(low, high, clip.high);
    }
    if ( !hybrid )
    {
        const TVector roomLow{low.x - 0.5, low.y - 0.5, low.z - 0.5};
        const TVector roomHigh{high.x + 0.5, high.y + 0.5, high.z + 0.5};
        if ( level::levelCreateSectorPrimitiveBox(level, level::SectorPrimitive::Cube, roomLow, roomHigh) != 0 )
        {
            refuse("could not construct the enclosing sector");
        }
        auto &room = level.sectors[0];
        room.ambient = ColorBlack;
        room.colormap = level.masterCMP;
        room.colormapIndex = 0;
        for ( auto &surface : room.surfaces )
        {
            surface.material.clear();
            surface.nMat = -1;
            surface.geo = 0;
            surface.surfaceFlags = SF_Collision;
        }
    }
    level.materials.clear();
    for ( const auto &material : result.materials )
    {
        if ( std::find(level.materials.begin(), level.materials.end(), material.name) == level.materials.end() )
        {
            level.materials.push_back(material.name);
        }
    }
    ThingTemplate base;
    base.name = "_jjworld";
    base.parent = "none";
    base.values = {deserializeTplValue("type", "cog"), deserializeTplValue("move", "path"),
                   deserializeTplValue("thingflags", worldModelThingFlags(options.worldModelRenderPass)),
                   deserializeTplValue("collide", options.renderMeshCollision ? "3" : "0")};
    level.templates.push_back(std::move(base));

    std::vector<PendingWorldModel> pendingModels;
    if ( hybrid )
    {
        addHybridRooms(level, *hybrid, result.materials);
        addHybridModels(level, *hybrid, options, result, pendingModels);
        for ( auto& start : starts )
        {
            const TVector point{start.x, start.y, start.z};
            const auto found = std::find_if(hybrid->cells.begin(), hybrid->cells.end(),
                [&](const auto& cell) { return joJaHybridContains(cell, point, 1e-9); });
            if ( found == hybrid->cells.end() )
            {
                refuse("a source player start is outside every generated cell; no position was moved");
            }
            start.sector = static_cast<std::int32_t>(found - hybrid->cells.begin());
        }
    }

    for ( const auto &[cell, triangles] : cells )
    {
        (void)cell;
        for ( std::size_t begin = 0; begin < triangles.size(); begin += options.trianglesPerModel )
        {
            const auto end = std::min(triangles.size(), begin + options.trianglesPerModel);
            TVector partLow{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::infinity()};
            TVector partHigh{-partLow.x, -partLow.y, -partLow.z};
            for ( std::size_t i = begin; i < end; ++i )
            {
                for ( const auto &corner : triangles[i].source.corners )
                {
                    extend(partLow, partHigh, scaled(corner.position, options.scale));
                }
            }
            JoJaModelReport modelReport;
            modelReport.name = numbered("jjm", result.models.size(), ".3do");
            modelReport.triangles = end - begin;
            modelReport.footstep = std::get<3>(cell);
            modelReport.position = {(partLow.x + partHigh.x) / 2, (partLow.y + partHigh.y) / 2,
                                    (partLow.z + partHigh.z) / 2};
            Model3do model;
            model.version = 2.1;
            model.geosetCount = 1;
            model.hierarchyTransformsApplied = false;
            Model3doMesh mesh;
            mesh.name = "world";
            mesh.geo = 4;
            mesh.light = 3;
            mesh.tex = 1;
            std::map<std::size_t, std::int32_t> localMaterials;
            std::set<std::size_t> sourceSurfaces;
            for ( std::size_t i = begin; i < end; ++i )
            {
                const auto &triangle = triangles[i];
                const auto &material = result.materials[triangle.material];
                const auto [at, added] =
                    localMaterials.emplace(triangle.material, static_cast<std::int32_t>(model.materials.size()));
                if ( added )
                {
                    model.materials.push_back(material.name);
                }
                Model3doFace face;
                face.material = at->second;
                face.geo = 4;
                face.light = 3;
                face.tex = 1;
                face.extraLight = ColorBlack;
                sourceSurfaces.insert(triangle.source.surface);
                std::array<TVector, 3> engineCorners;
                std::size_t cornerIndex = 0;
                for ( const auto &corner : triangle.source.corners )
                {
                    auto point = scaled(corner.position, options.scale);
                    point.x -= modelReport.position.x;
                    point.y -= modelReport.position.y;
                    point.z -= modelReport.position.z;
                    engineCorners[cornerIndex++] = {static_cast<float>(point.x), static_cast<float>(point.y),
                                                    static_cast<float>(point.z)};
                    model.radius =
                        std::max(model.radius, std::sqrt(point.x * point.x + point.y * point.y + point.z * point.z));
                    const float light = cornerLight(bsp, bsp.surfaces[triangle.source.surface], corner, result);
                    const auto vertex = static_cast<std::int32_t>(mesh.vertices.size());
                    mesh.vertices.push_back({point.x, point.y, point.z, {light, light, light, 1.0f}});
                    mesh.vertexNormals.push_back(corner.normal);
                    const double u = corner.u * material.width, v = corner.v * material.height;
                    if ( !std::isfinite(u) || !std::isfinite(v) || std::abs(u) > std::numeric_limits<float>::max() ||
                         std::abs(v) > std::numeric_limits<float>::max() )
                    {
                        refuse("texture coordinates cannot be represented in 3DO");
                    }
                    mesh.texVertices.push_back({static_cast<float>(u), static_cast<float>(v)});
                    face.vertices.push_back(vertex);
                    face.texVertices.push_back(vertex);
                }
                // The game reads model positions as single precision. A valid
                // source triangle can collapse after scale and recentering;
                // test the representable positions, not an arbitrary scale floor.
                const auto &a = engineCorners[0];
                const auto &b = engineCorners[1];
                const auto &c = engineCorners[2];
                const TVector ab{b.x - a.x, b.y - a.y, b.z - a.z};
                const TVector ac{c.x - a.x, c.y - a.y, c.z - a.z};
                if ( ab.y * ac.z == ab.z * ac.y && ab.z * ac.x == ab.x * ac.z && ab.x * ac.y == ab.y * ac.x )
                {
                    refuse("a triangle collapses at target single precision; adjust scale or chunking");
                }
                mesh.faces.push_back(std::move(face));
            }
            modelReport.radius = model.radius;
            modelReport.sourceSurfaces.assign(sourceSurfaces.begin(), sourceSurfaces.end());
            mesh.radius = model.radius;
            mesh.authoredRadius = model.radius;
            optimizeGeneratedMesh(mesh, options, result);
            model.meshes.push_back(std::move(mesh));
            Model3doNode node;
            node.id = 0;
            node.mesh = 0;
            node.parent = -1;
            node.name = "world";
            model.nodes.push_back(std::move(node));
            if ( !model3doEngineLimitIssues(model).empty() )
            {
                refuse("generated model exceeds loader limits");
            }
            ThingTemplate object;
            object.name = numbered("jjm", result.models.size(), "");
            object.parent = "_jjworld";
            object.values = {deserializeTplValue("model3d", modelReport.name),
                             deserializeTplValue("thingflags", worldModelThingFlags(options.worldModelRenderPass,
                                                                                   modelReport.footstep)),
                             deserializeTplValue("size", scalarText(model.radius + 0.001)),
                             deserializeTplValue("movesize", scalarText(model.radius + 0.001))};
            level.templates.push_back(object);
            auto &thing = level.newThing();
            modelReport.thing = level.things.size() - 1;
            thing.templateName = object.name;
            thing.name = object.name;
            thing.sector = 0;
            thing.x = modelReport.position.x;
            thing.y = modelReport.position.y;
            thing.z = modelReport.position.z;
            pendingModels.push_back({"3do/" + modelReport.name, std::move(model), modelReport.position, modelReport.thing});
            result.models.push_back(std::move(modelReport));
        }
    }
    finishWorldModels(level, pendingModels, options, result, addFile);
    if ( options.sourceVisibility )
    {
        result.sourceVisibility = describeVisibility(bsp, *options.sourceVisibility, level,
                                                    hybrid ? &*hybrid : nullptr, result.models);
    }
    addPlayerTemplates(level);
    for ( const auto &start : starts )
    {
        auto &thing = level.newThing();
        const auto id = thing.id, number = thing.num;
        thing = start;
        thing.id = id;
        thing.num = number;
    }
    result.playerStarts = starts.size();
    // Append collision Things after starts so the original visible-model and start ordinals stay stable.
    for (auto& clip : clips)
    {
        const bool barrier = clip.report.mode == JoJaPlayerClipMode::MovementBarrier;
        const auto name = clip.report.model.substr(0, clip.report.model.size() - 4);
        ThingTemplate shape;
        shape.name = name;
        shape.parent = "none";
        shape.values = {deserializeTplValue("type", barrier ? "weapon" : "cog"),
                        deserializeTplValue("move", "none"), deserializeTplValue("collide", "3"),
                        deserializeTplValue("model3d", clip.report.model),
                        deserializeTplValue("thingflags", "0x50"),
                        deserializeTplValue("size", scalarText(clip.radius)),
                        deserializeTplValue("movesize", scalarText(clip.radius))};
        if (barrier)
        {
            shape.values.push_back(deserializeTplValue("typeflags", "0"));
            shape.values.push_back(deserializeTplValue("damage", "-1"));
        }
        level.templates.push_back(std::move(shape));
        auto& thing = level.newThing();
        thing.name = thing.templateName = name;
        thing.sector = 0;
        thing.x = clip.origin.x; thing.y = clip.origin.y; thing.z = clip.origin.z;
        clip.report.thing = level.things.size() - 1;
        addFile("3do/" + clip.report.model, std::move(clip.bytes));
        result.playerClips.push_back(std::move(clip.report));
    }
    if ( options.stockPickups )
        placeJoJaStockPickups(bsp, options, hybrid ? &*hybrid : nullptr, low, high, level, result);
    JklSaveOptions save;
    save.omitDefaultMotsArchLighting = false;
    const auto built = buildJklExport(level, save);
    std::istringstream levelReadback(built.text);
    const auto exportedLevel = loadFromJKL(levelReadback, {.projectType = options.target});
    if ( options.stockPickups )
    {
        for ( const auto& finding : auditJoJaProjectiles(exportedLevel, options.target) )
        {
            result.warnings.push_back("Projectile " + finding.weapon + " " + finding.projectile + ": " +
                                      finding.status + "; " + finding.detail);
        }
    }
    addFile("jkl/" + options.levelName + ".jkl", built.text);
    addFile("misc/joja-source.ent", bsp.entityText);
    if ( palette )
    {
        addFile("misc/cmp/joja.cmp", options.palette);
    }
    EpisodeFile episode;
    episode.name = "JO/JA scene - authoring scaffold";
    episode.type = 2;
    EpisodeSequence sequence;
    sequence.fileName = options.levelName + ".jkl";
    episode.sequences.push_back(sequence);
    addFile("episode.jk", saveToEpisodeJkText(episode));

    result.warnings.push_back(
        "Runtime acceptance is unverified. Test the exact archive in the target game; this is an authoring scaffold.");
    if ( options.stockPickups )
        result.warnings.push_back("World model 0 only. Supported unconditional pickup counterparts are placed; "
                                  "other items require review. Doors, lifts, triggers and mission scripts are not converted.");
    else
        result.warnings.push_back(hybrid
        ? "World model 0 only. Doors, lifts, other submodels, triggers, pickups and mission scripts are not converted. "
          "This static-scene output does not replace an existing gameplay conversion."
        : "World model 0 only. Doors, lifts, other submodels, triggers, pickups, mission scripts "
          "and source visibility cells are not converted.");
    result.warnings.push_back(hybrid
        ? "Verified opaque static brush sections close matching portals. Interior collision volumes and "
          "player-only/weapon filters are not reconstructed; rendered-mesh collision remains an approximation."
        : options.sourcePlayerClip != JoJaPlayerClipMode::Omit
        ? "Only eligible static world PLAYERCLIP brushes are converted. Standable hulls can stop shots and visibility "
          "queries; movement barriers pass ordinary projectiles but can catch sabers and have no floor attachment."
        : "Collision brushes and player-only/weapon filters are not converted. Rendered-mesh "
          "collision is only an explicit approximation.");
    result.warnings.push_back("Sky/nodraw faces and billboards are omitted. Shader stages, animation, transparency "
                              "behavior and dynamic light styles need review.");
    result.warnings.push_back(
        "Fixed patch samples do not establish a geometric error bound or guarantee matching adjacent patch edges.");
    result.warnings.push_back(hybrid
        ? "Stock hard/metal/earth footsteps use native surface flags and separate model chunks. Model flags "
          "apply only when residual model collision is enabled; sound playback is not runtime verified."
        : "Stock hard/metal/earth footsteps are selected with Thing flags on separate chunks. These apply only when "
          "attached to a colliding model; sound playback is not runtime verified.");
    result.warnings.push_back(
        "Grass, wood, rug/fabric, foliage and NOSTEPS use a marked hard-surface fallback. Water contents require "
        "review independently of the material cue. Source dirt/sand/gravel/snow/mud use stock earth, not distinct "
        "source samples. No JO/JA sound bytes, ambience or music are imported.");
    result.warnings.push_back("Source light uses scalar luminance from the first atlas/style, without the source "
                              "renderer's gamma, overbright or shader modulation.");
    if ( hybrid )
    {
        result.warnings.push_back("Hybrid native boundaries collide. The rendered-mesh option controls residual "
            "model detail only. Source visibility leaves can contain solid detail; source collision semantics "
            "and player clearance remain unverified.");
        result.warnings.push_back("Hybrid counts and area checks are static evidence, not retail frame-rate results. "
            "Qualify portal traversal, per-view limits and both target renderers at the intended settings.");
        if ( result.targetCollapsedFragments )
        {
            result.warnings.push_back(std::to_string(result.targetCollapsedFragments) +
                " generated fragments vanish at target precision; the reported area allowance records this explicitly.");
        }
        if ( result.nativePromotionFallbacks )
        {
            result.warnings.push_back(std::to_string(result.nativePromotionFallbacks) +
                " source surfaces remain in models because native promotion could not preserve target precision.");
        }
    }
    std::sort(skippedShaders.begin(), skippedShaders.end());
    skippedShaders.erase(std::unique(skippedShaders.begin(), skippedShaders.end()), skippedShaders.end());
    std::ostringstream report;
    report.imbue(std::locale::classic());
    report << std::setprecision(std::numeric_limits<double>::max_digits10);
    report << "JO/JA scene import v1\nStatus: authoring scaffold; runtime acceptance unverified\nSource SHA-256: "
           << result.sourceSha256 << "\nTarget: " << (options.target == ProjectType::MOTS ? "MotS" : "JK")
           << "\nScale: " << options.scale
           << "; native Z-up\nPatch subdivisions per quadratic block: " << options.patchSubdivisions
           << (hybrid ? "\nChunk face limit: " : "\nChunk triangle limit: ") << options.trianglesPerModel
           << "\nTriangles: " << result.triangles
           << "\nModels: " << result.models.size() << "\nMaterials: " << result.materials.size()
           << "\nSource entities: " << result.sourceEntities << "; player starts: " << result.playerStarts
           << "\nOmitted submodels: " << result.omittedSubmodels << "; unconverted brushes: " << (result.sourceBrushes - result.playerClips.size())
           << "\nSkipped world surfaces: " << result.skippedSurfaces
           << "\nLightmapped corners: " << result.lightmapCorners << "; vertex-lit corners: " << result.vertexLitCorners
           << "\nCollision: "
           << (options.renderMeshCollision ? "rendered-mesh approximation" : "visual-only; no model collision")
           << "\nRequested audio policy: "
           << (options.audioPolicy == JoJaAudioPolicy::StockEquivalents ? "JK/MotS equivalents"
                                                                        : "import source sounds")
           << "\nSource audio imported: no; stock footstep flags assigned";
    if (options.sourcePlayerClip != JoJaPlayerClipMode::Omit)
    {
        report << "\nSource player clips: " << joJaPlayerClipName(options.sourcePlayerClip) << ", "
               << result.playerClips.size() << " hidden hulls";
        for (const auto& clip : result.playerClips)
            report << "\n  source brush " << clip.brush << " -> Thing " << clip.thing << " " << clip.model
                   << " (" << joJaPlayerClipName(clip.mode) << ", " << clip.faces << " faces)";
    }
    if ( options.worldModelRenderPass == JoJaWorldModelRenderPass::OrdinaryThing )
    {
        report << "\nWorld model render pass: ordinaryThing (LEVELGEO cleared; Gouraud distance cutoff disabled on generated static model Things)";
    }
    if ( options.meshOptimization != MeshOptimization::Off )
    {
        report << "\nMesh optimization: " << meshOptimizationName(options.meshOptimization) << "; vertices "
               << result.optimizedVerticesIn << " -> " << result.optimizedVerticesOut << "; faces "
               << result.optimizedFacesIn << " -> " << result.optimizedFacesOut;
        if ( options.meshOptimization == MeshOptimization::Loose )
        {
            report << "; straight corners removed " << result.straightCornersRemoved << " (fan triangles "
                   << result.optimizedFanTrianglesIn << " -> " << result.optimizedFanTrianglesOut << ")";
        }
        else
        {
            report << " (fan-equivalent triangles unchanged)";
        }
        report << "; largest vertex-normal turn " << result.worstNormalTurnDegrees << " degrees";
    }
    if ( options.sourceProfile != JoJaSourceProfile::Legacy )
    {
        report << "\nSource footstep profile: " << joJaSourceProfileName(options.sourceProfile);
    }
    if ( hybrid )
    {
        report << "\nGeometry: hybrid-sectors\nSectors: " << result.sectors << " / " << options.maxSectors
               << "; portal pairs: " << result.portalPairs << "; native textured faces: " << result.nativeFaces
               << "\nResidual model faces: " << result.residualFaces << "; fan-equivalent triangles: "
               << result.residualTriangles << "; source triangles split: "
               << result.splitSourceTriangles << "; fragment budget: " << options.maxFragments
               << "\nSource area (JKU squared): " << result.sourceArea << "; retained area: " << result.outputArea
               << "; maximum relative coverage error: " << result.maximumRelativeCoverageError
               << "\nWall tightening planes declined to retain source geometry: " << result.untrimmedBoundaryPlanes
               << "; source surfaces retained as models for precision: " << result.nativePromotionFallbacks
               << "\nTarget-vanishing generated fragments: " << result.targetCollapsedFragments
               << "; area allowance (JKU squared): " << result.targetCollapsedArea;
    }
    report << "\n\nMaterials\n";
    for ( const auto &material : result.materials )
    {
        report << material.shader << " -> " << material.name << " (" << material.width << 'x' << material.height
               << (material.placeholder ? ", PLACEHOLDER" : ", captured MAT") << ") " << material.sha256
               << (options.sourceProfile == JoJaSourceProfile::JediOutcastMultiplayer ? "; low surface bits=" : "; source material=")
               << material.sourceMaterial;
        if ( !material.sourceFootstep.empty() )
        {
            report << "; source cue=" << material.sourceFootstep;
            if ( material.noStepsFlag )
            {
                report << "; source NOSTEPS flag set; target silence unverified";
            }
        }
        report << "; stock footstep=" << material.footstep
               << (material.footstepNeedsReview ? " (NEEDS AUDIO REVIEW)" : "") << '\n';
    }
    report << "\nOmitted shader names\n";
    for ( const auto &shader : skippedShaders )
    {
        report << shader << '\n';
    }
    report << "\nSource entity classes (not a gameplay conversion)\n";
    std::map<std::string, std::size_t> classes;
    for ( const auto &entity : bsp.entities )
    {
        ++classes[std::string(field(entity, "classname"))];
    }
    for ( const auto &[name, count] : classes )
    {
        report << name << ": " << count << '\n';
    }
    report << "\nLimitations\n";
    for ( const auto &warning : result.warnings )
    {
        report << "- " << warning << '\n';
    }
    report << "\nGenerated files before report\n";
    for ( const auto &[name, data] : files )
    {
        report << name << ' ' << data.size() << ' ' << jklSourceSha256Hex(data) << '\n';
    }
    result.report = report.str();
    addFile("misc/joja-import.txt", result.report);
    std::vector<RuntimeArchiveInput> entries;
    for ( auto &[name, data] : files )
    {
        entries.push_back({name, std::move(data)});
    }
    result.archive = buildRuntimeArchive(std::move(entries));
    result.outputSha256 = jklSourceSha256Hex(result.archive.outputData);
    std::ostringstream manifest;
    manifest.imbue(std::locale::classic());
    manifest << std::setprecision(std::numeric_limits<double>::max_digits10) << "jo-ja-v1\n"
             << result.sourceSha256 << '\n'
             << result.outputSha256 << '\n'
             << options.levelName << '\n'
             << options.scale << '\n'
             << (options.target == ProjectType::MOTS ? "mots" : "jk") << '\n'
             << options.patchSubdivisions << ' ' << options.trianglesPerModel << ' ' << options.maxTriangles << ' '
             << options.maxModels << ' ' << options.allowPlaceholders << ' ' << options.renderMeshCollision << ' '
             << static_cast<int>(options.audioPolicy) << '\n';
    for ( const auto &name : explicitSkips )
    {
        manifest << "skip " << name.size() << ':' << name << '\n';
    }
    for ( const auto &material : options.materials )
    {
        manifest << material.shader.size() << ':' << material.shader << material.name.size() << ':' << material.name
                 << jklSourceSha256Hex(material.bytes) << '\n';
    }
    manifest << jklSourceSha256Hex(options.palette);
    if ( options.sourceProfile != JoJaSourceProfile::Legacy )
    {
        manifest << "\nsourceProfile " << joJaSourceProfileName(options.sourceProfile);
    }
    if ( useHybrid || options.maxSectors != 64 || options.maxFragments != 500000 )
    {
        manifest << "\ngeometry " << (useHybrid ? "hybrid-sectors" : "model-scene") << ' '
                 << options.maxSectors << ' ' << options.maxFragments;
    }
    if ( options.meshOptimization != MeshOptimization::Off )
    {
        manifest << "\nmeshOptimization " << meshOptimizationName(options.meshOptimization);
    }
    if ( options.worldModelRenderPass == JoJaWorldModelRenderPass::OrdinaryThing )
    {
        manifest << "\nworldModelRenderPass ordinaryThing";
    }
    if (options.sourcePlayerClip != JoJaPlayerClipMode::Omit)
    {
        manifest << "\nsourcePlayerClip " << joJaPlayerClipName(options.sourcePlayerClip);
        for (const auto id : std::set<std::size_t>(options.movementBarrierBrushes.begin(), options.movementBarrierBrushes.end()))
            manifest << ' ' << id;
    }
    if ( options.stockPickups )
        manifest << "\nstockPickups v1";
    result.manifestSha256 = jklSourceSha256Hex(manifest.str());
    return result;
}

const char *joJaSourceProfileName(JoJaSourceProfile profile)
{
    switch ( profile )
    {
    case JoJaSourceProfile::Legacy: return "legacy";
    case JoJaSourceProfile::JediOutcastMultiplayer: return "jo-mp";
    case JoJaSourceProfile::JediAcademyMultiplayer: return "ja-mp";
    }
    refuse("unknown source profile");
}

} // namespace sed::assets
