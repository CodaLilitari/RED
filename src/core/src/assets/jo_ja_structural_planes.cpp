#include "sed/assets/jo_ja_structural_planes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace sed::assets
{
namespace
{
constexpr double kSupportTolerance = 1.0 / 1024.0; // Raven source units.
constexpr double kNormalDifferenceSquared = 1e-12;
constexpr double kInfinity = std::numeric_limits<double>::infinity();

bool structural(const RavenBspShader& shader)
{
    return (shader.contents & 0x8001u) == 0x8001u && (shader.contents & 0x80000000u) == 0;
}

template <class T> std::span<const T> range(const std::vector<T>& items, int first, int count)
{
    if ( first < 0 || count < 0 || static_cast<std::size_t>(first) > items.size() ||
         static_cast<std::size_t>(count) > items.size() - static_cast<std::size_t>(first) )
    {
        throw std::runtime_error("JO/JA structural brush range is invalid");
    }
    return std::span(items).subspan(first, count);
}

struct Bounds
{
    std::array<double, 3> low{kInfinity, kInfinity, kInfinity};
    std::array<double, 3> high{-kInfinity, -kInfinity, -kInfinity};

    void include(const Bounds& other)
    {
        for ( int i = 0; i < 3; ++i )
        {
            low[i] = std::min(low[i], other.low[i]);
            high[i] = std::max(high[i], other.high[i]);
        }
    }

    bool overlaps(const Bounds& other) const
    {
        for ( std::size_t axis = 0; axis < 3; ++axis )
        {
            if ( high[axis] + kSupportTolerance < other.low[axis] || low[axis] - kSupportTolerance > other.high[axis] )
            {
                return false;
            }
        }
        return true;
    }

    bool contains(const Bounds& other) const
    {
        for ( int i = 0; i < 3; ++i )
        {
            if ( other.low[i] < low[i] - kSupportTolerance || other.high[i] > high[i] + kSupportTolerance )
            {
                return false;
            }
        }
        return true;
    }
};

} // namespace

class JoJaStructuralBrushIndex::Impl
{
    struct Brush
    {
        std::size_t source;
        Bounds bounds;
    };
    struct Node
    {
        Bounds bounds;
        std::size_t first{}, count{}, right{};
    };
    const RavenBsp& bsp_;
    std::vector<double> inverseLengths_;
    std::vector<Brush> brushes_;
    std::vector<Node> nodes_;
    // Coincident, very large brush sets cannot force an unbounded scan.
    std::size_t work_{};
    void charge(std::size_t amount = 1)
    {
        work_ += amount;
        if ( work_ > 128u * 1024u * 1024u )
        {
            throw std::runtime_error("JO/JA structural brush classification exceeded its work limit");
        }
    }

    RavenBspPlane plane(int id) const
    {
        const auto& p = bsp_.planes.at(id);
        const double inverse = inverseLengths_[id];
        return {p.normal * inverse, p.distance * inverse};
    }

    std::optional<Bounds> axialBounds(const RavenBspBrush& brush)
    {
        Bounds box;
        box.low.fill(-kInfinity);
        box.high.fill(kInfinity);
        const auto sides = range(bsp_.brushSides, brush.firstSide, brush.sideCount);
        charge(sides.size());
        for ( const auto& side : sides )
        {
            const auto& p = bsp_.planes.at(side.plane);
            const std::array<double, 3> n{p.normal.x, p.normal.y, p.normal.z};
            for ( int axis = 0; axis < 3; ++axis )
            {
                if ( n[axis] == 0 || n[(axis + 1) % 3] != 0 || n[(axis + 2) % 3] != 0 )
                {
                    continue;
                }
                const double bound = p.distance / n[axis];
                if ( n[axis] > 0 )
                {
                    box.high[axis] = std::min(box.high[axis], bound);
                }
                else
                {
                    box.low[axis] = std::max(box.low[axis], bound);
                }
            }
        }
        for ( int i = 0; i < 3; ++i )
        {
            if ( !std::isfinite(box.low[i]) || !std::isfinite(box.high[i]) || box.low[i] >= box.high[i] )
            {
                return std::nullopt;
            }
        }
        return box;
    }

    std::size_t build(std::size_t first, std::size_t count)
    {
        Bounds bounds;
        for ( auto i = first; i < first + count; ++i )
        {
            bounds.include(brushes_[i].bounds);
        }
        const auto index = nodes_.size();
        nodes_.push_back({bounds, first, count, 0});
        if ( count > 8 )
        {
            int axis = 0;
            for ( int i = 1; i < 3; ++i )
            {
                if ( bounds.high[i] - bounds.low[i] > bounds.high[axis] - bounds.low[axis] )
                {
                    axis = i;
                }
            }
            const auto middle = first + count / 2;
            std::nth_element(brushes_.begin() + first, brushes_.begin() + middle, brushes_.begin() + first + count,
                             [axis](const Brush& a, const Brush& b)
                             {
                                 const double ca = a.bounds.low[axis] + a.bounds.high[axis];
                                 const double cb = b.bounds.low[axis] + b.bounds.high[axis];
                                 return ca != cb ? ca < cb : a.source < b.source;
                             });
            build(first, count / 2);
            nodes_[index].right = build(middle, count - count / 2);
        }
        return index;
    }

    bool supports(const RavenBspBrush& brush, const RavenBspTriangle& t, int shader, const TVector& normal)
    {
        const auto sides = range(bsp_.brushSides, brush.firstSide, brush.sideCount);
        bool matched = false;
        // An inward ray from the triangle centroid must enter actual 3D solid.
        // This also rejects a zero-thickness intersection of oblique halfspaces.
        double enter = 0, leave = kInfinity;
        for ( const auto& side : sides )
        {
            charge();
            const auto p = plane(side.plane);
            double centre = 0, largest = 0;
            for ( const auto& corner : t.corners )
            {
                const double signedDistance = p.normal.dot(corner.position) - p.distance;
                if ( signedDistance > kSupportTolerance )
                {
                    return false;
                }
                centre += signedDistance / 3;
                largest = std::max(largest, std::abs(signedDistance));
            }
            matched = matched || (side.shader == shader && largest <= kSupportTolerance &&
                                  (p.normal - normal).lengthSqr() <= kNormalDifferenceSquared);
            const double slope = -p.normal.dot(normal);
            if ( slope < 0 )
            {
                enter = std::max(enter, -centre / slope);
            }
            else if ( slope > 0 )
            {
                leave = std::min(leave, -centre / slope);
            }
            else if ( centre >= 0 )
            {
                return false;
            }
        }
        return matched && enter < leave;
    }

    bool search(std::size_t nodeIndex, const Bounds& triangleBounds, const RavenBspTriangle& t, int shader,
                const TVector& normal)
    {
        charge();
        const auto& node = nodes_[nodeIndex];
        if ( !node.bounds.contains(triangleBounds) )
        {
            return false;
        }
        if ( node.right != 0 )
        {
            return search(nodeIndex + 1, triangleBounds, t, shader, normal) ||
                   search(node.right, triangleBounds, t, shader, normal);
        }
        for ( auto i = node.first; i < node.first + node.count; ++i )
        {
            const auto& brush = brushes_[i];
            if ( brush.bounds.contains(triangleBounds) && supports(bsp_.brushes[brush.source], t, shader, normal) )
            {
                return true;
            }
        }
        return false;
    }

    void intersectingBrushes(std::size_t nodeIndex, const Bounds& aperture, std::vector<std::size_t>& out)
    {
        charge();
        const auto& node = nodes_[nodeIndex];
        if ( !node.bounds.overlaps(aperture) )
        {
            return;
        }
        if ( node.right )
        {
            intersectingBrushes(nodeIndex + 1, aperture, out);
            intersectingBrushes(node.right, aperture, out);
            return;
        }
        for ( std::size_t i = node.first; i < node.first + node.count; ++i )
        {
            if ( brushes_[i].bounds.overlaps(aperture) )
            {
                out.push_back(brushes_[i].source);
            }
        }
    }

    // Clip the aperture against all halfspaces of one brush. The ring remains on
    // the original interface, preserving its winding and oblique cross-section.
    std::vector<TVector> brushSection(std::span<const RavenBspBrushSide> sides, const RavenBspPlane& aperturePlane,
                                      std::span<const TVector> aperture, double epsilon, bool& full)
    {
        std::vector<TVector> ring(aperture.begin(), aperture.end()), clipped;
        full = true;
        for ( const auto& side : sides )
        {
            const auto p = plane(side.plane);
            const auto distance = [&](const TVector& point)
            {
                const double d = p.normal.dot(point) - p.distance;
                return std::abs(d) <= epsilon ? 0.0 : d;
            };
            charge(ring.size());
            clipped.clear();
            auto previous = ring.back();
            auto a = distance(previous);
            for ( const auto& point : ring )
            {
                const auto b = distance(point);
                if ( (a < 0 && b > 0) || (a > 0 && b < 0) )
                {
                    auto cut = previous + (point - previous) * (a / (a - b));
                    cut = cut - aperturePlane.normal * (aperturePlane.normal.dot(cut) - aperturePlane.distance);
                    clipped.push_back(cut);
                }
                if ( b <= 0 )
                {
                    clipped.push_back(point);
                }
                else
                {
                    full = false;
                }
                previous = point;
                a = b;
            }
            if ( clipped.size() < 3 )
            {
                return {};
            }
            ring.swap(clipped);
        }
        TVector sum{}, centre{};
        for ( std::size_t i = 0; i < ring.size(); ++i )
        {
            centre = centre + ring[i] * (1.0 / ring.size());
            sum = sum + (ring[i] - ring.front()).cross(ring[(i + 1) % ring.size()] - ring.front());
        }
        if ( std::abs(sum.dot(aperturePlane.normal)) <= epsilon * epsilon )
        {
            return {};
        }
        // A finite axial box is necessary but not sufficient. An opposing pair
        // of oblique halfspaces can flatten the brush on the aperture itself.
        // The line through the ring's interior must enter a strictly 3D solid.
        double low = -kInfinity, high = kInfinity;
        for ( const auto& side : sides )
        {
            charge();
            const auto p = plane(side.plane);
            const auto offset = p.normal.dot(centre) - p.distance;
            const auto slope = p.normal.dot(aperturePlane.normal);
            if ( slope < 0 )
            {
                low = std::max(low, -offset / slope);
            }
            else if ( slope > 0 )
            {
                high = std::min(high, -offset / slope);
            }
            else if ( offset >= 0 )
            {
                return {};
            }
        }
        if ( !(high - low > epsilon) )
        {
            return {};
        }
        return ring;
    }

  public:
    std::vector<std::vector<TVector>> crossSections(const RavenBspPlane& rawPlane, std::span<const TVector> aperture,
                                                    std::size_t limit)
    {
        if ( limit == 0 || limit > 1000000 || aperture.size() < 3 || aperture.size() > 65536 )
        {
            throw std::runtime_error("Invalid JO/JA structural aperture bounds or fragment limit");
        }
        const double length = std::hypot(rawPlane.normal.x, rawPlane.normal.y, rawPlane.normal.z);
        if ( !(length > 0) || !std::isfinite(length) || !std::isfinite(rawPlane.distance) )
        {
            throw std::runtime_error("Invalid JO/JA structural aperture plane");
        }
        const RavenBspPlane unit{rawPlane.normal * (1.0 / length), rawPlane.distance / length};
        Bounds bounds;
        double magnitude = std::max(1.0, std::abs(unit.distance));
        for ( const auto& point : aperture )
        {
            const std::array<double, 3> values{point.x, point.y, point.z};
            for ( const double x : values )
            {
                if ( !std::isfinite(x) )
                {
                    throw std::runtime_error("Invalid JO/JA structural aperture coordinate");
                }
                magnitude = std::max(magnitude, std::abs(x));
            }
            bounds.include({values, values});
        }
        const double epsilon = 128 * std::numeric_limits<double>::epsilon() * magnitude;
        for ( const auto& point : aperture )
        {
            if ( std::abs(unit.normal.dot(point) - unit.distance) > std::max(1e-8, epsilon) )
            {
                throw std::runtime_error("Nonplanar JO/JA structural aperture");
            }
        }
        std::vector<std::size_t> candidates;
        if ( !nodes_.empty() )
        {
            intersectingBrushes(0, bounds, candidates);
        }
        std::sort(candidates.begin(), candidates.end());
        std::vector<std::vector<TVector>> masks;
        std::size_t retained = 0;
        for ( const auto index : candidates )
        {
            const auto& brush = bsp_.brushes[index];
            bool full = false;
            auto ring =
                brushSection(range(bsp_.brushSides, brush.firstSide, brush.sideCount), unit, aperture, epsilon, full);
            if ( ring.empty() )
            {
                continue;
            }
            if ( full )
            {
                return {std::move(ring)};
            }
            retained += ring.size() - 2;
            if ( retained > limit )
            {
                throw std::runtime_error("JO/JA structural aperture sections exceed the fragment limit");
            }
            masks.push_back(std::move(ring));
        }
        return masks;
    }

    const RavenBsp& source() const
    {
        return bsp_;
    }

    explicit Impl(const RavenBsp& bsp) : bsp_(bsp)
    {
        const auto& world = bsp.models.at(0);
        inverseLengths_.reserve(bsp.planes.size());
        for ( const auto& p : bsp.planes )
        {
            const double length = std::hypot(p.normal.x, p.normal.y, p.normal.z);
            if ( !std::isfinite(length) || length <= 0 || !std::isfinite(p.distance) )
            {
                throw std::runtime_error("JO/JA structural brush has an invalid plane");
            }
            inverseLengths_.push_back(1.0 / length);
        }
        auto worldBrushes = range(bsp.brushes, world.firstBrush, world.brushCount);
        for ( std::size_t i = 0; i < worldBrushes.size(); ++i )
        {
            if ( structural(bsp.shaders.at(worldBrushes[i].shader)) )
            {
                if ( const auto bounds = axialBounds(worldBrushes[i]) )
                {
                    brushes_.push_back({static_cast<std::size_t>(world.firstBrush) + i, *bounds});
                }
            }
        }
        if ( !brushes_.empty() )
        {
            build(0, brushes_.size());
        }
    }

    bool supports(const RavenBspTriangle& triangle, int shader, const TVector& normal)
    {
        Bounds bounds;
        for ( const auto& v : triangle.corners )
        {
            const std::array<double, 3> point{v.position.x, v.position.y, v.position.z};
            bounds.include({point, point});
        }
        return !nodes_.empty() && search(0, bounds, triangle, shader, normal);
    }
};

namespace
{
using PlaneKey = std::array<double, 4>;

PlaneKey key(const RavenBspPlane& p)
{
    return {p.normal.x, p.normal.y, p.normal.z, p.distance};
}

} // namespace

JoJaStructuralBrushIndex::JoJaStructuralBrushIndex(const RavenBsp& bsp) : impl_(std::make_unique<Impl>(bsp))
{
}
JoJaStructuralBrushIndex::~JoJaStructuralBrushIndex() = default;

std::vector<std::vector<TVector>> JoJaStructuralBrushIndex::crossSections(const RavenBspPlane& plane,
                                                                          std::span<const TVector> aperture,
                                                                          std::size_t maxFragments)
{
    return impl_->crossSections(plane, aperture, maxFragments);
}

std::vector<JoJaStructuralPlane> JoJaStructuralBrushIndex::planes(std::span<const JoJaHybridTriangle> sourceTriangles)
{
    const auto& bsp = impl_->source();
    const auto& world = bsp.models.at(0);
    const auto surfaces = range(bsp.surfaces, world.firstSurface, world.surfaceCount);
    std::map<PlaneKey, std::vector<std::size_t>> buckets;
    std::vector<JoJaStructuralPlane> result;
    std::size_t groupChecks = 0;
    for ( std::size_t i = 0; i < sourceTriangles.size(); ++i )
    {
        const auto& t = sourceTriangles[i].triangle;
        if ( t.surface < static_cast<std::size_t>(world.firstSurface) ||
             t.surface - world.firstSurface >= surfaces.size() )
        {
            continue;
        }
        const auto& s = bsp.surfaces[t.surface];
        if ( s.type != 1 || !structural(bsp.shaders.at(s.shader)) )
        {
            continue;
        }
        const auto& v = t.corners;
        auto normal = (v[1].position - v[0].position).cross(v[2].position - v[0].position);
        const double twiceArea = std::hypot(normal.x, normal.y, normal.z);
        if ( !std::isfinite(twiceArea) || twiceArea <= 0 )
        {
            continue;
        }
        normal = normal * (1.0 / twiceArea);
        if ( normal.dot(v[0].normal + v[1].normal + v[2].normal) < 0 )
        {
            normal = -normal;
        }
        if ( !impl_->supports(t, s.shader, normal) )
        {
            continue;
        }
        // Use the sign of the dominant component; the plane is unoriented.
        const std::array<double, 3> n{normal.x, normal.y, normal.z};
        if ( *std::max_element(n.begin(), n.end(),
                               [](double a, double b)
                               {
                                   return std::abs(a) < std::abs(b);
                               }) < 0 )
        {
            normal = -normal;
        }
        const RavenBspPlane p{normal, normal.dot(v[0].position)};
        // Buckets accelerate lookup only. Corners must lie on the chosen
        // representative draw plane, even when two brush planes nearly coincide.
        auto bucketKey = key(p);
        for ( auto& component : bucketKey )
        {
            component = std::round(component * 1e7);
        }
        auto& ids = buckets[bucketKey];
        const auto found = std::find_if(
            ids.begin(), ids.end(),
            [&](std::size_t id)
            {
                if ( ++groupChecks > 32u * 1024u * 1024u )
                {
                    throw std::runtime_error("JO/JA structural plane grouping exceeded its work limit");
                }
                const auto& rep = result[id].plane;
                return (rep.normal - p.normal).lengthSqr() <= 1e-20 &&
                       std::all_of(v.begin(), v.end(),
                                   [&](const auto& corner)
                                   {
                                       return std::abs(rep.normal.dot(corner.position) - rep.distance) <= 1e-8;
                                   });
            });
        if ( found == ids.end() )
        {
            ids.push_back(result.size());
            result.push_back({p, {i}, twiceArea * 0.5});
        }
        else
        {
            auto& group = result[*found];
            group.triangles.push_back(i);
            group.area += twiceArea * 0.5;
        }
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b)
              {
                  return a.area != b.area ? a.area > b.area : key(a.plane) < key(b.plane);
              });
    return result;
}
std::vector<JoJaStructuralPlane> findJoJaStructuralPlanes(const RavenBsp& bsp,
                                                          std::span<const JoJaHybridTriangle> sourceTriangles)
{
    return JoJaStructuralBrushIndex(bsp).planes(sourceTriangles);
}

} // namespace sed::assets
