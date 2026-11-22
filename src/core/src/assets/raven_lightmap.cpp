#include "sed/assets/raven_lightmap.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sed::assets
{
namespace
{

using Triple = std::array<double, 3>;

double luminance(double r, double g, double b)
{
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

template <typename ReadTexel>
double bilinear(std::size_t atlas, double u, double v, ReadTexel read)
{
    // Valid finite source UVs can lie far outside the image.
    const double x = std::clamp(u * 128.0 - 0.5, 0.0, 127.0);
    const double y = std::clamp(v * 128.0 - 0.5, 0.0, 127.0);
    const auto x0 = static_cast<std::size_t>(std::floor(x));
    const auto y0 = static_cast<std::size_t>(std::floor(y));
    const auto x1 = std::min<std::size_t>(127, x0 + 1);
    const auto y1 = std::min<std::size_t>(127, y0 + 1);
    const double fx = x - static_cast<double>(x0), fy = y - static_cast<double>(y0);
    const auto sample = [&](std::size_t px, std::size_t py)
    {
        return read(((atlas * 128 + py) * 128 + px) * 3);
    };
    return sample(x0, y0) * (1 - fx) * (1 - fy) + sample(x1, y0) * fx * (1 - fy) +
           sample(x0, y1) * (1 - fx) * fy + sample(x1, y1) * fx * fy;
}

Triple sampleRgb(const RavenBsp& bsp, int atlas, double u, double v)
{
    Triple rgb{};
    for ( std::size_t c = 0; c < rgb.size(); ++c )
    {
        rgb[c] = bilinear(atlas, u, v, [&](std::size_t at) { return bsp.lightmaps.at(at + c) / 255.0; });
    }
    return rgb;
}

void record(std::optional<RavenLightmapWitness>& worst, std::size_t triangle, const Triple& weights,
            const Triple& source, const Triple& approximation)
{
    double error = 0;
    for ( std::size_t c = 0; c < source.size(); ++c )
    {
        error = std::max(error, std::abs(source[c] - approximation[c]));
    }
    if ( !worst || error > worst->maxChannelError )
    {
        worst = RavenLightmapWitness{error, triangle, weights, source, approximation};
    }
}

} // namespace

float ravenLightmapLuminance(const RavenBsp& bsp, int atlas, double u, double v)
{
    return static_cast<float>(bilinear(atlas, u, v, [&](std::size_t at)
    {
        return luminance(bsp.lightmaps.at(at), bsp.lightmaps.at(at + 1), bsp.lightmaps.at(at + 2)) / 255.0;
    }));
}

RavenLightmapFidelity inspectRavenLightmap(const RavenBsp& bsp, std::size_t surface, std::uint32_t patchSubdivisions)
{
    if ( surface >= bsp.surfaces.size() || patchSubdivisions < 1 || patchSubdivisions > 16 )
    {
        throw std::invalid_argument("Surface index or patch subdivisions out of range");
    }
    RavenLightmapFidelity result;
    const auto atlas = bsp.surfaces[surface].lightmapIndex;
    if ( atlas < 0 )
    {
        return result;
    }
    const auto triangles = ravenBspSurfaceTriangles(bsp, surface, patchSubdivisions);
    result.triangleCount = triangles.size();
    for ( std::size_t t = 0; t < triangles.size(); ++t )
    {
        const auto& corners = triangles[t].corners;
        std::array<Triple, 3> colors{};
        for ( std::size_t i = 0; i < corners.size(); ++i )
        {
            colors[i] = sampleRgb(bsp, atlas, corners[i].lightmapU, corners[i].lightmapV);
            const auto scalar = ravenLightmapLuminance(bsp, atlas, corners[i].lightmapU, corners[i].lightmapV);
            Triple weight{};
            weight[i] = 1;
            record(result.cornerScalar, t, weight, colors[i], {scalar, scalar, scalar});
        }
        for ( int a = 0; a <= kRavenLightmapGridSteps; ++a )
        {
            for ( int b = 0; b <= kRavenLightmapGridSteps - a; ++b )
            {
                constexpr double grid = kRavenLightmapGridSteps;
                const Triple weights{a / grid, b / grid, (kRavenLightmapGridSteps - a - b) / grid};
                double u = 0, v = 0;
                Triple interpolated{};
                for ( std::size_t i = 0; i < corners.size(); ++i )
                {
                    u += weights[i] * corners[i].lightmapU;
                    v += weights[i] * corners[i].lightmapV;
                    for ( std::size_t c = 0; c < interpolated.size(); ++c )
                    {
                        interpolated[c] += weights[i] * colors[i][c];
                    }
                }
                record(result.rgbInterpolation, t, weights, sampleRgb(bsp, atlas, u, v), interpolated);
            }
        }
    }
    return result;
}

} // namespace sed::assets
