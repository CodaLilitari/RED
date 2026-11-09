#include "test_common.hpp"

#include "gl_view.hpp"
#include "main_window/main_window.hpp"
#include "sed/level/build.hpp"
#include "sed/level/clipboard.hpp"
#include "ui/components/clipboard_publication.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMap>
#include <QMimeData>
#include <QScopeGuard>
#include <QWidget>

#include <cstddef>
#include <functional>
#include <optional>
#include <span>

namespace sed::app_tests
{
namespace
{

const QString kEntityMime = QStringLiteral("application/x-sed-entities-v1");

/**
 * A scripted system clipboard. Writes either take effect or are lost, as
 * they are while another program holds the clipboard open; heldChecks makes
 * heldByAnotherWindow() report a holder that many times.
 */
struct FakeClipboard
{
    QMap<QString, QByteArray> current{};
    int writes{};
    int lostWrites{};
    int heldChecks{};
    int services{};
    int servicedMilliseconds{};
    std::uint32_t counter{ 100 };
    std::function<void()> onService{};

    ui::ClipboardPublicationBackend backend()
    {
        ui::ClipboardPublicationBackend result{};
        result.setMimeData = [this](std::unique_ptr<QMimeData> mime)
        {
            ++writes;
            if ( lostWrites > 0 )
            {
                --lostWrites;
                return;
            }
            current.clear();
            for ( const QString& format : mime->formats() )
            {
                current.insert(format, mime->data(format));
            }
            ++counter;
        };
        result.readFormat = [this](const QString& mimeType)
        {
            return current.value(mimeType);
        };
        result.changeCounter = [this]() -> std::optional<std::uint32_t>
        {
            return counter;
        };
        result.heldByAnotherWindow = [this]()
        {
            if ( heldChecks > 0 )
            {
                --heldChecks;
                return true;
            }
            return false;
        };
        result.serviceAndWait = [this](const int milliseconds)
        {
            ++services;
            servicedMilliseconds += milliseconds;
            if ( onService )
            {
                onService();
            }
        };
        return result;
    }
};

ui::ClipboardPublication entityPublication(const QByteArray& bytes)
{
    ui::ClipboardPublication publication{};
    publication.formats.push_back(ui::ClipboardFormatPayload{ kEntityMime, bytes });
    publication.text = QStringLiteral("SED: 1 Thing");
    return publication;
}

void clearTextFocus()
{
    if ( QWidget* const focused = QApplication::focusWidget() )
    {
        focused->clearFocus();
    }
}

void runPublicationContractTests()
{
    const QByteArray payload("payload-bytes");
    const ui::ClipboardPublicationPolicy policy{ 3, 50, 500 };

    {
        FakeClipboard fake{};
        const ui::ClipboardPublishOutcome outcome =
            ui::publishClipboard(entityPublication(payload), fake.backend(), policy);
        check(outcome == ui::ClipboardPublishOutcome::published && fake.writes == 1 && fake.services == 0
                && fake.current.value(kEntityMime) == payload
                && fake.current.value(QStringLiteral("text/plain")) == QByteArray("SED: 1 Thing"),
            "clipboard publication writes once, with its plain-text summary, when the entry reads back");
    }
    {
        FakeClipboard fake{};
        fake.heldChecks = 2;
        const ui::ClipboardPublishOutcome outcome =
            ui::publishClipboard(entityPublication(payload), fake.backend(), policy);
        check(outcome == ui::ClipboardPublishOutcome::published && fake.services == 2 && fake.writes == 1,
            "clipboard publication answers the program holding the clipboard before writing");
    }
    {
        FakeClipboard fake{};
        fake.lostWrites = 1;
        const ui::ClipboardPublishOutcome outcome =
            ui::publishClipboard(entityPublication(payload), fake.backend(), policy);
        check(outcome == ui::ClipboardPublishOutcome::published && fake.writes == 2 && fake.services == 1
                && fake.current.value(kEntityMime) == payload,
            "a lost clipboard write is retried after answering pending requests, then verified");
    }
    {
        FakeClipboard fake{};
        fake.lostWrites = 100;
        const ui::ClipboardPublishOutcome outcome =
            ui::publishClipboard(entityPublication(payload), fake.backend(), policy);
        check(outcome == ui::ClipboardPublishOutcome::unavailable && fake.writes == 3 && fake.services == 2,
            "a clipboard that stays unavailable is reported after the attempt budget");
    }
    {
        FakeClipboard fake{};
        fake.heldChecks = 1000;
        (void)ui::publishClipboard(entityPublication(payload), fake.backend(), policy);
        check(fake.writes >= 1 && fake.servicedMilliseconds <= policy.maxHeldWaitMs + 2 * policy.retryWaitMs,
            "waiting for a program that keeps the clipboard open is bounded");
    }
    {
        FakeClipboard fake{};
        fake.lostWrites = 1;
        fake.onService = [&fake]()
        {
            // Another program publishes its own entry while this one waits.
            fake.current.clear();
            fake.current.insert(QStringLiteral("text/plain"), QByteArray("theirs"));
            ++fake.counter;
        };
        const ui::ClipboardPublishOutcome outcome =
            ui::publishClipboard(entityPublication(payload), fake.backend(), policy);
        check(outcome == ui::ClipboardPublishOutcome::changedElsewhere && fake.writes == 1
                && fake.current.value(QStringLiteral("text/plain")) == QByteArray("theirs"),
            "clipboard publication never overwrites an entry another program published meanwhile");
    }
    {
        const ui::ClipboardPublicationBackend missing{};
        check(ui::publishClipboard(entityPublication(payload), missing, policy)
                == ui::ClipboardPublishOutcome::unavailable,
            "a missing clipboard reports unavailable");
    }
    check(!ui::clipboardPublishFailureMessage(ui::ClipboardPublishOutcome::unavailable).isEmpty()
            && !ui::clipboardPublishFailureMessage(ui::ClipboardPublishOutcome::changedElsewhere).isEmpty()
            && ui::clipboardPublishFailureMessage(ui::ClipboardPublishOutcome::published).isEmpty(),
        "unsuccessful clipboard publications have user-facing explanations");
}

void runEntityCopyPublicationTests()
{
    const auto restoreBackend = qScopeGuard(
        []()
        {
            ui::setClipboardPublicationBackendOverrideForTest(std::nullopt);
        });
    (void)restoreBackend;

    FakeClipboard empty{};
    ui::setClipboardPublicationBackendOverrideForTest(empty.backend());
    MainWindow window{};
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.activateSectorEditModeForTest();
    editor::DocumentState state{};
    state.level.kind = ProjectType::JKDF2;
    state.level.layers.emplace_back("World");
    level::levelCreateCube(state.level, 0.0, 0.0, 0.0, TVector::up, TVector::right);
    state.level.sectors[0].layer = 0;
    state.selection.sectors.insert(0);
    window.setDocumentStateForTest(std::move(state));

    QAction* const copyAction = window.findChild<QAction*>(QStringLiteral("edit.copy_entities"));
    QAction* const pasteAction = window.findChild<QAction*>(QStringLiteral("edit.paste_entities"));
    GlView* const view = window.glViewForTest();
    if ( !copyAction || !pasteAction || !view )
    {
        check(false, "entity Copy publication fixture finds its actions and viewport");
        return;
    }
    // The system clipboard of the test host is not consulted: Paste starts from
    // whatever it holds, so record the state this fixture controls.
    pasteAction->setEnabled(false);

    FakeClipboard busy{};
    busy.lostWrites = 1000;
    ui::setClipboardPublicationBackendOverrideForTest(busy.backend());
    clearTextFocus();
    copyAction->trigger();
    check(busy.writes >= 2 && !pasteAction->isEnabled()
            && view->toastTextForTest(GlView::ToastGroup::Error).startsWith(QStringLiteral("Copy failed")),
        "entity Copy reports a clipboard it could not write and leaves Paste disabled");

    FakeClipboard flaky{};
    flaky.lostWrites = 1;
    ui::setClipboardPublicationBackendOverrideForTest(flaky.backend());
    clearTextFocus();
    copyAction->trigger();
    const QByteArray published = flaky.current.value(kEntityMime);
    const std::optional<level::EntityClipboard> decoded = level::decodeEntityClipboard(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(published.constData()),
            static_cast<std::size_t>(published.size())));
    check(flaky.writes == 2 && decoded
            && level::entityClipboardKind(*decoded) == level::EntityClipboardKind::sectors
            && pasteAction->isEnabled(),
        "entity Copy retries a lost clipboard write and enables Paste once the entry reads back");
}

} // namespace

void runClipboardPublicationTests()
{
    runPublicationContractTests();
    runEntityCopyPublicationTests();
}

} // namespace sed::app_tests
