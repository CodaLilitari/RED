
#include "test_common.hpp"

#include "sed/assets/jkl.hpp"
#include "sed/level/dynamic_vertex_colors.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sed::tests
{
namespace
{

using namespace sed::level;

// One surface row in the port writer's version-1 layout: fixed fields,
// one extra-light token, corner count, `v,tv` pairs, then lighting tokens.
std::string surfaceRow(
    const int index,
    const int corners,
    const std::string& lighting)
{
    std::string row = std::to_string(index) + ": 0 0 0 4 5 0 -1 0.5 "
        + std::to_string(corners);
    for (int corner = 0; corner < corners; ++corner)
    {
        row += " 0,0";
    }
    return row + " " + lighting;
}

std::string repeatCorner(const std::string& corner, const int corners)
{
    std::string out;
    for (int i = 0; i < corners; ++i)
    {
        if (i > 0)
        {
            out += ' ';
        }
        out += corner;
    }
    return out;
}

std::string documentWithRows(const std::vector<std::string>& rows)
{
    std::string text = "SECTION: HEADER\nVERSION 1\nEND\n\n"
        "SECTION: GEORESOURCE\nWORLD SURFACES "
        + std::to_string(rows.size()) + "\n";
    for (const std::string& row : rows)
    {
        text += row + "\n";
    }
    return text + "END\n";
}

std::string singleRowDocument(const std::string& corner)
{
    return documentWithRows({ surfaceRow(0, 4, repeatCorner(corner, 4)) });
}

} // namespace

void runDynamicVertexColorTests()
{
    // --- Pure evaluator: the blue trap --------------------------------
    // W == R == G on every corner with only blue differing must not be
    // reported Ready: blue never affects retail compaction.
    {
        const std::vector<DynamicVertexColorSample> samples(
            4, DynamicVertexColorSample{ 1.0f, 1.0f, 1.0f, 0.25f });
        const DynamicVertexColorEvaluation result = evaluateDynamicVertexColors(samples, 4);
        check(result.readiness == VertexColorReadiness::NotReady
                && result.retail == VertexRgbStorage::Compacted
                && result.openMots == VertexRgbStorage::Retained
                && !result.distinguishingCorner.has_value(),
            "vertex-colors: blue-only difference stays NotReady (retail Compacted)");

        std::vector<DynamicVertexColorSample> varyingBlue = samples;
        varyingBlue[1].blue = 0.0f;
        varyingBlue[3].blue = 5.0f;
        check(evaluateDynamicVertexColors(varyingBlue, 4).readiness
                == VertexColorReadiness::NotReady,
            "vertex-colors: per-corner blue variation still stays NotReady");
    }

    // --- Pure evaluator: one distinguishing corner --------------------
    {
        const std::vector<DynamicVertexColorSample> samples{
            { 1.0f, 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f },
            { 1.0f, 0.5f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } };
        const DynamicVertexColorEvaluation result = evaluateDynamicVertexColors(samples, 4);
        check(result.readiness == VertexColorReadiness::Ready
                && result.retail == VertexRgbStorage::Retained
                && result.distinguishingCorner == 2,
            "vertex-colors: one distinguishing corner keeps the whole surface Ready");
    }

    // --- Pure evaluator: grey <-> recolor cycle -----------------------
    {
        std::vector<DynamicVertexColorSample> samples{
            { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.25f, 0.25f, 0.25f, 0.0f },
            { 0.5f, 0.5f, 0.5f, 1.0f }, { 1.0f, 1.0f, 1.0f, 0.0f } };
        check(evaluateDynamicVertexColors(samples, 4).readiness
                == VertexColorReadiness::NotReady,
            "vertex-colors: varying grey corners are NotReady");
        samples[3].red = 0.75f;
        const DynamicVertexColorEvaluation colored = evaluateDynamicVertexColors(samples, 4);
        check(colored.readiness == VertexColorReadiness::Ready
                && colored.distinguishingCorner == 3,
            "vertex-colors: recoloring only the last corner turns Ready");
        samples[3].red = 1.0f;
        check(evaluateDynamicVertexColors(samples, 4).readiness
                == VertexColorReadiness::NotReady,
            "vertex-colors: returning to grey turns NotReady again");
    }

    // --- Pure evaluator: channels, exactness, no clamping -------------
    {
        struct Case
        {
            DynamicVertexColorSample sample;
            VertexColorReadiness expected;
            const char* message;
        };
        const Case cases[]{
            { { 1.0f, 1.0f, 1.0f, 1.0f }, VertexColorReadiness::NotReady,
              "vertex-colors: uniform white is NotReady" },
            { { 0.5f, 1.0f, 1.0f, 1.0f }, VertexColorReadiness::Ready,
              "vertex-colors: white-only difference is Ready" },
            { { 1.0f, 1.0f, 0.5f, 1.0f }, VertexColorReadiness::Ready,
              "vertex-colors: green difference is Ready" },
            { { 2.0f, 2.0f, 2.0f, 5.0f }, VertexColorReadiness::NotReady,
              "vertex-colors: HDR grey above one is not clamped into a difference" },
            { { 2.0f, 1.0f, 1.0f, 1.0f }, VertexColorReadiness::Ready,
              "vertex-colors: HDR white difference is Ready without clamping" },
            { { -1.0f, 0.0f, 0.0f, 0.0f }, VertexColorReadiness::Ready,
              "vertex-colors: negative white difference is Ready" },
            { { 0.0f, -0.0f, 0.0f, 1.0f }, VertexColorReadiness::NotReady,
              "vertex-colors: signed zero compares exactly equal" },
        };
        for (const Case& entry : cases)
        {
            const std::vector<DynamicVertexColorSample> samples(4, entry.sample);
            check(evaluateDynamicVertexColors(samples, 4).readiness == entry.expected,
                entry.message);
        }
        check(std::string(vertexColorReadinessName(VertexColorReadiness::Ready)) == "Ready"
                && std::string(vertexColorReadinessName(VertexColorReadiness::NotReady)) == "NotReady"
                && std::string(vertexColorReadinessName(VertexColorReadiness::Unknown)) == "Unknown",
            "vertex-colors: readiness names are Ready/NotReady/Unknown");
    }

    // --- Pure evaluator: scalar rows are a distinct state -------------
    {
        const std::vector<DynamicVertexColorSample> samples(
            4, DynamicVertexColorSample{ 0.5f, 0.5f, 0.5f, 0.5f });
        const DynamicVertexColorEvaluation result = evaluateDynamicVertexColors(samples, 1);
        check(result.retail == VertexRgbStorage::Unknown
                && result.openMots == VertexRgbStorage::Scalar
                && result.readiness == VertexColorReadiness::Unknown,
            "vertex-colors: scalar row keeps retail Unknown and OpenMotS Scalar");
    }

    // --- Pure evaluator: invalid shapes and non-finite values ---------
    {
        const std::vector<DynamicVertexColorSample> grey(
            4, DynamicVertexColorSample{ 1.0f, 1.0f, 1.0f, 1.0f });
        check(evaluateDynamicVertexColors(
                std::vector<DynamicVertexColorSample>(2, grey.front()), 4).retail
                == VertexRgbStorage::Invalid,
            "vertex-colors: two corners are Invalid");
        check(evaluateDynamicVertexColors(
                std::vector<DynamicVertexColorSample>(25, grey.front()), 4).retail
                == VertexRgbStorage::Invalid,
            "vertex-colors: twenty-five corners are Invalid");
        check(evaluateDynamicVertexColors(grey, 3).retail == VertexRgbStorage::Invalid,
            "vertex-colors: three components per corner are Invalid");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        for (const DynamicVertexColorSample bad : {
                 DynamicVertexColorSample{ nan, 1.0f, 1.0f, 1.0f },
                 DynamicVertexColorSample{ 1.0f, 1.0f, 1.0f, inf },
                 DynamicVertexColorSample{ 1.0f, 1.0f, -inf, 1.0f } })
        {
            const std::vector<DynamicVertexColorSample> samples(3, bad);
            const DynamicVertexColorEvaluation result = evaluateDynamicVertexColors(samples, 4);
            check(result.retail == VertexRgbStorage::Invalid
                    && result.readiness == VertexColorReadiness::Unknown,
                "vertex-colors: non-finite samples (blue included) are Invalid/Unknown");
        }
    }

    // --- Serialized bytes: assessed rows, not in-memory colors --------
    {
        const std::string text = singleRowDocument("1 1 1 0.25");
        const DynamicVertexColorReport report = evaluateDynamicVertexColorsSerialized(text, true);
        check(report.status == "assessed" && report.surfaces.size() == 1
                && report.surfaces[0].readiness == VertexColorReadiness::NotReady
                && report.surfaces[0].layout == "4N",
            "vertex-colors: serialized blue-only row is assessed NotReady from bytes");
        check(report.proposedOutputSha256 == sed::assets::jklSourceSha256Hex(text),
            "vertex-colors: report hash binds the exact assessed bytes");

        // Grey corners that vary per corner stay NotReady; recoloring
        // only the last corner of the second surface turns that surface
        // Ready while its neighbour stays NotReady.
        const std::string grey = documentWithRows({
            surfaceRow(0, 4, "0 0 0 1 .25 .25 .25 0 .5 .5 .5 1 1 1 1 0"),
            surfaceRow(1, 4, repeatCorner("1 1 1 1", 4)) });
        const DynamicVertexColorReport greyReport = evaluateDynamicVertexColorsSerialized(grey, true);
        check(greyReport.status == "assessed"
                && countVertexColorReadiness(greyReport, VertexColorReadiness::NotReady) == 2,
            "vertex-colors: serialized varying grey rows are NotReady per surface");
        const std::string colored = documentWithRows({
            surfaceRow(0, 4, "0 0 0 1 .25 .25 .25 0 .5 .5 .5 1 1 1 1 0"),
            surfaceRow(1, 4, "1 1 1 1 1 1 1 1 1 1 1 1 1 0.75 1 0") });
        const DynamicVertexColorReport coloredReport = evaluateDynamicVertexColorsSerialized(colored, true);
        check(coloredReport.status == "assessed" && coloredReport.surfaces.size() == 2
                && coloredReport.surfaces[0].readiness == VertexColorReadiness::NotReady
                && coloredReport.surfaces[1].readiness == VertexColorReadiness::Ready
                && coloredReport.surfaces[1].distinguishingCorner == 3,
            "vertex-colors: serialized last-corner recolor is Ready, neighbour unchanged");

        // Assessment is read-only and repeatable over the same bytes.
        const DynamicVertexColorReport again = evaluateDynamicVertexColorsSerialized(colored, true);
        check(again.surfaces.size() == coloredReport.surfaces.size()
                && again.proposedOutputSha256 == coloredReport.proposedOutputSha256,
            "vertex-colors: repeated assessment of the same bytes is stable");
    }

    // --- Serialized bytes: float32 conversion boundaries --------------
    {
        struct Case
        {
            const char* corner;
            VertexColorReadiness expected;
            const char* message;
        };
        const Case cases[]{
            { "1 1.00000001 1 1", VertexColorReadiness::NotReady,
              "vertex-colors: decimal difference below float32 resolution collapses" },
            { "1 1.0000001192092896 1 1", VertexColorReadiness::Ready,
              "vertex-colors: first representable float32 step above one is Ready" },
            { "1 1.000000059604644775390625000001 1 1", VertexColorReadiness::Unknown,
              "vertex-colors: double-rounding boundary keeps retail Unknown" },
            { "1 1.000000059604644775390625 1 1", VertexColorReadiness::NotReady,
              "vertex-colors: exact float32 midpoint ties to grey" },
            { "1 1 1 1.000000059604644775390625000001", VertexColorReadiness::NotReady,
              "vertex-colors: blue conversion edge never promotes a row" },
            { "0 -0 1e-60 1", VertexColorReadiness::NotReady,
              "vertex-colors: underflow to zero stays grey" },
            { "3e38 3e38 3e38 0", VertexColorReadiness::NotReady,
              "vertex-colors: large finite grey stays NotReady without clamping" },
        };
        for (const Case& entry : cases)
        {
            const DynamicVertexColorReport report = evaluateDynamicVertexColorsSerialized(
                singleRowDocument(entry.corner), true);
            check(report.status == "assessed" && report.surfaces.size() == 1
                    && report.surfaces[0].readiness == entry.expected,
                entry.message);
        }
        const DynamicVertexColorReport unknown = evaluateDynamicVertexColorsSerialized(
            singleRowDocument("1 1.000000059604644775390625000001 1 1"), true);
        check(unknown.surfaces.size() == 1
                && unknown.surfaces[0].retail == VertexRgbStorage::Unknown
                && unknown.surfaces[0].openMots == VertexRgbStorage::Retained
                && !unknown.surfaces[0].distinguishingCorner.has_value(),
            "vertex-colors: conversion disagreement yields Unknown with no distinguishing corner");
    }

    // --- Serialized bytes: retained scalar rows stay distinct ---------
    {
        const std::string mixed = documentWithRows({
            surfaceRow(0, 4, "0.5 0.5 0.5 0.5"),
            surfaceRow(1, 4, repeatCorner("1 1 1 1", 4)) });
        const DynamicVertexColorReport report = evaluateDynamicVertexColorsSerialized(mixed, true);
        check(report.status == "assessed" && report.surfaces.size() == 2
                && report.surfaces[0].layout == "N"
                && report.surfaces[0].retail == VertexRgbStorage::Unknown
                && report.surfaces[0].openMots == VertexRgbStorage::Scalar
                && report.surfaces[0].readiness == VertexColorReadiness::Unknown
                && report.surfaces[1].layout == "4N"
                && report.surfaces[1].readiness == VertexColorReadiness::NotReady,
            "vertex-colors: retained scalar row is distinct from regenerated 4N rows");
        check(countVertexRgbStorage(report, VertexRgbStorage::Scalar, false) == 1
                && countVertexColorReadiness(report, VertexColorReadiness::Unknown) == 1,
            "vertex-colors: scalar state is counted separately, never merged into grey");
    }

    // --- Serialized bytes: refusal edges keep the output identity -----
    {
        for (const char* corner : { "NaN 1 1 1", "1 1 1 Infinity", "1e40 1 1 1", "1 1" })
        {
            const std::string text = singleRowDocument(corner);
            const DynamicVertexColorReport report = evaluateDynamicVertexColorsSerialized(text, true);
            check(report.status == "unavailable" && report.surfaces.empty()
                    && report.proposedOutputSha256 == sed::assets::jklSourceSha256Hex(text),
                "vertex-colors: non-finite or malformed rows are unavailable with exact identity");
        }
        const DynamicVertexColorReport missing = evaluateDynamicVertexColorsSerialized(
            "SECTION: HEADER\nVERSION 1\nEND\n", true);
        check(missing.status == "unavailable" && missing.surfaces.empty(),
            "vertex-colors: missing GEORESOURCE table is unavailable, never Ready");

        const std::string text = singleRowDocument("1 1 1 1");
        const DynamicVertexColorReport otherGame = evaluateDynamicVertexColorsSerialized(text, false);
        check(otherGame.status == "unsupported-game" && otherGame.surfaces.empty()
                && otherGame.proposedOutputSha256 == sed::assets::jklSourceSha256Hex(text),
            "vertex-colors: non-MotS bytes are unsupported-game, never guessed Ready");
    }

    // --- Level entry point: proposed export bytes are assessed --------
    {
        sed::Level level{};
        level.kind = sed::ProjectType::MOTS;
        level.header.version = 1;
        level.sectors.push_back(makeLitTestCubeSector(0.0, sed::ColorWhite));

        const sed::assets::JklBuiltExport built = sed::assets::buildJklExport(level);
        const DynamicVertexColorReport grey = evaluateDynamicVertexColors(level);
        check(grey.status == "assessed" && grey.surfaces.size() == 6
                && grey.proposedOutputSha256 == sed::assets::jklSourceSha256Hex(built.text)
                && countVertexColorReadiness(grey, VertexColorReadiness::NotReady) == 6,
            "vertex-colors: MotS level export bytes assess as grey NotReady with plan hash");
        check(grey.surfaces.size() == 6 && grey.surfaces[0].sectorIndex == 0
                && grey.surfaces[0].surfaceIndex == 0 && grey.surfaces[0].layout == "4N",
            "vertex-colors: level report carries writer-order surface addresses");

        // White lives in the alpha channel on the wire: separating it
        // from red on one corner must flip exactly that surface.
        level.sectors[0].surfaces[0].texVertices[0].color = sed::ColorF{ 1.0f, 1.0f, 1.0f, 0.5f };
        const sed::ColorF before = level.sectors[0].surfaces[0].texVertices[0].color;
        const DynamicVertexColorReport coloredReport = evaluateDynamicVertexColors(level);
        check(coloredReport.status == "assessed" && coloredReport.surfaces.size() == 6
                && coloredReport.surfaces[0].readiness == VertexColorReadiness::Ready
                && coloredReport.surfaces[0].distinguishingCorner == 0
                && countVertexColorReadiness(coloredReport, VertexColorReadiness::Ready) == 1,
            "vertex-colors: one recolored corner flips one surface to Ready");
        check(level.sectors[0].surfaces[0].texVertices[0].color == before,
            "vertex-colors: level assessment changes no modeled values");

        sed::Level jkLevel{};
        jkLevel.kind = sed::ProjectType::JKDF2;
        check(evaluateDynamicVertexColors(jkLevel).status == "unsupported-game",
            "vertex-colors: JK level is unsupported-game, never Ready");
    }
}

} // namespace sed::tests
