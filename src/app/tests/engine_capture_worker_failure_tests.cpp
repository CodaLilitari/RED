// Compile the real completion connections in this isolated regression target.
// The private value types remain private to the implementation; production has
// no test hooks. Faults enter through the watchers' ordinary QObject children.
#include "../src/ui/project/engine_capture_dialog.cpp"

#include <QApplication>
#include <QThread>
#include <iostream>

namespace
{
using Dialog = sed::ui::project::EngineCaptureDialog;
int failures{};
enum class Delivery { Unstarted, Current, Stale, Closed };

void check(bool condition, const char* message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <class Result> void failedFuture(bool recipe, Delivery delivery, bool nonstandard)
{
    QTemporaryDir root;
    const auto runner = root.filePath("unused-runner.py");
    QFile file(runner);
    check(file.open(QIODevice::WriteOnly), "create unused runner fixture");
    file.write("# No process should launch from a failed preparation future.\n");
    file.close();
    Dialog dialog({ .pythonExecutable = QCoreApplication::applicationFilePath(), .runnerScript = runner });
    QFutureWatcher<Result>* watcher{};
    for ( auto* child : dialog.children() )
    {
        if ( auto* candidate = dynamic_cast<QFutureWatcher<Result>*>(child) )
        {
            watcher = candidate;
        }
    }
    check(watcher != nullptr, "real dialog owns the expected typed future watcher");
    if ( !watcher )
    {
        return;
    }
    auto* prepare = dialog.findChild<QPushButton*>("capturePrepare");
    auto* run = dialog.findChild<QPushButton*>("captureRun");
    auto* exportButton = dialog.findChild<QPushButton*>("captureExport");
    auto* status = dialog.findChild<QLabel*>("captureStatus");
    if ( delivery != Delivery::Unstarted )
    {
        if ( recipe )
        {
            const auto path = root.filePath("recipe.json");
            QFile recipeFile(path);
            check(recipeFile.open(QIODevice::WriteOnly), "create recipe fixture");
            recipeFile.write(R"({"steps":[{"op":"checkpoint","name":"one"}]})");
            recipeFile.close();
            dialog.findChild<QCheckBox*>("captureGameplay")->setChecked(true);
            dialog.findChild<QLineEdit*>("captureGameplayPath")->setText(path);
            dialog.findChild<QPushButton*>("captureLoadGameplay")->click();
        }
        else
        {
            prepare->click();
        }
        check(!prepare->isEnabled(), "real request action establishes current busy worker ownership");
    }
    bool delivered{};
    QObject::connect(watcher, &QFutureWatcherBase::finished, &dialog, [&] { delivered = true; });
    watcher->setFuture(sed::ui::detail::runPreviewWorkerTask([nonstandard]() -> Result
    {
        if ( nonstandard )
        {
            throw 17;
        }
        throw std::runtime_error("deliberate capture worker failure");
    }));
    if ( delivery == Delivery::Stale )
    {
        dialog.findChild<QLineEdit*>("captureEpisode")->setText("changed-during-work");
    }
    else if ( delivery == Delivery::Closed )
    {
        dialog.reject();
    }
    const auto before = status->text();
    bool escaped{};
    QElapsedTimer time;
    time.start();
    try
    {
        while ( !delivered && time.elapsed() < 5000 )
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }
    }
    catch ( const std::exception& error )
    {
        escaped = true;
        std::cerr << "Escaped standard exception: " << error.what() << '\n';
    }
    catch (...)
    {
        escaped = true;
        std::cerr << "Escaped nonstandard exception\n";
    }
    check(!escaped, "worker failure cannot escape the real Qt completion connection");
    if ( escaped )
    {
        return;
    }
    check(delivered, "failed worker completion arrives within the bounded wait");
    check(!run->isEnabled() && !exportButton->isEnabled(), "failed worker cannot enable execution or publication");
    check(prepare->isEnabled() == (delivery != Delivery::Closed), "failed completion restores idle or closed controls");
    if ( delivery == Delivery::Current )
    {
        check(status->text().contains("worker") && status->text() != before,
            "current failed future produces a visible worker diagnostic");
    }
    else
    {
        check(status->text() == before, "unstarted, stale and closed failures cannot publish diagnostics into newer state");
    }
}
} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    for ( const auto delivery : { Delivery::Unstarted, Delivery::Current, Delivery::Stale, Delivery::Closed } )
    {
        for ( bool nonstandard : { false, true } )
        {
            failedFuture<std::shared_ptr<sed::ui::project::RecipeImport>>(true, delivery, nonstandard);
            failedFuture<std::shared_ptr<sed::ui::project::Preparation>>(false, delivery, nonstandard);
        }
    }
    std::cout << "Capture worker failure regressions: " << failures << '\n';
    return failures ? 1 : 0;
}
