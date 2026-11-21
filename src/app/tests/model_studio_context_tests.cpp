#include "main_window/main_window.hpp"
#include "sed/assets/sed.hpp"
#include "ui/resources/model_studio_dialog.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
using sed::MainWindow;
using sed::ui::resources::ModelStudioDialog;

void require(bool passed, const char* message)
{
    if ( !passed ) throw std::runtime_error(message);
}

template<class Widget>
Widget& control(ModelStudioDialog& dialog, const char* name)
{
    auto* widget = dialog.findChild<Widget*>(QString::fromLatin1(name));
    require(widget != nullptr, name);
    return *widget;
}

void press(ModelStudioDialog& dialog, const char* name)
{
    auto& button = control<QPushButton>(dialog, name);
    require(button.isEnabled(), name);
    button.click();
}

void settle(ModelStudioDialog& dialog)
{
    QElapsedTimer deadline;
    deadline.start();
    while ( dialog.state().busy && deadline.elapsed() < 5000 )
    {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    require(!dialog.state().busy, "Model Studio worker did not finish");
}

QByteArray bytes(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "could not read model file");
    return file.readAll();
}

template<class Visit>
void visitStudio(QAction& action, Visit visit)
{
    std::exception_ptr failure;
    bool visited = false;
    QTimer enter;
    enter.setSingleShot(true);
    QObject::connect(&enter, &QTimer::timeout, [&]
    {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        try
        {
            auto* dialog = dynamic_cast<ModelStudioDialog*>(modal);
            require(dialog && dialog->objectName() == QStringLiteral("modelStudioDialog"),
                "Tools action did not open the Model Studio dialog");
            visited = true;
            visit(*dialog);
            require(!dialog->isVisible(), "Studio stayed open after Close");
        }
        catch ( ... )
        {
            failure = std::current_exception();
            if ( modal ) modal->done(QDialog::Rejected);
        }
    });
    enter.start(0);
    action.trigger();
    if ( failure ) std::rethrow_exception(failure);
    require(visited, "Tools action did not enter the Model Studio workflow");
}

struct LevelSnapshot
{
    std::vector<std::byte> model;
    sed::editor::Selection selection;
    std::size_t undo;
    std::size_t redo;
    bool dirty;

    explicit LevelSnapshot(MainWindow& window)
        : model(sed::assets::saveToSedBytes(window.documentStateForTest().level)),
          selection(window.documentStateForTest().selection),
          undo(window.undoDepthForTest()), redo(window.redoDepthForTest()),
          dirty(window.hasUnsavedChangesForTest())
    {
    }

    bool matches(MainWindow& window) const
    {
        const auto& current = window.documentStateForTest();
        const auto& s = current.selection;
        return sed::assets::saveToSedBytes(current.level) == model
            && s.sectors == selection.sectors && s.surfaces == selection.surfaces
            && s.surfaceVertices == selection.surfaceVertices && s.things == selection.things
            && s.frames == selection.frames && s.lights == selection.lights && s.cogs == selection.cogs
            && window.undoDepthForTest() == undo && window.redoDepthForTest() == redo
            && window.hasUnsavedChangesForTest() == dirty;
    }
};

void run()
{
    // The MainWindow runner seeds process-wide, isolated settings and empty install roots.
    QTemporaryDir scratch;
    require(scratch.isValid(), "could not create isolated Model Studio project");
    const auto path = [&](const QString& relative)
    {
        return std::filesystem::path(scratch.filePath(relative).toStdU16String());
    };
    const auto fixture = QFileInfo(QString::fromUtf8(__FILE__)).dir().filePath(
        QStringLiteral("../../../tests/fixtures/relay-sentry/relay_sentry.3do"));
    const auto source = scratch.filePath(QStringLiteral("relay.3do"));
    require(QFile::copy(fixture, source), "could not copy the owned relay-sentry fixture");
    const auto original = bytes(source);
    MainWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.setRecoveryDirectoryForTest(path("recovery"));
    auto* action = window.findChild<QAction*>(QStringLiteral("tools.model_studio"));
    require(action && action->isEnabled(), "Tools is missing the enabled Model Studio action");

    bool inTools = false;
    for ( auto* menuAction : window.menuBar()->actions() )
    {
        auto* menu = menuAction->menu();
        if ( menu && menu->title().remove(QLatin1Char('&')) == QStringLiteral("Tools")
            && menu->actions().contains(action) )
        {
            inTools = true;
            break;
        }
    }
    require(inTools, "Model Studio action is absent from the Tools menu");

    sed::editor::DocumentState seed;
    seed.level.layers = {"Level"};
    seed.level.things.emplace_back().name = "studio_context_probe";
    window.setDocumentStateForTest(seed);
    window.markCurrentDocumentAsSavedForTest();
    const auto editLevel = [&](double x, bool selected)
    {
        require(window.applyStructuralHistoryEditForTest([&](sed::editor::DocumentState& state)
        {
            state.level.things.front().x = x;
            state.selection.clear();
            if ( selected ) state.selection.things.insert(0);
            return true;
        }), "independent Level edit failed");
    };
    editLevel(2.5, true);
    editLevel(7.5, false);
    const LevelSnapshot future(window);
    require(window.undoDocumentForTest(), "could not prepare Level Redo history");
    const LevelSnapshot active(window);
    require(active.dirty && active.undo == 1 && active.redo == 1 && active.selection.things.contains(0)
        && window.documentStateForTest().level.things.front().x == 2.5, "Level fixture lacks dirty selected history");
    require(window.undoDocumentForTest(), "could not capture Level Undo target");
    const LevelSnapshot past(window);
    require(window.redoDocumentForTest() && active.matches(window), "Level history did not restore baseline");

    const auto editModel = [&](ModelStudioDialog& dialog)
    {
        control<QLineEdit>(dialog, "modelStudioSource").setText(source);
        press(dialog, "modelStudioOpen");
        settle(dialog);
        const auto loaded = dialog.state();
        require(loaded.committed && loaded.committed->source() == original.toStdString(), "Studio did not admit source");
        control<QLineEdit>(dialog, "modelStudioX").setText(QStringLiteral("0.25"));
        press(dialog, "modelStudioPreview");
        settle(dialog);
        press(dialog, "modelStudioApply");
        settle(dialog);
        const auto changed = dialog.state();
        require(changed.dirty && changed.committed && changed.committed->source() != loaded.committed->source(),
            "Studio transform did not create an independent model revision");
        return std::string(changed.committed->source());
    };
    const auto review = [](ModelStudioDialog& dialog, const QString& output)
    {
        control<QLineEdit>(dialog, "modelStudioOutput").setText(output);
        press(dialog, "modelStudioReview");
        settle(dialog);
        require(control<QPlainTextEdit>(dialog, "modelStudioReviewText").toPlainText().contains(output),
            "Studio review omitted the actual destination");
    };

    const auto output = scratch.filePath(QStringLiteral("reviewed.3do"));
    visitStudio(*action, [&](ModelStudioDialog& dialog)
    {
        const auto transformed = editModel(dialog);
        press(dialog, "modelStudioUndo");
        settle(dialog);
        const auto undone = dialog.state();
        require(undone.committed && !undone.dirty && undone.committed->source() == original.toStdString(),
            "Studio Undo did not restore the source");
        press(dialog, "modelStudioRedo");
        settle(dialog);
        const auto redone = dialog.state();
        require(redone.committed && redone.dirty && redone.committed->source() == transformed,
            "Studio Redo did not restore the transform");
        review(dialog, output);
        press(dialog, "modelStudioSave");
        settle(dialog);
        require(!dialog.state().dirty && bytes(output).toStdString() == transformed, "wrong published model or clean state");
        press(dialog, "modelStudioClose");
    });
    require(active.matches(window), "published Studio session changed the open Level");

    const auto refused = scratch.filePath(QStringLiteral("stale.3do"));
    visitStudio(*action, [&](ModelStudioDialog& dialog)
    {
        const auto transformed = editModel(dialog);
        review(dialog, refused);
        struct StageGate { QSemaphore entered, release; };
        const auto gate = std::make_shared<StageGate>();
        dialog.setWorkHooksForTests({{}, [gate]
        {
            gate->entered.release();
            gate->release.acquire();
        }});
        const auto unblock = qScopeGuard([&] { gate->release.release(); });
        press(dialog, "modelStudioSave");
        require(gate->entered.tryAcquire(1, 5000), "Studio did not reach the staged-file boundary");
        require(QDir(scratch.path()).mkdir("other-resources"), "could not create replacement project resource root");
        window.addResourceRootForTest(path("other-resources"));
        gate->release.release();
        settle(dialog);
        const auto retained = dialog.state();
        require(!QFile::exists(refused) && retained.dirty && retained.canUndo
            && retained.committed && retained.committed->source() == transformed
            && control<QLabel>(dialog, "modelStudioStatus").text().contains("context", Qt::CaseInsensitive),
            "changed owner resource context allowed publication or lost the pending model");
        bool discarded = false;
        QTimer::singleShot(0, &dialog, [&]
        {
            if ( auto* decision = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()) )
            {
                if ( auto* discard = decision->button(QMessageBox::Discard) )
                {
                    discarded = true;
                    discard->click();
                }
            }
        });
        press(dialog, "modelStudioClose");
        require(discarded, "closing dirty Studio did not ask to discard the model");
    });
    require(bytes(source) == original && !QFile::exists(refused), "Studio changed the source or published a stale variant");
    require(active.matches(window), "closing Studio changed Level data, selection, dirty flag or history depth");
    require(window.undoDocumentForTest() && past.matches(window), "Studio corrupted the Level Undo target");
    require(window.redoDocumentForTest() && active.matches(window), "Studio corrupted the Level history cursor");
    require(window.redoDocumentForTest() && future.matches(window), "Studio corrupted the Level Redo target");
}
} // namespace

int runModelStudioContextTests()
{
    try
    {
        run();
        return 0;
    }
    catch ( const std::exception& error )
    {
        std::cerr << "Model Studio context: " << error.what() << '\n';
        return 1;
    }
}
