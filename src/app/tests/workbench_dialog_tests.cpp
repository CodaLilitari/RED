#include "ui/query/workbench_dialog.hpp"
#include "sed/editor/document.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>

#include <cmath>
#include <iostream>

namespace
{
int failures = 0;
void check(bool condition, const char* message)
{
    if ( !condition )
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template<class T>
T* control(QWidget& dialog, const char* name)
{
    auto* value = dialog.findChild<T*>(QString::fromLatin1(name));
    check(value != nullptr, name);
    return value;
}

void runQueryTests()
{
    sed::Level level;
    for ( int i = 0; i < 205; ++i )
    {
        sed::Thing thing;
        thing.name = "row_" + std::to_string(i);
        thing.templateName = i % 2 == 0 ? "even" : "odd";
        level.things.push_back(std::move(thing));
    }
    sed::ui::query::WorkbenchDialog dialog(level, {}, {}, {});
    auto* run = control<QPushButton>(dialog, "workbenchQueryRun");
    auto* next = control<QPushButton>(dialog, "workbenchQueryNext");
    auto* previous = control<QPushButton>(dialog, "workbenchQueryPrevious");
    auto* output = control<QPlainTextEdit>(dialog, "workbenchQueryOutput");
    auto* filter = control<QLineEdit>(dialog, "workbenchFilterTemplate");
    auto* collection = control<QComboBox>(dialog, "workbenchCollection");
    if ( !run || !next || !previous || !output || !filter || !collection )
    {
        return;
    }
    run->click();
    check(output->toPlainText().contains("Total matches: 205"), "query counts all matches");
    check(next->isEnabled() && !previous->isEnabled(), "first page has only next");
    for ( int i = 0; i < 4; ++i )
    {
        next->click();
    }
    check(output->toPlainText().contains("200\n201\n202\n203\n204\n"), "last page keeps original indices");
    check(!next->isEnabled() && previous->isEnabled(), "last page has only previous");
    filter->setText("odd");
    run->click();
    check(output->toPlainText().contains("Total matches: 102\n1\n3\n5\n"), "filter resets offset and preserves original indices");
    next->click();
    check(output->toPlainText().contains("101\n103\n105\n"), "pagination counts matching rows");
    filter->setText("absent");
    run->click();
    check(output->toPlainText() == "Total matches: 0\n", "unmatched predicate is an empty successful page");
    check(!next->isEnabled() && !previous->isEnabled(), "empty page disables navigation");
    collection->setCurrentIndex(collection->findData(static_cast<int>(sed::level::WorkbenchCollection::surfaces)));
    check(collection->currentData().toInt() == static_cast<int>(sed::level::WorkbenchCollection::surfaces), "surfaces are queryable from GUI");
    level.lights.resize(2);
    level.lights[0].flags = 8;
    level.lights[1].flags = 10;
    collection->setCurrentIndex(collection->findData(static_cast<int>(sed::level::WorkbenchCollection::lights)));
    auto* mask = control<QLineEdit>(dialog, "workbenchFilter_flags.all");
    if ( mask )
    {
        mask->setText("010");
        run->click();
        check(output->toPlainText() == "Total matches: 1\n1\n", "decimal mask with leading zero is not octal");
        mask->setText("0x08");
        run->click();
        check(output->toPlainText() == "Total matches: 2\n0\n1\n", "explicit hexadecimal mask works");
        mask->setText("4294967296");
        run->click();
        check(output->toPlainText().isEmpty() && !next->isEnabled(), "overflowing mask clears stale results and navigation");
    }
}

void runArrayTests()
{
    sed::editor::Document document;
    auto& level = document.level();
    int applied = 0;
    sed::level::PreparedLightArray captured;
    sed::ui::query::WorkbenchDialog::Handlers handlers;
    handlers.applyLightArray = [&](const sed::level::PreparedLightArray& value)
    {
        captured = value;
        ++applied;
        return document.applyUndoable("Array", sed::editor::DocumentDirty::Structure,
            [&](sed::editor::DocumentState& state) { return value.apply(state.level); });
    };
    sed::ui::query::WorkbenchDialog dialog(level, {}, {}, handlers);
    auto* shape = control<QComboBox>(dialog, "workbenchArrayShape");
    auto* count = control<QSpinBox>(dialog, "workbenchArrayCount");
    auto* originX = control<QDoubleSpinBox>(dialog, "workbenchArrayOriginX");
    auto* radius = control<QDoubleSpinBox>(dialog, "workbenchArrayRadius");
    auto* intensity = control<QDoubleSpinBox>(dialog, "workbenchLightIntensity");
    auto* range = control<QDoubleSpinBox>(dialog, "workbenchLightRange");
    auto* run = control<QPushButton>(dialog, "workbenchCreateLights");
    auto* countU = control<QSpinBox>(dialog, "workbenchArrayCountU");
    auto* countV = control<QSpinBox>(dialog, "workbenchArrayCountV");
    if ( !shape || !count || !originX || !radius || !intensity || !range || !run || !countU || !countV )
    {
        return;
    }
    auto* queryCollection = control<QComboBox>(dialog, "workbenchCollection");
    auto* queryRun = control<QPushButton>(dialog, "workbenchQueryRun");
    auto* queryOutput = control<QPlainTextEdit>(dialog, "workbenchQueryOutput");
    if ( queryCollection && queryRun && queryOutput )
    {
        queryCollection->setCurrentIndex(queryCollection->findData(static_cast<int>(sed::level::WorkbenchCollection::lights)));
        queryRun->click();
        check(queryOutput->toPlainText() == "Total matches: 0\n", "array query starts from current empty collection");
    }
    shape->setCurrentIndex(shape->findData(static_cast<int>(sed::level::PlacementLayoutKind::radial)));
    count->setValue(4);
    originX->setValue(10.0);
    radius->setValue(2.0);
    intensity->setValue(0.75);
    range->setValue(7.0);
    auto* previewButton = control<QPushButton>(dialog, "workbenchPreviewPositions");
    auto* preview = control<QPlainTextEdit>(dialog, "workbenchArrayPreview");
    if ( previewButton && preview )
    {
        previewButton->click();
        check(!preview->toPlainText().isEmpty(), "array preview reports actual positions");
        originX->setValue(11);
        check(preview->toPlainText().isEmpty(), "changing position inputs invalidates array preview");
        originX->setValue(10);
    }
    run->click();
    check(!queryOutput || queryOutput->toPlainText().isEmpty(), "array mutation invalidates prior query rows and count");
    check(applied == 1 && level.lights.size() == 4, "radial array applies once through shared handler");
    check(captured.intensity == 0.75 && captured.range == 7.0, "light values reach shared preparation unchanged");
    check(captured.positions.size() == 4, "radial array preserves requested count");
    for ( const auto& point : captured.positions )
    {
        const double dx = point.x - 10.0;
        check(std::abs(dx * dx + point.y * point.y - 4.0) < 1e-10 && point.z == 0.0,
            "radial output uses requested center and radius");
    }
    shape->setCurrentIndex(shape->findData(static_cast<int>(sed::level::PlacementLayoutKind::grid)));
    countU->setValue(2);
    countV->setValue(3);
    run->click();
    check(applied == 2 && level.lights.size() == 10, "grid creates countU times countV outputs");
    countU->setValue(256);
    countV->setValue(256);
    run->click();
    check(applied == 2 && level.lights.size() == 10, "oversized grid refuses before handler with no partial append");
    check(document.undoDepth() == 2, "successful arrays are exactly one document transaction each");
    check(document.undo() && level.lights.size() == 4, "Undo removes the whole grid array");
    check(document.redo() && level.lights.size() == 10, "Redo restores the whole grid array");
    shape->setCurrentIndex(shape->findData(static_cast<int>(sed::level::PlacementLayoutKind::radial)));
    auto* axisZ = control<QDoubleSpinBox>(dialog, "workbenchArrayAxisZ");
    if ( axisZ )
    {
        axisZ->setValue(0);
        run->click();
        check(applied == 2 && document.undoDepth() == 2 && level.lights.size() == 10,
            "zero radial axis neither appends nor creates undo history");
    }
}

void runRgbArrayTests()
{
    sed::editor::Document document;
    document.level().kind = sed::ProjectType::MOTS;
    sed::ui::query::WorkbenchDialog::Handlers handlers;
    handlers.applyLightArray = [&](const sed::level::PreparedLightArray& prepared)
    {
        return document.applyUndoable("RGB array", sed::editor::DocumentDirty::Lights,
            [&](sed::editor::DocumentState& state) { return prepared.apply(state.level); });
    };
    sed::ui::query::WorkbenchDialog dialog(document.level(), {}, {}, handlers);
    auto* inherit = control<QCheckBox>(dialog, "workbenchLightRgbFollowsWhite");
    auto* rgb = control<QDoubleSpinBox>(dialog, "workbenchLightRgbIntensity");
    auto* white = control<QDoubleSpinBox>(dialog, "workbenchLightIntensity");
    auto* run = control<QPushButton>(dialog, "workbenchCreateLights");
    if ( !inherit || !rgb || !white || !run )
    {
        return;
    }
    check(inherit->isChecked() && !rgb->isEnabled(), "MotS RGB arrays inherit white by default");
    white->setValue(0.75);
    run->click();
    check(!document.level().lights.empty() && !document.level().lights.back().rgbIntensity,
        "default array preserves absent RGB metadata");
    inherit->setChecked(false);
    check(rgb->isEnabled(), "explicit RGB override enables its numeric control");
    rgb->setValue(0.0);
    run->click();
    check(!document.level().lights.empty() && document.level().lights.back().rgbIntensity == 0.0
        && document.level().lights.back().intensity == 0.75, "explicit zero RGB is distinct from inherited white");
    check(document.undo() && !document.level().lights.back().rgbIntensity
        && document.redo() && document.level().lights.back().rgbIntensity == 0.0,
        "RGB array metadata is retained through one undo and redo");
    inherit->setChecked(true);
    white->setValue(0.25);
    run->click();
    check(!document.level().lights.back().rgbIntensity
        && document.level().lights.back().effectiveRgbIntensity() == 0.25,
        "returning to inheritance uses the new white channel");
    sed::Level classic;
    classic.kind = sed::ProjectType::JKDF2;
    sed::ui::query::WorkbenchDialog classicDialog(classic, {}, {}, {});
    auto* classicInherit = control<QCheckBox>(classicDialog, "workbenchLightRgbFollowsWhite");
    check(classicInherit && !classicInherit->isEnabled(), "classic light arrays cannot author MotS RGB metadata");
}

void runUvTests()
{
    sed::Level level;
    auto& sector = level.newSector();
    sector.addVertex(2, 3, 0);
    sector.addVertex(3, 3, 0);
    sector.addVertex(3, 4, 0);
    sector.addVertex(2, 4, 0);
    sed::Surface surface;
    surface.vertices = {0, 1, 2, 3};
    surface.texVertices.resize(4);
    surface.texVertices[2].color = {0.25f, 0.5f, 0.75f, 1.0f};
    surface.normal = {0, 0, 1};
    sector.surfaces.push_back(surface);
    int applied = 0;
    sed::ui::query::WorkbenchDialog::Handlers handlers;
    handlers.applyBatchUv = [&](const sed::level::PreparedBatchUv& plan)
    {
        ++applied;
        return plan.apply(level);
    };
    sed::ui::query::WorkbenchDialog dialog(level, {{0, 0}}, {}, handlers);
    auto* projection = control<QComboBox>(dialog, "workbenchUvProjection");
    auto* originX = control<QDoubleSpinBox>(dialog, "workbenchUvOriginX");
    auto* originY = control<QDoubleSpinBox>(dialog, "workbenchUvOriginY");
    auto* axisZ = control<QDoubleSpinBox>(dialog, "workbenchUvAxisZ");
    auto* density = control<QDoubleSpinBox>(dialog, "workbenchUvDensity");
    auto* apply = control<QPushButton>(dialog, "workbenchProjectUv");
    if ( !projection || !originX || !originY || !axisZ || !density || !apply )
    {
        return;
    }
    projection->setCurrentIndex(projection->findData(static_cast<int>(sed::level::UvProjectionDirection::positiveZ)));
    originX->setValue(2);
    originY->setValue(3);
    apply->click();
    const auto& uv = level.sectors[0].surfaces[0].texVertices;
    check(applied == 1 && uv[0].u == 0 && uv[0].v == 0 && uv[2].u == 16 && uv[2].v == 16,
        "projection uses explicit GUI origin with analytic texel coordinates");
    check(uv[2].color.r == 0.25f && uv[2].color.g == 0.5f && uv[2].color.b == 0.75f,
        "GUI projection preserves authored corner lighting");
    density->setValue(500000);
    apply->click();
    check(applied == 2 && uv[2].u == 500000 && uv[2].v == 500000,
        "GUI projection preserves admitted density above the old widget cap");
    density->setValue(0.00001);
    apply->click();
    check(applied == 3 && std::abs(uv[2].u - 0.00001) < 1e-10,
        "GUI projection preserves small positive density");
    projection->setCurrentIndex(projection->findData(-1));
    axisZ->setValue(0);
    apply->click();
    check(applied == 3 && std::abs(uv[2].u - 0.00001) < 1e-10,
        "degenerate GUI cylinder axis refuses before mutation");
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    runQueryTests();
    runArrayTests();
    runRgbArrayTests();
    runUvTests();
    return failures == 0 ? 0 : 1;
}
