#include "test_common.hpp"

#include "render/mesh/render_lighting_utils.hpp"
#include "ui/cog/cog_gui_wiring_utils.hpp"
#include "ui/cog/cog_script_language_utils.hpp"

namespace sed::app_tests
{

void runGuiWiringUtilsTests()
{
    using namespace sed::ui::cog;

    // Completion correction: non-working statements are recognised, and
    // the built completion list no longer presents them.
    check(cogCompletionWordIsNonWorkingStatement("break"),
        "break is a non-working statement for completion");
    check(cogCompletionWordIsNonWorkingStatement("Continue"),
        "continue matching folds case");
    check(cogCompletionWordIsNonWorkingStatement("switch"),
        "switch is a non-working statement for completion");
    check(!cogCompletionWordIsNonWorkingStatement("while"),
        "while stays a working statement");
    const QStringList completion = buildCogScriptCompletionWords(
        QStringLiteral("symbols\nend\ncode\n"), QStringList{});
    check(!completion.contains(QStringLiteral("break")),
        "completion omits break");
    check(!completion.contains(QStringLiteral("continue")),
        "completion omits continue");
    check(!completion.contains(QStringLiteral("switch")),
        "completion omits switch");
    check(completion.contains(QStringLiteral("while")),
        "completion keeps while");
    check(cogScriptKeywordWords().contains(QStringLiteral("break")),
        "keyword list keeps break for highlighting/diagnostics");

    // Positional fallback: a missing schema never yields typed rows.
    level::CogBindingDescription missing{};
    missing.scriptName = "gone.cog";
    missing.scriptStatus = level::CogScriptStatus::missing;
    check(placedCogRowMode(missing) == PlacedCogRowMode::positionalFallback,
        "missing schema stays positional");
    check(placedCogFallbackNote(missing).find(
              "editing values positionally without guessed types") == 0,
        "fallback note carries the exact positional label");

    // Typed rows require a complete schema with an exact count.
    level::CogBindingDescription typed{};
    typed.scriptName = "door.cog";
    typed.scriptStatus = level::CogScriptStatus::complete;
    typed.schemaComplete = true;
    typed.parameterCountMatches = true;
    check(placedCogRowMode(typed) == PlacedCogRowMode::typed,
        "complete exact schema yields typed rows");
    check(placedCogFallbackNote(typed).empty(),
        "typed mode has no fallback note");

    // Preflight lines always lead with the not-a-compiler qualifier, and
    // IJIM reports target-unmeasured instead of a substituted table.
    const assets::CogPreflightResult ijim = assets::runCogPreflight(
        "x.cog", "symbols\nend\ncode\n", assets::CogPreflightTarget::ijim, {});
    const std::vector<std::string> ijimLines = formatCogPreflightLines(ijim);
    check(!ijimLines.empty()
            && ijimLines.front().find("not-a-compiler") != std::string::npos,
        "preflight lines carry the not-a-compiler qualifier");
    bool sawUnmeasured = false;
    for (const std::string& line : ijimLines)
    {
        sawUnmeasured = sawUnmeasured
            || line.find("target-unmeasured") != std::string::npos;
    }
    check(sawUnmeasured, "IJIM preflight reports target-unmeasured");
    check(cogPreflightTargetForProject(ProjectType::IJIM)
            == assets::CogPreflightTarget::ijim,
        "IJIM projects map to the unmeasured target");
    check(cogPreflightTargetForProject(ProjectType::MOTS)
            == assets::CogPreflightTarget::mots,
        "MotS projects map to the mots registry");

    // Refusals display the stable code first.
    check(formatCogMutationRefusal({ "invalidCogTarget", "bad target" })
            == "invalidCogTarget: bad target",
        "refusal text leads with the stable code");

    // F-03(a): white and RGB channels stay independent; absent RGB
    // inherits white, explicit zero stays zero.
    SedLight inherit{};
    inherit.intensity = 4.0;
    inherit.color = ColorF{ 1.0f, 0.5f, 0.0f, 1.0f };
    const render::SourceLightChannels inherited =
        render::sourceLightChannels(inherit);
    check(!inherited.rgbExplicit && inherited.white == 4.0f
            && inherited.rgb.r == 4.0f && inherited.rgb.g == 2.0f,
        "absent RGB inherits the white intensity");
    SedLight explicitZero = inherit;
    explicitZero.rgbIntensity = 0.0;
    const render::SourceLightChannels zeroed =
        render::sourceLightChannels(explicitZero);
    check(zeroed.rgbExplicit && zeroed.rgb.r == 0.0f && zeroed.white == 4.0f,
        "explicit zero RGB stays zero while white is unchanged");
}

} // namespace sed::app_tests
