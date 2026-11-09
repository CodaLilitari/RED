#include "test_common.hpp"

#include "main_window/main_window.hpp"
#include "sed/level/build.hpp"
#include "sed/level/clipboard.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QKeySequence>
#include <QMimeData>
#include <QVector3D>

#include <cstddef>
#include <optional>
#include <span>

namespace sed::app_tests
{
namespace
{

// Waits for a decodable payload of the expected kind. An advertised format
// alone is not enough: an entry whose data is gone keeps advertising it.
bool waitForEntityClipboard(const level::EntityClipboardKind expectedKind)
{
    const QString mimeType = QStringLiteral("application/x-sed-entities-v1");
    QElapsedTimer timer{};
    timer.start();
    while ( timer.elapsed() < 1000 )
    {
        const QMimeData* const mime = QApplication::clipboard()->mimeData();
        if ( mime && mime->hasFormat(mimeType) )
        {
            const QByteArray encoded = mime->data(mimeType);
            const std::optional<level::EntityClipboard> decoded = level::decodeEntityClipboard(
                std::span<const std::byte>(reinterpret_cast<const std::byte*>(encoded.constData()),
                    static_cast<std::size_t>(encoded.size())));
            if ( decoded && level::entityClipboardKind(*decoded) == expectedKind )
            {
                return true;
            }
        }
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return false;
}

void clearTextFocus()
{
    if ( QWidget* const focused = QApplication::focusWidget() )
    {
        focused->clearFocus();
    }
}

bool sameVertices(const Sector& lhs, const Sector& rhs)
{
    if ( lhs.vertices.size() != rhs.vertices.size() )
    {
        return false;
    }
    for ( std::size_t i = 0; i < lhs.vertices.size(); ++i )
    {
        if ( lhs.vertices[i].x != rhs.vertices[i].x
            || lhs.vertices[i].y != rhs.vertices[i].y
            || lhs.vertices[i].z != rhs.vertices[i].z )
        {
            return false;
        }
    }
    return true;
}

void runSectorPasteInPlace()
{
    QApplication::clipboard()->clear();
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.activateSectorEditModeForTest();

    editor::DocumentState state{};
    state.level.kind = ProjectType::JKDF2;
    state.level.layers.emplace_back("World");
    level::levelCreateCube(state.level, 5.25, -2.5, 1.125, TVector::up, TVector::right);
    state.level.sectors[0].layer = 0;
    state.selection.sectors.insert(0);
    const Sector source = state.level.sectors[0];
    window.setDocumentStateForTest(std::move(state));

    QAction* const copyAction = window.findChild<QAction*>(QStringLiteral("edit.copy_entities"));
    QAction* const pasteAction = window.findChild<QAction*>(QStringLiteral("edit.paste_entities"));
    QAction* const inPlaceAction =
        window.findChild<QAction*>(QStringLiteral("edit.paste_entities_in_place"));
    check(inPlaceAction != nullptr, "Edit menu exposes a Paste in Place action");
    if ( !copyAction || !pasteAction || !inPlaceAction )
    {
        return;
    }
    check(inPlaceAction->shortcuts().contains(
              QKeySequence(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_V)),
        "Paste in Place defaults to the legacy Ctrl+Shift+V shortcut");
    check(!inPlaceAction->isEnabled(), "Paste in Place starts disabled without copied entities");

    // A distinct pointer target proves Paste in Place ignores pointer placement.
    window.setProperty(
        "sed_entity_clipboard_test_target",
        QVariant::fromValue(QVector3D(40.0f, 40.0f, 40.0f)));
    clearTextFocus();
    copyAction->trigger();
    check(waitForEntityClipboard(level::EntityClipboardKind::sectors) && inPlaceAction->isEnabled(),
        "Paste in Place is enabled once Sectors are on the entity clipboard");

    clearTextFocus();
    inPlaceAction->trigger();
    const editor::DocumentState& pasted = window.documentStateForTest();
    check(pasted.level.sectors.size() == 2
            && sameVertices(pasted.level.sectors[1], source),
        "Paste in Place appends Sectors at their exact copied coordinates");
    check(pasted.selection.sectors.size() == 1 && pasted.selection.sectors.contains(1),
        "Paste in Place selects only the pasted Sector");
    check(window.undoDepthForTest() == 1u, "Paste in Place records exactly one undo command");

    check(window.undoDocumentForTest()
            && window.documentStateForTest().level.sectors.size() == 1
            && sameVertices(window.documentStateForTest().level.sectors[0], source),
        "undoing Paste in Place restores the original single Sector");
}

void runThingPasteInPlace()
{
    QApplication::clipboard()->clear();
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.activateThingEditModeForTest();

    editor::DocumentState state{};
    state.level.kind = ProjectType::JKDF2;
    state.level.layers.emplace_back("World");
    level::levelCreateCube(state.level, 0.0, 0.0, 0.0, TVector::up, TVector::right);
    state.level.sectors[0].layer = 0;
    Thing thing{};
    thing.name = "walkplayer";
    thing.templateName = "walkplayer";
    thing.sector = 0;
    thing.x = 0.3125;
    thing.y = -0.1875;
    thing.z = 0.0625;
    thing.yaw = 37.5;
    state.level.things.push_back(thing);
    state.selection.things.insert(0);
    window.setDocumentStateForTest(std::move(state));

    QAction* const copyAction = window.findChild<QAction*>(QStringLiteral("edit.copy_entities"));
    QAction* const inPlaceAction =
        window.findChild<QAction*>(QStringLiteral("edit.paste_entities_in_place"));
    if ( !copyAction || !inPlaceAction )
    {
        check(false, "Thing Paste in Place fixture finds its clipboard actions");
        return;
    }
    window.setProperty(
        "sed_entity_clipboard_test_target",
        QVariant::fromValue(QVector3D(-9.0f, 9.0f, 3.0f)));
    clearTextFocus();
    copyAction->trigger();
    check(waitForEntityClipboard(level::EntityClipboardKind::things), "Thing copy publishes the entity clipboard");
    clearTextFocus();
    inPlaceAction->trigger();

    const editor::DocumentState& pasted = window.documentStateForTest();
    check(pasted.level.things.size() == 2
            && pasted.level.things[1].x == thing.x
            && pasted.level.things[1].y == thing.y
            && pasted.level.things[1].z == thing.z
            && pasted.level.things[1].yaw == thing.yaw
            && pasted.selection.things.size() == 1
            && pasted.selection.things.contains(1),
        "Paste in Place keeps a copied Thing's exact position and orientation and selects it");
    check(window.undoDepthForTest() == 1u, "Thing Paste in Place records one undo command");
}

void runReadOnlyPasteInPlaceDisabled()
{
    QApplication::clipboard()->clear();
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    QAction* const inPlaceAction =
        window.findChild<QAction*>(QStringLiteral("edit.paste_entities_in_place"));
    check(inPlaceAction && !inPlaceAction->isEnabled(),
        "Paste in Place stays disabled with an empty clipboard and no document");
    if ( inPlaceAction )
    {
        inPlaceAction->trigger();
    }
    check(window.undoDepthForTest() == 0u, "disabled Paste in Place does not record history");
}

void runDuplicateEntities()
{
    QApplication::clipboard()->clear();
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.activateThingEditModeForTest();

    editor::DocumentState state{};
    state.level.kind = ProjectType::JKDF2;
    state.level.layers.emplace_back("World");
    level::levelCreateCube(state.level, 0.0, 0.0, 0.0, TVector::up, TVector::right);
    state.level.sectors[0].layer = 0;
    Thing thing{};
    thing.name = "walkplayer";
    thing.templateName = "walkplayer";
    thing.sector = 0;
    thing.x = 0.3125;
    thing.y = -0.1875;
    thing.z = 0.0625;
    thing.yaw = 37.5;
    state.level.things.push_back(thing);
    window.setDocumentStateForTest(std::move(state));
    window.updateEditActionsForTest();

    QAction* const duplicateAction =
        window.findChild<QAction*>(QStringLiteral("edit.duplicate_entities"));
    check(duplicateAction != nullptr, "Edit menu exposes Duplicate Entities");
    if ( !duplicateAction )
    {
        return;
    }
    check(duplicateAction->shortcuts().contains(QKeySequence(Qt::ControlModifier | Qt::Key_D)),
        "Duplicate defaults to Ctrl+D");
    check(!duplicateAction->isEnabled(), "Duplicate is disabled without a selection");

    editor::DocumentState selected = window.documentStateForTest();
    selected.selection.things.insert(0);
    window.setDocumentStateForTest(std::move(selected));
    window.updateEditActionsForTest();
    check(duplicateAction->isEnabled(), "Duplicate is enabled with a Thing selected");

    window.setProperty(
        "sed_entity_clipboard_test_target",
        QVariant::fromValue(QVector3D(0.25f, 0.125f, -0.25f)));
    clearTextFocus();
    duplicateAction->trigger();

    const editor::DocumentState& duplicated = window.documentStateForTest();
    check(duplicated.level.things.size() == 2
            && close(duplicated.level.things[1].x, 0.25)
            && close(duplicated.level.things[1].y, 0.125)
            && close(duplicated.level.things[1].z, -0.25)
            && duplicated.level.things[1].yaw == thing.yaw
            && duplicated.level.things[0].x == thing.x,
        "Duplicate places a copy of the active Thing at the pointer and keeps the source");
    check(duplicated.selection.things.size() == 1 && duplicated.selection.things.contains(1),
        "Duplicate selects only the new Thing");
    check(window.undoDepthForTest() == 1u, "Duplicate records exactly one undo command");

    const QMimeData* const mime = QApplication::clipboard()->mimeData();
    check(!mime || !mime->hasFormat(QStringLiteral("application/x-sed-entities-v1")),
        "Duplicate leaves the system entity clipboard untouched");
    QAction* const pasteAction = window.findChild<QAction*>(QStringLiteral("edit.paste_entities"));
    check(pasteAction && !pasteAction->isEnabled(),
        "Duplicate does not fill the editor clipboard used by Paste");

    check(window.undoDocumentForTest() && window.documentStateForTest().level.things.size() == 1,
        "undoing Duplicate removes the copy");
}

} // namespace

void runPasteInPlaceTests()
{
    runSectorPasteInPlace();
    runThingPasteInPlace();
    runReadOnlyPasteInPlaceDisabled();
    runDuplicateEntities();
}

} // namespace sed::app_tests
