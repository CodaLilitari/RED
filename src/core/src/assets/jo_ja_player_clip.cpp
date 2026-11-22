#include "jo_ja_player_clip.hpp"

#include "sed/assets/3do.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace sed::assets
{
namespace
{
[[noreturn]] void invalid(std::size_t brush, const char* reason)
{
    throw std::runtime_error("joJaImport: source player clip brush " + std::to_string(brush) + ": " + reason);
}

TVector floatPosition(const TVector& point)
{
    return {static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)};
}

JoJaPlayerClipModel buildHull(const RavenBsp& bsp, std::size_t index, double scale,
                              JoJaPlayerClipMode mode, JoJaSourceProfile profile, const std::string& material,
                              std::size_t& work)
{
    const auto& brush = bsp.brushes.at(index);
    const auto n = static_cast<std::size_t>(brush.sideCount);
    if (n < 4 || n > 32)
        invalid(index, "expected a bounded convex brush with 4..32 sides");
    // One halfspace check per side for each triple; bound pathological inputs across the whole import.
    const std::size_t charge = n * (n - 1) * (n - 2) / 6 * n;
    if (charge > work)
        invalid(index, "convex intersection work budget exceeded");
    work -= charge;
    std::vector<RavenBspPlane> planes;
    double magnitude = 1;
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto& plane = bsp.planes.at(bsp.brushSides.at(static_cast<std::size_t>(brush.firstSide) + i).plane);
        const double length = plane.normal.Length();
        const double distance = plane.distance / length;
        planes.push_back({plane.normal * (1.0 / length), distance});
        magnitude = std::max(magnitude, std::abs(distance));
    }
    const double epsilon = 1e-10 * magnitude;
    std::vector<TVector> points;
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 1; j < n; ++j)
            for (std::size_t k = j + 1; k < n; ++k)
            {
                const auto& a = planes[i]; const auto& b = planes[j]; const auto& c = planes[k];
                const auto cross = b.normal.cross(c.normal);
                const double det = a.normal.dot(cross);
                if (std::abs(det) < 1e-12)
                    continue;
                const auto p = (cross * a.distance + c.normal.cross(a.normal) * b.distance +
                                a.normal.cross(b.normal) * c.distance) * (1.0 / det);
                if (std::any_of(planes.begin(), planes.end(), [&](const auto& plane) {
                        return plane.normal.dot(p) - plane.distance > epsilon;
                    }))
                    continue;
                if (std::none_of(points.begin(), points.end(), [&](const auto& q) { return p.distance(q) <= epsilon; }))
                    points.push_back(p);
            }
    if (points.size() < 4 || points.size() > 128)
        invalid(index, "empty, unbounded or unrepresentable hull");

    JoJaPlayerClipModel out;
    out.low = out.high = points.front() * scale;
    for (const auto& p : points)
    {
        const auto v = p * scale;
        out.low = {std::min(out.low.x,v.x), std::min(out.low.y,v.y), std::min(out.low.z,v.z)};
        out.high = {std::max(out.high.x,v.x), std::max(out.high.y,v.y), std::max(out.high.z,v.z)};
    }
    out.origin = floatPosition((out.low + out.high) * 0.5);
    Model3doMesh mesh;
    mesh.name = "clip";
    mesh.texVertices.push_back({0, 0});
    std::vector<TVector> target;
    std::vector<int> vertexIds;
    std::map<std::tuple<double,double,double>,int> worldIds;
    for (const auto& p : points)
    {
        const auto v = floatPosition(p * scale - out.origin);
        const auto world = floatPosition(out.origin + v);
        if (!std::isfinite(v.Length()) || !std::isfinite(world.Length()))
            invalid(index, "coordinates exceed target float precision");
        const auto [at, added] = worldIds.emplace(std::tuple{world.x,world.y,world.z}, static_cast<int>(target.size()));
        vertexIds.push_back(at->second);
        if (!added)
            continue;
        target.push_back(v);
        out.radius = std::max(out.radius, v.Length());
        mesh.vertices.push_back({v.x,v.y,v.z, ColorBlack});
    }

    mesh.vertexNormals.resize(target.size());
    std::set<std::vector<int>> sides;
    std::map<std::pair<int,int>, unsigned> edges;
    for ( std::size_t sideIndex = 0; sideIndex < planes.size(); ++sideIndex )
    {
        const auto& plane = planes[sideIndex];
        Model3doFace face;
        face.extraLight = ColorBlack;
        TVector center{};
        for (std::size_t i = 0; i < points.size(); ++i)
            if (std::abs(plane.normal.dot(points[i]) - plane.distance) <= epsilon * 2)
            {
                face.vertices.push_back(static_cast<int>(i));
                center = center + points[i];
            }
        if (face.vertices.size() < 3)
            continue; // Edge/vertex bevel or duplicate supporting plane.
        center = center * (1.0 / static_cast<double>(face.vertices.size()));
        const auto u = (points[face.vertices.front()] - center).Normalized();
        const auto v = plane.normal.cross(u);
        const auto angle = [&](int i) {
            const auto d = points[i] - center;
            return std::atan2(d.dot(v), d.dot(u));
        };
        std::sort(face.vertices.begin(), face.vertices.end(), [&](int a, int b) { return angle(a) < angle(b); });
        // A compiler bevel can be thinner than one JK world-coordinate ULP.
        // Merge exactly identical target points; certify the remaining shell below.
        for (auto& vertex : face.vertices)
            vertex = vertexIds[vertex];
        face.vertices.erase(std::unique(face.vertices.begin(), face.vertices.end()), face.vertices.end());
        if (face.vertices.size() > 1 && face.vertices.front() == face.vertices.back())
            face.vertices.pop_back();
        const auto count = face.vertices.size();
        if (count < 3)
            continue;
        auto key = face.vertices;
        std::sort(key.begin(), key.end());
        if (std::adjacent_find(key.begin(), key.end()) != key.end())
            invalid(index, "target face folds onto a nonadjacent corner");
        if (!sides.insert(std::move(key)).second)
            continue;
        if (count > 24)
            invalid(index, "face exceeds the classic 3DO corner limit");
        for (std::size_t j = 0; j < count; ++j)
        {
            const int a = face.vertices[j], b = face.vertices[(j+1)%count], c = face.vertices[(j+2)%count];
            if ((target[b]-target[a]).cross(target[c]-target[b]).dot(plane.normal) <= 0)
                invalid(index, "degenerate or nonconvex face at target precision");
            ++edges[{a,b}];
            mesh.vertexNormals[a] = mesh.vertexNormals[a] + plane.normal;
        }
        if ( mode == JoJaPlayerClipMode::Standable && profile != JoJaSourceProfile::Legacy )
        {
            // Measure the facets that survived target rounding and deduplication.
            TVector areaVector{};
            const auto& origin = target[face.vertices.front()];
            for ( std::size_t j = 2; j < count; ++j )
            {
                const auto edge = target[face.vertices[j - 1]] - origin;
                areaVector = areaVector + edge.cross(target[face.vertices[j]] - origin);
            }
            const double twiceArea = areaVector.Length();
            constexpr float kMinimumWalkNormalZ = 0.7f; // Raven MP MIN_WALK_NORMAL.
            if ( twiceArea > 0 && areaVector.z >= kMinimumWalkNormalZ * twiceArea )
            {
                const auto& side = bsp.brushSides.at(static_cast<std::size_t>(brush.firstSide) + sideIndex);
                out.walkableAreaByShader[side.shader] += 0.5 * twiceArea;
            }
        }
        face.texVertices.assign(count, 0);
        mesh.faces.push_back(std::move(face));
        mesh.faceNormals.push_back(plane.normal);
    }
    if (mesh.faces.size() < 4 || std::any_of(edges.begin(), edges.end(), [&](const auto& e) {
            const auto opposite = edges.find({e.first.second,e.first.first});
            return e.second != 1 || opposite == edges.end() || opposite->second != 1;
        }))
        invalid(index, "source planes do not form a closed convex shell");
    for (auto& normal : mesh.vertexNormals)
        normal.Normalize();
    out.radius += 0.001; // Conservative broadphase margin, like generated architecture.
    mesh.authoredRadius = mesh.radius = out.radius;
    Model3do model;
    model.version = 2.1;
    model.geosetCount = 1;
    model.hierarchyTransformsApplied = false;
    model.radius = out.radius;
    model.materials.push_back(material);
    model.meshes.push_back(std::move(mesh));
    Model3doNode node;
    node.mesh = 0;
    node.parent = -1;
    node.name = "clip";
    model.nodes.push_back(std::move(node));
    if (!model3doEngineLimitIssues(model).empty())
        invalid(index, "model exceeds classic loader limits");
    out.report = {index, 0, model.meshes.front().faces.size(), mode, "jjc" + std::to_string(index) + ".3do", std::nullopt};
    out.bytes = saveTo3DO(model, {.scalarVersion21Vertices = true});
    return out;
}
} // namespace

const char* joJaPlayerClipName(JoJaPlayerClipMode mode)
{
    switch (mode)
    {
    case JoJaPlayerClipMode::Omit: return "omit";
    case JoJaPlayerClipMode::Standable: return "standable";
    case JoJaPlayerClipMode::MovementBarrier: return "movementBarrier";
    }
    throw std::runtime_error("joJaImport: unknown source player clip mode");
}

std::vector<JoJaPlayerClipModel> buildJoJaPlayerClips(const RavenBsp& bsp, const JoJaImportOptions& options,
                                                      std::size_t availableModels, const std::string& material)
{
    (void)joJaPlayerClipName(options.sourcePlayerClip);
    std::set<std::size_t> overrides(options.movementBarrierBrushes.begin(), options.movementBarrierBrushes.end());
    if (overrides.size() != options.movementBarrierBrushes.size() || overrides.size() > 512 ||
        (!overrides.empty() && options.sourcePlayerClip != JoJaPlayerClipMode::Standable))
        throw std::runtime_error("joJaImport: movementBarrierBrushes requires standable and unique source brush IDs");
    std::vector<JoJaPlayerClipModel> clips;
    if (options.sourcePlayerClip == JoJaPlayerClipMode::Omit)
        return clips;
    if (options.target != ProjectType::JKDF2 || options.geometryMode != JoJaGeometryMode::ModelScene)
        throw std::runtime_error("joJaImport: source player clip hulls require JK model-scene");
    const auto& world = bsp.models.front();
    std::size_t work = 8000000;
    for (int j = 0; j < world.brushCount; ++j)
    {
        const auto index = static_cast<std::size_t>(world.firstBrush + j);
        const auto contents = bsp.shaders.at(bsp.brushes[index].shader).contents;
        // PLAYERCLIP may coexist with MONSTERCLIP and the translucent contents classification.
        // Mixed solid, shot, water or other gameplay contents require separate authoring.
        if (!(contents & 0x10u) || (contents & ~0x80000030u))
            continue;
        if (clips.size() >= availableModels)
            invalid(index, "visible architecture and source hulls exceed maxModels");
        const auto mode = overrides.erase(index) ? JoJaPlayerClipMode::MovementBarrier : options.sourcePlayerClip;
        clips.push_back(buildHull(bsp, index, options.scale, mode, options.sourceProfile, material, work));
    }
    if (!overrides.empty())
        invalid(*overrides.begin(), "override does not name an eligible world PLAYERCLIP brush");
    return clips;
}
} // namespace sed::assets
