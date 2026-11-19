#include "sed/assets/jo_ja_hybrid_validation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace sed::assets
{
namespace
{
using Ring = std::vector<RavenBspVertex>;
using PointKey = std::array<double, 3>;
constexpr std::size_t maxFaceCorners = 24;

[[noreturn]] void fail(const std::string& why)
{
    throw std::runtime_error("joJaHybrid topology: " + why);
}

PointKey key(const TVector& p)
{
    return {p.x, p.y, p.z};
}

TVector runtimePoint(const TVector& p)
{
    for ( const double x : key(p) )
    {
        if ( !std::isfinite(x) || std::abs(x) > std::numeric_limits<float>::max() )
        {
            fail("position exceeds finite retail float32");
        }
    }
    return {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)};
}

TVector areaVector(const Ring& r)
{
    TVector area{};
    for ( std::size_t i = 1; i + 1 < r.size(); ++i )
    {
        area = area + (r[i].position - r[0].position).Cross(r[i + 1].position - r[0].position);
    }
    return area.Scaled(0.5);
}

RavenBspVertex blend(const RavenBspVertex& a, const RavenBspVertex& b, double t)
{
    RavenBspVertex r;
    r.position = a.position + (b.position - a.position).Scaled(t);
    r.normal = a.normal + (b.normal - a.normal).Scaled(t);
    r.u = std::lerp(a.u, b.u, t);
    r.v = std::lerp(a.v, b.v, t);
    r.lightmapU = std::lerp(a.lightmapU, b.lightmapU, t);
    r.lightmapV = std::lerp(a.lightmapV, b.lightmapV, t);
    r.color = {
        static_cast<float>(std::lerp(a.color.r, b.color.r, t)), static_cast<float>(std::lerp(a.color.g, b.color.g, t)),
        static_cast<float>(std::lerp(a.color.b, b.color.b, t)), static_cast<float>(std::lerp(a.color.a, b.color.a, t))};
    return r;
}

struct Budget
{
    std::size_t work = 200000000;
    std::size_t output;
    std::size_t corners = 4000000;

    void charge(std::size_t n = 1)
    {
        if ( n > work )
        {
            work = 0;
            fail("edge incidence work budget exceeded");
        }
        work -= n;
    }
    void store(std::size_t n)
    {
        if ( n > corners )
        {
            fail("conformed corner budget exceeded");
        }
        corners -= n;
    }
};

struct Bounds
{
    PointKey low, high;
    explicit Bounds(const TVector& v) : low(key(v)), high(low)
    {
    }
    void add(const TVector& v)
    {
        const auto k = key(v);
        for ( int i = 0; i < 3; ++i )
        {
            low[i] = std::min(low[i], k[i]);
            high[i] = std::max(high[i], k[i]);
        }
    }
    bool touches(const PointKey& a, const PointKey& delta, double epsilon) const
    {
        double lo = 0, hi = 1;
        for ( int i = 0; i < 3; ++i )
        {
            if ( delta[i] == 0 )
            {
                if ( a[i] < low[i] - epsilon || a[i] > high[i] + epsilon )
                {
                    return false;
                }
                continue;
            }
            const double left = (low[i] - epsilon - a[i]) / delta[i];
            const double right = (high[i] + epsilon - a[i]) / delta[i];
            lo = std::max(lo, std::min(left, right));
            hi = std::min(hi, std::max(left, right));
            if ( lo > hi )
            {
                return false;
            }
        }
        return true;
    }
};

// One registry for mirror loops and every touching wall. Its tiny proximity
// radius handles clipping arithmetic. Exact float32 aliases always represent
// the same point to retail; preserve one authored double representative.
class Points
{
    using Bucket = std::array<std::int64_t, 3>;
    struct Node
    {
        Bounds box;
        std::size_t begin, end;
        int left = -1, right = -1;
    };
    std::map<Bucket, std::vector<std::size_t>> buckets;
    std::array<std::map<double, std::size_t>, 3> coordinates;
    std::vector<TVector> points;
    std::vector<std::size_t> order;
    std::vector<Node> nodes;
    double epsilon;
    Budget& budget;
    bool exact;

    int build(std::size_t begin, std::size_t end)
    {
        Bounds box(points[order[begin]]);
        for ( std::size_t i = begin + 1; i < end; ++i )
        {
            box.add(points[order[i]]);
        }
        budget.charge(end - begin);
        const auto index = static_cast<int>(nodes.size());
        nodes.push_back({box, begin, end});
        if ( end - begin > 12 )
        {
            int axis = 0;
            for ( int i = 1; i < 3; ++i )
            {
                if ( box.high[i] - box.low[i] > box.high[axis] - box.low[axis] )
                {
                    axis = i;
                }
            }
            const auto middle = begin + (end - begin) / 2;
            std::nth_element(order.begin() + begin, order.begin() + middle, order.begin() + end,
                             [&](auto a, auto b)
                             {
                                 return key(points[a])[axis] < key(points[b])[axis];
                             });
            const auto left = build(begin, middle);
            const auto right = build(middle, end);
            nodes[index].left = left;
            nodes[index].right = right;
        }
        return index;
    }

  public:
    Points(double e, Budget& b, bool exactIncidence = false) : epsilon(e), budget(b), exact(exactIncidence)
    {
    }
    void preserveZero(std::size_t axis)
    {
        coordinates[axis].emplace(0, 0);
    }

    TVector include(const TVector& source)
    {
        // Two independent clipping paths can leave different arithmetic
        // residues on the same constant-coordinate plane, especially at zero.
        // Their other coordinates can be far apart until encoded as float32.
        // Bound each change to the same double-arithmetic tolerance used by
        // the point registry. First-seen representatives prevent chain drift.
        auto k = key(source);
        if ( !exact )
        {
            for ( std::size_t axis = 0; axis < k.size(); ++axis )
            {
                auto& value = k[axis];
                auto& anchors = coordinates[axis];
                auto next = anchors.lower_bound(value);
                auto chosen = anchors.end();
                if ( next != anchors.end() && next->first - value <= epsilon )
                {
                    chosen = next;
                }
                if ( next != anchors.begin() )
                {
                    auto prev = std::prev(next);
                    if ( value - prev->first <= epsilon && (chosen == anchors.end() || prev->second < chosen->second) )
                    {
                        chosen = prev;
                    }
                }
                if ( chosen != anchors.end() )
                {
                    value = chosen->first;
                }
                else
                {
                    anchors.emplace_hint(next, value, anchors.size());
                }
            }
        }
        const TVector point{k[0], k[1], k[2]};
        Bucket grid;
        for ( int i = 0; i < 3; ++i )
        {
            grid[i] = static_cast<std::int64_t>(std::floor(k[i] / epsilon));
        }
        std::size_t found = points.size();
        for ( int x = -1; x <= 1; ++x )
        {
            for ( int y = -1; y <= 1; ++y )
            {
                for ( int z = -1; z <= 1; ++z )
                {
                    const auto bucket = buckets.find({grid[0] + x, grid[1] + y, grid[2] + z});
                    if ( bucket == buckets.end() )
                    {
                        continue;
                    }
                    for ( const auto i : bucket->second )
                    {
                        budget.charge();
                        if ( exact ? key(point) == key(points[i]) : point.Distance(points[i]) <= epsilon )
                        {
                            found = std::min(found, i);
                        }
                    }
                }
            }
        }
        if ( found < points.size() )
        {
            return points[found];
        }
        buckets[grid].push_back(points.size());
        points.push_back(point);
        return point;
    }

    void prepare()
    {
        if ( points.empty() )
        {
            fail("empty shell vertex registry");
        }
        order.resize(points.size());
        std::iota(order.begin(), order.end(), 0);
        build(0, order.size());
        buckets.clear();
        for ( auto& axis : coordinates )
        {
            axis.clear();
        }
    }

    void appendEdge(Ring& out, const RavenBspVertex& a, const RavenBspVertex& b)
    {
        // The incidence predicate must take the same arithmetic path on both
        // sides of a shared edge, including at the end-point error bound.
        const bool reversed = key(b.position) < key(a.position);
        const auto& first = reversed ? b.position : a.position;
        const auto& last = reversed ? a.position : b.position;
        const auto delta = last - first;
        const double len = delta.Length();
        if ( len <= (exact ? 0 : epsilon) )
        {
            fail("collapsed canonical shell edge");
        }
        const auto direction = delta.Scaled(1 / len);
        const auto start = key(first);
        const auto d = key(delta);
        std::vector<std::pair<double, std::size_t>> interior;
        std::vector<int> pending{0};
        while ( !pending.empty() )
        {
            const auto& n = nodes[pending.back()];
            pending.pop_back();
            budget.charge();
            if ( !n.box.touches(start, d, epsilon) )
            {
                continue;
            }
            if ( n.left >= 0 )
            {
                pending.push_back(n.left);
                pending.push_back(n.right);
                continue;
            }
            budget.charge(n.end - n.begin);
            for ( auto it = n.begin; it < n.end; ++it )
            {
                const auto i = order[it];
                const auto offset = points[i] - first;
                const double along = offset.Dot(direction);
                if ( key(points[i]) == key(a.position) || key(points[i]) == key(b.position) )
                {
                    continue;
                }
                const double margin = exact ? 0 : epsilon;
                const bool onLine =
                    exact ? key(offset.Cross(delta)) == PointKey{} : offset.Cross(direction).Length() <= epsilon;
                if ( along > margin && along < len - margin && onLine )
                {
                    interior.emplace_back(along / len, i);
                }
            }
        }
        std::sort(interior.begin(), interior.end());
        if ( reversed )
        {
            std::reverse(interior.begin(), interior.end());
        }
        budget.store(interior.size() + 1);
        out.push_back(a);
        for ( const auto& [t, i] : interior )
        {
            auto corner = blend(a, b, reversed ? 1 - t : t);
            corner.position = points[i];
            out.push_back(corner);
        }
    }
};

RavenBspVertex meanCorner(const Ring& ring)
{
    auto out = ring.front();
    for ( std::size_t i = 1; i < ring.size(); ++i )
    {
        out = blend(out, ring[i], 1.0 / (i + 1));
    }
    return out;
}

double turn(const TVector& a, const TVector& b, const TVector& c, const TVector& n)
{
    return (b - a).Cross(c - a).Dot(n);
}

bool positiveTriangle(const TVector& a, const TVector& b, const TVector& c, const TVector& n)
{
    // A nearly collinear diagonal can get a different sign if the mirrored
    // loop starts from another corner. A chosen triangulation must work from
    // every cyclic origin in the same arithmetic used by the face certificate.
    return turn(a, b, c, n) > 0 && turn(b, c, a, n) > 0 && turn(c, a, b, n) > 0;
}

std::vector<Ring> kernelFan(const Ring& ring, const TVector& normal, Budget& budget)
{
    // A tiny reflex bend introduced by float32 may forbid every vertex-to-
    // vertex diagonal. A point in both polygon kernels still provides one
    // positive triangle per original edge, without moving a boundary corner.
    const auto origin = ring[0].position;
    const auto u = normal.Cross(std::abs(normal.z) < 0.8 ? TVector{0, 0, 1} : TVector{1, 0, 0}).Normalized();
    const auto v = normal.Cross(u);
    double extent = 0;
    for ( const auto& p : ring )
    {
        extent = std::max(extent, p.position.Distance(origin));
    }
    extent *= 2;
    std::vector<TVector> kernel{origin + (u + v).Scaled(extent), origin + (v - u).Scaled(extent),
                                origin - (u + v).Scaled(extent), origin + (u - v).Scaled(extent)};
    for ( const bool runtime : {false, true} )
    {
        for ( std::size_t i = 0; i < ring.size() && !kernel.empty(); ++i )
        {
            auto a = ring[i].position, b = ring[(i + 1) % ring.size()].position;
            if ( runtime )
            {
                a = runtimePoint(a);
                b = runtimePoint(b);
            }
            std::vector<TVector> clipped;
            budget.charge(kernel.size());
            for ( std::size_t j = 0; j < kernel.size(); ++j )
            {
                const auto& p = kernel[j];
                const auto& q = kernel[(j + 1) % kernel.size()];
                const double d = turn(a, b, p, normal), e = turn(a, b, q, normal);
                if ( d >= 0 )
                {
                    clipped.push_back(p);
                }
                if ( (d < 0 && e > 0) || (d > 0 && e < 0) )
                {
                    clipped.push_back(p + (q - p).Scaled(d / (d - e)));
                }
            }
            kernel = std::move(clipped);
        }
    }
    if ( kernel.size() < 3 )
    {
        return {};
    }
    TVector p{};
    for ( const auto& vtx : kernel )
    {
        p = p + vtx.Scaled(1.0 / kernel.size());
    }
    const auto wire = runtimePoint(p);
    for ( std::size_t i = 0; i < ring.size(); ++i )
    {
        auto a = ring[i].position, b = ring[(i + 1) % ring.size()].position;
        if ( !positiveTriangle(a, b, p, normal) || !positiveTriangle(runtimePoint(a), runtimePoint(b), wire, normal) )
        {
            return {};
        }
    }
    // Interpolate native UVs and light on the original clipped source plane.
    for ( std::size_t i = 1; i + 1 < ring.size(); ++i )
    {
        const auto& a = ring[0];
        const auto& b = ring[i];
        const auto& c = ring[i + 1];
        const double sum = turn(a.position, b.position, c.position, normal);
        if ( sum <= 0 )
        {
            continue;
        }
        const double wb = turn(c.position, a.position, p, normal) / sum;
        const double wc = turn(a.position, b.position, p, normal) / sum;
        if ( wb < 0 || wc < 0 || wb + wc > 1.00000000001 || !(wb + wc > 0) )
        {
            continue;
        }
        auto center = blend(a, blend(b, c, wc / (wb + wc)), wb + wc);
        center.position = p;
        std::vector<Ring> pieces;
        for ( std::size_t j = 0; j < ring.size(); ++j )
        {
            pieces.push_back({center, ring[j], ring[(j + 1) % ring.size()]});
        }
        return pieces;
    }
    return {};
}

std::vector<Ring> triangulatePositive(const Ring& ring, const TVector& normal, Budget& budget)
{
    // Find a common triangulation for both embeddings. A greedy ear can leave
    // a final collinear triangle even when a different diagonal would work.
    const std::size_t count = ring.size();
    if ( count > 512 )
    {
        fail("nonconvex target face exceeds bounded triangulation work");
    }
    std::vector<TVector> target;
    for ( const auto& v : ring )
    {
        target.push_back(runtimePoint(v.position));
    }
    std::vector<std::int32_t> split(count * count, -1);
    const auto at = [count, &split](std::size_t a, std::size_t b) -> std::int32_t&
    {
        return split[a * count + b];
    };
    for ( std::size_t i = 0; i + 1 < count; ++i )
    {
        at(i, i + 1) = 0;
    }
    for ( std::size_t width = 2; width < count; ++width )
    {
        for ( std::size_t i = 0; i + width < count; ++i )
        {
            const auto j = i + width;
            for ( auto k = i + 1; k < j; ++k )
            {
                budget.charge();
                if ( at(i, k) >= 0 && at(k, j) >= 0 &&
                     positiveTriangle(ring[i].position, ring[k].position, ring[j].position, normal) &&
                     positiveTriangle(target[i], target[k], target[j], normal) )
                {
                    at(i, j) = static_cast<std::int32_t>(k);
                    break;
                }
            }
        }
    }
    if ( at(0, count - 1) < 0 )
    {
        auto fan = kernelFan(ring, normal, budget);
        if ( fan.empty() )
        {
            fail("face has no common positive-area authored/retail triangulation");
        }
        return fan;
    }
    std::vector<std::pair<std::size_t, std::size_t>> pending{{0, count - 1}};
    std::vector<Ring> output;
    while ( !pending.empty() )
    {
        const auto [a, b] = pending.back();
        pending.pop_back();
        if ( b == a + 1 )
        {
            continue;
        }
        const auto k = static_cast<std::size_t>(at(a, b));
        output.push_back({ring[a], ring[k], ring[b]});
        pending.emplace_back(a, k);
        pending.emplace_back(k, b);
    }
    return output;
}

bool hasReflexCorner(const Ring& r, const TVector& normal)
{
    for ( std::size_t i = 0; i < r.size(); ++i )
    {
        const auto& a = r[i].position;
        const auto& b = r[(i + 1) % r.size()].position;
        const auto& c = r[(i + 2) % r.size()].position;
        if ( turn(a, b, c, normal) < 0 || turn(runtimePoint(a), runtimePoint(b), runtimePoint(c), normal) < 0 )
        {
            return true;
        }
    }
    return false;
}

// A convex polygon tiled by center-to-edge triangles. Group consecutive
// triangles while the wedge stays convex and fits the retail corner limit.
// This preserves every collinear donor on the boundary of a long face.
std::vector<Ring> boundedSimple(const Ring& r, const TVector& normal, Budget& budget)
{
    if ( r.size() >= 4 && hasReflexCorner(r, normal) )
    {
        return triangulatePositive(r, normal, budget);
    }
    if ( r.size() <= maxFaceCorners )
    {
        return {r};
    }
    const auto center = meanCorner(r);
    std::vector<Ring> pieces;
    std::size_t begin = 0;
    while ( begin < r.size() )
    {
        std::size_t end = begin + 1;
        const auto first = r[begin].position - center.position;
        while ( end < r.size() && end - begin < maxFaceCorners - 2 )
        {
            const auto next = r[(end + 1) % r.size()].position - center.position;
            if ( first.Cross(next).Dot(normal) <= 0 )
            {
                break;
            }
            ++end;
        }
        Ring piece{center};
        for ( std::size_t i = begin; i <= end; ++i )
        {
            piece.push_back(r[i % r.size()]);
        }
        pieces.push_back(std::move(piece));
        begin = end;
    }
    return pieces;
}

// At retail precision a thin neck can contract to one exact vertex. Split
// the oriented walk there: both cycles must independently pass the same
// positive-area and winding checks, with every original edge retained.
std::vector<Ring> bounded(const Ring& ring, const TVector& normal, Budget& budget)
{
    if ( ring.size() < 6 )
    {
        return boundedSimple(ring, normal, budget);
    }
    std::vector<Ring> pending{ring}, pieces;
    while ( !pending.empty() )
    {
        Ring loop = std::move(pending.back());
        pending.pop_back();
        bool separated = false;
        std::map<PointKey, std::size_t> seen;
        for ( std::size_t j = 0; j < loop.size(); ++j )
        {
            budget.charge();
            const auto [it, inserted] = seen.emplace(key(loop[j].position), j);
            if ( inserted )
            {
                continue;
            }
            const auto i = it->second;
            if ( j - i < 3 || loop.size() - (j - i) < 3 )
            {
                fail("pinched face has an empty cycle");
            }
            pending.emplace_back(loop.begin() + i, loop.begin() + j);
            loop.erase(loop.begin() + i, loop.begin() + j);
            pending.push_back(std::move(loop));
            separated = true;
            break;
        }
        if ( separated )
        {
            continue;
        }
        auto part = boundedSimple(loop, normal, budget);
        pieces.insert(pieces.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
    }
    return pieces;
}

using DirectedEdge = std::pair<PointKey, PointKey>;

using FaceLists = std::vector<std::vector<JoJaHybridFace>>;

TVector fromKey(const PointKey& p)
{
    return {p[0], p[1], p[2]};
}

// Only unmatched, opposite directed edges in this particular closed-shell
// graph may imply an extra split. Require a unique monotone reverse chain
// from B to A, and require every intermediate donor and its projection onto
// AB to encode to the exact same retail position.
void completeReverseChains(FaceLists& cells, std::vector<std::vector<double>>& displacement,
                           std::vector<double>& cellDisplacement, Budget& budget)
{
    std::map<DirectedEdge, std::vector<PointKey>> split;
    for ( const auto& faces : cells )
    {
        std::map<DirectedEdge, std::size_t> counts;
        for ( const auto& f : faces )
        {
            for ( std::size_t i = 0; i < f.corners.size(); ++i )
            {
                ++counts[{key(f.corners[i].position), key(f.corners[(i + 1) % f.corners.size()].position)}];
            }
        }
        std::map<PointKey, std::vector<PointKey>> outgoing;
        for ( const auto& [e, count] : counts )
        {
            if ( count == 1 && !counts.contains({e.second, e.first}) )
            {
                outgoing[e.first].push_back(e.second);
            }
        }
        for ( const auto& [start, ends] : outgoing )
        {
            for ( const auto& end : ends )
            {
                const auto origin = fromKey(end);
                const auto direction = fromKey(start) - origin;
                const double lengthSquared = direction.Dot(direction);
                auto current = end;
                double previous = 0;
                std::vector<PointKey> chain;
                while ( current != start )
                {
                    auto it = outgoing.find(current);
                    if ( it == outgoing.end() )
                    {
                        break;
                    }
                    const PointKey* choice = nullptr;
                    double fraction = 0;
                    for ( const auto& next : it->second )
                    {
                        budget.charge();
                        const double t = next == start ? 1 : (fromKey(next) - origin).Dot(direction) / lengthSquared;
                        if ( !(previous < t && t <= 1) )
                        {
                            continue;
                        }
                        if ( next != start && (t == 1 || key(runtimePoint(origin + direction.Scaled(t))) !=
                                                             key(runtimePoint(fromKey(next)))) )
                        {
                            continue;
                        }
                        if ( choice )
                        {
                            choice = nullptr;
                            break;
                        }
                        choice = &next;
                        fraction = t;
                    }
                    if ( !choice )
                    {
                        break;
                    }
                    current = *choice;
                    previous = fraction;
                    if ( current != start )
                    {
                        chain.push_back(current);
                    }
                }
                if ( current != start || chain.empty() )
                {
                    continue;
                }
                if ( start < end )
                {
                    std::reverse(chain.begin(), chain.end());
                }
                const auto ordered = std::minmax(start, end);
                const auto [found, inserted] = split.emplace(DirectedEdge{ordered.first, ordered.second}, chain);
                if ( !inserted && found->second != chain )
                {
                    fail("sectors require inconsistent target edge chains");
                }
            }
        }
    }
    if ( split.empty() )
    {
        return;
    }
    for ( std::size_t s = 0; s < cells.size(); ++s )
    {
        for ( std::size_t f = 0; f < cells[s].size(); ++f )
        {
            auto& ring = cells[s][f].corners;
            Ring expanded;
            for ( std::size_t i = 0; i < ring.size(); ++i )
            {
                const auto& a = ring[i];
                const auto& b = ring[(i + 1) % ring.size()];
                const auto start = key(a.position), end = key(b.position);
                const auto ordered = std::minmax(start, end);
                const auto it = split.find({ordered.first, ordered.second});
                expanded.push_back(a);
                if ( it == split.end() )
                {
                    continue;
                }
                const auto d = b.position - a.position;
                budget.store(it->second.size());
                for ( std::size_t j = 0; j < it->second.size(); ++j )
                {
                    const auto donor = fromKey(it->second[start < end ? j : it->second.size() - 1 - j]);
                    const double t = (donor - a.position).Dot(d) / d.Dot(d);
                    auto v = blend(a, b, t);
                    const auto shift = v.position.Distance(donor);
                    displacement[s][f] = std::max(displacement[s][f], shift);
                    cellDisplacement[s] = std::max(cellDisplacement[s], shift);
                    v.position = donor;
                    expanded.push_back(v);
                }
            }
            ring = std::move(expanded);
        }
    }
}

Ring joinedBoundary(const Ring& a, const Ring& b, Budget& budget)
{
    std::map<DirectedEdge, RavenBspVertex> edges;
    bool shared = false;
    for ( const auto* ring : {&a, &b} )
    {
        for ( std::size_t i = 0; i < ring->size(); ++i )
        {
            budget.charge();
            const auto& p = (*ring)[i];
            const auto q = key((*ring)[(i + 1) % ring->size()].position);
            const auto k = key(p.position);
            if ( edges.erase({q, k}) )
            {
                shared = true;
            }
            else if ( !edges.emplace(DirectedEdge{k, q}, p).second )
            {
                return {};
            }
        }
    }
    if ( !shared || edges.size() < 3 )
    {
        return {};
    }
    std::map<PointKey, std::pair<PointKey, RavenBspVertex>> outgoing;
    for ( const auto& [edge, vertex] : edges )
    {
        if ( !outgoing.emplace(edge.first, std::pair{edge.second, vertex}).second )
        {
            return {};
        }
    }
    Ring ring;
    const auto start = outgoing.begin()->first;
    auto next = start;
    do
    {
        const auto it = outgoing.find(next);
        if ( it == outgoing.end() )
        {
            return {};
        }
        ring.push_back(it->second.second);
        next = it->second.first;
        outgoing.erase(it);
    } while ( next != start );
    return outgoing.empty() ? ring : Ring{};
}

bool canDecompose(const Ring& ring, const TVector& normal, Budget& budget)
{
    if ( ring.size() < 3 )
    {
        return false;
    }
    try
    {
        for ( const auto& part : bounded(ring, normal, budget) )
        {
            if ( !(areaVector(part).Dot(normal) > 0) )
            {
                return false;
            }
            auto target = part;
            for ( auto& p : target )
            {
                p.position = runtimePoint(p.position);
            }
            if ( !(areaVector(target).Dot(normal) > 0) )
            {
                return false;
            }
        }
    }
    catch ( const std::runtime_error& )
    {
        if ( !budget.work )
        {
            throw;
        }
        return false;
    }
    return true;
}
bool oppositeLoop(const Ring& a, const Ring& b)
{
    if ( a.empty() || a.size() != b.size() )
    {
        return false;
    }
    for ( std::size_t start = 0; start < b.size(); ++start )
    {
        if ( key(a.front().position) != key(b[start].position) )
        {
            continue;
        }
        bool opposite = true;
        for ( std::size_t i = 1; i < a.size() && opposite; ++i )
        {
            opposite = key(a[i].position) == key(b[(start + b.size() - i) % b.size()].position);
        }
        if ( opposite )
        {
            return true;
        }
    }
    return false;
}

void checkPair(const Ring& a, const Ring& b)
{
    if ( a.size() != b.size() )
    {
        fail("mirror aperture corner counts differ after conformance");
    }
    const auto it = std::find_if(b.begin(), b.end(),
                                 [&](const auto& p)
                                 {
                                     return key(p.position) == key(a[0].position);
                                 });
    if ( it == b.end() )
    {
        fail("mirror aperture loops differ");
    }
    const auto offset = static_cast<std::size_t>(it - b.begin());
    for ( std::size_t i = 0; i < a.size(); ++i )
    {
        if ( key(a[i].position) != key(b[(offset + b.size() - i) % b.size()].position) )
        {
            fail("mirror aperture vertices do not reverse exactly");
        }
    }
}

void areaPreserved(double before, double after, double perimeter, double epsilon)
{
    if ( !(before > 0) || !(after > 0) || !std::isfinite(before) || !std::isfinite(after) ||
         std::abs(before - after) > 8 * perimeter * epsilon + 1e-10 * before )
    {
        std::ostringstream detail;
        detail.precision(16);
        detail << "native face coverage changed during conformance from " << before << " to " << after << ", perimeter "
               << perimeter << ", arithmetic displacement " << epsilon;
        fail(detail.str());
    }
}

double perimeter(const Ring& r)
{
    double value = 0;
    for ( std::size_t i = 0; i < r.size(); ++i )
    {
        value += r[i].position.Distance(r[(i + 1) % r.size()].position);
    }
    return value;
}

void certify(const JoJaHybridCell& cell, const std::vector<JoJaHybridFace>& faces, double epsilon, bool runtime,
             Budget& budget)
{
    std::map<DirectedEdge, std::size_t> owners;
    double sixVolume = 0;
    const auto center = runtime ? runtimePoint(cell.interior) : cell.interior;
    for ( const auto& f : faces )
    {
        if ( f.corners.size() < 3 || f.corners.size() > maxFaceCorners )
        {
            fail("invalid native face corner count");
        }
        const TVector n = runtime ? runtimePoint(f.normal) : f.normal;
        if ( std::abs(n.Length() - 1) > 1e-5 )
        {
            fail("invalid native face normal");
        }
        Ring r = f.corners;
        double tol = epsilon;
        if ( runtime )
        {
            for ( auto& v : r )
            {
                const auto p = runtimePoint(v.position);
                tol = std::max(tol, 4 * p.Distance(v.position));
                v.position = p;
            }
        }
        const auto twiceArea = areaVector(r).Scaled(2);
        const auto& origin = r.front().position;
        if ( !(twiceArea.Dot(n) > 0) )
        {
            std::ostringstream details;
            details.precision(16);
            details << (runtime ? "float32" : "authored") << " face " << (&f - faces.data()) << " (source "
                    << f.sourceSurface << ", neighbor " << f.neighbor << ", material "
                    << (f.material ? std::to_string(*f.material) : "none") << ") has signed area "
                    << twiceArea.Dot(n) * .5;
            fail(details.str());
        }
        if ( !((center - origin).Dot(n) > 0) )
        {
            fail("sector interior is outside a directed face");
        }
        sixVolume += twiceArea.Dot(center - origin);
        for ( std::size_t i = 0; i < r.size(); ++i )
        {
            const auto& a = r[i].position;
            const auto& b = r[(i + 1) % r.size()].position;
            const auto edge = b - a;
            const double edgeLength = edge.Length();
            const double deviation = std::abs((a - origin).Dot(n));
            if ( !(edgeLength > 0) || deviation > tol )
            {
                std::ostringstream details;
                details << (runtime ? "float32" : "authored") << " face " << (&f - faces.data()) << " edge " << i
                        << " length " << edgeLength << " plane deviation " << deviation << " tolerance " << tol
                        << " corner " << a.x << ',' << a.y << ',' << a.z;
                fail(details.str());
            }
            const auto index = static_cast<std::size_t>(&f - faces.data());
            const auto [owner, inserted] = owners.emplace(DirectedEdge{key(a), key(b)}, index);
            if ( !inserted )
            {
                std::ostringstream details;
                details.precision(16);
                details << (runtime ? "float32" : "authored") << " directed shell edge has multiple owners "
                        << owner->second << " and " << index << " (source " << f.sourceSurface << ", neighbor "
                        << f.neighbor << ") from " << a.x << ',' << a.y << ',' << a.z << " to " << b.x << ',' << b.y
                        << ',' << b.z;
                fail(details.str());
            }
            budget.charge(r.size() + cell.planes.size());
            for ( const auto& p : r )
            {
                if ( edge.Cross(p.position - a).Dot(n) < -tol * edgeLength )
                {
                    fail("native face is concave at target precision");
                }
            }
            for ( const auto& plane : cell.planes )
            {
                if ( plane.normal.Dot(a) - plane.distance > tol )
                {
                    fail("shell vertex is outside its owning convex volume");
                }
            }
        }
    }
    if ( !std::isfinite(sixVolume) || !(sixVolume > 0) )
    {
        fail("shell has no positive volume");
    }
    for ( const auto& [edge, owner] : owners )
    {
        if ( owners.contains({edge.second, edge.first}) )
        {
            continue;
        }
        const auto& f = faces[owner];
        std::ostringstream details;
        details.precision(16);
        details << (runtime ? "float32" : "authored") << " face " << owner << " (source " << f.sourceSurface
                << ", neighbor " << f.neighbor << ") has an unmatched edge from " << edge.first[0] << ','
                << edge.first[1] << ',' << edge.first[2] << " to " << edge.second[0] << ',' << edge.second[1] << ','
                << edge.second[2];
        fail(details.str());
    }
}

} // namespace

void finalizeJoJaHybridGeometry(JoJaHybridGeometry& geometry, std::size_t maxFragments)
{
    if ( maxFragments == 0 || maxFragments > 1000000 || geometry.cells.empty() || geometry.cells.size() > 96 ||
         geometry.residual.size() > maxFragments )
    {
        fail("invalid partition or fragment budget");
    }
    Budget budget{200000000, maxFragments - geometry.residual.size()};
    double magnitude = 1;
    std::array<bool, 3> hasExactZero{};
    for ( const auto& cell : geometry.cells )
    {
        if ( cell.planes.size() < 4 || cell.planes.size() > 256 )
        {
            fail("invalid convex half-space count");
        }
        for ( const auto& f : cell.faces )
        {
            if ( f.corners.size() < 3 )
            {
                fail("native face has fewer than three corners");
            }
            budget.store(f.corners.size());
            for ( const auto& v : f.corners )
            {
                runtimePoint(v.position);
                const auto values = key(v.position);
                for ( std::size_t axis = 0; axis < values.size(); ++axis )
                {
                    magnitude = std::max(magnitude, std::abs(values[axis]));
                    hasExactZero[axis] |= values[axis] == 0;
                }
            }
        }
    }
    const double epsilon = magnitude * 4096 * std::numeric_limits<double>::epsilon();
    Points points(epsilon, budget);
    for ( std::size_t axis = 0; axis < hasExactZero.size(); ++axis )
    {
        if ( hasExactZero[axis] )
        {
            points.preserveZero(axis);
        }
    }
    std::vector<std::vector<JoJaHybridFace>> conformed;
    std::vector<std::vector<double>> displacement;
    std::vector<double> cellDisplacement;
    for ( const auto& cell : geometry.cells )
    {
        auto& copies = conformed.emplace_back(cell.faces);
        auto& shifts = displacement.emplace_back();
        for ( auto& f : copies )
        {
            double movement = 0;
            for ( auto& corner : f.corners )
            {
                const auto p = points.include(corner.position);
                movement = std::max(movement, p.Distance(corner.position));
                corner.position = p;
            }
            shifts.push_back(movement);
        }
    }
    points.prepare();
    for ( auto& cell : conformed )
    {
        for ( auto& f : cell )
        {
            Ring r;
            for ( std::size_t i = 0; i < f.corners.size(); ++i )
            {
                if ( key(f.corners[i].position) != key(f.corners[(i + 1) % f.corners.size()].position) )
                {
                    points.appendEdge(r, f.corners[i], f.corners[(i + 1) % f.corners.size()]);
                }
            }
            f.corners = std::move(r);
        }
    }

    // Establish incidence before target contraction: moving a clipping point
    // by one representable unit first could otherwise conceal its exact T-junction.
    // A clipped point can coincide with an exact source coordinate at retail
    // precision. Retain the authored representative nearest that encoded point,
    // independently of which sector or mask tile happened to be visited first.
    std::map<PointKey, TVector> representatives;
    for ( const auto& cell : conformed )
    {
        for ( const auto& f : cell )
        {
            for ( const auto& corner : f.corners )
            {
                const auto wire = runtimePoint(corner.position);
                const auto [it, inserted] = representatives.try_emplace(key(wire), corner.position);
                if ( inserted )
                {
                    continue;
                }
                const double oldDistance = wire.Distance(it->second);
                const double newDistance = wire.Distance(corner.position);
                if ( newDistance < oldDistance ||
                     (newDistance == oldDistance && key(corner.position) < key(it->second)) )
                {
                    it->second = corner.position;
                }
            }
        }
    }
    std::size_t collapsed = 0;
    double collapsedArea = 0;
    for ( std::size_t s = 0; s < conformed.size(); ++s )
    {
        double cellShift = 0;
        for ( std::size_t i = 0; i < conformed[s].size(); ++i )
        {
            auto& f = conformed[s][i];
            auto& shift = displacement[s][i];
            Ring r;
            for ( auto& corner : f.corners )
            {
                const auto k = key(runtimePoint(corner.position));
                const auto p = representatives.at(k);
                shift = std::max(shift, p.Distance(corner.position));
                corner.position = p;
                if ( r.empty() || key(r.back().position) != key(p) )
                {
                    r.push_back(corner);
                }
            }
            if ( r.size() > 1 && key(r.front().position) == key(r.back().position) )
            {
                r.pop_back();
            }
            f.corners = std::move(r);
            cellShift = std::max(cellShift, shift);
        }
        cellDisplacement.push_back(cellShift);
    }

    // Independent float rounding can turn two previously angled edges into
    // an exact out-and-back segment. Conform in the encoded space as well,
    // then cancel only opposite traversals of the identical segment.
    Points encoded(epsilon, budget, true);
    for ( const auto& [k, position] : representatives )
    {
        encoded.include(runtimePoint(position));
    }
    encoded.prepare();
    for ( std::size_t s = 0; s < conformed.size(); ++s )
    {
        for ( std::size_t i = 0; i < conformed[s].size(); ++i )
        {
            auto& f = conformed[s][i];
            Ring r;
            for ( std::size_t j = 0; j < f.corners.size(); ++j )
            {
                auto a = f.corners[j];
                auto b = f.corners[(j + 1) % f.corners.size()];
                a.position = runtimePoint(a.position);
                b.position = runtimePoint(b.position);
                if ( key(a.position) != key(b.position) )
                {
                    encoded.appendEdge(r, a, b);
                }
            }
            bool encodedChange = f.corners.size() != r.size();
            // A circular A-B-A spike contributes exactly zero signed area.
            bool changed = true;
            while ( changed && r.size() >= 3 )
            {
                changed = false;
                for ( std::size_t j = 0; j < r.size(); ++j )
                {
                    budget.charge();
                    if ( key(r[(j + r.size() - 1) % r.size()].position) != key(r[(j + 1) % r.size()].position) )
                    {
                        continue;
                    }
                    std::rotate(r.begin(), r.begin() + static_cast<std::ptrdiff_t>((j + r.size() - 1) % r.size()),
                                r.end());
                    r.erase(r.begin() + 1, r.begin() + 3);
                    encodedChange = true;
                    changed = true;
                    break;
                }
            }
            for ( auto& corner : r )
            {
                corner.position = representatives.at(key(corner.position));
            }
            if ( encodedChange )
            {
                // New encoded incidences are exact on the retail line. The
                // retained source-double representatives may differ from it by
                // their measured coordinate rounding on this particular face.
                auto& shift = displacement[s][i];
                for ( const auto* loop : {&f.corners, &r} )
                {
                    for ( const auto& v : *loop )
                    {
                        shift = std::max(shift, v.position.Distance(runtimePoint(v.position)));
                    }
                }
                cellDisplacement[s] = std::max(cellDisplacement[s], shift);
            }
            bool flat = true;
            if ( r.size() >= 3 )
            {
                const auto a = runtimePoint(r.front().position);
                const auto edge = runtimePoint(r[1].position) - a;
                for ( std::size_t j = 2; j < r.size(); ++j )
                {
                    if ( edge.Cross(runtimePoint(r[j].position) - a).Length() > 0 )
                    {
                        flat = false;
                        break;
                    }
                }
            }
            if ( flat )
            {
                if ( f.neighbor < 0 || s < static_cast<std::size_t>(f.neighbor) )
                {
                    ++collapsed;
                    collapsedArea += areaVector(geometry.cells[s].faces[i].corners).Length();
                }
                r.clear();
            }
            f.corners = std::move(r);
        }
    }

    completeReverseChains(conformed, displacement, cellDisplacement, budget);

    // An internal line used to tile a blank wall can reverse at retail
    // precision even though the wall perimeter still has positive area.
    // Reunite only equal-function tiles sharing exact opposite edges. Each
    // original perimeter edge survives unless it cancels inside that union.
    std::vector<std::vector<double>> initialArea(conformed.size()), initialPerimeter(conformed.size());
    std::vector<std::vector<bool>> merged(conformed.size());
    for ( std::size_t s = 0; s < conformed.size(); ++s )
    {
        merged[s].resize(conformed[s].size());
        for ( const auto& f : geometry.cells[s].faces )
        {
            initialArea[s].push_back(areaVector(f.corners).Length());
            initialPerimeter[s].push_back(perimeter(f.corners));
        }
    }
    for ( std::size_t s = 0; s < conformed.size(); ++s )
    {
        for ( std::size_t i = 0; i < conformed[s].size(); ++i )
        {
            auto& f = conformed[s][i];
            if ( merged[s][i] || f.corners.empty() || f.material ||
                 (f.neighbor >= 0 && f.neighbor < static_cast<int>(s)) || canDecompose(f.corners, f.normal, budget) )
            {
                continue;
            }
            std::set<DirectedEdge> reversed;
            for ( std::size_t c = 0; c < f.corners.size(); ++c )
            {
                reversed.emplace(key(f.corners[(c + 1) % f.corners.size()].position), key(f.corners[c].position));
            }
            std::vector<std::pair<double, std::size_t>> adjacent;
            for ( std::size_t j = 0; j < conformed[s].size(); ++j )
            {
                const auto& candidate = conformed[s][j];
                if ( i == j || merged[s][j] || candidate.material || candidate.neighbor != f.neighbor ||
                     f.normal.Dot(candidate.normal) < 1 - 1e-12 )
                {
                    continue;
                }
                double common = 0;
                for ( std::size_t c = 0; c < candidate.corners.size(); ++c )
                {
                    budget.charge();
                    const auto p = candidate.corners[c].position;
                    const auto q = candidate.corners[(c + 1) % candidate.corners.size()].position;
                    if ( reversed.contains({key(p), key(q)}) )
                    {
                        common += p.Distance(q);
                    }
                }
                if ( common > 0 )
                {
                    adjacent.emplace_back(-common, j);
                }
            }
            std::sort(adjacent.begin(), adjacent.end());
            for ( const auto& [score, j] : adjacent )
            {
                (void)score;
                auto& g = conformed[s][j];
                // Two positive blank mask tiles can contract to the same
                // three or more corners, wound oppositely. The entire union
                // then has exactly zero directed boundary at retail precision.
                if ( f.neighbor < 0 && oppositeLoop(f.corners, g.corners) )
                {
                    const auto& a = geometry.cells[s].faces[i];
                    const auto& b = geometry.cells[s].faces[j];
                    const double allowed = 8 * (initialPerimeter[s][i] * (displacement[s][i] + epsilon) +
                                                initialPerimeter[s][j] * (displacement[s][j] + epsilon));
                    const double area = initialArea[s][i] + initialArea[s][j];
                    if ( areaVector(a.corners).Dot(f.normal) > 0 && areaVector(b.corners).Dot(g.normal) > 0 &&
                         area <= allowed )
                    {
                        merged[s][i] = merged[s][j] = true;
                        collapsed += 2;
                        collapsedArea += area;
                        break;
                    }
                }
                auto joined = joinedBoundary(f.corners, g.corners, budget);
                if ( !canDecompose(joined, f.normal, budget) )
                {
                    continue;
                }
                if ( f.neighbor >= 0 )
                {
                    const auto other = static_cast<std::size_t>(f.neighbor);
                    const auto x = static_cast<std::size_t>(f.mirror), y = static_cast<std::size_t>(g.mirror);
                    if ( other >= conformed.size() || x >= conformed[other].size() || y >= conformed[other].size() ||
                         conformed[other][x].mirror != static_cast<int>(i) ||
                         conformed[other][y].mirror != static_cast<int>(j) )
                    {
                        fail("invalid reciprocal candidate for aperture reunion");
                    }
                    auto reverse = joinedBoundary(conformed[other][x].corners, conformed[other][y].corners, budget);
                    if ( reverse.size() != joined.size() )
                    {
                        continue;
                    }
                    checkPair(joined, reverse);
                    conformed[other][x].corners = std::move(reverse);
                    merged[other][y] = true;
                    initialArea[other][x] += initialArea[other][y];
                    initialPerimeter[other][x] += initialPerimeter[other][y];
                    displacement[other][x] = std::max(displacement[other][x], displacement[other][y]);
                }
                f.corners = std::move(joined);
                merged[s][j] = true;
                initialArea[s][i] += initialArea[s][j];
                initialPerimeter[s][i] += initialPerimeter[s][j];
                displacement[s][i] = std::max(displacement[s][i], displacement[s][j]);
                break;
            }
        }
    }

    std::vector<std::vector<JoJaHybridFace>> final(conformed.size());
    std::size_t pairs = 0, native = 0;
    double nativeArea = 0;
    std::map<std::size_t, std::pair<double, double>> sourceAreas;
    std::set<std::size_t> demotions;
    std::string firstIssue;
    auto recordPlacement = [&](std::size_t s, std::size_t i, const std::string& reason)
    {
        const auto& f = conformed[s][i];
        bool identified = false;
        if ( f.material )
        {
            demotions.insert(f.sourceSurface);
            identified = true;
        }
        else if ( f.neighbor < 0 )
        {
            // Only an actual shared directed edge implicates a drawn surface
            // in an unrepresentable blank-mask sliver. Apertures remain fixed.
            std::set<DirectedEdge> edges;
            for ( std::size_t c = 0; c < f.corners.size(); ++c )
            {
                edges.emplace(key(f.corners[(c + 1) % f.corners.size()].position), key(f.corners[c].position));
            }
            for ( const auto& donor : conformed[s] )
            {
                if ( !donor.material || donor.normal.Dot(f.normal) < 1 - 1e-12 )
                {
                    continue;
                }
                for ( std::size_t c = 0; c < donor.corners.size(); ++c )
                {
                    budget.charge();
                    if ( edges.contains({key(donor.corners[c].position),
                                         key(donor.corners[(c + 1) % donor.corners.size()].position)}) )
                    {
                        demotions.insert(donor.sourceSurface);
                        identified = true;
                        break;
                    }
                }
            }
        }
        const std::string issue = "sector " + std::to_string(s) + " face " + std::to_string(i) + " source " +
                                  std::to_string(f.sourceSurface) + " neighbor " + std::to_string(f.neighbor) + ": " +
                                  reason;
        if ( !identified )
        {
            throw std::runtime_error(issue);
        }
        if ( firstIssue.empty() )
        {
            firstIssue = issue;
        }
    };
    auto append = [&](std::size_t sector, const JoJaHybridFace& f, Ring piece) -> std::size_t
    {
        if ( budget.output == 0 )
        {
            fail("conforming native geometry exceeds maxFragments");
        }
        --budget.output;
        budget.store(piece.size());
        const auto index = final[sector].size();
        final[sector].push_back({std::move(piece), f.normal, f.material, f.sourceSurface, f.neighbor, -1});
        return index;
    };
    for ( std::size_t s = 0; s < conformed.size(); ++s )
    {
        for ( std::size_t i = 0; i < conformed[s].size(); ++i )
        {
            const auto& f = conformed[s][i];
            if ( merged[s][i] )
            {
                continue;
            }
            const auto& original = geometry.cells[s].faces[i];
            if ( f.corners.empty() )
            {
                if ( f.material )
                {
                    sourceAreas[f.sourceSurface].first += areaVector(original.corners).Length();
                }
                if ( f.neighbor >= 0 )
                {
                    const auto other = static_cast<std::size_t>(f.neighbor);
                    const auto mirror = static_cast<std::size_t>(f.mirror);
                    if ( other >= conformed.size() || mirror >= conformed[other].size() ||
                         conformed[other][mirror].neighbor != static_cast<int>(s) ||
                         conformed[other][mirror].mirror != static_cast<int>(i) ||
                         !conformed[other][mirror].corners.empty() )
                    {
                        fail("inconsistent collapsed aperture pair");
                    }
                }
                continue;
            }
            const double faceTolerance = epsilon + displacement[s][i];
            const double before = initialArea[s][i];
            const bool portal = f.neighbor >= 0;
            const auto other = static_cast<std::size_t>(f.neighbor);
            const auto mirror = static_cast<std::size_t>(f.mirror);
            if ( portal )
            {
                if ( other == s || other >= conformed.size() || mirror >= conformed[other].size() )
                {
                    fail("out-of-range portal mirror");
                }
                const auto& m = conformed[other][mirror];
                if ( m.neighbor != static_cast<int>(s) || m.mirror != static_cast<int>(i) || f.material || m.material ||
                     f.normal.Dot(m.normal) > -0.999999 )
                {
                    fail("inconsistent generated portal pair");
                }
                if ( other < s )
                {
                    continue;
                }
                try
                {
                    checkPair(f.corners, m.corners);
                }
                catch ( const std::runtime_error& e )
                {
                    throw std::runtime_error("portal " + std::to_string(s) + "/" + std::to_string(i) + " (" +
                                             std::to_string(f.corners.size()) + " corners) to " +
                                             std::to_string(other) + "/" + std::to_string(mirror) + " (" +
                                             std::to_string(m.corners.size()) + " corners): " + e.what());
                }
            }
            else if ( f.mirror >= 0 )
            {
                fail("unattached face carries a mirror");
            }

            double after = 0;
            std::vector<Ring> pieces;
            try
            {
                areaPreserved(before, areaVector(f.corners).Length(), initialPerimeter[s][i], faceTolerance);
                pieces = bounded(f.corners, f.normal, budget);
                for ( const auto& piece : pieces )
                {
                    Ring target = piece;
                    for ( auto& p : target )
                    {
                        p.position = runtimePoint(p.position);
                    }
                    if ( !(areaVector(piece).Dot(f.normal) > 0) || !(areaVector(target).Dot(f.normal) > 0) )
                    {
                        fail("face collapses or reverses at target precision");
                    }
                    after += areaVector(piece).Length();
                }
                areaPreserved(before, after, initialPerimeter[s][i], faceTolerance);
            }
            catch ( const std::runtime_error& e )
            {
                if ( !budget.work )
                {
                    throw;
                }
                recordPlacement(s, i, e.what());
                continue;
            }
            for ( auto& piece : pieces )
            {
                const double area = areaVector(piece).Length();
                if ( f.material )
                {
                    ++native;
                    nativeArea += area;
                }
                if ( portal )
                {
                    // Portals are untextured. Reusing the exact boundary and
                    // partition vertices ensures bit-identical mirror geometry.
                    Ring reverse(piece.rbegin(), piece.rend());
                    const auto a = append(s, f, std::move(piece));
                    const auto b = append(other, conformed[other][mirror], std::move(reverse));
                    final[s][a].mirror = static_cast<int>(b);
                    final[other][b].mirror = static_cast<int>(a);
                    ++pairs;
                }
                else
                {
                    append(s, f, std::move(piece));
                }
            }
            if ( f.material )
            {
                auto& [initial, emitted] = sourceAreas[f.sourceSurface];
                initial += before;
                emitted += after;
            }
        }
    }
    double outputArea = nativeArea;
    for ( const auto& residual : geometry.residual )
    {
        const auto& c = residual.triangle.corners;
        const double a = .5 * (c[1].position - c[0].position).Cross(c[2].position - c[0].position).Length();
        outputArea += a;
        auto& [initial, emitted] = sourceAreas[residual.triangle.surface];
        initial += a;
        emitted += a;
    }
    double maximumError = geometry.maximumRelativeCoverageError;
    for ( const auto& [source, areas] : sourceAreas )
    {
        if ( !(areas.first > 0) )
        {
            fail("invalid original source area");
        }
        if ( !(areas.second > 0) )
        {
            demotions.insert(source);
            if ( firstIssue.empty() )
            {
                firstIssue = "source surface " + std::to_string(source) + " vanishes at retail precision";
            }
            continue;
        }
        const double relative = std::abs(areas.first - areas.second) / areas.first;
        if ( relative > 1e-6 )
        {
            std::ostringstream detail;
            detail.precision(16);
            detail << "source surface " << source << " changes covered area from " << areas.first << " to "
                   << areas.second << " after target conformance (relative change " << relative << ')';
            if ( firstIssue.empty() )
            {
                firstIssue = detail.str();
            }
            demotions.insert(source);
        }
        maximumError = std::max(maximumError, relative);
    }
    if ( !demotions.empty() )
    {
        throw JoJaNativePromotionError({demotions.begin(), demotions.end()}, firstIssue);
    }
    if ( !std::isfinite(outputArea) ||
         (geometry.sourceArea > 0 && std::abs(outputArea - geometry.sourceArea) > geometry.sourceArea * 1e-6) )
    {
        fail("final emitted source coverage changed");
    }
    for ( std::size_t s = 0; s < final.size(); ++s )
    {
        try
        {
            certify(geometry.cells[s], final[s], (epsilon + cellDisplacement[s]) * 8, false, budget);
            certify(geometry.cells[s], final[s], (epsilon + cellDisplacement[s]) * 8, true, budget);
        }
        catch ( const std::runtime_error& e )
        {
            throw std::runtime_error("sector " + std::to_string(s) + ": " + e.what());
        }
    }
    // Removing a zero-area aperture must not separate originally connected sectors.
    std::vector<std::size_t> parent(final.size());
    std::iota(parent.begin(), parent.end(), 0);
    const auto component = [&parent](std::size_t i)
    {
        while ( parent[i] != i )
        {
            i = parent[i];
        }
        return i;
    };
    for ( std::size_t s = 0; s < final.size(); ++s )
    {
        for ( const auto& f : final[s] )
        {
            if ( f.neighbor >= 0 )
            {
                parent[component(s)] = component(f.neighbor);
            }
        }
    }
    for ( std::size_t s = 0; s < conformed.size(); ++s )
    {
        for ( const auto& f : conformed[s] )
        {
            if ( f.neighbor >= 0 && component(s) != component(f.neighbor) )
            {
                fail("a collapsed aperture disconnects the sector graph");
            }
        }
    }
    geometry.maximumRelativeCoverageError = maximumError;
    geometry.targetCollapsedFragments += collapsed;
    geometry.targetCollapsedArea += collapsedArea;
    for ( std::size_t s = 0; s < final.size(); ++s )
    {
        geometry.cells[s].faces = std::move(final[s]);
    }
    geometry.portalPairs = pairs;
    geometry.nativeFaces = native;
    geometry.outputArea = outputArea;
}
} // namespace sed::assets
