#include "sed/assets/jo_ja_hybrid_validation.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <numbers>
#include <stdexcept>
#include <tuple>

namespace
{
using namespace sed;
using namespace sed::assets;

void check(bool condition, const char* message)
{
    if ( !condition )
    {
        throw std::runtime_error(message);
    }
}

JoJaHybridFace face(std::vector<TVector> points)
{
    JoJaHybridFace f;
    f.normal = points[1].Sub(points[0]).Cross(points[2].Sub(points[0])).Normalized();
    for ( const auto& point : points )
    {
        RavenBspVertex v;
        v.position = point;
        v.u = point.x;
        v.v = point.y;
        v.color.r = static_cast<float>((point.x + 1) / 2);
        f.corners.push_back(v);
    }
    return f;
}

JoJaHybridCell prism(const std::vector<TVector>& ring, double z)
{
    JoJaHybridCell c;
    c.interior = {0, 0, z + 0.5};
    auto top = ring;
    auto bottom = ring;
    for ( auto& v : bottom )
    {
        v.z = z;
    }
    for ( auto& v : top )
    {
        v.z = z + 1;
    }
    c.faces.push_back(face(bottom));
    c.faces.push_back(face({top.rbegin(), top.rend()}));
    for ( std::size_t i = 0; i < ring.size(); ++i )
    {
        const auto j = (i + 1) % ring.size();
        c.faces.push_back(face({bottom[j], bottom[i], top[i], top[j]}));
    }
    for ( const auto& f : c.faces )
    {
        const auto n = f.normal.Scaled(-1);
        c.planes.push_back({n, n.Dot(f.corners.front().position)});
    }
    return c;
}

auto key(const TVector& p)
{
    return std::tuple{p.x, p.y, p.z};
}

void closed(const JoJaHybridCell& c)
{
    using K = decltype(key(c.interior));
    std::map<std::pair<K, K>, int> edges;
    for ( const auto& f : c.faces )
    {
        check(f.corners.size() <= 24, "native face exceeds retail count");
        for ( std::size_t i = 0; i < f.corners.size(); ++i )
        {
            ++edges[{key(f.corners[i].position), key(f.corners[(i + 1) % f.corners.size()].position)}];
        }
    }
    for ( const auto& [edge, count] : edges )
    {
        check(count == 1 && edges.at({edge.second, edge.first}) == 1, "shell not closed with actual indexed edges");
    }
}

void run()
{
    const std::vector<TVector> square{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
    JoJaHybridGeometry g;
    g.cells.push_back(prism(square, 0));
    auto& bottom = g.cells[0].faces[0];
    bottom.material = 3;
    bottom.sourceSurface = 19;
    auto middle = bottom.corners[0];
    middle.position.x = 0;
    middle.u = 0;
    middle.color.r = .5f;
    bottom.corners.insert(bottom.corners.begin() + 1, middle);
    // A clipped boundary can produce distinct doubles at one retail position.
    auto redundant = bottom.corners.back();
    redundant.position.x += 2e-9;
    bottom.corners.push_back(redundant);
    finalizeJoJaHybridGeometry(g);
    closed(g.cells[0]);
    check(g.cells[0].faces[2].corners.size() == 5, "neighbor receives T junction");
    for ( const auto& v : g.cells[0].faces[2].corners )
    {
        if ( v.position.x == 0 )
        {
            check(v.u == 0 && v.color.r == .5f, "edge attributes interpolated");
        }
    }

    std::vector<TVector> ring;
    for ( int i = 0; i < 31; ++i )
    {
        const double a = 2 * std::numbers::pi * i / 31;
        ring.push_back({std::cos(a), std::sin(a), 0});
    }
    JoJaHybridGeometry portals;
    portals.cells = {prism(ring, 0), prism(ring, 1)};
    portals.cells[0].faces[1].neighbor = 1;
    portals.cells[0].faces[1].mirror = 0;
    portals.cells[1].faces[0].neighbor = 0;
    portals.cells[1].faces[0].mirror = 1;
    // Only one exterior wall initially knows this extra portal-edge corner.
    // Both independently wound apertures and the opposite wall must inherit it.
    auto& side = portals.cells[1].faces[2];
    auto donor = side.corners[0];
    donor.position = (side.corners[0].position + side.corners[1].position).Scaled(.5);
    side.corners.insert(side.corners.begin() + 1, donor);
    finalizeJoJaHybridGeometry(portals);
    check(portals.portalPairs >= 2 && portals.portalPairs <= 4, "bounded portal count after partition");
    for ( std::size_t s = 0; s < 2; ++s )
    {
        closed(portals.cells[s]);
        bool hasDonor = false;
        for ( std::size_t i = 0; i < portals.cells[s].faces.size(); ++i )
        {
            const auto& f = portals.cells[s].faces[i];
            if ( f.neighbor < 0 )
            {
                continue;
            }
            for ( const auto& point : f.corners )
            {
                hasDonor |= key(point.position) == key(donor.position);
            }
            const auto& mirror = portals.cells.at(f.neighbor).faces.at(f.mirror);
            check(mirror.neighbor == static_cast<int>(s) && mirror.mirror == static_cast<int>(i) &&
                      mirror.corners.size() == f.corners.size(),
                  "reciprocal bounded portals");
        }
        check(hasDonor, "each side of the paired portal inherits the donor");
    }
    // This blank diagonal tile is CCW in source coordinates but clockwise
    // after float32 decoding. The union with the adjoining blank tile has
    // identical solid-wall semantics and a representable external boundary.
    JoJaHybridGeometry blank;
    blank.cells.push_back(prism({{1, 1, 0}, {3, 1, 0}, {3, 3, 0}, {1, 3, 0}}, 0));
    auto& box = blank.cells.front();
    box.interior = {2, 2, .5};
    const TVector a{1, 1, 0}, b{2, 1.49999983, 0}, c{3, 1.99999967, 0};
    box.faces[0] = face({a, b, c});
    box.faces.push_back(face({a, {3, 1, 0}, c, b}));
    box.faces.push_back(face({a, c, {3, 3, 0}, {1, 3, 0}}));
    auto materialEdges = blank;
    auto& edgeFaces = materialEdges.cells.front().faces;
    edgeFaces[6].material = edgeFaces[7].material = 3;
    edgeFaces[6].sourceSurface = 200;
    edgeFaces[7].sourceSurface = 100;
    bool foundOwners = false;
    try
    {
        finalizeJoJaHybridGeometry(materialEdges);
    }
    catch ( const JoJaNativePromotionError& e )
    {
        foundOwners = e.sourceSurfaces == std::vector<std::size_t>{100, 200};
    }
    check(foundOwners && materialEdges.cells.front().faces.size() == 8,
          "identify only adjacent native sources for a reversed blank tile before mutating the shell");
    finalizeJoJaHybridGeometry(blank);
    closed(blank.cells.front());
    check(blank.targetCollapsedFragments == 0, "reuniting equal walls discards no solid area");

    JoJaHybridGeometry pinched;
    pinched.cells.push_back(prism(square, 0));
    auto& cap = pinched.cells.front().faces;
    const TVector center{};
    cap[0] = face({square[0], square[1], center, square[2], square[3], center});
    cap[0].material = 2;
    cap[0].sourceSurface = 21;
    cap.push_back(face({square[0], center, square[3]}));
    cap.push_back(face({square[1], square[2], center}));
    finalizeJoJaHybridGeometry(pinched);
    closed(pinched.cells.front());
    check(pinched.nativeFaces == 2 && pinched.outputArea == 2,
          "split a pinched positive ring at its exact shared vertex");

    JoJaHybridGeometry nearEdge;
    const std::vector<TVector> skew{{-1, -1, 0}, {1, -.321234567, 0}, {1, 1, 0}, {-1, 1, 0}};
    nearEdge.cells.push_back(prism(skew, 0));
    auto& skewFloor = nearEdge.cells.front().faces[0];
    auto clipped = skewFloor.corners[0];
    clipped.position = skew[0] + (skew[1] - skew[0]).Scaled(.37812);
    clipped.position.y += 2e-10;
    skewFloor.corners.insert(skewFloor.corners.begin() + 1, clipped);
    const auto wallNormal = nearEdge.cells.front().faces[2].normal;
    finalizeJoJaHybridGeometry(nearEdge);
    closed(nearEdge.cells.front());
    bool foundClip = false;
    for ( const auto& f : nearEdge.cells.front().faces )
    {
        if ( f.normal.Dot(wallNormal) > .999 )
        {
            for ( const auto& c : f.corners )
            {
                foundClip |= key(c.position) == key(clipped.position);
            }
        }
    }
    check(foundClip, "conform an actual reverse boundary chain when its projection has the same retail tuple");

    JoJaHybridGeometry opposing;
    opposing.cells.push_back(prism({{1, 1, 0}, {3, 1, 0}, {3, 3, 0}, {1, 3, 0}}, 0));
    auto& flat = opposing.cells.front();
    flat.interior = {2, 2, .5};
    const TVector below{2, 1.49999983, 0}, above{2, 1.49999984, 0};
    flat.faces[0] = face({a, below, c});
    flat.faces.push_back(face({c, above, a}));
    flat.faces.push_back(face({a, {3, 1, 0}, c, below}));
    flat.faces.push_back(face({a, above, c, {3, 3, 0}, {1, 3, 0}}));
    flat.faces.back().normal = {0, 0, 1};
    finalizeJoJaHybridGeometry(opposing);
    closed(opposing.cells.front());
    check(opposing.targetCollapsedFragments == 2 && opposing.targetCollapsedArea > 0,
          "account for a wholly contracted blank two-tile union");

    JoJaHybridGeometry sliver;
    sliver.cells.push_back(prism(square, 0));
    const double cut = 1 - 1e-8;
    auto& floor = sliver.cells.front().faces.front();
    floor = face({{-1, -1, 0}, {cut, -1, 0}, {cut, 1, 0}, {-1, 1, 0}});
    floor.material = 1;
    floor.sourceSurface = 45;
    auto strip = face({{cut, -1, 0}, {1, -1, 0}, {1, 1, 0}, {cut, 1, 0}});
    strip.material = floor.material;
    strip.sourceSurface = floor.sourceSurface;
    sliver.cells.front().faces.push_back(strip);
    auto separate = sliver;
    separate.cells.front().faces.back().sourceSurface = 46;
    finalizeJoJaHybridGeometry(sliver);
    closed(sliver.cells.front());
    check(sliver.targetCollapsedFragments == 1 && std::abs(sliver.targetCollapsedArea - 2e-8) < 1e-14,
          "report the measured area lost to a float32-contracted source fragment");
    check(sliver.nativeFaces == 1, "the rest of the original surface remains");
    bool lostSource = false;
    try
    {
        finalizeJoJaHybridGeometry(separate);
    }
    catch ( const JoJaNativePromotionError& e )
    {
        lostSource = e.sourceSurfaces == std::vector<std::size_t>{46};
    }
    catch ( const std::runtime_error& )
    {
    }
    check(lostSource && separate.cells.front().faces.size() == 7 && separate.targetCollapsedFragments == 0,
          "refuse complete loss of one source surface atomically");

    JoJaHybridGeometry collapse;
    auto tiny = square;
    for ( auto& p : tiny )
    {
        p.x = 100000 + p.x * 1e-7;
    }
    collapse.cells.push_back(prism(tiny, 0));
    collapse.cells[0].interior.x = 100000;
    bool rejected = false;
    try
    {
        finalizeJoJaHybridGeometry(collapse);
    }
    catch ( const std::runtime_error& )
    {
        rejected = true;
    }
    check(rejected, "float32 aliasing must refuse");
}
} // namespace

int main()
{
    try
    {
        run();
        return 0;
    }
    catch ( const std::exception& e )
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
