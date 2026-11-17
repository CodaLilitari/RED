#include "test_common.hpp"

#include "app_settings.hpp"
#include "main_window/main_window.hpp"
#include "sed/level.hpp"
#include "sed/level/geometry.hpp"
#include "ui/lighting/bake_options_dialog.hpp"

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace sed::app_tests
{
namespace
{

using ui::lighting::BakeOptions;
using ui::lighting::BakeOptionsDialog;

constexpr std::array kKeys{
    "lighting/emissive_intensity", "lighting/emissive_range", "lighting/bounce_strength", "lighting/bounce_range",
    "lighting/sky_intensity", "lighting/sky_range", "lighting/sky_color", "lighting/angle_shading",
    "lighting/water_absorption" };

BakeOptions sample()
{
    BakeOptions options{};
    options.emissiveIntensity = 2.5;
    options.emissiveRange = 4.0;
    options.bounceStrength = 0.5;
    options.bounceRange = 3.0;
    options.skyIntensity = 1.25;
    options.skyRange = 6.0;
    options.skyColor = QColor(QStringLiteral("#8090a0"));
    options.angleShading = true;
    options.waterAbsorption = true;
    return options;
}

// Keeps the nine bake option keys as they were before a test changed them.
class SavedBakeKeys
{
public:
    SavedBakeKeys()
    {
        QSettings store = settingsStore();
        for ( std::size_t i = 0; i < kKeys.size(); ++i )
        {
            saved_[i] = { store.contains(kKeys[i]), store.value(kKeys[i]) };
            store.remove(kKeys[i]);
        }
        store.sync();
    }
    ~SavedBakeKeys()
    {
        QSettings store = settingsStore();
        for ( std::size_t i = 0; i < kKeys.size(); ++i )
        {
            if ( saved_[i].first ) store.setValue(kKeys[i], saved_[i].second);
            else store.remove(kKeys[i]);
        }
        store.sync();
    }
    SavedBakeKeys(const SavedBakeKeys&) = delete;
    SavedBakeKeys& operator=(const SavedBakeKeys&) = delete;

private:
    std::array<std::pair<bool, QVariant>, kKeys.size()> saved_{};
};

// A closed 4 JKU box with one Baked light off its centre, so walls meet the
// light at many angles.
editor::DocumentState litBox()
{
    editor::DocumentState state{};
    state.level.kind = ProjectType::JKDF2;
    state.level.layers.emplace_back("Layer0");
    Sector& sector = state.level.newSector();
    sector.layer = 0;
    sector.vertices = {
        Vertex{ -2.0, -2.0, -2.0, 0 }, Vertex{ 2.0, -2.0, -2.0, 0 }, Vertex{ 2.0, 2.0, -2.0, 0 },
        Vertex{ -2.0, 2.0, -2.0, 0 }, Vertex{ -2.0, -2.0, 2.0, 0 }, Vertex{ 2.0, -2.0, 2.0, 0 },
        Vertex{ 2.0, 2.0, 2.0, 0 }, Vertex{ -2.0, 2.0, 2.0, 0 },
    };
    constexpr std::array<std::array<std::int32_t, 4>, 6> faces{ {
        { { 1, 5, 6, 2 } }, { { 0, 3, 7, 4 } }, { { 3, 2, 6, 7 } },
        { { 0, 4, 5, 1 } }, { { 4, 7, 6, 5 } }, { { 0, 1, 2, 3 } },
    } };
    for ( const auto& face : faces )
    {
        Surface& surface = sector.surfaces.emplace_back();
        surface.vertices.assign(face.begin(), face.end());
        surface.texVertices.resize(face.size());
        surface.normal = level::surfaceCalcNormalNewell(sector, surface);
    }
    SedLight& light = state.level.newLight();
    light.layer = 0;
    light.position = TVector{ 1.2, 0.5, 0.3 };
    light.intensity = 0.5;
    light.range = 6.0;
    return state;
}

// Runs Recalc. All on the lit box and returns every channel of every baked
// vertex color.
std::vector<float> bakeLitBox(MainWindow& window)
{
    window.setDocumentStateForTest(litBox());
    // The strip's Recalculate is enabled for a level with sectors in Baked light mode.
    window.activateBakedLightEditModeForTest();
    auto* recalculate = window.findChild<QAction*>(QStringLiteral("lightBakeRecalculateAllAction"));
    if ( !recalculate ) return {};
    recalculate->trigger();
    QElapsedTimer timer{};
    timer.start();
    while ( window.lightBakeRunningForTest() && timer.elapsed() < 10000 )
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    if ( window.lightBakeRunningForTest() ) return {};
    std::vector<float> lit{};
    for ( const Surface& surface : window.documentStateForTest().level.sectors.at(0).surfaces )
    {
        for ( const TexVertex& vertex : surface.texVertices )
        {
            lit.insert(lit.end(), { vertex.color.r, vertex.color.g, vertex.color.b, vertex.color.a });
        }
    }
    return lit;
}

void settingsRoundTrip()
{
    QTemporaryDir dir;
    check(dir.isValid(), "bake options: a temporary settings folder exists");
    QSettings settings(dir.filePath(QStringLiteral("red.ini")), QSettings::IniFormat);
    const BakeOptions defaults = ui::lighting::readBakeOptions(settings);
    check(defaults == ui::lighting::defaultBakeOptions() && defaults.emissiveIntensity == 1.0
            && defaults.emissiveRange == 10.0 && defaults.bounceStrength == 0.0 && defaults.skyIntensity == 0.0
            && defaults.skyColor == QColor(Qt::white) && !defaults.angleShading && !defaults.waterAbsorption,
        "bake options: an empty red.ini reads as the bake's own defaults");
    ui::lighting::writeBakeOptions(settings, sample());
    check(ui::lighting::readBakeOptions(settings) == sample(), "bake options: written options read back unchanged");
    check(settings.value(QStringLiteral("lighting/sky_color")).toString() == QStringLiteral("#8090a0")
            && settings.value(QStringLiteral("lighting/angle_shading")).toBool()
            && settings.value(QStringLiteral("lighting/bounce_strength")).toDouble() == 0.5,
        "bake options: the keys are the ones the light bake reads, sky color as #rrggbb");
    settings.setValue(QStringLiteral("lighting/sky_color"), QStringLiteral("not a color"));
    settings.setValue(QStringLiteral("lighting/emissive_range"), QStringLiteral("nan"));
    const BakeOptions repaired = ui::lighting::readBakeOptions(settings);
    check(repaired.skyColor == QColor(Qt::white) && repaired.emissiveRange == 10.0,
        "bake options: an unreadable color or number falls back to its default");
}

void dialogEditsACopy()
{
    BakeOptionsDialog dialog(sample());
    check(dialog.options() == sample(), "bake options dialog: shows the options it was opened with");
    auto* bounce = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("bakeOptions.bounceStrength"));
    auto* water = dialog.findChild<QCheckBox*>(QStringLiteral("bakeOptions.waterAbsorption"));
    check(bounce && water, "bake options dialog: exposes the bounce and water controls");
    if ( bounce && water )
    {
        bounce->setValue(0.0);
        water->setChecked(false);
        const BakeOptions edited = dialog.options();
        check(edited.bounceStrength == 0.0 && !edited.waterAbsorption && edited.angleShading
                && edited.skyColor == sample().skyColor,
            "bake options dialog: options() reflects the edits and keeps the rest");
        check(bounce->text() == QStringLiteral("Off"), "bake options dialog: a zero bounce reads Off");
    }
    auto* buttons = dialog.findChild<QDialogButtonBox*>(QStringLiteral("bakeOptions.buttons"));
    QPushButton* restore = buttons ? buttons->button(QDialogButtonBox::RestoreDefaults) : nullptr;
    check(restore != nullptr, "bake options dialog: offers Restore Defaults");
    if ( restore )
    {
        restore->click();
        check(dialog.options() == ui::lighting::defaultBakeOptions(),
            "bake options dialog: Restore Defaults returns every control to the bake's defaults");
    }
}

void stripOpensTheDialog()
{
    const SavedBakeKeys keep{};
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    auto* button = window.findChild<QToolButton*>(QStringLiteral("lightBakeOptionsButton"));
    check(button != nullptr, "light bake strip: has an Options button");
    if ( !button ) return;
    button->click();
    QPointer<QDialog> dialog = window.findChild<QDialog*>(QStringLiteral("bakeOptionsDialog"));
    check(!dialog.isNull(), "light bake strip: Options opens Bake Options");
    button->click();
    check(window.findChildren<QDialog*>(QStringLiteral("bakeOptionsDialog")).size() == 1,
        "light bake strip: a second click does not open a second dialog");
    auto* angle = dialog ? dialog->findChild<QCheckBox*>(QStringLiteral("bakeOptions.angleShading")) : nullptr;
    check(angle && !angle->isChecked(), "Bake Options: starts from red.ini (angle shading off)");
    if ( dialog && angle )
    {
        angle->setChecked(true);
        dialog->accept();
        QSettings after = settingsStore();
        check(after.value(QStringLiteral("lighting/angle_shading")).toBool()
                && after.value(QStringLiteral("lighting/emissive_range")).toDouble() == 10.0,
            "Bake Options: OK writes the options to red.ini for the next bake");
    }
}

void bakeFollowsTheOptions()
{
    const SavedBakeKeys keep{};
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    const std::vector<float> plain = bakeLitBox(window);
    check(plain.size() == 96, "Bake Options: Recalc. All bakes the test box");

    QSettings store = settingsStore();
    BakeOptions options = ui::lighting::readBakeOptions(store);
    options.angleShading = true;
    ui::lighting::writeBakeOptions(store, options);
    const std::vector<float> shaded = bakeLitBox(window);
    bool darker = false;
    bool brighter = false;
    for ( std::size_t i = 0; i < plain.size() && i < shaded.size(); ++i )
    {
        darker = darker || shaded[i] < plain[i] - 1e-4f;
        brighter = brighter || shaded[i] > plain[i] + 1e-4f;
    }
    check(shaded.size() == plain.size() && darker && !brighter,
        "Bake Options: angle shading from the dialog's settings darkens the next bake and brightens nothing");

    options.angleShading = false;
    ui::lighting::writeBakeOptions(store, options);
    check(bakeLitBox(window) == plain, "Bake Options: turning it off again bakes as before");
}

} // namespace

void runBakeOptionsDialogTests()
{
    settingsRoundTrip();
    dialogEditsACopy();
    stripOpensTheDialog();
    bakeFollowsTheOptions();
}

} // namespace sed::app_tests
