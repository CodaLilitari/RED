#include "test_common.hpp"

#include "sed/level/overview.hpp"

#include <cmath>
#include <limits>

namespace sed::tests
{

namespace
{

constexpr double kTol = 1e-9;

bool near(const double expected, const double actual)
{
    return std::abs(expected - actual) <= kTol;
}
bool nearVec(const TVector& expected, const TVector& actual)
{
    return near(expected.x, actual.x) && near(expected.y, actual.y) && near(expected.z, actual.z);
}

template <typename Exception, typename Fn>
bool throwsAs(Fn&& fn)
{
    try
    {
        fn();
    }
    catch (const Exception&)
    {
        return true;
    }
    catch (...)
    {
        return false;
    }
    return false;
}

// Golden box shared with the Wren framing reference values.
TBox goldenBox()
{
    TBox box{};
    box.Set(-10.0, -5.0, 0.0, 10.0, 5.0, 4.0);
    return box;
}

} // namespace

void runOverviewTests()
{
    using namespace sed::level;

    // Overhead golden camera (1280x960, margin 0.025, both framings).
    for (const std::string_view framing : { kOverviewWren, kOverviewContain })
    {
        const OverviewCamera cam = computeOverviewCamera(goldenBox(), kOverviewOverhead, framing, 1280, 960, 0.025);
        check(cam.orthographic, "overview.overhead is orthographic");
        check(nearVec(TVector{ 0, 0, 24 }, cam.position), "overview.overhead golden position");
        check(nearVec(TVector{ 0, 0, 2 }, cam.target), "overview.overhead golden target");
        check(near(15.75, cam.orthographicHeight), "overview.overhead golden orthographicHeight");
        check(near(21.0, cam.orthographicHeight * 1280.0 / 960.0), "overview.overhead golden width");
        check(near(10.0, cam.nearPlane), "overview.overhead golden near");
        check(near(34.0, cam.farPlane), "overview.overhead golden far");
        check(cam.fieldOfViewDegrees == 0.0, "overview.overhead fov is zero");
    }
    {
        const OverviewCamera cam = computeOverviewCamera(goldenBox(), kOverviewOverhead, kOverviewWren, 600, 800, 0.025);
        check(near(28.0, cam.orthographicHeight), "overview.overhead portrait golden orthographicHeight");
        check(near(21.0, cam.orthographicHeight * 600.0 / 800.0), "overview.overhead portrait golden width");
    }

    // Three-quarter Wren golden camera.
    {
        const OverviewCamera cam = computeOverviewCamera(goldenBox(), kOverviewThreeQuarter, kOverviewWren, 1280, 960, 0.025);
        check(!cam.orthographic, "overview.threequarter is perspective");
        check(nearVec(TVector{ 11.925707526180574, -11.925707526180574, 12.222035022440492 }, cam.position),
              "overview.threequarter wren golden position");
        check(nearVec(TVector{ 0, 0, 2 }, cam.target), "overview.threequarter wren golden target");
        check(near(42.18467901165499, cam.fieldOfViewDegrees), "overview.threequarter golden fovY");
        check(near(4.181809173731455, cam.nearPlane), "overview.threequarter wren golden near");
        check(near(31.192829897580012, cam.farPlane), "overview.threequarter wren golden far");
        check(cam.orthographicHeight == 0.0, "overview.threequarter ortho height is zero");
    }

    // Three-quarter contain golden camera.
    {
        const OverviewCamera cam = computeOverviewCamera(goldenBox(), kOverviewThreeQuarter, kOverviewContain, 1280, 960, 0.025);
        check(nearVec(TVector{ 16.75338631061249, -16.75338631061249, 16.360045409096422 }, cam.position),
              "overview.threequarter contain golden position");
        check(near(42.18467901165499, cam.fieldOfViewDegrees), "overview.threequarter contain golden fovY");
        check(near(8.1735536185632, cam.nearPlane), "overview.threequarter contain golden near");
        check(near(39.1763187872435, cam.farPlane), "overview.threequarter contain golden far");
    }

    // Portrait aspect widens the vertical FOV (AUTO fit).
    check(near(54.43222311461495,
               computeOverviewCamera(goldenBox(), kOverviewThreeQuarter, kOverviewWren, 600, 800, 0.025).fieldOfViewDegrees),
          "overview.threequarter portrait golden fovY");

    // Contain: every box corner projects inside the margin, and the
    // limiting corner touches it.
    {
        const TBox box = goldenBox();
        const int width = 1280, height = 960;
        const double margin = 0.025;
        const OverviewCamera cam = computeOverviewCamera(box, kOverviewThreeQuarter, kOverviewContain, width, height, margin);
        const TVector forward = cam.target.Sub(cam.position).Normalized();
        const TVector right = forward.Cross(TVector::up).Normalized();
        const TVector up = right.Cross(forward);
        const double tanV = std::tan(cam.fieldOfViewDegrees * 3.141592653589793 / 360.0);
        const double tanH = tanV * width / height;
        const double bound = 1.0 - 2.0 * margin;
        double worst = 0.0;
        bool allInside = true;
        for (const double x : { box.x1, box.x2 })
            for (const double y : { box.y1, box.y2 })
                for (const double z : { box.z1, box.z2 })
                {
                    const TVector q = TVector{ x, y, z }.Sub(cam.position);
                    const double depth = forward.Dot(q);
                    const double ndcX = right.Dot(q) / (depth * tanH);
                    const double ndcY = up.Dot(q) / (depth * tanV);
                    if (!(depth > cam.nearPlane && depth < cam.farPlane)) allInside = false;
                    if (std::abs(ndcX) > bound + 1e-12 || std::abs(ndcY) > bound + 1e-12) allInside = false;
                    worst = std::max(worst, std::max(std::abs(ndcX), std::abs(ndcY)));
                }
        check(allInside, "overview.contain keeps every corner inside the margin and planes");
        check(near(bound, worst), "overview.contain limiting corner touches the margin");
    }

    // Validation refusals.
    check(throwsAs<std::invalid_argument>([] { (void)computeOverviewCamera(goldenBox(), "side", kOverviewWren, 64, 64, 0.0); }),
          "overview refuses unknown view");
    check(throwsAs<std::invalid_argument>([] { (void)computeOverviewCamera(goldenBox(), kOverviewOverhead, "fit", 64, 64, 0.0); }),
          "overview refuses unknown framing");
    check(throwsAs<std::invalid_argument>([] { (void)computeOverviewCamera(goldenBox(), kOverviewOverhead, kOverviewWren, 0, 64, 0.0); }),
          "overview refuses zero width");
    check(throwsAs<std::invalid_argument>([] { (void)computeOverviewCamera(goldenBox(), kOverviewOverhead, kOverviewWren, 64, 64, 0.26); }),
          "overview refuses margin above 0.25");
    check(throwsAs<std::invalid_argument>([] {
              (void)computeOverviewCamera(goldenBox(), kOverviewOverhead, kOverviewWren, 64, 64,
                                         std::numeric_limits<double>::quiet_NaN());
          }),
          "overview refuses NaN margin");
    check(throwsAs<std::runtime_error>([] {
              TBox flat{};
              flat.Set(1, 1, 0, 1, 1, 4);
              (void)computeOverviewCamera(flat, kOverviewOverhead, kOverviewWren, 64, 64, 0.0);
          }),
          "overview refuses X/Y-degenerate bounds");

    // Drawn-surface bounds over a synthetic level: geo==0, two-corner
    // and material-less surfaces are excluded; things never counted.
    {
        Level level{};
        Sector sector{};
        sector.vertices = { Vertex{ -10, -5, 0 }, Vertex{ 10, -5, 0 }, Vertex{ 10, 5, 4 }, Vertex{ -10, 5, 4 },
                            Vertex{ 100, 100, 100 }, Vertex{ 200, 100, 100 }, Vertex{ 200, 200, 100 } };
        Surface drawn{};
        drawn.geo = 1;
        drawn.material = "wall.mat";
        drawn.vertices = { 0, 1, 2, 3 };
        Surface noGeo{};
        noGeo.geo = 0;
        noGeo.material = "wall.mat";
        noGeo.vertices = { 4, 5, 6 };
        Surface thin{};
        thin.geo = 1;
        thin.material = "wall.mat";
        thin.vertices = { 4, 5 };
        Surface noMat{};
        noMat.geo = 1;
        noMat.vertices = { 4, 5, 6 };
        sector.surfaces = { drawn, noGeo, thin, noMat };
        level.sectors = { sector };
        const auto bounds = worldSurfaceBounds(level);
        check(bounds.has_value(), "overview bounds found for drawn surface");
        if (bounds)
        {
            check(near(bounds->x1, -10) && near(bounds->y1, -5) && near(bounds->z1, 0)
                      && near(bounds->x2, 10) && near(bounds->y2, 5) && near(bounds->z2, 4),
                  "overview bounds cover only drawn surfaces");
        }
        Level empty{};
        check(!worldSurfaceBounds(empty).has_value(), "overview bounds empty level is nullopt");
    }
}

} // namespace sed::tests
