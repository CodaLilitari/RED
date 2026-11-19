#include "sed/render/snapshot_things.hpp"
#include "sed/assets/3do.hpp"
#include "sed/assets/spr.hpp"
#include "sed/level/thing.hpp"
#include "sed/text.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <span>
#include <sstream>
#include <stdexcept>

namespace sed::render
{
namespace
{
void charge(std::size_t& used, const std::size_t count, const std::size_t maximum)
{
    if ( used > maximum || count > maximum - used )
    {
        throw std::runtime_error("snapshot Things budget exceeded");
    }
    used += count;
}

// Conservative admission, not a lookup replacement or actual work measurement.
// Existing thingFindValue remains authoritative, including first duplicate
// template, raw whitespace, last scalar field and the 1000-hop ceiling.
void admitTemplateLookup(const Level& level, const Thing& thing, std::size_t& work, const std::size_t maximum)
{
    const auto fields = [&](const std::size_t count)
    {
        constexpr std::size_t unitsPerField = 3 * 9;
        if ( count > maximum / unitsPerField )
        {
            throw std::runtime_error("snapshot Things budget exceeded");
        }
        charge(work, count * unitsPerField, maximum);
    };
    fields(thing.values.size());
    std::string_view current = thing.templateName.empty() ? thing.name : thing.templateName;
    for ( int depth = 0; depth < 1000; ++depth )
    {
        const ThingTemplate* matched = nullptr;
        for ( const auto& candidate : level.templates )
        {
            // Separate additions avoid overflowing name-size sums/products.
            for ( int query = 0; query < 3; ++query )
            {
                charge(work, 1, maximum);
                charge(work, candidate.name.size(), maximum);
                charge(work, current.size(), maximum);
            }
            if ( sed::text::iequalsAscii(candidate.name, current) )
            {
                matched = &candidate;
                break;
            }
        }
        if ( !matched )
        {
            return;
        }
        fields(matched->values.size());
        if ( matched->parent.empty() || sed::text::iequalsAscii(matched->parent, "none") )
        {
            return;
        }
        current = matched->parent;
    }
}
bool finite(const TVector& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

std::string resourceName(const TplValue* value, const std::string_view extension)
{
    if ( !value )
    {
        return {};
    }
    if ( value->stringValue.size() > 255 )
    {
        throw std::runtime_error("snapshot Things resource name exceeds 255 bytes");
    }
    const std::string text = value->stringValue.empty() ? value->asString() : value->stringValue;
    if ( text.size() > 255 )
    {
        throw std::runtime_error("snapshot Things resource name exceeds 255 bytes");
    }
    std::string name(sed::text::trimView(text));
    std::transform(
        name.begin(), name.end(), name.begin(), [](const unsigned char c)
        { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c); });
    if ( name.empty() || name == "none" )
    {
        return {};
    }
    if ( !name.ends_with(extension) )
    {
        if ( name.size() > 255 - extension.size() )
        {
            throw std::runtime_error("snapshot Things resource name exceeds 255 bytes");
        }
        name += extension;
    }
    return name;
}

struct Cached
{
    std::optional<assets::Model3do> model;
    std::optional<assets::SprFile> sprite;
    std::string status;
    std::string message;
};

// Estimates retained vector capacities as well as owned names. The core parser
// has its own independent decode/transform budgets; this is cache admission,
// not a replacement for those limits or a pre-allocation parser limit.
void accountModel(const assets::Model3do& model, std::size_t& used, const std::size_t maximum)
{
    const auto bytes = [&](const std::size_t count, const std::size_t size)
    {
        if ( size != 0 && count > maximum / size )
        {
            throw std::runtime_error("snapshot Things budget exceeded");
        }
        charge(used, count * size, maximum);
    };
    bytes(1, sizeof(model));
    bytes(model.materials.capacity(), sizeof(std::string));
    for ( const auto& name : model.materials )
    {
        bytes(name.capacity(), 1);
    }
    bytes(model.nodes.capacity(), sizeof(assets::Model3doNode));
    for ( const auto& node : model.nodes )
    {
        bytes(node.name.capacity(), 1);
    }
    bytes(model.meshSourceIndices.capacity(), sizeof(std::int32_t));
    bytes(model.meshes.capacity(), sizeof(assets::Model3doMesh));
    for ( const auto& mesh : model.meshes )
    {
        bytes(mesh.name.capacity(), 1);
        bytes(mesh.vertices.capacity(), sizeof(assets::Model3doVertex));
        bytes(mesh.texVertices.capacity(), sizeof(assets::Model3doTexVertex));
        bytes(mesh.vertexNormals.capacity(), sizeof(TVector));
        bytes(mesh.faceNormals.capacity(), sizeof(TVector));
        bytes(mesh.faces.capacity(), sizeof(assets::Model3doFace));
        for ( const auto& face : mesh.faces )
        {
            bytes(face.vertices.capacity(), sizeof(std::int32_t));
            bytes(face.texVertices.capacity(), sizeof(std::int32_t));
        }
    }
}

void validateModel(const assets::Model3do& model)
{
    if ( std::abs(model.version - 2.1) > 1e-6 && std::abs(model.version - 2.3) > 1e-6 )
    {
        throw std::runtime_error("Only 3DO 2.1 and 2.3 static geometry is supported");
    }
    for ( const auto& name : model.materials )
    {
        if ( name.size() > 255 )
        {
            throw std::runtime_error("Model material name exceeds 255 bytes");
        }
    }
    for ( const auto& mesh : model.meshes )
    {
        for ( const auto& v : mesh.vertices )
        {
            if ( !finite({v.x, v.y, v.z}) )
            {
                throw std::runtime_error("Non-finite model vertex");
            }
        }
        for ( const auto& face : mesh.faces )
        {
            if ( mesh.geo == 0 || face.geo == 0 || face.vertices.size() < 3 )
            {
                continue;
            }
            if ( face.material < -1 ||
                 (face.material >= 0 && static_cast<std::size_t>(face.material) >= model.materials.size()) )
            {
                throw std::runtime_error("Invalid model material index");
            }
            if ( face.geo >= 4 && face.texVertices.size() != face.vertices.size() )
            {
                throw std::runtime_error("Invalid model texture corner count");
            }
            for ( std::size_t i = 0; i < face.vertices.size(); ++i )
            {
                if ( face.vertices[i] < 0 ||
                     static_cast<std::size_t>(face.vertices[i]) >= mesh.vertices.size() )
                {
                    throw std::runtime_error("Invalid model vertex index");
                }
                if ( face.geo >= 4 )
                {
                    const auto t = face.texVertices[i];
                    if ( t < 0 || static_cast<std::size_t>(t) >= mesh.texVertices.size() ||
                         !std::isfinite(mesh.texVertices[t].u) || !std::isfinite(mesh.texVertices[t].v) )
                    {
                        throw std::runtime_error("Invalid model texture index or coordinate");
                    }
                }
            }
        }
    }
}

assets::SprFile classicSprite(const std::string_view bytes)
{
    std::istringstream input{std::string(bytes)};
    std::string line;
    std::string record;
    while ( std::getline(input, line) )
    {
        sed::text::stripLineComments(line);
        const auto trimmed = sed::text::trimView(line);
        if ( trimmed.empty() )
        {
            continue;
        }
        // Narrow profile rather than allowing automatic IJIM RGBA admission.
        // Bound tokenization and reject extra noncomment records explicitly.
        if ( !record.empty() || trimmed.size() > 4096 )
        {
            throw std::runtime_error("Unsupported classic sprite record layout");
        }
        const auto words = sed::text::splitWords(trimmed);
        if ( words.size() != 11 || words[0].size() > 255 )
        {
            throw std::runtime_error("Expected one classic eleven-token sprite record");
        }
        record = trimmed;
    }
    if ( record.empty() )
    {
        throw std::runtime_error("Missing classic sprite record");
    }
    std::istringstream admitted(record);
    auto sprite = assets::loadFromSPR(admitted);
    if ( !std::isfinite(sprite.width) || !std::isfinite(sprite.height) || sprite.width <= 0 ||
         sprite.height <= 0 )
    {
        throw std::runtime_error("Sprite dimensions must be finite and positive");
    }
    return sprite;
}
ColorF ambient(const Level& level, const Thing& thing)
{
    return thing.sector >= 0 && static_cast<std::size_t>(thing.sector) < level.sectors.size()
               ? level.sectors[thing.sector].ambient
               : ColorWhite;
}
} // namespace

SnapshotThingScene prepareSnapshotThings(const Level& level, const SnapshotCamera& camera,
                                         const SnapshotThingResourceResolver& resolver,
                                         const SnapshotThingLimits& limits,
                                         const SnapshotThingOptions& options)
{
    SnapshotThingScene scene;
    std::size_t templateLookupWork{};
    std::size_t things{}, resourceCount{}, resourceBytes{}, cacheBytes{}, vertices{}, corners{}, meshes{};
    charge(things, level.things.size(), limits.maxThings);
    scene.diagnostics.reserve(things);
    std::map<std::string, Cached> cache;
    const auto load = [&](const std::string& name, const bool model) -> const Cached&
    {
        if ( const auto found = cache.find(name); found != cache.end() )
        {
            return found->second;
        }
        if ( resourceCount >= limits.maxResources )
        {
            throw std::runtime_error("snapshot Things distinct resource budget exceeded");
        }
        ++resourceCount;
        const auto bytes = resolver ? resolver(name) : std::nullopt;
        Cached result;
        if ( !bytes )
        {
            result.status = "missing-resource";
            result.message = "Captured resource unavailable";
        }
        else
        {
            std::size_t single{};
            charge(single, bytes->size(), limits.maxResourceBytes);
            charge(resourceBytes, bytes->size(), limits.maxTotalResourceBytes);
            try
            {
                if ( model )
                {
                    result.model =
                        assets::loadFrom3DO(std::as_bytes(std::span(bytes->data(), bytes->size())));
                    validateModel(*result.model);
                }
                else
                {
                    result.sprite = classicSprite(*bytes);
                    if ( result.sprite->material.size() > 255 )
                    {
                        throw std::runtime_error("Sprite material name exceeds 255 bytes");
                    }
                }
            }
            catch ( const std::runtime_error& error )
            {
                // Parser admission failures must not become successful partial
                // previews. Missing/invalid syntax has an explicit per-Thing status.
                if ( std::string_view(error.what()).find("budget") != std::string_view::npos )
                {
                    throw;
                }
                result.model.reset();
                result.sprite.reset();
                result.status = "invalid-resource";
                result.message = "Captured resource could not be parsed as supported static geometry";
            }
            if ( result.model )
            {
                accountModel(*result.model, cacheBytes, limits.maxCachedModelBytes);
            }
            if ( result.sprite )
            {
                charge(cacheBytes, sizeof(assets::SprFile), limits.maxCachedModelBytes);
                charge(cacheBytes, result.sprite->material.capacity(), limits.maxCachedModelBytes);
            }
        }
        return cache.emplace(name, std::move(result)).first->second;
    };
    for ( std::size_t index = 0; index < level.things.size(); ++index )
    {
        const auto& thing = level.things[index];
        SnapshotThingDiagnostic diagnostic{index, {}, "no-visual", "No supported model or sprite reference"};
        const TVector position{thing.x, thing.y, thing.z};
        const TVector angles{thing.pch, thing.yaw, thing.rol};
        if ( !finite(position) || !finite(angles) )
        {
            diagnostic.status = "invalid-transform";
            diagnostic.message = "Non-finite Thing placement";
            scene.diagnostics.push_back(std::move(diagnostic));
            continue;
        }
        if ( thing.sector < 0 || static_cast<std::size_t>(thing.sector) >= level.sectors.size() )
        {
            diagnostic.status = "invalid-sector";
            diagnostic.message = "Thing sector is invalid; ambient lighting cannot be assigned";
            scene.diagnostics.push_back(std::move(diagnostic));
            continue;
        }
        admitTemplateLookup(level, thing, templateLookupWork, limits.maxTemplateLookupWork);
        if ( !options.drawPlayerStarts )
        {
            const auto* typeValue = level::thingFindValue(level, thing, "type");
            const std::string type = typeValue
                ? (typeValue->stringValue.empty() ? typeValue->asString() : typeValue->stringValue)
                : std::string{};
            if ( sed::text::iequalsAscii(type, "player") )
            {
                diagnostic.status = "omitted-player-start";
                diagnostic.message = "A player start is a spawn point; the game does not draw it from the player's view";
                scene.diagnostics.push_back(std::move(diagnostic));
                continue;
            }
        }
        const auto* modelValue = level::thingFindValue(level, thing, "model3d");
        if ( !modelValue )
        {
            modelValue = level::thingFindValue(level, thing, "model3do");
        }
        const std::string modelName = resourceName(modelValue, ".3do");
        const std::string spriteName = resourceName(level::thingFindValue(level, thing, "sprite"), ".spr");
        const bool isModel = !modelName.empty();
        diagnostic.resource = isModel ? modelName : spriteName;
        if ( diagnostic.resource.empty() )
        {
            scene.diagnostics.push_back(std::move(diagnostic));
            continue;
        }
        if ( !isModel && level.kind != ProjectType::JKDF2 && level.kind != ProjectType::MOTS )
        {
            diagnostic.status = "unsupported-sprite";
            diagnostic.message = "Only classic JK/MotS static billboards are supported";
            scene.diagnostics.push_back(std::move(diagnostic));
            continue;
        }
        const auto& resource = load(diagnostic.resource, isModel);
        if ( !resource.status.empty() )
        {
            diagnostic.status = resource.status;
            diagnostic.message = resource.message;
            scene.diagnostics.push_back(std::move(diagnostic));
            continue;
        }
        const auto firstGeometry = scene.geometry.size();
        if ( resource.model )
        {
            const auto& model = *resource.model;
            const auto transform = TMatrix43::Create(position, angles);
            // Charge all visible-instance storage before allocating or transforming.
            for ( const auto& mesh : model.meshes )
            {
                if ( mesh.geo == 0 )
                {
                    continue;
                }
                charge(meshes, 1, limits.maxMeshes);
                charge(vertices, mesh.vertices.size(), limits.maxVertices);
                for ( const auto& face : mesh.faces )
                {
                    if ( face.geo != 0 && face.vertices.size() >= 3 )
                    {
                        charge(corners, face.vertices.size(), limits.maxCorners);
                    }
                }
            }
            for ( const auto& mesh : model.meshes )
            {
                if ( mesh.geo == 0 )
                {
                    continue;
                }
                SnapshotGeometry geometry;
                geometry.uvCoordinateSpace = std::abs(model.version - 2.3) <= 1e-6
                                                 ? UvCoordinateSpace::Normalized
                                                 : UvCoordinateSpace::TexturePixels;
                geometry.sector.ambient = ambient(level, thing);
                geometry.sector.vertices.reserve(mesh.vertices.size());
                for ( const auto& v : mesh.vertices )
                {
                    const auto p = transform.TransformPoint({v.x, v.y, v.z});
                    if ( !finite(p) )
                    {
                        throw std::runtime_error("snapshot Things non-finite transformed geometry");
                    }
                    geometry.sector.vertices.push_back({p.x, p.y, p.z});
                }
                for ( const auto& face : mesh.faces )
                {
                    if ( face.geo == 0 || face.vertices.size() < 3 )
                    {
                        continue;
                    }
                    Surface surface;
                    surface.geo = face.geo;
                    // Light mode 0 (fully lit) draws at full brightness, as in the engine.
                    surface.light = face.light;
                    surface.faceFlags = face.faceFlags;
                    surface.vertices = face.vertices;
                    if ( face.material >= 0 )
                    {
                        surface.material = model.materials[face.material];
                    }
                    surface.texVertices.reserve(face.vertices.size());
                    for ( std::size_t i = 0; i < face.vertices.size(); ++i )
                    {
                        if ( face.geo >= 4 )
                        {
                            const auto& uv = mesh.texVertices[face.texVertices[i]];
                            surface.texVertices.push_back({uv.u, uv.v});
                        }
                        else
                        {
                            surface.texVertices.push_back({0, 0});
                        }
                    }
                    geometry.sector.surfaces.push_back(std::move(surface));
                }
                if ( !geometry.sector.surfaces.empty() )
                {
                    scene.geometry.push_back(std::move(geometry));
                }
            }
            if ( scene.geometry.size() > firstGeometry )
            {
                ++scene.models;
            }
        }
        else if ( resource.sprite )
        {
            const auto& sprite = *resource.sprite;
            if ( sprite.type < 0 || sprite.type > 2 )
            {
                diagnostic.status = "unsupported-sprite";
                diagnostic.message = "Unsupported classic sprite type";
                scene.diagnostics.push_back(std::move(diagnostic));
                continue;
            }
            if ( sprite.geometryMode != 0 )
            {
                const auto direction = camera.target.Sub(camera.position);
                if ( !finite(camera.position) || !finite(camera.target) || !finite(direction) ||
                     direction.Length() < 1e-12 )
                {
                    throw std::runtime_error("snapshot Things invalid billboard camera");
                }
                const auto forward = direction.Normalized();
                const TVector hint =
                    std::abs(forward.Dot(TVector::up)) > 0.999 ? TVector{0, 1, 0} : TVector::up;
                auto right = forward.Cross(hint).Normalized();
                auto up = right.Cross(forward);
                // Matches gl_view_mesh buildSpriteInstanceVertices: camera frame,
                // Rodrigues roll around forward, then offset in the rolled frame.
                const double radians = std::remainder(thing.rol, 360.0) * 3.14159265358979323846 / 180.0;
                right = right * std::cos(radians) + forward.Cross(right) * std::sin(radians);
                up = up * std::cos(radians) + forward.Cross(up) * std::sin(radians);
                const auto center = position + right * sprite.offsetXYZ[0] + forward * sprite.offsetXYZ[1] +
                                    up * sprite.offsetXYZ[2];
                const auto r = right * (sprite.width * 0.5);
                const auto u = up * (sprite.height * 0.5);
                charge(meshes, 1, limits.maxMeshes);
                charge(vertices, 4, limits.maxVertices);
                charge(corners, 4, limits.maxCorners);
                SnapshotGeometry geometry;
                geometry.doubleSided = true;
                geometry.sector.ambient = ambient(level, thing);
                for ( const auto& p : {center - r - u, center + r - u, center + r + u, center - r + u} )
                {
                    if ( !finite(p) )
                    {
                        throw std::runtime_error("snapshot Things non-finite transformed geometry");
                    }
                    geometry.sector.vertices.push_back({p.x, p.y, p.z});
                }
                Surface surface;
                surface.geo = sprite.geometryMode;
                surface.light = 3; // lit by its sector, as before
                surface.material = sprite.material;
                surface.vertices = {0, 1, 2, 3};
                surface.texVertices = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                geometry.sector.surfaces.push_back(std::move(surface));
                scene.geometry.push_back(std::move(geometry));
                ++scene.sprites;
            }
        }
        diagnostic.status = scene.geometry.size() > firstGeometry ? "prepared" : "hidden-geometry";
        diagnostic.message = scene.geometry.size() > firstGeometry
                                 ? "Static geometry prepared; simplified shading, no gameplay verification"
                                 : "No visible faces in the static resource";
        scene.diagnostics.push_back(std::move(diagnostic));
    }
    return scene;
}
} // namespace sed::render
