#include "test_common.hpp"

#include "main_window/main_window.hpp"
#include "ui/components/light_parameters_dialog.hpp"
#include "ui/components/scrub_float_edit.hpp"
#include "ui/lighting/light_bake_utils.hpp"

#include <QCheckBox>
#include <QPointer>
#include <QToolButton>

#include <limits>
#include <array>
#include <memory>
#include <optional>

namespace sed::app_tests
{
namespace
{

editor::DocumentState sourceState(const ProjectType kind = ProjectType::MOTS)
{
    editor::DocumentState state{};
    state.level.kind = kind;
    state.level.layers = { "Source lights" };
    SedLight source{};
    source.intensity = 4.0;
    source.range = 6.0;
    source.rgbIntensity = 0.0;
    source.legacyRgbRange = -2.0;
    state.level.lights.push_back(source);
    source.id = 1;
    source.rgbIntensity.reset();
    source.legacyRgbRange = 12.0;
    state.level.lights.push_back(source);
    state.selection.lights.insert(0);
    state.selection.lights.insert(1);
    return state;
}

std::unique_ptr<MainWindow> sourceWindow(editor::DocumentState state)
{
    auto window = std::make_unique<MainWindow>();
    window->setAttribute(Qt::WA_DontShowOnScreen);
    window->setDocumentStateForTest(std::move(state));
    return window;
}

QPointer<ui::LightParametersDialog> openSourcePopup(MainWindow& window)
{
    auto* button = window.findChild<QToolButton*>(QStringLiteral("prop.baked_light.editor"));
    check(button != nullptr, "baked RGB fixture exposes the existing source popup action");
    if ( button )
    {
        button->click();
    }
    return dynamic_cast<ui::LightParametersDialog*>(
        window.findChild<QWidget*>(QStringLiteral("lightParametersPopup")));
}

void setPopupValue(ui::ScrubFloatEdit* editor, const double value)
{
    editor->setValue(value);
    editor->valueCommitted(value);
}

void popupNullableChannels()
{
    auto window = sourceWindow(sourceState());
    const auto depth = window->undoDepthForTest();
    auto popup = openSourcePopup(*window);
    auto* follows = popup ? popup->findChild<QCheckBox*>(QStringLiteral("lightParameters.rgbFollowsWhite")) : nullptr;
    auto* rgb = popup ? popup->findChild<ui::ScrubFloatEdit*>(QStringLiteral("lightParameters.rgbIntensity")) : nullptr;
    auto* retained = popup ? popup->findChild<QCheckBox*>(QStringLiteral("lightParameters.retainLegacyRgbRange")) : nullptr;
    auto* legacy = popup ? popup->findChild<ui::ScrubFloatEdit*>(QStringLiteral("lightParameters.legacyRgbRange")) : nullptr;
    check(follows && rgb && retained && legacy,
        "MotS baked popup exposes nullable RGB intensity and retained legacy range controls");
    if ( !follows || !rgb || !retained || !legacy )
    {
        if ( popup )
        {
            popup->close();
        }
        return;
    }
    check(!follows->isChecked() && rgb->isEnabled() && rgb->value() == 0.0
            && retained->isChecked() && legacy->value() == -2.0
            && window->undoDepthForTest() == depth,
        "opening baked popup preserves explicit zero and signed metadata without history");
    auto* white = popup->findChild<ui::ScrubFloatEdit*>(QStringLiteral("lightParameters.intensity"));
    auto* range = popup->findChild<ui::ScrubFloatEdit*>(QStringLiteral("lightParameters.range"));
    check(white && range, "MotS baked popup retains white intensity and primary range");
    if ( !white || !range )
    {
        popup->close();
        return;
    }
    setPopupValue(white, 8.0);
    setPopupValue(range, 9.0);
    const auto& unrelated = window->documentStateForTest().level.lights;
    check(unrelated[0].rgbIntensity == 0.0 && !unrelated[1].rgbIntensity
            && unrelated[0].legacyRgbRange == -2.0 && unrelated[1].legacyRgbRange == 12.0,
        "unrelated popup edits retain each selected source's distinct optional metadata");
    setPopupValue(rgb, 0.75);
    setPopupValue(legacy, -100000.0);
    const auto& explicitValues = window->documentStateForTest().level.lights;
    check(explicitValues[0].rgbIntensity == 0.75 && explicitValues[1].rgbIntensity == 0.75
            && explicitValues[0].legacyRgbRange == -100000.0
            && explicitValues[1].legacyRgbRange == -100000.0
            && explicitValues[0].range == 9.0,
        "popup edits RGB and legacy metadata independently of primary falloff for all selected sources");
    rgb->lineEdit()->setText(QStringLiteral("1001"));
    rgb->lineEdit()->textEdited(QStringLiteral("1001"));
    setPopupValue(white, 8.0);
    check(window->documentStateForTest().level.lights[0].rgbIntensity == 0.75
            && window->documentStateForTest().level.lights[1].rgbIntensity == 0.75,
        "an out-of-range RGB draft cannot leak through an unrelated popup callback");
    rgb->setValue(0.75);
    follows->setChecked(true);
    retained->setChecked(false);
    const auto& inherited = window->documentStateForTest().level.lights;
    check(!inherited[0].rgbIntensity && !inherited[1].rgbIntensity
            && inherited[0].effectiveRgbIntensity() == 8.0
            && !inherited[0].legacyRgbRange && !inherited[1].legacyRgbRange
            && !rgb->isEnabled() && !legacy->isEnabled(),
        "popup can restore inheritance and remove legacy metadata explicitly");
    popup->close();
    check(window->undoDepthForTest() == depth + 1 && window->undoForTest(),
        "one live baked popup interaction remains exactly one undo transaction");
    const auto& restored = window->documentStateForTest().level.lights;
    check(restored[0].intensity == 4.0 && restored[0].range == 6.0
            && restored[0].rgbIntensity == 0.0 && !restored[1].rgbIntensity
            && restored[0].legacyRgbRange == -2.0 && restored[1].legacyRgbRange == 12.0,
        "popup undo restores absent versus explicit source fields exactly");
}

void propertyNullableChannels()
{
    auto window = sourceWindow(sourceState());
    auto rgbEditor = [&]()
    {
        return window->findChild<ui::ScrubFloatEdit*>(QStringLiteral("prop.baked_light.rgbIntensity"));
    };
    auto legacyEditor = [&]()
    {
        return window->findChild<ui::ScrubFloatEdit*>(QStringLiteral("prop.baked_light.legacyRgbRange"));
    };
    auto* follows = window->findChild<QCheckBox*>(QStringLiteral("prop.baked_light.rgbFollowsWhite"));
    auto* retained = window->findChild<QCheckBox*>(QStringLiteral("prop.baked_light.retainLegacyRgbRange"));
    check(follows && retained && rgbEditor() && legacyEditor(),
        "MotS source property panel exposes both nullable source fields");
    if ( !follows || !retained || !rgbEditor() || !legacyEditor() )
    {
        return;
    }
    const auto depth = window->undoDepthForTest();
    rgbEditor()->valueCommitted(std::numeric_limits<double>::quiet_NaN());
    check(window->undoDepthForTest() == depth
            && window->documentStateForTest().level.lights[0].rgbIntensity == 0.0,
        "invalid RGB property commit leaves model and undo untouched");
    rgbEditor()->valueCommitted(1000.0);
    check(window->documentStateForTest().level.lights[0].rgbIntensity == 1000.0
            && window->documentStateForTest().level.lights[1].rgbIntensity == 1000.0
            && window->undoDepthForTest() == depth + 1,
        "RGB source property supports the upper authoring boundary as one selected edit");
    rgbEditor()->valueCommitted(0.0);
    check(window->documentStateForTest().level.lights[0].rgbIntensity == 0.0
            && window->documentStateForTest().level.lights[1].rgbIntensity == 0.0,
        "RGB property zero remains explicit instead of inheriting white");
    const auto scrubDepth = window->undoDepthForTest();
    rgbEditor()->scrubStarted(0.0);
    rgbEditor()->scrubValueChanged(0.25);
    rgbEditor()->scrubValueChanged(0.5);
    rgbEditor()->scrubFinished(0.5);
    check(window->undoDepthForTest() == scrubDepth + 1
            && window->documentStateForTest().level.lights[0].rgbIntensity == 0.5
            && window->documentStateForTest().level.lights[1].rgbIntensity == 0.5,
        "RGB property scrubbing groups live updates into one undo record");
    check(window->undoForTest()
            && window->documentStateForTest().level.lights[0].rgbIntensity == 0.0,
        "RGB property scrub undo restores explicit zero");
    window->rebuildPropertiesForTest();
    legacyEditor()->valueCommitted(0.0);
    check(window->documentStateForTest().level.lights[0].legacyRgbRange == 0.0
            && window->documentStateForTest().level.lights[1].legacyRgbRange == 0.0
            && window->documentStateForTest().level.lights[0].range == 6.0,
        "legacy range property preserves explicit zero without changing physical falloff");
    follows = window->findChild<QCheckBox*>(QStringLiteral("prop.baked_light.rgbFollowsWhite"));
    follows->setChecked(true);
    check(!window->documentStateForTest().level.lights[0].rgbIntensity
            && !window->documentStateForTest().level.lights[1].rgbIntensity,
        "property follows-white toggle restores null for the selected sources");
    check(window->undoForTest()
            && window->documentStateForTest().level.lights[0].rgbIntensity == 0.0
            && window->documentStateForTest().level.lights[1].rgbIntensity == 0.0,
        "property toggle undo restores explicit zero");
    window->rebuildPropertiesForTest();
    retained = window->findChild<QCheckBox*>(QStringLiteral("prop.baked_light.retainLegacyRgbRange"));
    retained->setChecked(false);
    check(!window->documentStateForTest().level.lights[0].legacyRgbRange
            && !window->documentStateForTest().level.lights[1].legacyRgbRange,
        "property retained-range toggle removes optional metadata");
}

void inheritedPopupAuthoring()
{
    auto state = sourceState();
    state.level.lights.resize(1);
    state.selection.lights = { 0 };
    state.level.lights[0].rgbIntensity.reset();
    state.level.lights[0].legacyRgbRange.reset();
    auto window = sourceWindow(std::move(state));
    auto popup = openSourcePopup(*window);
    auto* follows = popup ? popup->findChild<QCheckBox*>(QStringLiteral("lightParameters.rgbFollowsWhite")) : nullptr;
    auto* rgb = popup ? popup->findChild<ui::ScrubFloatEdit*>(QStringLiteral("lightParameters.rgbIntensity")) : nullptr;
    auto* white = popup ? popup->findChild<ui::ScrubFloatEdit*>(QStringLiteral("lightParameters.intensity")) : nullptr;
    check(follows && rgb && white, "inherited source exposes the MotS popup controls");
    if ( !follows || !rgb || !white )
    {
        if ( popup )
        {
            popup->close();
        }
        return;
    }
    const auto depth = window->undoDepthForTest();
    setPopupValue(white, 4.0);
    check(window->undoDepthForTest() == depth && follows->isChecked() && !rgb->isEnabled(),
        "unchanged inherited popup values do not create history");
    setPopupValue(white, 6.0);
    check(!window->documentStateForTest().level.lights[0].rgbIntensity && rgb->value() == 6.0,
        "follow-white RGB display tracks white edits without authoring an override");
    follows->setChecked(false);
    setPopupValue(white, 8.0);
    check(window->documentStateForTest().level.lights[0].rgbIntensity == 6.0,
        "unchecking follow-white authors the displayed value and retains it through later white edits");
    popup->close();
}

void rgbOnlyEditsPreserveSelectedPrimaryFields()
{
    for ( const auto* field : { "lightParameters.rgbIntensity", "lightParameters.legacyRgbRange" } )
    {
        auto state = sourceState();
        state.level.lights[0].color = ColorF{ 1.0f, 0.0f, 0.0f, 1.0f };
        state.level.lights[1].color = ColorF{ 0.0f, 0.0f, 1.0f, 1.0f };
        state.level.lights[1].intensity = 7.0;
        state.level.lights[1].range = 10.0;
        const auto before = state.level.lights;
        auto window = sourceWindow(std::move(state));
        auto popup = openSourcePopup(*window);
        auto* editor = popup ? popup->findChild<ui::ScrubFloatEdit*>(QString::fromLatin1(field)) : nullptr;
        check(editor != nullptr, "mixed-primary source fixture exposes the optional field");
        if ( editor )
        {
            setPopupValue(editor, 0.5);
            const auto& after = window->documentStateForTest().level.lights;
            for ( std::size_t i = 0; i < before.size(); ++i )
            {
                check(after[i].intensity == before[i].intensity && after[i].range == before[i].range
                        && after[i].color.r == before[i].color.r && after[i].color.g == before[i].color.g
                        && after[i].color.b == before[i].color.b,
                    "an optional-source-only popup edit preserves each selected light's untouched primary fields");
            }
        }
        if ( popup )
        {
            popup->close();
        }
    }
}

void projectGatingAndImportedPreservation()
{
    for ( const auto kind : { ProjectType::JKDF2, ProjectType::IJIM } )
    {
        auto window = sourceWindow(sourceState(kind));
        check(!window->findChild<QCheckBox*>(QStringLiteral("prop.baked_light.rgbFollowsWhite"))
                && !window->findChild<ui::ScrubFloatEdit*>(QStringLiteral("prop.baked_light.legacyRgbRange")),
            "independent source RGB property authoring is confined to MotS");
        auto popup = openSourcePopup(*window);
        check(popup && !popup->findChild<QCheckBox*>(QStringLiteral("lightParameters.rgbFollowsWhite"))
                && !popup->findChild<ui::ScrubFloatEdit*>(QStringLiteral("lightParameters.legacyRgbRange")),
            "independent source RGB popup authoring is confined to MotS");
        if ( popup )
        {
            auto* range = popup->findChild<ui::ScrubFloatEdit*>(QStringLiteral("lightParameters.range"));
            if ( range )
            {
                setPopupValue(range, 10.0);
            }
            popup->close();
        }
        check(window->documentStateForTest().level.lights[0].rgbIntensity == 0.0
                && window->documentStateForTest().level.lights[0].legacyRgbRange == -2.0,
            "non-MotS unrelated editing retains imported source metadata");
    }
    auto state = sourceState();
    state.level.lights[0].rgbIntensity = 2000.0;
    state.level.lights[0].legacyRgbRange = -200000.0;
    auto window = sourceWindow(std::move(state));
    auto popup = openSourcePopup(*window);
    if ( popup )
    {
        auto* range = popup->findChild<ui::ScrubFloatEdit*>(QStringLiteral("lightParameters.range"));
        if ( range )
        {
            setPopupValue(range, 10.0);
        }
        popup->close();
    }
    check(window->documentStateForTest().level.lights[0].rgbIntensity == 2000.0
            && window->documentStateForTest().level.lights[0].legacyRgbRange == -200000.0,
        "unrelated MotS popup edits do not clamp imported optional metadata to authoring bounds");
}

void rgbBakeTracking()
{
    using namespace ui::lighting;
    Level level{};
    level.kind = ProjectType::MOTS;
    level.lights.resize(1);
    level.lights[0].intensity = 4.0;
    level.lights[0].range = 6.0;
    level.lights[0].rgbIntensity.reset();
    const auto inherited = level.lights;
    const auto originalFingerprint = lightBakeSourceFingerprint(level);
    level.lights[0].rgbIntensity = 0.0;
    check(!lightBakeLightSourcesEqual(inherited, level.lights)
            && !lightBakeChangedInfluences(inherited, level.lights).empty(),
        "an RGB-only source edit schedules its affected automatic bake region");
    check(lightBakeSourceFingerprint(level) != originalFingerprint,
        "an RGB-only source edit invalidates an older world bake snapshot");
    level.lights[0].rgbIntensity = 4.0;
    check(!lightBakeLightSourcesEqual(inherited, level.lights)
            && lightBakeSourceFingerprint(level) != originalFingerprint,
        "bake tracking retains the distinction between inherited and explicit equal RGB intensity");
    const auto beforeMetadata = level.lights;
    const auto metadataFingerprint = lightBakeSourceFingerprint(level);
    level.lights[0].legacyRgbRange = 100000.0;
    check(lightBakeLightSourcesEqual(beforeMetadata, level.lights)
            && lightBakeSourceFingerprint(level) == metadataFingerprint,
        "legacy RGB range metadata alone does not invalidate a world bake");

    level.sectors.resize(1);
    level.sectors[0].vertices.push_back(Vertex{});
    level.lights[0].mark = 0;
    level.lights[0].position = {};
    level.lights[0].intensity = 0.0;
    level.lights[0].rgbIntensity = 0.5;
    const std::array<std::int32_t, 1> targets{ 0 };
    check(processedBakedLightCount(level, targets) == 1,
        "MotS completed-bake count includes an RGB-only source with white intensity zero");
    level.lights[0].intensity = 4.0;
    level.lights[0].rgbIntensity = 0.0;
    check(processedBakedLightCount(level, targets) == 0,
        "MotS completed-bake count excludes an explicitly disabled RGB source");
    level.lights[0].rgbIntensity = std::numeric_limits<double>::infinity();
    check(processedBakedLightCount(level, targets) == 0,
        "completed-bake count rejects a non-finite effective MotS RGB intensity");
    for ( const auto kind : { ProjectType::JKDF2, ProjectType::IJIM } )
    {
        level.kind = kind;
        level.lights[0].intensity = 0.0;
        level.lights[0].rgbIntensity = 0.5;
        check(processedBakedLightCount(level, targets) == 0,
            "non-MotS completed-bake count ignores imported MotS RGB overrides");
    }
}

} // namespace

void runBakedLightRgbControlsTests()
{
    popupNullableChannels();
    propertyNullableChannels();
    inheritedPopupAuthoring();
    rgbOnlyEditsPreserveSelectedPrimaryFields();
    projectGatingAndImportedPreservation();
    rgbBakeTracking();
}

} // namespace sed::app_tests
