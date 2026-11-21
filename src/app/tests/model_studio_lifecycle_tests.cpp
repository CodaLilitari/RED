#include "ui/resources/model_studio_dialog.hpp"
#include "ui/components/preview_worker_pool.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QThread>
#include <QTimer>

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace
{
using sed::ui::resources::ModelStudioDialog;

void checkStudio(bool condition, const char* diagnostic)
{
    if ( !condition ) throw std::runtime_error(diagnostic);
}

template <class Widget>
Widget* field(ModelStudioDialog& dialog, const char* name)
{
    auto* result = dialog.findChild<Widget*>(QString::fromLatin1(name));
    checkStudio(result != nullptr, name);
    return result;
}

void press(ModelStudioDialog& dialog, const char* name)
{
    auto* button = field<QPushButton>(dialog, name);
    checkStudio(button->isEnabled(), name);
    button->click();
}

void until(const std::function<bool()>& predicate)
{
    QElapsedTimer deadline;
    deadline.start();
    while ( !predicate() && deadline.elapsed() < 10000 )
    {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    checkStudio(predicate(), "Studio lifecycle operation timed out");
}

void idle(ModelStudioDialog& dialog)
{
    until([&] { return !dialog.state().busy; });
}

void openStudio(ModelStudioDialog& dialog, const QString& source)
{
    dialog.show();
    field<QLineEdit>(dialog, "modelStudioSource")->setText(source);
    press(dialog, "modelStudioOpen");
    idle(dialog);
    checkStudio(dialog.state().committed.has_value(), "lifecycle source admission failed");
}

void edit(ModelStudioDialog& dialog, const QString& translation)
{
    field<QLineEdit>(dialog, "modelStudioX")->setText(translation);
    press(dialog, "modelStudioPreview");
    idle(dialog);
    press(dialog, "modelStudioApply");
    idle(dialog);
    checkStudio(dialog.state().dirty, "expected independent dirty model");
}

struct Gate
{
    std::mutex mutex;
    std::condition_variable condition;
    bool started{}, released{};

    void block()
    {
        std::unique_lock lock(mutex);
        started = true;
        condition.wait(lock, [&] { return released; });
    }

    bool entered()
    {
        std::lock_guard lock(mutex);
        return started;
    }

    void release()
    {
        {
            std::lock_guard lock(mutex);
            released = true;
        }
        condition.notify_all();
    }
};

// Also release a worker if a test assertion unwinds before the normal release.
struct ReleaseOnExit
{
    std::shared_ptr<Gate> gate = std::make_shared<Gate>();
    ~ReleaseOnExit() { gate->release(); }
};

void staleCloseContinuation(const QString& source, const QDir& directory)
{
    ModelStudioDialog dialog({});
    openStudio(dialog, source);
    edit(dialog, QStringLiteral("0.125"));
    auto* destination = field<QLineEdit>(dialog, "modelStudioOutput");
    destination->setText(source);
    bool requestedSave = false;
    QTimer::singleShot(0, &dialog, [&]
    {
        if ( auto* decision = dialog.findChild<QMessageBox*>(QStringLiteral("modelStudioUnsavedDecision")) )
        {
            requestedSave = decision->button(QMessageBox::Save)->isEnabled();
            decision->button(requestedSave ? QMessageBox::Save : QMessageBox::Cancel)->click();
        }
    });
    dialog.reject();
    idle(dialog);
    checkStudio(requestedSave && dialog.isVisible() && dialog.state().dirty, "refused close-save lost the active model");
    edit(dialog, QStringLiteral("0.25"));
    const auto reviewed = dialog.state().revision;
    destination->setText(directory.filePath(QStringLiteral("later-independent-save.3do")));
    press(dialog, "modelStudioReview");
    idle(dialog);
    press(dialog, "modelStudioSave");
    idle(dialog);
    checkStudio(QFile::exists(destination->text()) && !dialog.state().dirty && dialog.state().revision == reviewed,
        "independent later save did not acknowledge the reviewed model");
    checkStudio(dialog.isVisible(), "later independent save invoked stale Close continuation from refused review");
}

void latestAndCancel(const QString& source)
{
    ModelStudioDialog dialog({});
    openStudio(dialog, source);
    edit(dialog, QStringLiteral("0.125"));
    const auto baseline = dialog.state();
    const auto x = baseline.committed->geosets()[0].meshes[0].vertices.front().x;
    auto* translate = field<QLineEdit>(dialog, "modelStudioX");
    ReleaseOnExit first;
    dialog.setWorkHooksForTests({[gate = first.gate] { gate->block(); }, {}});
    translate->setText(QStringLiteral("1"));
    press(dialog, "modelStudioPreview");
    until([&] { return first.gate->entered(); });
    translate->setText(QStringLiteral("2"));
    press(dialog, "modelStudioPreview");
    translate->setText(QStringLiteral("4"));
    press(dialog, "modelStudioPreview");
    first.gate->release();
    idle(dialog);
    const auto ready = dialog.state();
    checkStudio(ready.displayed->geosets()[0].meshes[0].vertices.front().x == x + 4
        && ready.committed->source() == baseline.committed->source()
        && ready.revision == baseline.revision, "latest pending preview did not win from the committed baseline");

    ReleaseOnExit second;
    dialog.setWorkHooksForTests({[gate = second.gate] { gate->block(); }, {}});
    translate->setText(QStringLiteral("8"));
    press(dialog, "modelStudioPreview");
    until([&] { return second.gate->entered(); });
    translate->setText(QStringLiteral("16"));
    press(dialog, "modelStudioPreview");
    press(dialog, "modelStudioCancel");
    checkStudio(!field<QPushButton>(dialog, "modelStudioApply")->isEnabled(), "Cancel left stale Apply enabled");
    second.gate->release();
    idle(dialog);
    const auto cancelled = dialog.state();
    checkStudio(!cancelled.pendingInput && cancelled.dirty && cancelled.canUndo
        && cancelled.revision == baseline.revision && cancelled.displayed->source() == baseline.committed->source(),
        "Cancel during queued preview lost dirty history or restored an obsolete proposal");
}

void closeWhileStaging(const QString& source, const QDir& directory)
{
    ModelStudioDialog dialog({});
    openStudio(dialog, source);
    edit(dialog, QStringLiteral("0.5"));
    const auto output = directory.filePath(QStringLiteral("discard-while-staging.3do"));
    field<QLineEdit>(dialog, "modelStudioOutput")->setText(output);
    press(dialog, "modelStudioReview");
    idle(dialog);

    ReleaseOnExit pending;
    dialog.setWorkHooksForTests({{}, [gate = pending.gate] { gate->block(); }});
    press(dialog, "modelStudioSave");
    until([&] { return pending.gate->entered(); });
    bool discarded = false;
    bool finished = false;
    QTimer::singleShot(0, &dialog, [&]
    {
        if ( auto* decision = dialog.findChild<QMessageBox*>(QStringLiteral("modelStudioUnsavedDecision")) )
        {
            pending.gate->release();
            // Queued completion is now available to the nested modal loop before Discard.
            finished = sed::ui::detail::previewWorkerPool().waitForDone(5000);
            QApplication::processEvents();
            decision->button(QMessageBox::Discard)->click();
            discarded = true;
        }
    });
    dialog.reject();
    QApplication::processEvents();
    checkStudio(discarded && finished && !dialog.isVisible(), "close during staging failed to release its worker");
    checkStudio(!QFile::exists(output), "stage published in the nested close decision before Discard");
}
void emptyLodCanReturnToMesh(const QString& source, const QDir& directory)
{
    QFile input(source);
    checkStudio(input.open(QIODevice::ReadOnly), "cannot read two-LOD fixture");
    auto bytes = input.readAll();
    bytes.replace("\r\n", "\n");
    const auto secondLod = bytes.indexOf("GEOSET 1\n");
    checkStudio(secondLod >= 0, "unexpected relay-sentry fixture layout");
    bytes.truncate(secondLod);
    // Remove hierarchy references so LOD 0 contains orphan meshes and LOD 1 may legally be empty.
    bytes.append("GEOSET 1\nMESHES 0\nSECTION: HIERARCHYDEF\nHIERARCHY NODES 0\n");
    const auto path = directory.filePath(QStringLiteral("empty-second-lod.3do"));
    QFile fixture(path);
    checkStudio(fixture.open(QIODevice::WriteOnly) && fixture.write(bytes) == bytes.size(), "cannot write empty LOD fixture");
    fixture.close();

    ModelStudioDialog dialog({});
    openStudio(dialog, path);
    auto* lod = field<QComboBox>(dialog, "modelStudioLod");
    checkStudio(lod->count() == 2 && lod->currentIndex() == 0 && dialog.state().selection.has_value(),
        "empty LOD fixture failed to select its first source mesh");
    lod->setCurrentIndex(1);
    idle(dialog);
    checkStudio(!dialog.state().selection, "empty LOD unexpectedly selected a source mesh");
    lod->setCurrentIndex(0);
    idle(dialog);
    const auto selection = dialog.state().selection;
    checkStudio(selection && selection->geoset == 0 && selection->sourceMesh == 0,
        "LOD with orphan meshes became unreachable after selecting an empty LOD");
}

void throwingContextPreservesSession(const QString& source)
{
    bool throwOnCheck = false;
    ModelStudioDialog dialog({{}, [&]
    {
        if ( throwOnCheck ) throw std::runtime_error("injected context failure");
        return true;
    }});
    openStudio(dialog, source);
    edit(dialog, QStringLiteral("0.125"));
    const auto previous = dialog.state();
    throwOnCheck = true;
    press(dialog, "modelStudioOpen");
    idle(dialog);
    const auto current = dialog.state();
    checkStudio(current.dirty && current.revision == previous.revision && current.canUndo
        && current.committed->source() == previous.committed->source(),
        "throwing context discarded the live dirty model");
    checkStudio(field<QLabel>(dialog, "modelStudioStatus")->text().contains(QStringLiteral("injected context failure")),
        "context error details were not shown in Studio");
}
} // namespace

void runModelStudioLifecycleTests(const QString& source, const QString& directory)
{
    QStringList failures;
    const auto exercise = [&](const QString& name, const auto& operation)
    {
        try
        {
            operation();
        }
        catch ( const std::exception& error )
        {
            failures.push_back(name + QStringLiteral(": ") + QString::fromUtf8(error.what()));
        }
    };
    exercise(QStringLiteral("Stale close/save"), [&] { staleCloseContinuation(source, QDir(directory)); });
    exercise(QStringLiteral("Queued previews"), [&] { latestAndCancel(source); });
    exercise(QStringLiteral("Discard while staging"), [&] { closeWhileStaging(source, QDir(directory)); });
    exercise(QStringLiteral("Empty LOD selection"), [&] { emptyLodCanReturnToMesh(source, QDir(directory)); });
    exercise(QStringLiteral("Throwing context callback"), [&] { throwingContextPreservesSession(source); });
    if ( !failures.isEmpty() ) throw std::runtime_error(failures.join(QLatin1Char('\n')).toStdString());
}
