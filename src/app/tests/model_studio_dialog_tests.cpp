#include "ui/resources/model_studio_dialog.hpp"

#include "sed/assets/key.hpp"
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>

void runModelStudioLifecycleTests(const QString& source, const QString& directory);
void runModelStudioInteractionTests(const QString& source, const QString& directory);

namespace
{
using sed::ui::resources::ModelStudioDialog;

void require(bool value, const char* message)
{
    if ( !value ) throw std::runtime_error(message);
}

template <typename Widget>
Widget* control(ModelStudioDialog& dialog, const char* name)
{
    auto* widget = dialog.findChild<Widget*>(QString::fromLatin1(name));
    require(widget != nullptr, name);
    return widget;
}

void waitUntil(const std::function<bool()>& predicate)
{
    QElapsedTimer timer;
    timer.start();
    while ( !predicate() && timer.elapsed() < 10000 )
    {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    require(predicate(), "Studio operation did not finish within the test deadline");
}

void settle(ModelStudioDialog& dialog)
{
    waitUntil([&] { return !dialog.state().busy; });
}

QByteArray read(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "read fixture or published variant");
    return file.readAll();
}

void write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "create isolated source");
    require(file.write(bytes) == bytes.size(), "write complete source");
}

void click(ModelStudioDialog& dialog, const char* name)
{
    auto* button = control<QPushButton>(dialog, name);
    require(button->isEnabled(), name);
    button->click();
}

void load(ModelStudioDialog& dialog, const QString& source)
{
    control<QLineEdit>(dialog, "modelStudioSource")->setText(source);
    click(dialog, "modelStudioOpen");
    settle(dialog);
    require(dialog.state().committed.has_value(), "source admitted");
}

std::string committed(ModelStudioDialog& dialog)
{
    return std::string(dialog.state().committed->source());
}

void choose(ModelStudioDialog& dialog, int lod, int mesh, int uv = -1)
{
    control<QComboBox>(dialog, "modelStudioLod")->setCurrentIndex(lod);
    settle(dialog);
    control<QComboBox>(dialog, "modelStudioMesh")->setCurrentIndex(mesh);
    settle(dialog);
    control<QComboBox>(dialog, "modelStudioUvRow")->setCurrentIndex(uv + 1);
    settle(dialog);
}

void transform(ModelStudioDialog& dialog, const QString& scale, const QString& x)
{
    control<QComboBox>(dialog, "modelStudioOperation")->setCurrentIndex(0);
    control<QLineEdit>(dialog, "modelStudioScale")->setText(scale);
    control<QLineEdit>(dialog, "modelStudioX")->setText(x);
    click(dialog, "modelStudioPreview");
    settle(dialog);
}

void type(QLineEdit& edit, const QString& text)
{
    edit.setFocus();
    edit.selectAll();
    for ( const auto character : text )
    {
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(character));
        QApplication::sendEvent(&edit, &press);
        QKeyEvent release(QEvent::KeyRelease, 0, Qt::NoModifier, QString(character));
        QApplication::sendEvent(&edit, &release);
    }
}

struct Barrier
{
    std::mutex mutex;
    std::condition_variable ready;
    bool entered{}, released{};
    void block()
    {
        std::unique_lock lock(mutex);
        entered = true;
        ready.wait(lock, [&] { return released; });
    }
    bool hasEntered()
    {
        std::lock_guard lock(mutex);
        return entered;
    }
    void release()
    {
        std::lock_guard lock(mutex);
        released = true;
        ready.notify_all();
    }
    ~Barrier() { release(); }
};

void numericHistoryAndPublication(const QString& source, const QString& output, const QString& evidenceDirectory = {})
{
    const auto original = read(source);
    ModelStudioDialog dialog({});
    dialog.show();
    load(dialog, source);
    choose(dialog, 1, 1, 2);
    require(dialog.state().selection == sed::editor::NativeModelSelection{1, 1, 2}, "explicit source address");
    transform(dialog, "1.25", "0.125");
    require(committed(dialog) == original.toStdString(), "preview preserves committed bytes");
    const auto proposed = std::string(dialog.state().displayed->source());
    require(proposed != committed(dialog), "transform preview visible in owning document");
    click(dialog, "modelStudioApply");
    settle(dialog);
    const auto transformedRevision = dialog.state().revision;
    require(committed(dialog) == proposed && dialog.state().dirty, "one Apply commits exact proposal");
    click(dialog, "modelStudioUndo");
    settle(dialog);
    require(committed(dialog) == original.toStdString() && !dialog.state().dirty, "Undo restores exact clean source");
    transform(dialog, "1", "0");
    click(dialog, "modelStudioApply");
    settle(dialog);
    require(control<QPushButton>(dialog, "modelStudioRedo")->isEnabled(), "no-op after Undo preserves Redo");
    click(dialog, "modelStudioRedo");
    settle(dialog);
    require(dialog.state().revision == transformedRevision && committed(dialog) == proposed, "Redo restores exact authored revision");

    control<QComboBox>(dialog, "modelStudioOperation")->setCurrentIndex(1);
    control<QLineEdit>(dialog, "modelStudioU")->setText("-16.5");
    control<QLineEdit>(dialog, "modelStudioV")->setText("35.25");
    click(dialog, "modelStudioPreview");
    settle(dialog);
    const auto& uv = dialog.state().displayed->geosets()[1].meshes[1].texVertices[2];
    require(uv.u == -16.5f && uv.v == 35.25f, "UV values are native pixels");
    click(dialog, "modelStudioApply");
    settle(dialog);
    const auto mixed = committed(dialog);
    require(mixed != proposed, "session retains both committed operation families");
    control<QLineEdit>(dialog, "modelStudioOutput")->setText(output);
    click(dialog, "modelStudioReview");
    settle(dialog);
    const auto reviewText = control<QPlainTextEdit>(dialog, "modelStudioReviewText")->toPlainText();
    if ( !reviewText.contains(output) )
        std::cerr << "Review: " << reviewText.toStdString() << "\nStatus: "
            << control<QLabel>(dialog, "modelStudioStatus")->text().toStdString() << '\n';
    require(reviewText.contains(output), "review names actual destination");
    const auto outputHash = QString::fromLatin1(QCryptographicHash::hash(QByteArray::fromStdString(mixed), QCryptographicHash::Sha256).toHex());
    require(reviewText.contains(outputHash), "review binds exact cumulative output hash");
    require(!control<QPushButton>(dialog, "modelStudioUndo")->isEnabled(), "review holds the accepted session revision");
    if ( !evidenceDirectory.isEmpty() )
    {
        dialog.resize(1220, 930);
        QApplication::processEvents();
        require(dialog.grab().save(QDir(evidenceDirectory).filePath("model-studio-review.png")), "capture actual Studio review widget");
    }
    click(dialog, "modelStudioSave");
    settle(dialog);
    require(read(output).toStdString() == mixed && read(source) == original, "Save publishes exact cumulative variant only");
    require(!dialog.state().dirty, "successful creation acknowledges exact revision");
    if ( !evidenceDirectory.isEmpty() )
    {
        const QJsonArray operations{
            QJsonObject{{"op", "transformMesh"}, {"geoset", 1}, {"sourceMesh", 1}, {"scale", 1.25},
                {"translation", QJsonArray{0.125, 0.0, 0.0}}, {"radiusPolicy", "preserve"}},
            QJsonObject{{"op", "setTextureVertex"}, {"geoset", 1}, {"sourceMesh", 1}, {"textureVertex", 2},
                {"u", -16.5}, {"v", 35.25}}};
        const QJsonObject receipt{{"sourceFile", QFileInfo(source).fileName()}, {"candidateFile", QFileInfo(output).fileName()},
            {"sourceSha256", QString::fromLatin1(QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex())},
            {"reviewedOutputSha256", outputHash}, {"originalUnchanged", true}, {"runtimeVerified", false},
            {"operations", operations}};
        write(QDir(evidenceDirectory).filePath("studio-receipt.json"), QJsonDocument(receipt).toJson());
    }
    click(dialog, "modelStudioUndo");
    settle(dialog);
    require(dialog.state().dirty && read(output).toStdString() == mixed, "model Undo cannot undo an external asset publication");
}

void invalidAndStaleDrafts(const QString& source)
{
    ModelStudioDialog dialog({});
    dialog.show();
    load(dialog, source);
    choose(dialog, 1, 1);
    const auto original = committed(dialog);
    transform(dialog, "1", "0.25");
    require(control<QPushButton>(dialog, "modelStudioApply")->isEnabled(), "valid proposal enables Apply");
    auto* x = control<QLineEdit>(dialog, "modelStudioX");
    type(*x, "1e");
    require(x->text() == "1e" && !control<QPushButton>(dialog, "modelStudioApply")->isEnabled(), "incomplete raw input immediately revokes Apply");
    control<QPushButton>(dialog, "modelStudioApply")->setFocus();
    require(x->text() == "1e" && committed(dialog) == original, "focus transfer preserves invalid draft without falling back");
    click(dialog, "modelStudioCancel");
    settle(dialog);

    auto barrier = std::make_shared<Barrier>();
    dialog.setWorkHooksForTests({[barrier] { barrier->block(); }, {}});
    control<QLineEdit>(dialog, "modelStudioX")->setText("0.5");
    click(dialog, "modelStudioPreview");
    waitUntil([&] { return barrier->hasEntered(); });
    type(*x, "-");
    barrier->release();
    settle(dialog);
    require(!control<QPushButton>(dialog, "modelStudioApply")->isEnabled(), "older completion cannot re-enable invalid draft");
    require(dialog.state().displayed->source() == original, "older completion cannot restore obsolete visible proposal");
    click(dialog, "modelStudioCancel");
    settle(dialog);
    dialog.setWorkHooksForTests({});
    transform(dialog, "1", "0.25");
    click(dialog, "modelStudioApply");
    settle(dialog);
    const auto dirtyBytes = committed(dialog);
    const auto revision = dialog.state().revision;
    control<QLineEdit>(dialog, "modelStudioX")->setText("1e");
    const auto selection = dialog.state().selection;
    control<QLineEdit>(dialog, "modelStudioSource")->setText(source + ".missing.3do");
    click(dialog, "modelStudioOpen");
    settle(dialog);
    require(committed(dialog) == dirtyBytes && dialog.state().revision == revision && dialog.state().selection == selection,
        "failed replacement Open preserves dirty session and exact selection/history");
    require(x->text() == "1e" && dialog.state().pendingInput, "failed Open preserves unresolved numeric text");
}

void guardedStaging(const QString& source, const QString& output)
{
    bool current = true;
    ModelStudioDialog dialog({{}, [&] { return current; }});
    load(dialog, source);
    transform(dialog, "1", "0.125");
    click(dialog, "modelStudioApply");
    settle(dialog);
    const auto bytes = committed(dialog);
    const auto revision = dialog.state().revision;
    control<QLineEdit>(dialog, "modelStudioOutput")->setText(output);
    click(dialog, "modelStudioReview");
    settle(dialog);
    auto barrier = std::make_shared<Barrier>();
    dialog.setWorkHooksForTests({{}, [barrier] { barrier->block(); }});
    click(dialog, "modelStudioSave");
    waitUntil([&] { return barrier->hasEntered(); });
    current = false;
    barrier->release();
    settle(dialog);
    require(!QFile::exists(output), "GUI context change prevents staged publication");
    require(dialog.state().dirty && dialog.state().revision == revision && committed(dialog) == bytes,
        "failed save leaves exact dirty revision and history intact");
}

void largeUvSelection(const QString& source, const QString& path)
{
    auto bytes = read(source);
    const auto start = bytes.indexOf("TEXTURE VERTICES ");
    const auto end = bytes.indexOf("VERTEX NORMALS", start);
    require(start >= 0 && end > start, "large UV fixture has explicit source block");
    constexpr int rows = 100000;
    QByteArray block("TEXTURE VERTICES 100000\n");
    for ( int row = 0; row < rows; ++row )
        block += QByteArray::number(row) + ": " + QByteArray::number(row) + " 0\n";
    bytes.replace(start, end - start, block);
    write(path, bytes);
    ModelStudioDialog dialog({});
    QElapsedTimer clock;
    clock.start();
    qint64 last = 0, maximumGap = 0;
    QTimer heartbeat;
    heartbeat.setInterval(1);
    const auto recordGap = [&]
    {
        const auto now = clock.nsecsElapsed();
        maximumGap = std::max(maximumGap, now - last);
        last = now;
    };
    QObject::connect(&heartbeat, &QTimer::timeout, recordGap);
    heartbeat.start();
    load(dialog, path);
    recordGap();
    const auto admittedMs = clock.elapsed();
    auto* row = control<QComboBox>(dialog, "modelStudioUvRow");
    require(row->count() == rows + 1, "every admitted UV ordinal is selectable");
    const auto selectionStart = clock.elapsed();
    row->setCurrentIndex(rows);
    settle(dialog);
    recordGap();
    require(dialog.state().selection->textureVertex == rows - 1, "large final UV row selects exact source ordinal");
    require(control<QLineEdit>(dialog, "modelStudioU")->text() == "99999", "large final UV row preserves stored value");
    heartbeat.stop();
    std::cout << "Studio 100000-UV fixture: admission " << admittedMs << " ms; row selection "
        << clock.elapsed() - selectionStart << " ms; largest GUI heartbeat gap " << maximumGap / 1000000.0 << " ms\n";
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    try
    {
        QTemporaryDir temporary;
        require(temporary.isValid(), "isolated Studio fixture directory");
        const auto fixture = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path()
            / "tests/fixtures/relay-sentry/relay_sentry.3do";
        const auto arguments = application.arguments();
        const int evidenceOption = arguments.indexOf(QStringLiteral("--evidence-directory"));
        if ( evidenceOption >= 0 )
        {
            require(evidenceOption + 1 < arguments.size(), "evidence option requires a new directory");
            const auto destination = arguments[evidenceOption + 1];
            require(!QFileInfo::exists(destination) && QDir().mkpath(destination), "evidence directory must be new");
            const auto source = QDir(destination).filePath("source.3do");
            write(source, read(QString::fromStdString(fixture.string())));
            numericHistoryAndPublication(source, QDir(destination).filePath("mixed.3do"), destination);
            return 0;
        }
        const auto source = temporary.filePath(QStringLiteral("source.3do"));
        write(source, read(QString::fromStdString(fixture.string())));
        numericHistoryAndPublication(source, temporary.filePath(QStringLiteral("mixed.3do")));
        invalidAndStaleDrafts(source);
        guardedStaging(source, temporary.filePath(QStringLiteral("stale.3do")));
        largeUvSelection(source, temporary.filePath(QStringLiteral("large-uv.3do")));
        runModelStudioLifecycleTests(source, temporary.path());
        runModelStudioInteractionTests(source, temporary.path());
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
