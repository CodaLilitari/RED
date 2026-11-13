#include "app_settings.hpp"
#include "gl_view.hpp"
#include "main_window/main_window.hpp"
#include "sed/assets/sed.hpp"
#include "sed/level/build.hpp"
#include "sed/level/workbench.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <functional>
#include <iostream>
#include <string>

namespace
{
int failures{};
constexpr sed::SurfaceRef kSurface{0, 0};

void check(const bool condition, const std::string& message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool waitFor(const std::function<bool()>& ready)
{
    QElapsedTimer timer;
    timer.start();
    while ( !ready() && timer.elapsed() < 2000 )
    {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    return ready();
}

sed::editor::DocumentState fixture()
{
    sed::editor::DocumentState state;
    auto& level = state.level;
    level.kind = sed::ProjectType::MOTS;
    level.uvCoordinateSpace = sed::UvCoordinateSpace::Normalized;
    level.layers = {"Geometry", "Destination"};
    const auto sector = sed::level::levelCreateSectorPrimitiveBoxAxes(
        level, sed::level::SectorPrimitive::Cube, {-2.0, -2.0, -2.0},
        {2.0, 2.0, 2.0}, 0, 1, 2, 0);
    check(sector == 0, "Workbench fixture has a synthetic cube");
    sed::ThingTemplate thingTemplate;
    thingTemplate.name = "fixture_crate";
    sed::TplValue model;
    model.name = "model3d";
    model.type = sed::ValueType::str;
    model.stringValue = "workbench_synthetic_missing_model.3do";
    thingTemplate.values.push_back(model);
    level.templates.push_back(thingTemplate);
    state.selection.surfaces.insert(kSurface);
    return state;
}

void installFixture(sed::MainWindow& window, const QString& directory)
{
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.setDocumentStateForTest(fixture());
    window.glViewForTest()->setLevel(&window.mutableDocumentStateForTest().level,
        sed::GlView::SetLevelMode::keepView);
    window.setCurrentPathForTitleForTest(
        std::filesystem::path(directory.toStdWString()) / "fixture.sed");
    window.markCurrentDocumentAsSavedForTest();
    // Drain an already queued startup Checks debounce before creating a Thing.
    // Otherwise that unrelated refresh could hide a missing edit notification.
    check(waitFor([&window]()
    {
        for ( const auto* timer : window.findChildren<QTimer*>(QString{}, Qt::FindDirectChildrenOnly) )
        {
            if ( timer->isSingleShot() && timer->interval() == 200 && timer->isActive() )
            {
                return false;
            }
        }
        return true;
    }), "Workbench baseline drains pending Checks refreshes");
    window.rebuildChecksForTest();
    check(!window.hasUnsavedChangesForTest(), "Workbench baseline is saved");
}

QLineEdit* formText(QDialog& dialog, const QString& label)
{
    for ( auto* form : dialog.findChildren<QFormLayout*>() )
    {
        for ( int row = 0; row < form->rowCount(); ++row )
        {
            const auto* labelItem = form->itemAt(row, QFormLayout::LabelRole);
            const auto* fieldItem = form->itemAt(row, QFormLayout::FieldRole);
            const auto* labelWidget = labelItem
                ? qobject_cast<QLabel*>(labelItem->widget()) : nullptr;
            if ( labelWidget && labelWidget->text() == label && fieldItem )
            {
                return qobject_cast<QLineEdit*>(fieldItem->widget());
            }
        }
    }
    return nullptr;
}

enum class Operation
{
    uv,
    things,
    lights,
};

const char* operationName(const Operation operation)
{
    switch ( operation )
    {
        case Operation::uv:
        {
            return "UV";
        }
        case Operation::things:
        {
            return "Thing array";
        }
        case Operation::lights:
        {
            return "light array";
        }
    }
    return "unknown";
}

QPushButton* configureOperation(QDialog& dialog, const sed::Level& level,
    const Operation operation, const int layer)
{
    if ( operation == Operation::uv )
    {
        auto* direction = dialog.findChild<QComboBox*>("workbenchUvProjection");
        auto* density = dialog.findChild<QDoubleSpinBox*>("workbenchUvDensity");
        if ( !direction || !density )
        {
            return nullptr;
        }
        const std::vector<sed::SurfaceRef> refs{kSurface};
        for ( int axis = 0; axis < 6; ++axis )
        {
            if ( sed::level::prepareProjectSurfaceUv(level, refs,
                    static_cast<sed::level::UvProjectionDirection>(axis), {}, 3.0).ok() )
            {
                direction->setCurrentIndex(direction->findData(axis));
                break;
            }
        }
        density->setValue(3.0);
        return dialog.findChild<QPushButton*>("workbenchProjectUv");
    }
    auto* count = dialog.findChild<QSpinBox*>("workbenchArrayCount");
    auto* destination = dialog.findChild<QSpinBox*>("workbenchArrayLayer");
    auto* thingTemplate = formText(dialog, QStringLiteral("Thing template"));
    if ( !count || !destination || !thingTemplate )
    {
        return nullptr;
    }
    count->setValue(2);
    destination->setValue(layer);
    thingTemplate->setText(QStringLiteral("fixture_crate"));
    return dialog.findChild<QPushButton*>(operation == Operation::things
        ? "workbenchCreateThings" : "workbenchCreateLights");
}

void throughWorkbench(sed::MainWindow& window,
    const std::function<void(QDialog&)>& action)
{
    auto* menu = window.findChild<QAction*>("tools.workbench");
    check(menu && menu->isEnabled(), "Tools exposes Workbench");
    if ( !menu || !menu->isEnabled() )
    {
        return;
    }
    bool visited = false;
    QTimer driver;
    driver.setSingleShot(true);
    QObject::connect(&driver, &QTimer::timeout, &window, [&]()
    {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(dialog && dialog->parentWidget() == &window,
            "Workbench action opens its owned dialog");
        if ( dialog && dialog->parentWidget() == &window )
        {
            visited = true;
            action(*dialog);
            dialog->reject();
        }
    });
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &window, [&]()
    {
        check(false, "Workbench integration completes within the dialog deadline");
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if ( dialog && dialog->parentWidget() == &window )
        {
            dialog->reject();
        }
    });
    deadline.start(10000);
    driver.start(0);
    menu->trigger();
    deadline.stop();
    check(visited, "Workbench integration drove the ordinary dialog");
}

bool cachedUvsMatch(const sed::MainWindow& window, const sed::GlView& view)
{
    const auto& authored = window.documentStateForTest().level.sectors[0].surfaces[0].texVertices;
    const auto cached = view.cachedLevelSurfaceUvsForTest(kSurface);
    if ( cached.empty() )
    {
        return false;
    }
    for ( const auto& vertex : cached )
    {
        if ( vertex.sourceTexVertex < 0
            || static_cast<std::size_t>(vertex.sourceTexVertex) >= authored.size() )
        {
            return false;
        }
        const auto& uv = authored[static_cast<std::size_t>(vertex.sourceTexVertex)];
        if ( vertex.cachedU != static_cast<float>(uv.u)
            || vertex.cachedV != static_cast<float>(uv.v)
            || vertex.publishedU != static_cast<float>(uv.u)
            || vertex.publishedV != static_cast<float>(uv.v) )
        {
            return false;
        }
    }
    return true;
}

void successfulOperation(const QString& directory, const Operation operation)
{
    sed::MainWindow window;
    installFixture(window, directory);
    auto* view = window.glViewForTest();
    const auto before = sed::assets::saveToSedBytes(window.documentStateForTest().level);
    int viewChanges = 0;
    QObject::connect(view, &sed::GlView::viewTransformChanged, &window,
        [&viewChanges]()
        {
            ++viewChanges;
        });
    throughWorkbench(window, [&](QDialog& dialog)
    {
        auto* button = configureOperation(dialog, window.documentStateForTest().level, operation, 1);
        check(button, "Workbench exposes requested creation control");
        if ( !button )
        {
            return;
        }
        const auto meshRebuilds = view->meshRebuildCountForTest();
        const auto uvPublications = view->surfaceUvFastPublicationCountForTest();
        const auto notifications = viewChanges;
        button->click();
        if ( operation == Operation::uv )
        {
            check(cachedUvsMatch(window, *view), "Workbench UV edit publishes exact authored corner UVs");
            check(view->surfaceUvFastPublicationCountForTest() == uvPublications + 1,
                "Workbench UV edit uses one targeted UV publication");
            check(view->meshRebuildCountForTest() == meshRebuilds,
                "Workbench UV edit avoids rebuilding unrelated geometry");
        }
        else if ( operation == Operation::things )
        {
            check(window.documentStateForTest().level.things.size() == 2,
                "Workbench Thing array appends both validated outputs");
            check(view->meshRebuildCountForTest() > meshRebuilds,
                "Workbench Thing array invalidates the Thing render cache");
        }
        else
        {
            check(window.documentStateForTest().level.lights.size() == 2,
                "Workbench light array appends both validated outputs");
            check(viewChanges > notifications, "Workbench light array notifies the viewport");
        }
    });
    const auto after = sed::assets::saveToSedBytes(window.documentStateForTest().level);
    const auto label = std::string(operationName(operation));
    check(after != before, label + " edits authored state");
    check(window.undoDepthForTest() == 1, label + " creates exactly one Undo step");
    check(window.hasUnsavedChangesForTest(), label + " marks the document dirty");
    check(window.documentStateForTest().selection.surfaces.contains(kSurface),
        label + " preserves the existing selection");
    if ( operation == Operation::things )
    {
        check(waitFor([&]()
        {
            return window.indexedThingCheckRowCountForTest() >= 2;
        }), "Workbench Thing array refreshes missing-resource Checks for both outputs");
    }
    if ( QWidget* focus = QApplication::focusWidget() )
    {
        focus->clearFocus();
    }
    auto* undo = window.findChild<QAction*>("edit.undo");
    auto* redo = window.findChild<QAction*>("edit.redo");
    check(undo && redo, "Workbench fixture has document Undo/Redo actions");
    if ( undo && redo )
    {
        auto meshRebuilds = view->meshRebuildCountForTest();
        auto notifications = viewChanges;
        undo->trigger();
        check(sed::assets::saveToSedBytes(window.documentStateForTest().level) == before,
            label + " Undo restores exact authored baseline");
        check(!window.hasUnsavedChangesForTest(), label + " Undo restores the saved marker");
        check(cachedUvsMatch(window, *view), label + " Undo publishes current surface UVs");
        if ( operation == Operation::things )
        {
            check(view->meshRebuildCountForTest() > meshRebuilds,
                "Thing array Undo invalidates the removed Thing render cache");
        }
        if ( operation == Operation::lights )
        {
            check(viewChanges > notifications, "Light array Undo notifies the viewport");
        }
        meshRebuilds = view->meshRebuildCountForTest();
        notifications = viewChanges;
        redo->trigger();
        check(sed::assets::saveToSedBytes(window.documentStateForTest().level) == after,
            label + " Redo restores exact authored result");
        check(window.undoDepthForTest() == 1 && window.hasUnsavedChangesForTest(),
            label + " Redo restores one history step and dirty state");
        check(cachedUvsMatch(window, *view), label + " Redo publishes current surface UVs");
        if ( operation == Operation::things )
        {
            check(view->meshRebuildCountForTest() > meshRebuilds,
                "Thing array Redo invalidates the restored Thing render cache");
        }
        if ( operation == Operation::lights )
        {
            check(viewChanges > notifications, "Light array Redo notifies the viewport");
        }
    }
}

enum class Refusal
{
    hiddenSource,
    lockedSource,
    hiddenDestination,
    lockedDestination,
    gamePreview,
    replacedDocument,
    changedResources,
};

void refusedOperation(const QString& directory, const Operation operation, const Refusal refusal)
{
    sed::MainWindow window;
    installFixture(window, directory);
    throughWorkbench(window, [&](QDialog& dialog)
    {
        auto* button = configureOperation(dialog, window.documentStateForTest().level, operation, 1);
        check(button, "Workbench refusal fixture exposes requested operation");
        if ( !button )
        {
            return;
        }
        std::vector<bool> visible{true, true};
        std::vector<bool> locked{false, false};
        switch ( refusal )
        {
            case Refusal::hiddenSource:
            {
                visible[0] = false;
                break;
            }
            case Refusal::lockedSource:
            {
                locked[0] = true;
                break;
            }
            case Refusal::hiddenDestination:
            {
                visible[1] = false;
                break;
            }
            case Refusal::lockedDestination:
            {
                locked[1] = true;
                break;
            }
            case Refusal::gamePreview:
            {
                window.glViewForTest()->setRenderPipeline(sed::GlView::RenderPipeline::GamePreview);
                break;
            }
            case Refusal::replacedDocument:
            {
                window.setDocumentStateForTest(fixture());
                window.glViewForTest()->setLevel(&window.mutableDocumentStateForTest().level,
                    sed::GlView::SetLevelMode::keepView);
                break;
            }
            case Refusal::changedResources:
            {
                window.addResourceRootForTest(std::filesystem::path(directory.toStdWString()));
                break;
            }
        }
        window.setLayerVisualStateForTest(std::move(visible), std::move(locked));
        window.markCurrentDocumentAsSavedForTest();
        const auto before = sed::assets::saveToSedBytes(window.documentStateForTest().level);
        const auto undoDepth = window.undoDepthForTest();
        button->click();
        const std::string label = std::string(operationName(operation)) + " refusal "
            + std::to_string(static_cast<int>(refusal));
        check(sed::assets::saveToSedBytes(window.documentStateForTest().level) == before,
            label + " preserves authored state");
        check(window.undoDepthForTest() == undoDepth, label + " creates no Undo step");
        check(!window.hasUnsavedChangesForTest(), label + " preserves saved state");
        bool explained = false;
        for ( const auto* status : dialog.findChildren<QLabel*>() )
        {
            explained = explained || (status->text().contains(QStringLiteral("Preview"))
                && status->text().contains(QStringLiteral("layer"))
                && status->text().contains(QStringLiteral("context")));
        }
        check(explained, label + " explains availability and context checks in the dialog");
    });
}
} // namespace

int runWorkbenchIntegrationTests()
{
    failures = 0;
    QTemporaryDir directory;
    check(directory.isValid(), "Workbench integration has isolated settings and recovery");
    if ( !directory.isValid() )
    {
        return 1;
    }
    const auto oldSettings = sed::appSettingsIniPath();
    sed::setAppSettingsIniPathOverrideForTest(
        std::filesystem::path(directory.filePath("red.ini").toStdWString()));
    for ( const Operation operation : {Operation::uv, Operation::things, Operation::lights} )
    {
        successfulOperation(directory.path(), operation);
        refusedOperation(directory.path(), operation, Refusal::gamePreview);
        refusedOperation(directory.path(), operation, Refusal::replacedDocument);
        refusedOperation(directory.path(), operation, Refusal::changedResources);
        if ( operation != Operation::lights )
        {
            refusedOperation(directory.path(), operation, Refusal::hiddenSource);
            refusedOperation(directory.path(), operation, Refusal::lockedSource);
        }
        if ( operation != Operation::uv )
        {
            refusedOperation(directory.path(), operation, Refusal::hiddenDestination);
            refusedOperation(directory.path(), operation, Refusal::lockedDestination);
        }
    }
    sed::setAppSettingsIniPathOverrideForTest(oldSettings);
    std::cout << "Workbench integration failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}

#ifdef SED_WORKBENCH_INTEGRATION_STANDALONE
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    return runWorkbenchIntegrationTests();
}
#endif
