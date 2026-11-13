#include "ui/resources/model_lighting_dialog.hpp"
#include "../../../tools/sed-agent/tests/model_lighting_fixture.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>

#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
int failures{};

void check(const bool condition, const char* message)
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
    while ( !ready() && timer.elapsed() < 10000 )
    {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    return ready();
}

void run()
{
    QTemporaryDir directory;
    check(directory.isValid(), "model context fixture has isolated scratch");
    if ( !directory.isValid() )
    {
        return;
    }
    const QString path = directory.filePath("original.3do");
    const QByteArray original(kModelLightingFixture);
    {
        QFile file(path);
        check(file.open(QIODevice::WriteOnly) && file.write(original) == original.size(),
            "model context fixture is complete");
    }
    bool throwContext = true;
    sed::ui::resources::ModelLightingDialog::Context context;
    context.isCurrent = [&]
    {
        if ( throwContext )
        {
            throw std::runtime_error("model owner context unavailable");
        }
        return true;
    };
    sed::ui::resources::ModelLightingDialog dialog(std::move(context));
    auto* source = dialog.findChild<QLineEdit*>("modelLightingSource");
    auto* output = dialog.findChild<QLineEdit*>("modelLightingOutput");
    auto* load = dialog.findChild<QPushButton*>("modelLightingLoad");
    auto* calculate = dialog.findChild<QPushButton*>("modelLightingCalculate");
    auto* save = dialog.findChild<QPushButton*>("modelLightingSave");
    auto* status = dialog.findChild<QLabel*>("modelLightingStatus");
    check(source && output && load && calculate && save && status, "model context test uses ordinary controls");
    if ( !source || !output || !load || !calculate || !save || !status )
    {
        return;
    }
    source->setText(path);
    bool escaped{};
    try
    {
        load->click();
    }
    catch ( const std::exception& )
    {
        escaped = true;
    }
    check(!escaped && load->isEnabled() && !save->isEnabled()
        && status->text().contains("model owner context unavailable"),
        "model load contains throwing owner callback and preserves inline error detail");
    throwContext = false;
    load->click();
    throwContext = true;
    escaped = false;
    try
    {
        check(waitFor([&]
        {
            return load->isEnabled();
        }), "model completion restores idle controls");
    }
    catch ( const std::exception& )
    {
        escaped = true;
    }
    check(!escaped && load->isEnabled() && !save->isEnabled()
        && status->text().contains("model owner context unavailable"),
        "model worker completion contains callback exceptions and discards the result");
    throwContext = false;
    // A failed implementation may have left a disabled button after throwing.
    // A separate dialog makes the independent export regression deterministic.
    sed::ui::resources::ModelLightingDialog::Context saveContext;
    saveContext.isCurrent = [&]
    {
        if ( throwContext )
        {
            throw std::runtime_error("model owner context unavailable");
        }
        return true;
    };
    sed::ui::resources::ModelLightingDialog publishing(std::move(saveContext));
    auto* publishingSource = publishing.findChild<QLineEdit*>("modelLightingSource");
    auto* publishingOutput = publishing.findChild<QLineEdit*>("modelLightingOutput");
    auto* publishingLoad = publishing.findChild<QPushButton*>("modelLightingLoad");
    auto* publishingCalculate = publishing.findChild<QPushButton*>("modelLightingCalculate");
    auto* publishingSave = publishing.findChild<QPushButton*>("modelLightingSave");
    auto* publishingStatus = publishing.findChild<QLabel*>("modelLightingStatus");
    publishingSource->setText(path);
    publishingLoad->click();
    check(waitFor([&]
    {
        return publishingLoad->isEnabled();
    }) && publishingCalculate->isEnabled(), "model independent fixture loads successfully");
    publishingCalculate->click();
    check(waitFor([&]
    {
        return publishingLoad->isEnabled();
    }) && publishingSave->isEnabled(), "model independent fixture calculates successfully");
    publishingOutput->setText(directory.filePath("refused.3do"));
    throwContext = true;
    escaped = false;
    try
    {
        publishingSave->click();
    }
    catch ( const std::exception& )
    {
        escaped = true;
    }
    check(!escaped && !publishingSave->isEnabled() && !QFile::exists(publishingOutput->text())
        && publishingStatus->text().contains("model owner context unavailable"),
        "model publication rejects throwing owner context without losing its error detail");
    QFile sourceFile(path);
    check(sourceFile.open(QIODevice::ReadOnly) && sourceFile.readAll() == original,
        "model context failures preserve the original file");
}
}

int runModelLightingContextTests()
{
    failures = 0;
    run();
    return failures == 0 ? 0 : 1;
}

#ifdef SED_MODEL_CONTEXT_STANDALONE
int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    return runModelLightingContextTests();
}
#endif
