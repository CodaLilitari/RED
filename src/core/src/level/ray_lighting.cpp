#include "sed/level/ray_lighting.hpp"
#include "sed/level/mirror.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sed::level
{
namespace
{
using Vec = RayLightVec;
using Triangle = RayLightTriangle;
constexpr std::size_t kBatch = 512;
constexpr std::size_t kMaxPaths = 8'000'000;
constexpr unsigned kMaxPortalSteps = 256;
constexpr float kFar = std::numeric_limits<float>::max();

void require(bool valid, const char* message)
{
    if ( !valid )
    {
        throw std::runtime_error(message);
    }
}

Vec operator+(Vec a, Vec b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec operator-(Vec a, Vec b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec operator*(Vec a, float k)
{
    return {a.x * k, a.y * k, a.z * k};
}
float dot(Vec a, Vec b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec cross(Vec a, Vec b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
bool finite(Vec v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float maxAbs(Vec v)
{
    return std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
}
Vec unit(Vec v)
{
    const float length = std::sqrt(dot(v, v));
    require(length > 0 && std::isfinite(length), "Invalid normal in ray lighting scene.");
    return v * (1.0f / length);
}
float axis(Vec v, int i)
{
    return i == 0 ? v.x : (i == 1 ? v.y : v.z);
}
Vec lower(Vec a, Vec b)
{
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
Vec upper(Vec a, Vec b)
{
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}
Vec rgb(ColorF c)
{
    return {c.r, c.g, c.b};
}
Vec modulate(Vec a, Vec b)
{
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}
bool validColor(Vec c, float maximum)
{
    return finite(c) && c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x <= maximum && c.y <= maximum && c.z <= maximum;
}

bool boundsHit(const RayLightNode& node, const RayLightRay& ray, float near, float far)
{
    for ( int i = 0; i < 3; ++i )
    {
        const float d = axis(ray.direction, i), p = axis(ray.origin, i);
        const float low = axis(node.minimum, i), high = axis(node.maximum, i);
        if ( d == 0 )
        {
            if ( p < low || p > high )
            {
                return false;
            }
            continue;
        }
        float a = (low - p) / d, b = (high - p) / d;
        if ( a > b )
        {
            std::swap(a, b);
        }
        near = std::max(near, a);
        far = std::min(far, b);
        if ( far < near )
        {
            return false;
        }
    }
    return true;
}

void triangleHit(const Triangle& tri, int index, const RayLightRay& ray, RayLightHit& best)
{
    const Vec ab = tri.b - tri.a, ac = tri.c - tri.a;
    const Vec p = cross(ray.direction, ac);
    const float det = dot(ab, p);
    // Scale the parallel tolerance with the face area. A small mesh triangle
    // still blocks a head-on ray, even if its area is below 1e-8 square JKU.
    const float tolerance = 1.e-8f * maxAbs(cross(ab, ac)) * maxAbs(ray.direction);
    if ( !std::isfinite(det) || std::abs(det) <= tolerance )
    {
        return;
    }
    const float inv = 1.0f / det;
    const Vec diff = ray.origin - tri.a;
    const float u = dot(diff, p) * inv;
    if ( u < 0 || u > 1 )
    {
        return;
    }
    const Vec q = cross(diff, ab);
    const float v = dot(ray.direction, q) * inv;
    if ( v < 0 || u + v > 1 )
    {
        return;
    }
    const float t = dot(ac, q) * inv;
    if ( t > ray.minDistance && t <= ray.maxDistance &&
         (best.triangle < 0 || t < best.distance || (t == best.distance && index < best.triangle)) )
    {
        best = {index, t, u, v};
    }
}

struct FaceData
{
    int sector{}, surface{}, portal{-1};
    std::size_t firstCorner{};
    bool sky{};
    Vec albedo{1, 1, 1}, emission{};
};
struct Primitive
{
    Triangle shape;
    int face{-1};
    std::array<int, 3> corner{};
    int ordinal{};
};
struct Corner
{
    Vec origin, normal;
    int sector{};
};
struct Scene
{
    std::vector<RayLightNode> nodes;
    std::vector<Triangle> triangles;
    std::vector<Primitive> primitives;
    std::vector<FaceData> faces;
    std::vector<Corner> corners;
    std::vector<int> roots, firstTriangle, endTriangle;
};

int buildNode(Scene& scene, int begin, int end)
{
    Vec low{kFar, kFar, kFar}, high{-kFar, -kFar, -kFar};
    for ( int i = begin; i < end; ++i )
    {
        const auto& t = scene.primitives[i].shape;
        low = lower(low, lower(t.a, lower(t.b, t.c)));
        high = upper(high, upper(t.a, upper(t.b, t.c)));
    }
    const int index = static_cast<int>(scene.nodes.size());
    scene.nodes.push_back({low, high, -1, -1, begin, end - begin});
    if ( end - begin <= 8 )
    {
        std::sort(scene.primitives.begin() + begin, scene.primitives.begin() + end,
                  [](const Primitive& a, const Primitive& b) { return a.ordinal < b.ordinal; });
        return index;
    }
    const Vec extent = high - low;
    const int dimension = extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
    const int mid = begin + (end - begin) / 2;
    auto center = [dimension](const Triangle& t)
    { return axis(t.a, dimension) + axis(t.b, dimension) + axis(t.c, dimension); };
    std::nth_element(scene.primitives.begin() + begin, scene.primitives.begin() + mid, scene.primitives.begin() + end,
                     [&](const auto& a, const auto& b)
                     {
                         const float x = center(a.shape), y = center(b.shape);
                         return x == y ? a.ordinal < b.ordinal : x < y;
                     });
    const int left = buildNode(scene, begin, mid);
    const int right = buildNode(scene, mid, end);
    scene.nodes[index] = {low, high, left, right, 0, 0};
    return index;
}

Vec point(const Sector& sector, int index)
{
    require(index >= 0 && static_cast<std::size_t>(index) < sector.vertices.size(),
            "Ray lighting surface has an out-of-range vertex.");
    const auto& p = sector.vertices[index];
    Vec v{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)};
    require(finite(v) && std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)}) <= 1.e7f,
            "Ray lighting position is outside its finite coordinate budget.");
    return v;
}

int portalTarget(const Level& level, const Surface& face, int sectorIndex, int faceIndex)
{
    if ( !face.adjoin )
    {
        return -1;
    }
    const auto [s, f] = *face.adjoin;
    require(s >= 0 && static_cast<std::size_t>(s) < level.sectors.size() && s != sectorIndex && f >= 0 &&
                static_cast<std::size_t>(f) < level.sectors[s].surfaces.size(),
            "Ray lighting requires valid reciprocal adjoins.");
    const auto& other = level.sectors[s].surfaces[f];
    require(other.adjoin == SurfaceRef{sectorIndex, faceIndex} && other.vertices.size() == face.vertices.size(),
            "Ray lighting requires valid reciprocal adjoins.");
    // The two directions must describe the same physical opening, in reverse.
    const auto count = face.vertices.size();
    bool matching = false;
    for ( std::size_t start = 0; start < count && !matching; ++start )
    {
        matching = true;
        for ( std::size_t i = 0; i < count; ++i )
        {
            const Vec delta = point(level.sectors[sectorIndex], face.vertices[i]) -
                              point(level.sectors[s], other.vertices[(start + count - i) % count]);
            if ( dot(delta, delta) > 1.e-12f )
            {
                matching = false;
                break;
            }
        }
    }
    require(matching, "Ray lighting adjoin polygons do not match.");
    return ((face.adjoinFlags | other.adjoinFlags) & SAF_BlockLight) ? -1 : s;
}

void appendFace(Scene& scene, const Level& level, int s, int f, const RayLightingMaterial* material)
{
    const auto& sec = level.sectors[s];
    const auto& surface = sec.surfaces[f];
    const std::size_t n = surface.vertices.size();
    require(n >= 3 && n <= 64 && surface.texVertices.size() == n,
            "Ray lighting needs matched geometry and lighting corners on 3 to 64 vertex faces.");
    require(scene.corners.size() + n <= kRayLightingMaxCorners &&
                scene.primitives.size() + n - 2 <= kRayLightingMaxTriangles,
            "Ray lighting geometry budget exceeded.");
    FaceData data{s, f, portalTarget(level, surface, s, f), scene.corners.size()};
    data.sky = (surface.surfaceFlags & (SF_SkyHorizon | SF_SkyCeiling)) != 0;
    if ( material )
    {
        data.sky |= material->sky;
        data.albedo = rgb(material->albedo);
        data.emission = rgb(material->emission);
    }
    if ( surface.adjoin )
    {
        // A blocked portal remains a blocker even when it carries a sky MAT.
        data.sky = false;
        data.emission = {};
    }
    const int faceIndex = static_cast<int>(scene.faces.size());
    scene.faces.push_back(data);
    std::array<Vec, 64> vertices{};
    Vec center{};
    for ( std::size_t i = 0; i < n; ++i )
    {
        vertices[i] = point(sec, surface.vertices[i]);
        center = center + vertices[i] * (1.0f / static_cast<float>(n));
    }
    const Vec norm = unit({static_cast<float>(surface.normal.dx), static_cast<float>(surface.normal.dy),
                           static_cast<float>(surface.normal.dz)});
    // A fan represents convex JKL faces. Refuse concavity or nonplanarity
    // rather than inventing blockers outside the authored boundary.
    float winding = 0;
    for ( std::size_t i = 0; i < n; ++i )
    {
        const Vec a = vertices[i], edge = vertices[(i + 1) % n] - a;
        const float length = std::sqrt(dot(edge, edge));
        require(length > 0 && std::abs(dot(a - vertices[0], norm)) <= 1.e-5f * std::max(1.0f, length),
                "Ray lighting requires planar, noncollapsed faces.");
        for ( std::size_t j = 0; j < n; ++j )
        {
            const float side = dot(cross(edge, vertices[j] - a), norm);
            if ( std::abs(side) <= 1.e-7f * length )
            {
                continue;
            }
            if ( winding == 0 )
            {
                winding = side;
            }
            require((side < 0) == (winding < 0), "Ray lighting requires convex surface polygons.");
        }
        const Vec localOrigin = a + (center - a) * 0.0001f + norm * 0.000001f;
        scene.corners.push_back({localOrigin, norm, s});
    }
    require(winding != 0, "Ray lighting face is degenerate.");
    for ( std::size_t i = 1; i + 1 < n; ++i )
    {
        const Vec area = cross(vertices[i] - vertices[0], vertices[i + 1] - vertices[0]);
        if ( dot(area, area) == 0 )
        {
            continue;
        }
        scene.primitives.push_back({{vertices[0], vertices[i], vertices[i + 1]},
                                    faceIndex,
                                    {0, static_cast<int>(i), static_cast<int>(i + 1)},
                                    static_cast<int>(scene.primitives.size())});
    }
}

struct Cancelled
{
};
void poll(const LightingProgressCallback& progress, std::size_t done, std::size_t total)
{
    if ( progress && !progress(done, total) )
    {
        throw Cancelled{};
    }
}

Scene prepare(const Level& level, std::span<const RayLightingMaterial> materials,
              std::span<const RayLightingBlocker> blockers, const LightingProgressCallback& progress)
{
    require(!level.sectors.empty() && level.sectors.size() <= 65536 && blockers.size() <= kRayLightingMaxTriangles &&
                materials.size() <= kRayLightingMaxTriangles,
            "Ray lighting scene exceeds its sector or input budget.");
    const int count = static_cast<int>(level.sectors.size());
    std::map<std::pair<int, int>, const RayLightingMaterial*> overrides;
    for ( const auto& m : materials )
    {
        require(m.sector >= 0 && m.sector < count && m.surface >= 0 &&
                    static_cast<std::size_t>(m.surface) < level.sectors[m.sector].surfaces.size() &&
                    validColor(rgb(m.albedo), 1) && validColor(rgb(m.emission), 1000),
                "Invalid ray lighting material reference, albedo or emission.");
        require(overrides.emplace(std::pair{m.sector, m.surface}, &m).second, "Duplicate ray lighting material.");
    }
    std::vector<std::vector<Triangle>> models(count);
    for ( const auto& b : blockers )
    {
        const auto& t = b.triangle;
        const Vec area = cross(t.b - t.a, t.c - t.a);
        require(b.sector >= 0 && b.sector < count && finite(t.a) && finite(t.b) && finite(t.c) && finite(area) &&
                    std::isfinite(dot(area, area)) && dot(area, area) > 0,
                "Invalid ray lighting model blocker.");
        models[b.sector].push_back(t);
    }
    Scene scene;
    std::size_t vertices = 0;
    for ( int s = 0; s < count; ++s )
    {
        poll(progress, 0, 1);
        const auto& sector = level.sectors[s];
        require(sector.vertices.size() <= kRayLightingMaxCorners - vertices,
                "Ray lighting world vertex budget exceeded.");
        vertices += sector.vertices.size();
        scene.firstTriangle.push_back(static_cast<int>(scene.primitives.size()));
        for ( std::size_t f = 0; f < sector.surfaces.size(); ++f )
        {
            if ( f % 128 == 0 )
            {
                poll(progress, 0, 1);
            }
            const auto it = overrides.find({s, static_cast<int>(f)});
            appendFace(scene, level, s, static_cast<int>(f), it == overrides.end() ? nullptr : it->second);
        }
        require(models[s].size() <= kRayLightingMaxTriangles - scene.primitives.size(),
                "Ray lighting triangle budget exceeded.");
        for ( const auto& triangle : models[s] )
        {
            scene.primitives.push_back({triangle, -1, {}, static_cast<int>(scene.primitives.size())});
        }
        const int end = static_cast<int>(scene.primitives.size());
        scene.endTriangle.push_back(end);
        scene.roots.push_back(scene.firstTriangle.back() < end ? buildNode(scene, scene.firstTriangle.back(), end)
                                                               : -1);
    }
    scene.triangles.reserve(scene.primitives.size());
    for ( const auto& p : scene.primitives )
    {
        scene.triangles.push_back(p.shape);
    }
    return scene;
}

std::vector<Vec> hemisphere(unsigned samples)
{
    std::vector<Vec> directions;
    for ( unsigned i = 0; i < samples; ++i )
    {
        // Equal projected-area annuli with golden-angle azimuth ordering.
        const double r2 = (i + 0.5) / samples;
        const double angle = i * (std::numbers::pi * (3.0 - std::sqrt(5.0)));
        directions.push_back({static_cast<float>(std::sqrt(r2) * std::cos(angle)),
                              static_cast<float>(std::sqrt(r2) * std::sin(angle)),
                              static_cast<float>(std::sqrt(1 - r2))});
    }
    return directions;
}

struct Contact
{
    int triangle{-1};
    float u{}, v{};
};

bool intersectCpu(std::span<const RayLightTriangle> triangles, std::span<const RayLightNode> nodes,
                  std::span<const RayLightRay> rays, std::span<RayLightHit> hits, std::uint64_t& remaining);

std::vector<Contact> trace(const Scene& scene, const std::vector<Vec>& directions, const RayLightIntersector& intersect,
                           const LightingProgressCallback& progress, RayLightingResult& result)
{
    const std::size_t count = scene.corners.size() * directions.size();
    std::vector<Contact> contacts(count);
    std::uint64_t remaining = kRayLightingMaxTraversalWork;
    std::array<RayLightRay, kBatch> rays;
    std::array<RayLightHit, kBatch> hits;
    std::array<int, kBatch> sectors{};
    std::array<std::size_t, kBatch> paths{};
    for ( std::size_t base = 0; base < count; base += kBatch )
    {
        std::size_t active = std::min(kBatch, count - base);
        for ( std::size_t j = 0; j < active; ++j )
        {
            const auto& corner = scene.corners[(base + j) / directions.size()];
            const Vec n = corner.normal;
            const Vec t = unit(cross(std::abs(n.z) < .9f ? Vec{0, 0, 1} : Vec{0, 1, 0}, n));
            const Vec local = directions[(base + j) % directions.size()];
            rays[j] = {corner.origin, t * local.x + cross(n, t) * local.y + n * local.z, 0, kFar,
                       scene.roots[corner.sector]};
            paths[j] = base + j;
            sectors[j] = corner.sector;
        }
        for ( unsigned step = 0; active; ++step )
        {
            poll(progress, base, count);
            require(step <= kMaxPortalSteps, "Ray lighting portal traversal budget exceeded.");
            result.rays += active;
            require(result.rays <= kRayLightingMaxQueries, "Ray lighting query budget exceeded.");
            const auto packets = std::span(rays).first(active);
            auto responses = std::span(hits).first(active);
            std::fill(responses.begin(), responses.end(), RayLightHit{-2, 0, 0, 0});
            const bool ok = intersect ? intersect(scene.triangles, scene.nodes, packets, responses)
                                      : intersectCpu(scene.triangles, scene.nodes, packets, responses, remaining);
            require(ok, intersect ? "Ray lighting intersection backend failed."
                                  : "Ray lighting CPU traversal budget exceeded or prepared BVH is invalid.");
            std::size_t continued = 0;
            for ( std::size_t j = 0; j < active; ++j )
            {
                const auto h = hits[j];
                require(std::isfinite(h.distance) && std::isfinite(h.u) && std::isfinite(h.v),
                        "Ray lighting backend returned a non-finite hit.");
                if ( h.triangle == -1 )
                {
                    continue;
                }
                const int s = sectors[j];
                require(h.triangle >= scene.firstTriangle[s] && h.triangle < scene.endTriangle[s] &&
                            h.distance > rays[j].minDistance && h.distance <= rays[j].maxDistance && h.u >= 0 &&
                            h.v >= 0 && h.u + h.v <= 1,
                        "Ray lighting backend returned an invalid hit or sector.");
                const int face = scene.primitives[h.triangle].face;
                if ( face >= 0 && scene.faces[face].portal >= 0 )
                {
                    auto ray = rays[j];
                    const auto& boundary = scene.faces[face];
                    const Vec normal = scene.corners[boundary.firstCorner].normal;
                    int destination = s;
                    if ( dot(normal, ray.direction) < 0 )
                    {
                        destination = boundary.portal;
                        const auto& triangle = scene.triangles[h.triangle];
                        // Start the next query on the crossed polygon itself. A
                        // world-distance epsilon can skip a real thin blocker.
                        ray.origin = triangle.a + (triangle.b - triangle.a) * h.u + (triangle.c - triangle.a) * h.v;
                        ray.minDistance = 0;
                        ray.root = scene.roots[destination];
                    }
                    else
                    {
                        // Rounding may put the incoming mirror just ahead of
                        // our new origin. It is an entrance, not an exit.
                        ray.minDistance = h.distance;
                    }
                    rays[continued] = ray;
                    paths[continued] = paths[j];
                    sectors[continued++] = destination;
                }
                else
                {
                    contacts[paths[j]] = {h.triangle, h.u, h.v};
                }
            }
            active = continued;
        }
    }
    return contacts;
}

bool intersectCpu(std::span<const RayLightTriangle> triangles, std::span<const RayLightNode> nodes,
                  std::span<const RayLightRay> rays, std::span<RayLightHit> hits, std::uint64_t& remaining)
{
    if ( rays.size() != hits.size() || triangles.size() > kRayLightingMaxTriangles ||
         nodes.size() > 2 * kRayLightingMaxTriangles )
    {
        return false;
    }
    for ( std::size_t i = 0; i < rays.size(); ++i )
    {
        const auto& ray = rays[i];
        hits[i] = {};
        if ( !finite(ray.origin) || !finite(ray.direction) || !std::isfinite(ray.minDistance) ||
             !std::isfinite(ray.maxDistance) || ray.minDistance < 0 || ray.minDistance >= ray.maxDistance ||
             dot(ray.direction, ray.direction) <= 0 )
        {
            return false;
        }
        if ( ray.root == -1 )
        {
            continue;
        }
        std::uint32_t operations = 0;
        std::array<int, 64> stack{};
        std::size_t size = 1, visited = 0;
        stack[0] = ray.root;
        while ( size )
        {
            if ( remaining == 0 || ++operations > kRayLightingMaxQueryWork )
            {
                return false;
            }
            --remaining;
            const int index = stack[--size];
            if ( index < 0 || static_cast<std::size_t>(index) >= nodes.size() || ++visited > nodes.size() )
            {
                return false;
            }
            const auto& node = nodes[index];
            const float far = hits[i].triangle < 0 ? ray.maxDistance : hits[i].distance;
            if ( !boundsHit(node, ray, ray.minDistance, far) )
            {
                continue;
            }
            if ( node.count > 0 )
            {
                if ( node.first < 0 || static_cast<std::size_t>(node.first) > triangles.size() ||
                     static_cast<std::size_t>(node.count) > triangles.size() - node.first )
                {
                    return false;
                }
                const auto tests = static_cast<std::uint32_t>(node.count);
                if ( tests > remaining || tests > kRayLightingMaxQueryWork - operations )
                {
                    return false;
                }
                remaining -= tests;
                operations += tests;
                for ( int j = node.first; j < node.first + node.count; ++j )
                {
                    triangleHit(triangles[j], j, ray, hits[i]);
                }
            }
            else
            {
                if ( node.count < 0 || size + 2 > stack.size() )
                {
                    return false;
                }
                stack[size++] = node.right;
                stack[size++] = node.left;
            }
        }
    }
    return true;
}

} // namespace

bool intersectRayLightingCpu(std::span<const RayLightTriangle> triangles, std::span<const RayLightNode> nodes,
                             std::span<const RayLightRay> rays, std::span<RayLightHit> hits)
{
    std::uint64_t remaining = kRayLightingMaxTraversalWork;
    return intersectCpu(triangles, nodes, rays, hits, remaining);
}

RayLightingResult bakeRayLighting(Level& level, std::span<const std::int32_t> targets,
                                  const RayLightingOptions& options, std::span<const RayLightingMaterial> materials,
                                  std::span<const RayLightingBlocker> blockers, const RayLightIntersector& intersector,
                                  const LightingProgressCallback& progress)
{
    RayLightingResult result;
    try
    {
        require(options.samples > 0 && options.samples <= kRayLightingMaxSamples &&
                    options.bounces <= kRayLightingMaxBounces && std::isfinite(options.reflectivity) &&
                    options.reflectivity >= 0 && options.reflectivity <= 1 && std::isfinite(options.skyIntensity) &&
                    options.skyIntensity >= 0 && options.skyIntensity <= 1000 && validColor(rgb(options.skyColor), 1),
                "Invalid ray lighting sampling, sky or reflectivity options.");
        require(!targets.empty() && targets.size() <= level.sectors.size() && level.sectors.size() <= 65536,
                "Ray lighting requires target sectors within the scene budget.");
        std::vector<bool> selected(level.sectors.size());
        for ( int s : targets )
        {
            require(s >= 0 && static_cast<std::size_t>(s) < level.sectors.size() && !selected[s],
                    "Ray lighting requires unique valid target sectors.");
            selected[s] = true;
        }
        poll(progress, 0, 1);
        Scene scene = prepare(level, materials, blockers, progress);
        require(levelFindMirrorFloors(level).empty(), "Ray lighting does not support generated mirror floors.");
        const auto paths = scene.corners.size() * static_cast<std::uint64_t>(options.samples);
        require(paths <= kMaxPaths && paths * (options.bounces + 1) <= kRayLightingMaxQueries,
                "Ray lighting sampling/work budget exceeded.");
        require(level.lights.size() <= 65536 &&
                    (level.lights.empty() || scene.corners.size() <= kRayLightingMaxQueries / level.lights.size()),
                "Ray lighting direct-light budget exceeded.");
        Level prepared;
        prepared.kind = level.kind;
        prepared.header = level.header;
        prepared.sectors = level.sectors;
        prepared.lights = level.lights;
        std::vector<int> all(prepared.sectors.size());
        std::iota(all.begin(), all.end(), 0);
        if ( !levelCalcRayDirectLightingSeed(prepared, all, progress, options.direct) )
        {
            throw Cancelled{};
        }
        auto contacts = trace(scene, hemisphere(options.samples), intersector, progress, result);
        std::vector<Vec> total, previous, next(scene.corners.size());
        total.reserve(scene.corners.size());
        for ( const auto& f : scene.faces )
        {
            for ( const auto& v : prepared.sectors[f.sector].surfaces[f.surface].texVertices )
            {
                require(finite(rgb(v.color)), "Direct bake returned non-finite radiance.");
                total.push_back(rgb(v.color));
            }
        }
        previous = total;
        const Vec sky = rgb(options.skyColor) * static_cast<float>(options.skyIntensity);
        for ( unsigned order = 0; order <= options.bounces; ++order )
        {
            for ( std::size_t j = 0; j < scene.corners.size(); ++j )
            {
                if ( j % 128 == 0 )
                {
                    poll(progress, j, scene.corners.size());
                }
                Vec sum{};
                for ( unsigned k = 0; k < options.samples; ++k )
                {
                    const auto& hit = contacts[j * options.samples + k];
                    if ( hit.triangle < 0 )
                    {
                        continue;
                    }
                    const auto& p = scene.primitives[hit.triangle];
                    if ( p.face < 0 )
                    {
                        continue;
                    }
                    const auto& f = scene.faces[p.face];
                    if ( order == 0 )
                    {
                        sum = sum + (f.sky ? modulate(sky, f.albedo) : f.emission);
                    }
                    else if ( !f.sky && !level.sectors[f.sector].surfaces[f.surface].adjoin )
                    {
                        const auto start = f.firstCorner;
                        const Vec incoming = previous[start + p.corner[0]] * (1 - hit.u - hit.v) +
                                             previous[start + p.corner[1]] * hit.u +
                                             previous[start + p.corner[2]] * hit.v;
                        sum = sum + modulate(incoming, f.albedo) * static_cast<float>(options.reflectivity);
                    }
                }
                next[j] = sum * (1.0f / static_cast<float>(options.samples));
                total[j] = total[j] + next[j];
                require(finite(total[j]), "Ray lighting generated non-finite radiance.");
            }
            if ( order == 0 )
            {
                previous = total; // Direct plus primary sky/emission seeds bounce one.
            }
            else
            {
                previous.swap(next);
            }
        }
        const bool scalar = level.kind == ProjectType::JKDF2 || level.kind == ProjectType::DroidWorks;
        for ( const auto& f : scene.faces )
        {
            if ( !selected[f.sector] )
            {
                continue;
            }
            auto& corners = prepared.sectors[f.sector].surfaces[f.surface].texVertices;
            for ( std::size_t i = 0; i < corners.size(); ++i )
            {
                const Vec c = total[f.firstCorner + i];
                const float grey = (c.x + c.y + c.z) / 3;
                corners[i].color =
                    scalar ? ColorF{grey, grey, grey, grey}
                           : ColorF{c.x, c.y, c.z, level.sectors[f.sector].surfaces[f.surface].texVertices[i].color.a};
                corners[i].color.normalize();
            }
        }
        poll(progress, 1, 1);
        for ( const int s : targets )
        {
            auto& out = level.sectors[s];
            const auto& source = prepared.sectors[s];
            for ( std::size_t f = 0; f < out.surfaces.size(); ++f )
            {
                auto& corners = out.surfaces[f].texVertices;
                for ( std::size_t i = 0; i < corners.size(); ++i )
                {
                    const float originalWhite = corners[i].color.a;
                    corners[i].color = source.surfaces[f].texVertices[i].color;
                    if ( !scalar )
                    {
                        corners[i].color.a = originalWhite;
                    }
                }
            }
        }
        result.succeeded = true;
    }
    catch ( const Cancelled& )
    {
        result.cancelled = true;
        result.message = "Ray lighting cancelled.";
    }
    catch ( const std::exception& e )
    {
        result.message = e.what();
    }
    return result;
}

} // namespace sed::level
