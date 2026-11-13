#include "test_common.hpp"

#include "app_settings.hpp"
#include "sed/assets/gob.hpp"
#include "sed/assets/resource_locator.hpp"
#include "ui/project/game_test_workflow.hpp"

#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>

namespace sed::app_tests
{
namespace
{

QByteArray readFile(const std::filesystem::path& path)
{
    QFile file(QString::fromStdWString(path.wstring()));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

void explicitMissionStrings(const bool includeCustomStrings)
{
    QTemporaryDir scratch;
    check(scratch.isValid(), "extra test resources: synthetic scratch exists");
    if ( !scratch.isValid() )
    {
        return;
    }
    const auto root = std::filesystem::path(scratch.path().toStdWString());
    std::filesystem::create_directories(root / "game" / "resource");
    std::filesystem::create_directories(root / "tests");
    for ( const auto& filePath : { root / "game" / "engine.exe", root / "game" / "resource" / "jk_.cd" } )
    {
        QFile file(QString::fromStdWString(filePath.wstring()));
        check(file.open(QIODevice::WriteOnly), "extra test resources: synthetic prerequisite opens");
        file.write("synthetic prerequisite; never executed");
    }
    auto settings = settingsStore();
    settings.remove(QStringLiteral("game_test"));
    settings.sync();

    const std::string cog = "symbols\nmessage startup\nend\ncode\nstartup:\n"
                            "jkPrintUniString(GetLocalPlayerThing(), 96000);\nreturn;\nend\n";
    const std::string uni = "MSGS 1\n\"COG_96000\" 0 \"Synthetic mission starts here.\"\nEND\n";
    const auto gobBytes = assets::buildGobBytes({ { "cog/mission.cog", cog }, { "misc/cogStrings.uni", uni } });
    const auto* bytes = reinterpret_cast<const std::byte*>(gobBytes.data());
    auto resources = std::make_shared<assets::ResourceLocator>();
    resources->mountGob(assets::GobArchive::fromBytes({ bytes, bytes + gobBytes.size() }));
    ui::project::GameTestContext context;
    context.game = ProjectType::JKDF2;
    context.sourcePath = root / "mission.sed";
    context.resources = resources;
    std::filesystem::path stage;
    const QByteArray mapBytes("SECTION: COGS\nWorld Cogs 1\n0: mission.cog\nEND\n");
    context.exportMap = [&](const std::filesystem::path& destination)
    {
        stage = destination.parent_path().parent_path();
        QFile file(QString::fromStdWString(destination.wstring()));
        return file.open(QIODevice::WriteOnly) && file.write(mapBytes) == mapBytes.size();
    };
    context.snapshotStillCurrent = [] { return true; };

    bool setupSeen = false;
    bool explicitControls = false;
    bool reviewSeen = false;
    bool exactStagedStrings = false;
    bool physicalSpelling = false;
    bool ordinaryPayloads = false;
    bool unexpectedDialog = false;
    bool timedOut = false;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, [&]()
    {
        timedOut = true;
        if ( auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()) )
        {
            dialog->reject();
        }
    });
    QTimer driver;
    driver.setInterval(5);
    QObject::connect(&driver, &QTimer::timeout, [&]()
    {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if ( !dialog )
        {
            return;
        }
        if ( dialog->objectName() == QStringLiteral("gameTestDialog") && !setupSeen )
        {
            setupSeen = true;
            const auto set = [&](const char* name, const QString& value)
            {
                if ( auto* edit = dialog->findChild<QLineEdit*>(QString::fromLatin1(name)) )
                {
                    edit->setText(value);
                }
            };
            set("gameTestGameDirectory", QString::fromStdWString((root / "game").wstring()));
            set("gameTestExecutable", QString::fromStdWString((root / "game" / "engine.exe").wstring()));
            set("gameTestStageRoot", QString::fromStdWString((root / "tests").wstring()));
            set("gameTestLevelName", QStringLiteral("mission.jkl"));
            if ( includeCustomStrings )
            {
                auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("gameTestExtraResources"));
                auto* add = dialog->findChild<QPushButton*>(QStringLiteral("gameTestExtraAdd"));
                auto* remove = dialog->findChild<QPushButton*>(QStringLiteral("gameTestExtraRemove"));
                explicitControls = table && add && remove && table->columnCount() == 2;
                if ( explicitControls )
                {
                    add->click();
                    explicitControls = table->rowCount() == 1;
                    if ( explicitControls )
                    {
                        table->setItem(0, 0, new QTableWidgetItem(QStringLiteral("misc")));
                        table->setItem(0, 1, new QTableWidgetItem(QStringLiteral("cogStrings.uni")));
                    }
                }
            }
            if ( auto* run = dialog->findChild<QPushButton*>(QStringLiteral("gameTestRun")) )
            {
                if ( includeCustomStrings )
                {
                    dialog->resize(640, 480);
                    check(dialog->height() <= 480 && dialog->width() <= 640
                            && dialog->rect().contains(QRect(run->mapTo(dialog, QPoint{}), run->size())),
                        "extra test resources: setup keeps its actions usable within a 640 by 480 window");
                }
                run->click();
            }
            else
            {
                dialog->reject();
            }
        }
        else if ( dialog->objectName() == QStringLiteral("gameTestDependencyReview") )
        {
            reviewSeen = true;
            exactStagedStrings = readFile(stage / "misc" / "cogstrings.uni") == QByteArray::fromStdString(uni);
            if ( std::filesystem::is_directory(stage / "misc") )
            {
                for ( const auto& entry : std::filesystem::directory_iterator(stage / "misc") )
                {
                    physicalSpelling = physicalSpelling || entry.path().filename().string() == "cogstrings.uni";
                }
            }
            ordinaryPayloads = readFile(stage / "cog" / "mission.cog") == QByteArray::fromStdString(cog)
                && readFile(stage / "jkl" / "mission.jkl") == mapBytes;
            dialog->reject(); // Never create an engine process.
        }
        else if ( dialog->objectName() != QStringLiteral("gameTestDialog") )
        {
            // Progress is not a choice; only unexpected input dialogs fail.
            if ( dialog->findChildren<QPushButton*>().size() > 0 )
            {
                unexpectedDialog = true;
                dialog->reject();
            }
        }
    });
    driver.start();
    deadline.start(5000);
    bool cancelled = false;
    QString failure;
    try
    {
        cancelled = !ui::project::saveJklAndTest(nullptr, context).started;
    }
    catch ( const std::exception& error )
    {
        failure = QString::fromUtf8(error.what());
    }
    driver.stop();
    deadline.stop();
    if ( !failure.isEmpty() )
    {
        std::cerr << "extra resource fixture: " << failure.toStdString() << '\n';
    }
    check(setupSeen && reviewSeen && ordinaryPayloads && !unexpectedDialog && !timedOut && failure.isEmpty(),
        "extra test resources: real staging reaches cancellable review with exact COG and map bytes");
    if ( includeCustomStrings )
    {
        check(explicitControls, "extra test resources: GUI offers explicit logical folder/name Add and Remove controls");
        check(exactStagedStrings && physicalSpelling,
            "extra test resources: explicitly selected mission UNI is staged with exact bytes and canonical physical name");
    }
    else
    {
        check(!exactStagedStrings && !physicalSpelling,
            "extra test resources: an empty list does not silently include the mounted UNI");
    }
    check(cancelled && !stage.empty() && !std::filesystem::exists(stage),
        "extra test resources: review cancellation removes owned stage before any engine process");
    check(!std::filesystem::exists(root / "game" / "episode") && !std::filesystem::exists(context.sourcePath),
        "extra test resources: fixture leaves game installation and native source untouched");
    settings.remove(QStringLiteral("game_test"));
    settings.sync();
}

void extraResourceBoundaries()
{
    QTemporaryDir scratch;
    check(scratch.isValid(), "extra test resource boundaries: scratch exists");
    if ( !scratch.isValid() )
    {
        return;
    }
    const auto root = std::filesystem::path(scratch.path().toStdWString());
    std::filesystem::create_directories(root / "game" / "resource");
    std::filesystem::create_directories(root / "tests");
    const auto write = [](const std::filesystem::path& target, const QByteArray& bytes)
    {
        QFile file(QString::fromStdWString(target.wstring()));
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    check(write(root / "game" / "engine.exe", "not executable")
            && write(root / "game" / "resource" / "jk_.cd", "synthetic marker"),
        "extra test resource boundaries: fake prerequisites exist");
    const QByteArray strings("MSGS 1\n\"COG_96000\" 0 \"Explicit test text.\"\nEND\n");
    const QString expectedHash = QString::fromLatin1(QCryptographicHash::hash(strings, QCryptographicHash::Sha256).toHex());
    const auto archive = assets::buildGobBytes({ { "misc/cogstrings.uni", strings.toStdString() } });
    const auto* data = reinterpret_cast<const std::byte*>(archive.data());
    auto resources = std::make_shared<assets::ResourceLocator>();
    resources->mountGob(assets::GobArchive::fromBytes({ data, data + archive.size() }));
    ui::project::GameTestContext context;
    context.game = ProjectType::JKDF2;
    context.resources = resources;
    context.sourcePath = root / "a.sed";
    std::filesystem::path stage;
    bool exported = false;
    context.exportMap = [&](const std::filesystem::path& destination)
    {
        exported = true;
        stage = destination.parent_path().parent_path();
        return write(destination, "SECTION: MATERIALS\nWorld Materials 0\nEND\n");
    };
    context.snapshotStillCurrent = [] { return true; };
    auto settings = settingsStore();
    settings.remove(QStringLiteral("game_test"));
    settings.sync();
    struct Observed
    {
        bool setup{};
        bool controls{};
        bool review{};
        bool cancelled{};
        bool timedOut{};
        bool capturedInventory{};
        bool omittedStock{};
        bool stagedStrings{};
        QStringList loaded;
        QString error;
    };
    const auto execute = [&](const QStringList& rows, const bool changeRows, const bool accept,
        const std::function<void(QDialog&, QTableWidget&)>& editControls = {})
    {
        Observed observed;
        exported = false;
        stage.clear();
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, [&]()
        {
            observed.timedOut = true;
            if ( auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()) )
            {
                dialog->reject();
            }
        });
        QTimer driver;
        driver.setInterval(5);
        QObject::connect(&driver, &QTimer::timeout, [&]()
        {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if ( !dialog )
            {
                return;
            }
            if ( dialog->objectName() == QStringLiteral("gameTestDialog") && !observed.setup )
            {
                observed.setup = true;
                const auto set = [&](const char* name, const QString& value)
                {
                    if ( auto* edit = dialog->findChild<QLineEdit*>(QString::fromLatin1(name)) )
                    {
                        edit->setText(value);
                    }
                };
                set("gameTestGameDirectory", QString::fromStdWString((root / "game").wstring()));
                set("gameTestExecutable", QString::fromStdWString((root / "game" / "engine.exe").wstring()));
                set("gameTestStageRoot", QString::fromStdWString((root / "tests").wstring()));
                set("gameTestLevelName", QStringLiteral("mission.jkl"));
                auto* table = dialog->findChild<QTableWidget*>(QStringLiteral("gameTestExtraResources"));
                observed.controls = table != nullptr;
                if ( table )
                {
                    for ( int row = 0; row < table->rowCount(); ++row )
                    {
                        observed.loaded << table->item(row, 0)->text() << table->item(row, 1)->text();
                    }
                    if ( changeRows )
                    {
                        table->setRowCount(static_cast<int>(rows.size() / 2));
                        for ( qsizetype index = 0; index < rows.size(); ++index )
                        {
                            table->setItem(static_cast<int>(index / 2), static_cast<int>(index % 2),
                                new QTableWidgetItem(rows.at(index)));
                        }
                    }
                    if ( editControls )
                    {
                        editControls(*dialog, *table);
                    }
                }
                if ( accept && table )
                {
                    dialog->accept();
                }
                else
                {
                    dialog->reject();
                }
            }
            else if ( dialog->objectName() == QStringLiteral("gameTestDependencyReview") )
            {
                observed.review = true;
                observed.stagedStrings = readFile(stage / "misc" / "cogstrings.uni") == strings;
                if ( auto* inventory = dialog->findChild<QTableWidget*>(QStringLiteral("gameTestResourceReport")) )
                {
                    for ( int row = 0; row < inventory->rowCount(); ++row )
                    {
                        if ( inventory->item(row, 0)->text() != QStringLiteral("misc/cogstrings.uni") )
                        {
                            continue;
                        }
                        observed.capturedInventory = inventory->item(row, 3)->text() == expectedHash
                            && inventory->item(row, 2)->text().toLongLong() == strings.size();
                        observed.omittedStock = inventory->item(row, 4)->text().contains(QStringLiteral("omitted"))
                            && inventory->item(row, 1)->text() == QStringLiteral("selected game installation");
                    }
                }
                dialog->reject();
            }
        });
        driver.start();
        deadline.start(5000);
        try
        {
            observed.cancelled = !ui::project::saveJklAndTest(nullptr, context).started;
        }
        catch ( const std::exception& error )
        {
            observed.error = QString::fromUtf8(error.what());
        }
        driver.stop();
        deadline.stop();
        check(observed.setup && observed.controls && !observed.timedOut,
            "extra test resource boundaries: real setup exposes the requested list within deadline");
        return observed;
    };
    const QStringList selected{ QStringLiteral("misc"), QStringLiteral("cogStrings.uni") };
    const auto cancelled = execute(selected, true, false);
    check(cancelled.cancelled && !exported && settings.allKeys().isEmpty()
            && std::filesystem::is_empty(root / "tests"),
        "extra test resources: cancelling edited setup changes no preferences or output");
    const auto editedControls = execute({}, true, false, [](QDialog& dialog, QTableWidget& table)
    {
        auto* add = dialog.findChild<QPushButton*>(QStringLiteral("gameTestExtraAdd"));
        auto* remove = dialog.findChild<QPushButton*>(QStringLiteral("gameTestExtraRemove"));
        check(add && remove, "extra test resources: list editing controls exist");
        if ( !add || !remove )
        {
            return;
        }
        table.setRowCount(3);
        for ( int row = 0; row < table.rowCount(); ++row )
        {
            table.setItem(row, 0, new QTableWidgetItem(QStringLiteral("misc")));
            table.setItem(row, 1, new QTableWidgetItem(QString::number(row) + QStringLiteral(".uni")));
        }
        table.selectRow(1);
        remove->click();
        check(table.rowCount() == 2 && table.item(0, 1)->text() == QStringLiteral("0.uni")
                && table.item(1, 1)->text() == QStringLiteral("2.uni"),
            "extra test resources: Remove selected preserves the other rows and their order");
        table.setRowCount(1023);
        add->click();
        check(table.rowCount() == 1024 && !add->isEnabled(),
            "extra test resources: Add reaches exactly 1024 rows and disables at the limit");
        add->click();
        check(table.rowCount() == 1024, "extra test resources: disabled Add cannot exceed the limit");
        table.selectRow(1023);
        remove->click();
        check(table.rowCount() == 1023 && add->isEnabled(),
            "extra test resources: Remove below the limit re-enables Add");
    });
    check(editedControls.cancelled && !exported && settings.allKeys().isEmpty()
            && std::filesystem::is_empty(root / "tests"),
        "extra test resources: cancelling list control exercises changes no preferences or output");
    const auto accepted = execute(selected, true, true);
    check(accepted.review && accepted.cancelled && accepted.capturedInventory && accepted.stagedStrings
            && !accepted.omittedStock && !std::filesystem::exists(stage),
        "extra test resources: explicit captured winner/hash is reviewed and cancelled stage is removed");
    context.sourcePath = root / "b.sed";
    const auto otherProject = execute({}, false, false);
    check(otherProject.loaded.isEmpty(), "extra test resources: another source does not inherit mission extras");
    context.sourcePath.clear();
    const auto untitled = execute({}, false, false);
    check(untitled.loaded.isEmpty(), "extra test resources: untitled source does not inherit a saved source list");
    context.sourcePath = root / "a.sed";
    const auto restored = execute({}, false, false);
    check(restored.loaded == selected, "extra test resources: original source retains exact logical spelling after review cancel");
    const auto cancelledReplacement = execute({ QStringLiteral("misc"), QStringLiteral("other.uni") }, true, false);
    const auto restoredAfterCancel = execute({}, false, false);
    check(cancelledReplacement.cancelled && restoredAfterCancel.loaded == selected,
        "extra test resources: cancelled setup preserves an existing source's saved list");
    const auto keysBefore = settings.allKeys();
    for ( const QStringList& invalid : {
            QStringList{ QStringLiteral("misc"), QStringLiteral("../outside.uni") },
            QStringList{ QStringLiteral("misc"), QStringLiteral("") },
            QStringList{ QStringLiteral("other"), QStringLiteral("misc/cogstrings.uni") },
            QStringList{ QStringLiteral("MISC"), QStringLiteral("cogStrings.uni"), QStringLiteral("misc"), QStringLiteral("cogstrings.uni") } } )
    {
        const auto rejected = execute(invalid, true, true);
        check(rejected.error.contains(QStringLiteral("Additional test resource row")) && !exported && !rejected.review
                && settings.allKeys() == keysBefore && std::filesystem::is_empty(root / "tests"),
            "extra test resources: malformed or duplicate canonical names refuse before export/settings/output");
    }
    const auto qualified = execute({ QStringLiteral("MISC"), QStringLiteral("misc/cogStrings.uni") }, true, true);
    check(qualified.review && qualified.cancelled && qualified.capturedInventory && qualified.stagedStrings
            && qualified.error.isEmpty() && !std::filesystem::exists(stage),
        "extra test resources: a matching qualified name resolves and stages the same exact captured winner");
    const auto missing = execute({ QStringLiteral("misc"), QStringLiteral("missing.uni") }, true, true);
    check(exported && missing.error.contains(QStringLiteral("missing.uni")) && !missing.review
            && !stage.empty() && !std::filesystem::exists(stage),
        "extra test resources: missing explicit resource refuses and removes its unlaunched stage");
    std::filesystem::create_directories(root / "game" / "resource" / "misc");
    check(write(root / "game" / "resource" / "misc" / "cogstrings.uni", strings),
        "extra test resources: synthetic stock-identical resource exists");
    const auto stock = execute(selected, true, true);
    check(stock.review && stock.cancelled && stock.capturedInventory && stock.omittedStock
            && !stock.stagedStrings && !std::filesystem::exists(stage),
        "extra test resources: stock-identical bytes remain omitted with exact reviewed hash and source category");
    settings.remove(QStringLiteral("game_test"));
    settings.sync();
}

} // namespace

void runGameTestExtraResourcesTests()
{
    explicitMissionStrings(false);
    explicitMissionStrings(true);
    extraResourceBoundaries();
}

} // namespace sed::app_tests
