#include "ui/resources/model_uv_inspector.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSemaphore>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>

#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace sed::ui::resources;

void expect(bool condition, const char* message)
{
    if ( !condition ) throw std::runtime_error(message);
}

template<class Widget> Widget* child(QWidget& panel, const char* name)
{
    auto* result = panel.findChild<Widget*>(QString::fromLatin1(name));
    expect(result, name);
    return result;
}

void waitFor(const std::function<bool()>& ready)
{
    QDeadlineTimer deadline(5000);
    while ( !ready() && !deadline.hasExpired() )
    {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    expect(ready(), "worker completion deadline");
}

void save(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write owned fixture");
}

void checkPanel()
{
    const auto fixture = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path()
        / "tests/fixtures/relay-sentry";
    QFile model(fixture / "relay_sentry.3do");
    expect(model.open(QIODevice::ReadOnly), "open two-LOD fixture");
    auto source = model.readAll();
    source.replace("MATERIALS 1\n0: relay_skin.mat", "MATERIALS 2\n0: relay_skin.mat\n1: seam.mat");
    source.replace("1: 0 0x0 4 3 3 0.000000 3 1,1 3,3 2,2", "1: 1 0x0 4 3 3 0.000000 3 1,1 3,3 2,2");
    auto session = sed::editor::NativeModelSession::open(source.toStdString());
    const auto doc = session.committed();
    const auto revision = session.revision();

    QTemporaryDir dir;
    expect(dir.isValid(), "create owned palette folder");
    QByteArray cmp(832 + 64 * 256, '\0');
    cmp.replace(0, 4, "CMP ");
    cmp[64] = char(23);
    cmp[65] = char(45);
    cmp[66] = char(67);
    cmp[64 + 25 * 3] = char(255);
    const auto first = dir.filePath("first.cmp");
    const auto second = dir.filePath("second.cmp");
    save(first, cmp);
    cmp[64 + 25 * 3 + 1] = char(90);
    save(second, cmp);
    auto locator = std::make_shared<sed::assets::ResourceLocator>();
    locator->addRoot(fixture);
    bool current = true;
    bool throwContext = false;
    bool wrongThread = false;
    ModelUvInspector::Context context{locator, {}, [&] {
        wrongThread |= QThread::currentThread() != qApp->thread();
        if ( throwContext ) throw std::runtime_error("injected context failure");
        return current;
    }};
    ModelUvInspector panel(context);
    panel.setModel(doc, {0, 0, 1}, 1);
    auto* slotsBox = child<QComboBox>(panel, "uvMaterialSlot");
    expect(slotsBox->count() == 2 && slotsBox->currentText().contains("relay_skin.mat"), "material slot names and ordinals");
    auto* table = child<QTableView>(panel, "uvCornerTable");
    expect(table->model()->rowCount() == 1 && table->model()->index(0, 0).data().toInt() == 0
        && table->model()->index(0, 1).data().toInt() == 1 && panel.canvas()->totalSelectedUses() == 2,
        "selected material shows exact source face and corner among shared row uses");
    slotsBox->setCurrentIndex(1);
    expect(table->model()->rowCount() == 1 && table->model()->index(0, 0).data().toInt() == 1,
        "slot switch follows the other face sharing the UV row");
    slotsBox->setCurrentIndex(0);
    auto* mat = child<QLineEdit>(panel, "uvMaterialPath");
    auto* palette = child<QLineEdit>(panel, "uvPalettePath");
    auto* load = child<QPushButton>(panel, "uvLoadMaterial");
    const auto reload = [&] { load->click(); waitFor([&] { return !panel.busy(); }); };
    reload();
    expect(panel.capture() && panel.capture()->sourceSize == QSize(2, 2) && panel.capture()->image.isNull(),
        "missing palette retains dimensions for project material");
    palette->setText(first);
    expect(!panel.capture(), "palette change immediately clears old capture");
    reload();
    expect(panel.capture() && panel.capture()->image.pixelColor(0, 0) == QColor(255, 0, 0), "captured palette colors");
    const auto originalMatHash = panel.capture()->material.sha256;
    const auto originalCmpHash = panel.capture()->palette.sha256;
    const auto edited = doc.applyTextureVertexEdits(std::array{sed::assets::NativeTextureVertexEdit{0, 0, 1, -4, 7}});
    const auto frame = panel.canvas()->frameBounds();
    panel.setModel(edited, {0, 0, 1}, 1);
    panel.setModel(edited, {0, 0, 2}, 1);
    expect(panel.capture() && panel.canvas()->frameBounds() == frame, "proposal and row selection retain snapshot and frame");
    palette->setText(second);
    expect(!panel.capture() && panel.canvas()->frameBounds() == frame,
        "changing only the CMP clears colors while preserving the UV inspection frame");
    reload();
    expect(panel.canvas()->frameBounds() == frame, "same-size palette-only recapture preserves the inspection frame");
    expect(panel.capture() && panel.capture()->image.pixelColor(0, 0) == QColor(255, 90, 0)
        && panel.capture()->material.sha256 == originalMatHash && panel.capture()->palette.sha256 != originalCmpHash,
        "palette-only reload identifies exactly its new bytes");
    // A color MAT has a single 24-byte solid record and no stored texture extent.
    QByteArray solid(100, char(0));
    solid.replace(0, 4, "MAT ");
    solid[4] = char(0x32);
    solid[12] = char(1);
    solid[24] = char(8);
    const auto solidPath = dir.filePath("solid.mat");
    save(solidPath, solid);
    mat->setText(solidPath);
    reload();
    expect(panel.capture() && panel.capture()->sourceSize == QSize(0, 0) && panel.capture()->image.isNull(),
        "solid index zero has no origin tile dimensions");
    auto* chip = child<QWidget>(panel, "uvSolidColorChip");
    expect(!chip->isHidden() && chip->grab().toImage().pixelColor(chip->width() / 2, chip->height() / 2) == QColor(23, 45, 67, 255),
        "a separate visible solid-color swatch paints the opaque captured color of index zero");
    mat->setText(dir.filePath("missing.mat"));
    expect(chip->isHidden(), "invalidating the material immediately clears the solid-color swatch");
    reload();
    expect(!panel.capture(), "failed material capture clears former success");
    mat->setText(QString::fromStdString((fixture / "relay_skin.mat").string()));
    const auto staleCapture = [&](auto invalidate)
    {
        auto entered = std::make_shared<QSemaphore>();
        auto release = std::make_shared<QSemaphore>();
        panel.setWorkHooksForTests({[=] { entered->release(); release->tryAcquire(1, 5000); }});
        load->click();
        expect(entered->tryAcquire(1, 5000), "capture worker entered");
        invalidate();
        release->release();
        waitFor([&] { return !panel.busy(); });
        expect(!panel.capture(), "discard invalidated capture");
    };
    staleCapture([&] { palette->setText(first); });
    staleCapture([&] { throwContext = true; });
    expect(child<QLabel>(panel, "uvCaptureStatus")->text().contains("context", Qt::CaseInsensitive),
        "context exception on completion becomes a visible refusal");
    reload();
    expect(!panel.capture() && !panel.busy() && child<QLabel>(panel, "uvCaptureStatus")->text().contains("context", Qt::CaseInsensitive),
        "context exception before capture becomes a visible refusal");
    throwContext = false;
    staleCapture([&] { current = false; });
    expect(child<QLabel>(panel, "uvCaptureStatus")->text().contains("context", Qt::CaseInsensitive),
        "changed project context is visible");
    current = true;
    staleCapture([&] { panel.setModel(doc, {1, 0, 1}, 2); });
    expect(mat->text().isEmpty() && palette->text().isEmpty(), "new source clears both loose overrides");
    expect(!wrongThread && session.revision() == revision && !session.canUndo() && session.committed().source() == source.toStdString(),
        "inspection preserves model state; context checks stay on GUI thread");

    auto disposable = std::make_unique<ModelUvInspector>(context);
    disposable->setModel(doc, {0, 0, 1}, 1);
    auto entered = std::make_shared<QSemaphore>();
    auto release = std::make_shared<QSemaphore>();
    disposable->setWorkHooksForTests({[=] { entered->release(); release->tryAcquire(1, 5000); }});
    child<QPushButton>(*disposable, "uvLoadMaterial")->click();
    expect(entered->tryAcquire(1, 5000), "destruction worker entered");
    QElapsedTimer destruction;
    destruction.start();
    disposable.reset();
    release->release();
    expect(destruction.elapsed() < 3000, "panel destruction releases the GUI while the capture worker is blocked");
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    try { checkPanel(); }
    catch ( const std::exception& error ) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
