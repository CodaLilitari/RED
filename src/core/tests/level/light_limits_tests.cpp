#include "test_common.hpp"
#include "sed/level/light_limits.hpp"
#include "sed/assets/jkl.hpp"

#include <limits>
#include <sstream>

namespace sed::tests
{
namespace
{
Level fixture()
{
    Level level;
    level.kind = ProjectType::MOTS;
    level.sectors.resize(1);
    auto& sector = level.sectors[0];
    sector.id = 42;
    sector.ambient = ColorF{ 0.13f, 0.17f, 0.19f, 0.23f };
    sector.surfaces.resize(1);
    auto& surface = sector.surfaces[0];
    surface.num = 77;
    surface.material = "unchanged.mat";
    surface.vertices = { 0, 1, 2 };
    surface.texVertices.resize(3);
    surface.texVertices[0].color = ColorF{ 0.0f, 0.25f, 1.0f, 0.375f };
    surface.texVertices[1].color = ColorF{ 0.25f, 0.25f, 0.25f, 0.5f };
    surface.texVertices[2].color = ColorF{ 0.0f, 0.0f, 0.0f, 0.75f };
    return level;
}
}

void runLightLimitsTests()
{
    using namespace sed::level;
    {
        Level jk;
        jk.clear();
        jk.kind = ProjectType::JKDF2;
        jk.header.version = 1;
        jk.sectors.push_back(makeLitTestCubeSector(0.0, ColorBlack));
        for ( auto& surface : jk.sectors[0].surfaces )
        {
            surface.material.clear();
            surface.nMat = -1;
            for ( auto& vertex : surface.texVertices )
            {
                vertex.color = ColorF{ 0, 0, 0, 0.9f };
            }
        }
        check(applyLightLimits(jk, { 0.25, 0.75 }) == LightLimitsStatus::Ok,
            "light limits: JK scalar policy accepted");
        const auto& c = jk.sectors[0].surfaces[0].texVertices[0].color;
        check(c.r == 0.25f && c.g == 0.25f && c.b == 0.25f && c.a == 0.25f,
            "light limits: JK wire mono matches fresh baked grayscale");
        try
        {
            std::ostringstream out;
            sed::assets::saveToJKL(jk, out);
            std::istringstream in(out.str());
            sed::assets::JklLoadOptions options;
            options.projectType = ProjectType::JKDF2;
            const auto loaded = sed::assets::loadFromJKL(in, options);
            check(!loaded.sectors.empty() && !loaded.sectors[0].surfaces.empty()
                && !loaded.sectors[0].surfaces[0].texVertices.empty()
                && loaded.sectors[0].surfaces[0].texVertices[0].color.a == 0.25f,
                "light limits: JK floor survives actual JKL roundtrip");
        }
        catch ( const std::exception& error )
        {
            std::cerr << "JK light limits roundtrip fixture error: " << error.what() << '\n';
            check(false, "light limits: JK roundtrip fixture must serialize and load");
        }

        auto mono = fixture();
        mono.kind = ProjectType::JKDF2;
        mono.sectors[0].surfaces[0].texVertices[0].color =
            ColorF{ 0.0f, 0.75f, 0.75f, std::numeric_limits<float>::quiet_NaN() };
        check(applyLightLimits(mono, { 0.25, 0.75 }) == LightLimitsStatus::Ok,
            "light limits: JK replaces stale mono rather than reading it");
        const auto& mc = mono.sectors[0].surfaces[0].texVertices[0].color;
        check(mc.r == 0.75f && mc.g == 0.75f && mc.b == 0.75f && mc.a == 0.75f,
            "light limits: JK scalar uses RGB mean before policy and replaces invalid stale mono");
    }
    auto level = fixture();
    {
        auto dw = fixture();
        dw.kind = ProjectType::DroidWorks;
        check(applyLightLimits(dw, { 0.25, 0.75 }) == LightLimitsStatus::InvalidProfile
            && dw.sectors[0].surfaces[0].texVertices[0].color.r == 0,
            "light limits: DroidWorks policy refused without mutation");
        auto ijim = fixture();
        ijim.kind = ProjectType::IJIM;
        check(applyLightLimits(ijim, { 0.25, 0.75 }) == LightLimitsStatus::Ok
            && ijim.sectors[0].surfaces[0].texVertices[0].color.a == 0.375f,
            "light limits: IJIM alpha preserved");
        auto lit = fixture();
        lit.kind = ProjectType::JKDF2;
        lit.sectors[0].surfaces[0].texVertices[0].color = ColorF{ 0.25f, 0.25f, 0.25f, 1.0f };
        check(applyLightLimits(lit, { 0.25, 0.75 }) == LightLimitsStatus::Ok
            && lit.sectors[0].surfaces[0].texVertices[0].color.a == 0.5f,
            "light limits: lit JK gray ignores stale unit alpha");
    }
    check(applyLightLimits(level, { 0.25, 0.75 }) == LightLimitsStatus::Ok,
        "light limits: valid policy accepted");
    const auto& s = level.sectors[0];
    const auto& colors = s.surfaces[0].texVertices;
    check(colors[0].color.r == 0.25f && colors[0].color.g == 0.5f && colors[0].color.b == 0.75f,
        "light limits: RGB floor plus baked capped independently");
    check(colors[1].color.r == 0.5f && colors[1].color.g == 0.5f && colors[1].color.b == 0.5f,
        "light limits: grayscale remains grayscale");
    check(colors[0].color.a == 0.375f && colors[1].color.a == 0.5f && colors[2].color.a == 0.75f,
        "light limits: alpha preserved");
    check(s.ambient.r == 0.13f && s.ambient.g == 0.17f && s.ambient.b == 0.19f
        && s.ambient.a == 0.23f && s.id == 42 && s.surfaces[0].material == "unchanged.mat"
        && s.surfaces[0].vertices == std::vector<std::int32_t>({ 0, 1, 2 }),
        "light limits: ambient and geometry untouched");

    for ( const LightLimits policy : { LightLimits{ -0.1, 1 }, { 0.8, 0.7 }, { 0, 1.1 },
        { std::numeric_limits<double>::quiet_NaN(), 1 }, { 0, std::numeric_limits<double>::infinity() } } )
    {
        auto bad = fixture();
        check(applyLightLimits(bad, policy) == LightLimitsStatus::InvalidProfile,
            "light limits: invalid profile refused");
        check(bad.sectors[0].surfaces[0].texVertices[0].color.g == 0.25f,
            "light limits: invalid profile leaves data unchanged");
    }
    for ( float badValue : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() } )
    {
        auto bad = fixture();
        bad.sectors[0].surfaces[0].texVertices.back().color.b = badValue;
        check(applyLightLimits(bad, { 0.25, 0.75 }) == LightLimitsStatus::NonFiniteInput,
            "light limits: nonfinite late corner refused");
        check(bad.sectors[0].surfaces[0].texVertices[0].color.r == 0.0f,
            "light limits: refusal has no partial writes");
        const auto report = inspectLightLimits(bad);
        check(report.status == LightLimitsStatus::NonFiniteInput && report.corners == 0
            && report.diagnostics.empty(), "light limits: nonfinite scan has no partial report");
    }
    auto boundary = fixture();
    check(applyLightLimits(boundary, { 0, 0 }) == LightLimitsStatus::Ok
        && boundary.sectors[0].surfaces[0].texVertices[0].color.b == 0,
        "light limits: zero equal floor cap accepted");
    boundary = fixture();
    check(applyLightLimits(boundary, { 1, 1 }) == LightLimitsStatus::Ok
        && boundary.sectors[0].surfaces[0].texVertices[2].color.r == 1,
        "light limits: unit equal floor cap accepted");

    auto scan = fixture();
    scan.sectors[0].surfaces[0].texVertices[0].color.b = 1.25f;
    const auto report = inspectLightLimits(scan, { 0, 0.25, 1 });
    check(report.status == LightLimitsStatus::Ok && report.corners == 3
        && report.minimum == 0 && report.maximum == 1.25 && report.nearBlackCorners == 1
        && report.cappedCorners == 2 && report.overUnitCorners == 1,
        "light limits: full statistics and inclusive boundaries");
    check(report.diagnostics.size() == 1 && report.truncated,
        "light limits: diagnostics bounded independently from counts");
    if ( !report.diagnostics.empty() )
    {
        const auto& d = report.diagnostics[0];
        check(d.sectorIndex == 0 && d.sectorId == 42 && d.surfaceIndex == 0
            && d.surfaceNum == 77 && d.cornerIndex == 0 && d.overUnit,
            "light limits: deterministic diagnostic identity");
    }
    check(scan.sectors[0].surfaces[0].texVertices[0].color.b == 1.25f,
        "light limits: scan does not clamp input");
    const auto zero = inspectLightLimits(scan, { 0, 1, 0 });
    check(zero.diagnostics.empty() && zero.truncated && zero.corners == 3,
        "light limits: zero diagnostic budget retains counts");
    check(inspectLightLimits(scan, { 0, 1, kMaxLightDiagnostics + 1 }).status == LightLimitsStatus::InvalidProfile,
        "light limits: oversized diagnostic budget refused");
    check(inspectLightLimits(scan, { -0.1, 1, 1 }).status == LightLimitsStatus::InvalidProfile,
        "light limits: invalid diagnostic threshold refused");
    for ( const double cap : { 0.7, 0.8 } )
    {
        auto quantized = fixture();
        check(applyLightLimits(quantized, { 0, cap }) == LightLimitsStatus::Ok,
            "light limits: decimal cap accepted");
        const auto decimalReport = inspectLightLimits(quantized, { 0, cap, 10 });
        check(decimalReport.cappedCorners == 1,
            "light limits: diagnostic cap matches stored float for decimal bounds");
    }
    const auto empty = inspectLightLimits(Level{});
    check(empty.status == LightLimitsStatus::Ok && empty.corners == 0 && empty.minimum == 0
        && empty.maximum == 0 && !empty.truncated, "light limits: empty report defined");
}
}
