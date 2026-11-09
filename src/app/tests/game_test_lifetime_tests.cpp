#include "test_common.hpp"

#include "app_settings.hpp"
#include "ui/project/game_test_workflow.hpp"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QLineEdit>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace sed::app_tests
{
namespace
{
enum class OwnerLossPoint { Cancel, Setup, Export, Staging };

class StageOwnerDeletion final : public QObject
{
public:
    explicit StageOwnerDeletion(std::function<void()> destroyOwner)
        : destroyOwner_(std::move(destroyOwner))
    {
        qApp->installEventFilter(this);
    }

    ~StageOwnerDeletion() override
    {
        qApp->removeEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        auto* progress = qobject_cast<QProgressDialog*>(watched);
        if ( !fired_ && event->type() == QEvent::Show && progress
            && progress->labelText() == QStringLiteral("Preparing test resources...") )
        {
            fired_ = true;
            QPointer<QObject> watchedGuard(watched);
            destroyOwner_();
            return watchedGuard.isNull();
        }
        return false;
    }

private:
    std::function<void()> destroyOwner_;
    bool fired_{};
};

void checkOwnerLoss(const OwnerLossPoint loss)
{
    std::cerr << "game lifetime begin: " << static_cast<int>(loss) << std::endl;
    QTemporaryDir scratch;
    check(scratch.isValid(), "game lifetime: isolated scratch exists");
    if ( !scratch.isValid() ) return;
    const auto root = std::filesystem::path(scratch.path().toStdWString());
    const auto game = root / "game";
    const auto output = root / "tests";
    std::filesystem::create_directories(game / "resource");
    std::filesystem::create_directories(output);
    const QByteArray sentinel("synthetic marker; never a runnable executable");
    const auto write = [](const std::filesystem::path& destination, const QByteArray& bytes)
    {
        QFile file(QString::fromStdWString(destination.wstring()));
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    check(write(game / "engine.exe", sentinel) && write(game / "resource" / "jk_.cd", sentinel),
        "game lifetime: synthetic runtime files exist");
    auto settings = settingsStore();
    settings.remove(QStringLiteral("game_test"));

    auto owner = std::make_unique<QWidget>();
    bool ownerDeleted = false;
    const auto destroyOwner = [&]()
    {
        std::cerr << "game lifetime deleting owner: " << static_cast<int>(loss) << std::endl;
        ownerDeleted = true;
        owner.reset();
        std::cerr << "game lifetime deleted owner: " << static_cast<int>(loss) << std::endl;
    };
    StageOwnerDeletion progressDeletion([&]()
    {
        if ( loss == OwnerLossPoint::Staging ) destroyOwner();
    });
    int exportCalls = 0;
    int snapshotCalls = 0;
    ui::project::GameTestContext context;
    context.game = ProjectType::JKDF2;
    context.defaultGameDirectory = game;
    context.exportMap = [&](const std::filesystem::path& destination)
    {
        ++exportCalls;
        // An asset-free package source; no game process should reach this file.
        const bool written = write(destination, QByteArray("SECTION: HEADER\nVersion 1\nEND\n"));
        if ( loss == OwnerLossPoint::Export ) destroyOwner();
        return written;
    };
    context.snapshotStillCurrent = [&]()
    {
        ++snapshotCalls;
        return true;
    };

    bool setupSeen = false;
    bool submitted = false;
    bool deadlineReached = false;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer driver;
    driver.setInterval(5);
    QObject::connect(&driver, &QTimer::timeout, [&]()
    {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if ( elapsed.elapsed() > 5000 )
        {
            deadlineReached = true;
            if ( dialog ) dialog->reject();
            return;
        }
        if ( !dialog || dialog->objectName() != QStringLiteral("gameTestDialog") || setupSeen ) return;
        setupSeen = true;
        if ( loss == OwnerLossPoint::Cancel )
        {
            dialog->reject();
            return;
        }
        if ( loss == OwnerLossPoint::Setup )
        {
            destroyOwner();
            return;
        }
        const auto set = [&](const char* name, const std::filesystem::path& value)
        {
            auto* edit = dialog->findChild<QLineEdit*>(QString::fromLatin1(name));
            if ( edit ) edit->setText(QString::fromStdWString(value.wstring()));
            return edit != nullptr;
        };
        auto* run = dialog->findChild<QPushButton*>(QStringLiteral("gameTestRun"));
        if ( !set("gameTestGameDirectory", game)
            || !set("gameTestExecutable", game / "engine.exe")
            || !set("gameTestStageRoot", output) || !run )
        {
            dialog->reject();
            return;
        }
        submitted = true;
        run->click();
    });
    bool cancelled = false;
    std::string error;
    driver.start();
    try
    {
        const auto result = ui::project::saveJklAndTest(owner.get(), context);
        std::cerr << "game lifetime workflow returned: " << static_cast<int>(loss) << std::endl;
        cancelled = !result.started && result.stageDirectory.empty();
    }
    catch ( const std::exception& exception )
    {
        error = exception.what();
    }
    driver.stop();
    if ( !error.empty() ) std::cerr << "game lifetime diagnostic: " << error << '\n';
    check(setupSeen && !deadlineReached, "game lifetime: setup interaction completes without watchdog rejection");
    check(cancelled && error.empty(), "game lifetime: cancellation or owner loss returns without process launch or error");
    check(snapshotCalls == 0, "game lifetime: owner loss never invokes the caller's snapshot callback");
    check(ownerDeleted == (loss != OwnerLossPoint::Cancel),
        "game lifetime: the intended heap owner lifetime boundary was reached");
    check(exportCalls == ((loss == OwnerLossPoint::Export || loss == OwnerLossPoint::Staging) ? 1 : 0)
            && submitted == (exportCalls == 1),
        "game lifetime: setup cancellation avoids export and later owner loss retains one export only");
    check(QDir(QString::fromStdWString(output.wstring())).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty(),
        "game lifetime: cancelled staging removes only its owned temporary output");
    bool visibleProgress = false;
    for ( auto* window : QApplication::topLevelWidgets() )
    {
        const auto* progress = qobject_cast<QProgressDialog*>(window);
        visibleProgress = visibleProgress || (progress && progress->isVisible()
            && progress->labelText() == QStringLiteral("Preparing test resources..."));
    }
    check(!visibleProgress, "game lifetime: owner loss leaves no orphaned visible progress window");
    for ( const auto& source : { game / "engine.exe", game / "resource" / "jk_.cd" } )
    {
        QFile file(QString::fromStdWString(source.wstring()));
        check(file.open(QIODevice::ReadOnly) && file.readAll() == sentinel,
            "game lifetime: cancellation preserves the selected runtime source bytes");
    }
    settings.remove(QStringLiteral("game_test"));
    std::cerr << "game lifetime end: " << static_cast<int>(loss) << std::endl;
}
} // namespace

void runGameTestLifetimeTests()
{
    checkOwnerLoss(OwnerLossPoint::Cancel);
    checkOwnerLoss(OwnerLossPoint::Setup);
    checkOwnerLoss(OwnerLossPoint::Export);
    checkOwnerLoss(OwnerLossPoint::Staging);
}
} // namespace sed::app_tests
