#include "sed/level/light_limits.hpp"

#include <algorithm>
#include <cmath>

namespace sed::level
{
namespace
{
bool validRange(double low, double high) noexcept
{
    return std::isfinite(low) && std::isfinite(high) && low >= 0.0 && low <= high && high <= 1.0;
}

bool finiteCorners(const Level& level) noexcept
{
    for ( const auto& sector : level.sectors )
    {
        for ( const auto& surface : sector.surfaces )
        {
            for ( const auto& vertex : surface.texVertices )
            {
                const auto& c = vertex.color;
                if ( !std::isfinite(c.r) || !std::isfinite(c.g) || !std::isfinite(c.b) )
                {
                    return false;
                }
            }
        }
    }
    return true;
}
}

LightLimitsStatus applyLightLimits(Level& level, const LightLimits& limits) noexcept
{
    if ( !validRange(limits.floor, limits.cap)
        || (level.kind != ProjectType::JKDF2 && level.kind != ProjectType::MOTS
            && level.kind != ProjectType::IJIM) )
    {
        return LightLimitsStatus::InvalidProfile;
    }
    if ( !finiteCorners(level) )
    {
        return LightLimitsStatus::NonFiniteInput;
    }
    // No allocation or other fallible operation after the complete preflight.
    // Double arithmetic avoids overflow when adding to a finite float channel.
    const auto limit = [&limits](float value) noexcept
    {
        return static_cast<float>(std::min(limits.cap, limits.floor + static_cast<double>(value)));
    };
    for ( auto& sector : level.sectors )
    {
        for ( auto& surface : sector.surfaces )
        {
            for ( auto& vertex : surface.texVertices )
            {
                if ( level.kind == ProjectType::JKDF2 )
                {
                    // VERSION 1 JK writes its mono corner value from a. Fresh
                    // bake RGB is authoritative; old a is not a bake input.
                    const double mono = (static_cast<double>(vertex.color.r)
                        + static_cast<double>(vertex.color.g)
                        + static_cast<double>(vertex.color.b)) / 3.0;
                    const float value = static_cast<float>(std::min(limits.cap, limits.floor + mono));
                    vertex.color = ColorF{ value, value, value, value };
                }
                else
                {
                    vertex.color.r = limit(vertex.color.r);
                    vertex.color.g = limit(vertex.color.g);
                    vertex.color.b = limit(vertex.color.b);
                }
            }
        }
    }
    return LightLimitsStatus::Ok;
}

LightDiagnosticReport inspectLightLimits(const Level& level, const LightDiagnosticOptions& options)
{
    LightDiagnosticReport report;
    if ( !validRange(options.nearBlack, options.cap) || options.maxDiagnostics > kMaxLightDiagnostics )
    {
        report.status = LightLimitsStatus::InvalidProfile;
        return report;
    }
    if ( !finiteCorners(level) )
    {
        report.status = LightLimitsStatus::NonFiniteInput;
        return report;
    }
    report.diagnostics.reserve(options.maxDiagnostics);
    for ( std::size_t si = 0; si < level.sectors.size(); ++si )
    {
        const auto& sector = level.sectors[si];
        for ( std::size_t fi = 0; fi < sector.surfaces.size(); ++fi )
        {
            const auto& surface = sector.surfaces[fi];
            for ( std::size_t ci = 0; ci < surface.texVertices.size(); ++ci )
            {
                const auto& c = surface.texVertices[ci].color;
                const double low = std::min({ c.r, c.g, c.b });
                const double high = std::max({ c.r, c.g, c.b });
                if ( report.corners == 0 )
                {
                    report.minimum = low;
                    report.maximum = high;
                }
                else
                {
                    report.minimum = std::min(report.minimum, low);
                    report.maximum = std::max(report.maximum, high);
                }
                ++report.corners;
                const bool black = high <= options.nearBlack;
                // Match the representable cap written by applyLightLimits;
                // a double threshold would miss downward-rounded decimal caps.
                const bool capped = high >= static_cast<float>(options.cap);
                const bool over = high > 1.0;
                report.nearBlackCorners += black ? 1u : 0u;
                report.cappedCorners += capped ? 1u : 0u;
                report.overUnitCorners += over ? 1u : 0u;
                if ( black || capped || over )
                {
                    if ( report.diagnostics.size() < options.maxDiagnostics )
                    {
                        report.diagnostics.push_back({ si, sector.id, fi, surface.num, ci, black, capped, over });
                    }
                    else
                    {
                        report.truncated = true;
                    }
                }
            }
        }
    }
    return report;
}
}
