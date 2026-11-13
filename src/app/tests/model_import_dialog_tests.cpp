#include "test_common.hpp"
#include "sed/assets/3do.hpp"
#include "sed/assets/obj_import.hpp"
#include "sed/assets/mat_write.hpp"
#include "ui/components/preview_worker_pool.hpp"
#include "ui/components/ui_decimal_format.hpp"
#include "ui/resources/model_import_dialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QBuffer>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSemaphore>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <bit>
#include <span>

namespace sed::app_tests
{
namespace
{
using Dialog = ui::resources::ModelImportDialog;
const QByteArray kTriangle = "# owned synthetic triangle\nmtllib unused.mtl\no Triangle\n"
    "v 0 0 0\nv 2 0 0\nv 0 2 0\nvt 0 0\nvt 1 0\nvt 0 1\n"
    "vn 0 0 1\nusemtl crate\nf 1/1/1 2/2/1 3/3/1\n";

template <class T> T* widget(Dialog& dialog, const char* name)
{
    return dialog.findChild<T*>(QString::fromLatin1(name));
}

bool controls(Dialog& dialog)
{
    const bool found = widget<QLineEdit>(dialog, "modelImportSource")
        && widget<QTableWidget>(dialog, "modelImportMaterials")
        && widget<QPushButton>(dialog, "modelImportAddMaterial")
        && widget<QPushButton>(dialog, "modelImportRemoveMaterial")
        && widget<QLineEdit>(dialog, "modelImportScale")
        && widget<QComboBox>(dialog, "modelImportUpAxis")
        && widget<QComboBox>(dialog, "modelImportOrigin")
        && widget<QComboBox>(dialog, "modelImportNormals")
        && widget<QLineEdit>(dialog, "modelImportMeshName")
        && widget<QCheckBox>(dialog, "modelImportSplit")
        && widget<QCheckBox>(dialog, "modelImportAllowUntextured")
        && widget<QLineEdit>(dialog, "modelImportOutput")
        && widget<QLineEdit>(dialog, "modelImportImageOutput")
        && widget<QPushButton>(dialog, "modelImportPrepare")
        && widget<QPushButton>(dialog, "modelImportCreate")
        && widget<QPushButton>(dialog, "modelImportSaveImage")
        && widget<QPushButton>(dialog, "modelImportCancel")
        && widget<QPlainTextEdit>(dialog, "modelImportReport")
        && widget<QLabel>(dialog, "modelImportStatus");
    check(found, "model import GUI exposes source, mappings, options, report and independent publication controls");
    return found;
}

void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "open owned model import fixture");
    check(file.write(bytes) == bytes.size(), "write complete owned model import fixture");
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "read owned model output");
    return file.readAll();
}

void mapping(Dialog& dialog, const QString& name = "crate", const QString& mat = "crate.mat")
{
    widget<QPushButton>(dialog, "modelImportAddMaterial")->click();
    auto* table = widget<QTableWidget>(dialog, "modelImportMaterials");
    const int row = table->rowCount() - 1;
    const QStringList values{ name, mat, "64", "32", "" };
    for ( int column = 0; column < values.size(); ++column )
    {
        table->setItem(row, column, new QTableWidgetItem(values[column]));
    }
}

bool waitReady(Dialog& dialog)
{
    auto* prepare = widget<QPushButton>(dialog, "modelImportPrepare");
    QElapsedTimer deadline;
    deadline.start();
    while ( !prepare->isEnabled() && deadline.elapsed() < 5000 )
    {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    check(prepare->isEnabled(), "model import worker completes within test deadline");
    return prepare->isEnabled();
}

bool prepare(Dialog& dialog, const QString& source)
{
    widget<QLineEdit>(dialog, "modelImportSource")->setText(source);
    mapping(dialog);
    widget<QPushButton>(dialog, "modelImportPrepare")->click();
    return waitReady(dialog) && widget<QPushButton>(dialog, "modelImportCreate")->isEnabled();
}

void objConversion()
{
    QTemporaryDir directory;
    Dialog dialog({});
    if ( !controls(dialog) )
    {
        return;
    }
    const QString source = directory.filePath("triangle.obj");
    const QString destination = directory.filePath("triangle.3do");
    writeFile(source, kTriangle);
    check(widget<QCheckBox>(dialog, "modelImportSplit")->isChecked()
            && !widget<QCheckBox>(dialog, "modelImportAllowUntextured")->isChecked()
            && widget<QPlainTextEdit>(dialog, "modelImportReport")->isReadOnly(),
        "model import defaults retain splitting and refuse implicit untextured import; report is read-only");
    check(prepare(dialog, source), "synthetic OBJ prepares through human controls");
    auto* report = widget<QPlainTextEdit>(dialog, "modelImportReport");
    check(report->toPlainText().contains(QString::fromLatin1(
              QCryptographicHash::hash(kTriangle, QCryptographicHash::Sha256).toHex()))
            && report->toPlainText().contains("mtllib"),
        "model import report identifies exact source hash and ignored constructs");
    widget<QLineEdit>(dialog, "modelImportOutput")->setText(destination);
    widget<QPushButton>(dialog, "modelImportCreate")->click();
    waitReady(dialog);
    assets::ObjImportOptions expected;
    expected.materials.push_back({ "crate", "crate.mat", 64, 32 });
    const auto text = assets::saveTo3DO(assets::importModel(kTriangle.toStdString(), expected).model);
    check(QFile::exists(destination) && readFile(destination) == QByteArray::fromStdString(text),
        "GUI model output equals authoritative core conversion byte for byte");
    check(readFile(source) == kTriangle, "model import never changes the source OBJ");
    widget<QPushButton>(dialog, "modelImportCreate")->click();
    waitReady(dialog);
    check(readFile(destination) == QByteArray::fromStdString(text)
            && widget<QLabel>(dialog, "modelImportStatus")->text().contains("exists", Qt::CaseInsensitive),
        "model import refuses an existing destination without replacing bytes");
}

void placementAndInvalidation()
{
    QTemporaryDir directory;
    Dialog dialog({});
    if ( !controls(dialog) )
    {
        return;
    }
    const QString source = directory.filePath("triangle.obj");
    writeFile(source, kTriangle);
    widget<QLineEdit>(dialog, "modelImportScale")->setText("2.5");
    widget<QComboBox>(dialog, "modelImportUpAxis")->setCurrentIndex(1);
    widget<QComboBox>(dialog, "modelImportOrigin")->setCurrentIndex(2);
    widget<QComboBox>(dialog, "modelImportNormals")->setCurrentIndex(1);
    widget<QLineEdit>(dialog, "modelImportMeshName")->setText("prop");
    check(prepare(dialog, source), "placement and normal options prepare a model");
    const QString output = directory.filePath("placed.3do");
    widget<QLineEdit>(dialog, "modelImportOutput")->setText(output);
    widget<QPushButton>(dialog, "modelImportCreate")->click();
    waitReady(dialog);
    assets::ObjImportOptions expected;
    expected.materials.push_back({ "crate", "crate.mat", 64, 32 });
    expected.scale = 2.5;
    expected.upAxis = assets::ObjUpAxis::Z;
    expected.origin = assets::ObjOrigin::None;
    expected.normalPolicy = assets::ObjNormalPolicy::Recalculate;
    expected.meshNamePrefix = "prop";
    check(QFile::exists(output)
            && readFile(output) == QByteArray::fromStdString(
                assets::saveTo3DO(assets::importModel(kTriangle.toStdString(), expected).model)),
        "GUI placement, axis, origin, normals and mesh name reach the core unchanged");
    widget<QLineEdit>(dialog, "modelImportScale")->setText("3");
    check(!widget<QPushButton>(dialog, "modelImportCreate")->isEnabled()
            && widget<QPlainTextEdit>(dialog, "modelImportReport")->toPlainText().isEmpty(),
        "input edits invalidate prepared output and its provenance");
}

void refusedInputsAndChangedSource()
{
    QTemporaryDir directory;
    Dialog dialog({});
    if ( !controls(dialog) )
    {
        return;
    }
    const QString source = directory.filePath("triangle.obj");
    writeFile(source, kTriangle);
    check(prepare(dialog, source), "prepare before mutation boundary");
    const QString output = directory.filePath("refused.3do");
    widget<QLineEdit>(dialog, "modelImportOutput")->setText(output);
    writeFile(source, kTriangle + "# changed\n");
    widget<QPushButton>(dialog, "modelImportCreate")->click();
    waitReady(dialog);
    check(!QFile::exists(output)
            && widget<QLabel>(dialog, "modelImportStatus")->text().contains("changed", Qt::CaseInsensitive),
        "publication refuses a source whose bytes changed after preparation");
    mapping(dialog);
    widget<QPushButton>(dialog, "modelImportPrepare")->click();
    waitReady(dialog);
    check(!widget<QPushButton>(dialog, "modelImportCreate")->isEnabled(),
        "duplicate exact source material mappings do not produce a publishable model");
}

void canceledAndStaleWork()
{
    QTemporaryDir directory;
    bool current = true;
    Dialog dialog({ .isCurrent = [&] { return current; } });
    if ( !controls(dialog) )
    {
        return;
    }
    const QString source = directory.filePath("triangle.obj");
    writeFile(source, kTriangle);
    widget<QLineEdit>(dialog, "modelImportSource")->setText(source);
    mapping(dialog);
    auto& pool = ui::detail::previewWorkerPool();
    const int oldCount = pool.maxThreadCount();
    pool.setMaxThreadCount(1);
    QSemaphore entered, release;
    auto blocker = ui::detail::runPreviewWorkerTask([&] { entered.release(); release.acquire(); });
    check(entered.tryAcquire(1, 5000), "owned model worker blocker begins");
    widget<QPushButton>(dialog, "modelImportPrepare")->click();
    widget<QPushButton>(dialog, "modelImportCancel")->click();
    release.release();
    blocker.waitForFinished();
    waitReady(dialog);
    check(!widget<QPushButton>(dialog, "modelImportCreate")->isEnabled(),
        "cancelled model preparation never publishes a stale ready result");
    widget<QPushButton>(dialog, "modelImportPrepare")->click();
    waitReady(dialog);
    check(widget<QPushButton>(dialog, "modelImportCreate")->isEnabled(), "model preparation can be retried after cancel");
    current = false;
    const QString output = directory.filePath("stale.3do");
    widget<QLineEdit>(dialog, "modelImportOutput")->setText(output);
    widget<QPushButton>(dialog, "modelImportCreate")->click();
    check(!QFile::exists(output), "changed host context cannot publish a prepared model");
    pool.setMaxThreadCount(oldCount);
}

QByteArray singleImageGlb(const QByteArray& png)
{
    QByteArray bin;
    const auto append32 = [](QByteArray& target, const std::uint32_t value)
    {
        for ( unsigned shift = 0; shift < 32; shift += 8 )
        {
            target.append(static_cast<char>((value >> shift) & 255u));
        }
    };
    for ( const float value : { 0.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f } )
    {
        append32(bin, std::bit_cast<std::uint32_t>(value));
    }
    QByteArray json = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":60}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"}],
"materials":[{"name":"crate","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],
"textures":[{"source":0}],"images":[{"name":"synthetic","uri":"data:image/png;base64,)"
        + png.toBase64() + R"("}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"material":0}]}],
"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    while ( json.size() % 4 )
    {
        json.append(' ');
    }
    QByteArray glb("glTF", 4);
    append32(glb, 2);
    append32(glb, static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
    append32(glb, static_cast<std::uint32_t>(json.size()));
    glb.append("JSON", 4);
    glb.append(json);
    append32(glb, static_cast<std::uint32_t>(bin.size()));
    glb.append("BIN\0", 4);
    glb.append(bin);
    return glb;
}

void glbAndIndependentImage()
{
    QTemporaryDir directory;
    Dialog dialog({});
    if ( !controls(dialog) )
    {
        return;
    }
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray png;
    QBuffer buffer(&png);
    check(buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG"), "make owned synthetic PNG");
    const QByteArray glb = singleImageGlb(png);
    const QString source = directory.filePath("triangle.glb");
    const QString destination = directory.filePath("triangle.3do");
    const QString imagePath = directory.filePath("base.png");
    writeFile(source, glb);
    check(prepare(dialog, source), "GLB prepares through the same GUI controls");
    auto* save = widget<QPushButton>(dialog, "modelImportSaveImage");
    check(save->isEnabled(), "single used GLB base-color image is offered for independent export");
    widget<QLineEdit>(dialog, "modelImportImageOutput")->setText(directory.filePath("wrong.jpg"));
    save->click();
    check(!QFile::exists(directory.filePath("wrong.jpg")), "embedded image export refuses a mismatched extension");
    widget<QLineEdit>(dialog, "modelImportImageOutput")->setText(imagePath);
    save->click();
    waitReady(dialog);
    check(QFile::exists(imagePath) && readFile(imagePath) == png && !QFile::exists(destination),
        "embedded image preserves exact captured bytes and does not implicitly publish the model");
    widget<QLineEdit>(dialog, "modelImportOutput")->setText(destination);
    widget<QPushButton>(dialog, "modelImportCreate")->click();
    waitReady(dialog);
    assets::ObjImportOptions options;
    options.materials.push_back({ "crate", "crate.mat", 64, 32 });
    check(QFile::exists(destination) && readFile(destination)
            == QByteArray::fromStdString(assets::saveTo3DO(assets::importModel(glb.toStdString(), options).model)),
        "GLB model output agrees exactly with core conversion including UV orientation");
}

void matDimensionsAndProtectedInput()
{
    QTemporaryDir directory;
    Dialog dialog({});
    if ( !controls(dialog) )
    {
        return;
    }
    assets::RgbaImage image;
    image.width = 16;
    image.height = 8;
    image.rgba.assign(16 * 8 * 4, 255);
    assets::CmpPalette palette{};
    assets::IndexedMatOptions options;
    options.mipmaps = 1;
    const auto mat = assets::buildIndexedMat({ image }, palette, options);
    const QByteArray matBytes(reinterpret_cast<const char*>(mat.bytes.data()), static_cast<qsizetype>(mat.bytes.size()));
    const QString matPath = directory.filePath("dimensions.mat");
    const QString source = directory.filePath("triangle.obj");
    writeFile(matPath, matBytes);
    writeFile(source, kTriangle);
    widget<QLineEdit>(dialog, "modelImportSource")->setText(source);
    mapping(dialog);
    auto* table = widget<QTableWidget>(dialog, "modelImportMaterials");
    table->setItem(0, 4, new QTableWidgetItem(matPath));
    table->setItem(0, 2, new QTableWidgetItem("not used"));
    widget<QPushButton>(dialog, "modelImportPrepare")->click();
    waitReady(dialog);
    check(widget<QPushButton>(dialog, "modelImportCreate")->isEnabled()
            && widget<QPlainTextEdit>(dialog, "modelImportReport")->toPlainText().contains("16 × 8 texels")
            && widget<QPlainTextEdit>(dialog, "modelImportReport")->toPlainText().contains(
                QString::fromLatin1(QCryptographicHash::hash(matBytes, QCryptographicHash::Sha256).toHex())),
        "selected MAT supplies authoritative dimensions and exact provenance instead of the explicit-size cells");
    const QString output = directory.filePath("sized.3do");
    widget<QLineEdit>(dialog, "modelImportOutput")->setText(output);
    writeFile(matPath, matBytes + "changed");
    widget<QPushButton>(dialog, "modelImportCreate")->click();
    waitReady(dialog);
    check(!QFile::exists(output), "changed MAT source blocks publication as well as changed model source");

    // A source with a .3do suffix is still dispatched by its bytes; output must not overwrite it.
    const QString protectedSource = directory.filePath("protected.3do");
    writeFile(protectedSource, kTriangle);
    Dialog protectedDialog({});
    check(prepare(protectedDialog, protectedSource), "source dispatch is independent of its filename suffix");
    widget<QLineEdit>(protectedDialog, "modelImportOutput")->setText(protectedSource);
    widget<QPushButton>(protectedDialog, "modelImportCreate")->click();
    check(readFile(protectedSource) == kTriangle, "create-only model publication preserves every protected input");
}

void hostCallbacks()
{
    QTemporaryDir directory;
    const QString source = directory.filePath("triangle.obj");
    writeFile(source, kTriangle);
    Dialog* currentDialog{};
    bool armed = false;
    int calls{};
    Dialog reentrant({ .isCurrent = [&]
    {
        if ( armed && ++calls < 3 )
        {
            widget<QPushButton>(*currentDialog, "modelImportPrepare")->click();
        }
        return true;
    } });
    currentDialog = &reentrant;
    check(prepare(reentrant, source), "host reentrancy fixture initially prepares");
    calls = 0;
    armed = true;
    widget<QPushButton>(reentrant, "modelImportPrepare")->click();
    waitReady(reentrant);
    armed = false;
    check(calls <= 2 && widget<QPushButton>(reentrant, "modelImportCreate")->isEnabled(),
        "a host context callback cannot recursively start another preparation");

    Dialog notified({ .published = [](const QString&) { throw std::runtime_error("notification failed"); } });
    check(prepare(notified, source), "publication notification fixture prepares");
    const QString output = directory.filePath("created.3do");
    widget<QLineEdit>(notified, "modelImportOutput")->setText(output);
    widget<QPushButton>(notified, "modelImportCreate")->click();
    waitReady(notified);
    const QString status = widget<QLabel>(notified, "modelImportStatus")->text();
    check(QFile::exists(output) && status.contains("Created") && status.contains("notification failed"),
        "notification failure reports the successful file publication separately");

    QPointer<Dialog> destroyed;
    destroyed = new Dialog({ .isCurrent = [&] { delete destroyed.data(); return true; } });
    widget<QPushButton>(*destroyed, "modelImportPrepare")->click();
    check(destroyed.isNull(), "owner destruction in a context callback returns without accessing dead controls");
}

void compactLayout()
{
    Dialog dialog({});
    dialog.resize(640, 480);
    dialog.show();
    QCoreApplication::processEvents();
    bool inside = dialog.width() <= 640 && dialog.height() <= 480;
    for ( const char* name : { "modelImportPrepare", "modelImportCreate", "modelImportSaveImage", "modelImportCancel" } )
    {
        auto* action = widget<QPushButton>(dialog, name);
        inside = inside && dialog.rect().contains(QRect(action->mapTo(&dialog, QPoint()), action->size()));
    }
    check(inside, "model import scroll body leaves every action accessible at 640 by 480");
}

void stagingAndOwnerLoss()
{
    QTemporaryDir directory;
    const QString source = directory.filePath("triangle.obj");
    writeFile(source, kTriangle);
    auto& pool = ui::detail::previewWorkerPool();
    const int oldCount = pool.maxThreadCount();
    pool.setMaxThreadCount(1);
    Dialog dialog({});
    check(prepare(dialog, source), "staging boundary fixture prepares");
    const QString oldOutput = directory.filePath("old.3do");
    const QString newOutput = directory.filePath("new.3do");
    widget<QLineEdit>(dialog, "modelImportOutput")->setText(oldOutput);
    QSemaphore entered, release;
    auto blocker = ui::detail::runPreviewWorkerTask([&] { entered.release(); release.acquire(); });
    check(entered.tryAcquire(1, 5000), "owned staging blocker begins");
    widget<QPushButton>(dialog, "modelImportCreate")->click();
    widget<QLineEdit>(dialog, "modelImportOutput")->setText(newOutput);
    release.release();
    blocker.waitForFinished();
    waitReady(dialog);
    check(!QFile::exists(oldOutput) && !QFile::exists(newOutput)
            && widget<QPushButton>(dialog, "modelImportCreate")->isEnabled(),
        "destination edit cancels queued staging while retaining the prepared conversion");
    widget<QPushButton>(dialog, "modelImportCreate")->click();
    waitReady(dialog);
    check(QFile::exists(newOutput), "new destination requires and accepts an explicit retry");

    const QString contested = directory.filePath("contested.3do");
    widget<QLineEdit>(dialog, "modelImportOutput")->setText(contested);
    auto raceBlocker = ui::detail::runPreviewWorkerTask([&] { entered.release(); release.acquire(); });
    check(entered.tryAcquire(1, 5000), "owned publication race blocker begins");
    widget<QPushButton>(dialog, "modelImportCreate")->click();
    writeFile(contested, "existing concurrent file");
    release.release();
    raceBlocker.waitForFinished();
    waitReady(dialog);
    check(readFile(contested) == "existing concurrent file", "competing output creation wins without replacement");

    auto* owned = new Dialog({});
    check(prepare(*owned, source), "owner-loss staging fixture prepares");
    const QString abandoned = directory.filePath("abandoned.3do");
    widget<QLineEdit>(*owned, "modelImportOutput")->setText(abandoned);
    auto ownerBlocker = ui::detail::runPreviewWorkerTask([&] { entered.release(); release.acquire(); });
    check(entered.tryAcquire(1, 5000), "owned owner-loss blocker begins");
    widget<QPushButton>(*owned, "modelImportCreate")->click();
    delete owned;
    release.release();
    ownerBlocker.waitForFinished();
    check(pool.waitForDone(5000), "abandoned model worker finishes within a bounded deadline");
    QCoreApplication::processEvents();
    check(!QFile::exists(abandoned) && QDir(directory.path()).entryList({ ".sed-save-*.tmp" }, QDir::Files | QDir::Hidden).isEmpty(),
        "owner loss revokes queued publication and leaves no staged file");
    pool.setMaxThreadCount(oldCount);

    int contextCalls{};
    Dialog stale({ .isCurrent = [&] { return ++contextCalls < 4; } });
    check(prepare(stale, source), "final-context fixture prepares");
    const QString staleOutput = directory.filePath("stale-after-stage.3do");
    widget<QLineEdit>(stale, "modelImportOutput")->setText(staleOutput);
    widget<QPushButton>(stale, "modelImportCreate")->click();
    waitReady(stale);
    check(contextCalls == 4 && !QFile::exists(staleOutput)
            && QDir(directory.path()).entryList({ ".sed-save-*.tmp" }, QDir::Files | QDir::Hidden).isEmpty(),
        "context refusal after staging immediately releases the unpublished temporary file");
}

void malformedAndMappingControls()
{
    QTemporaryDir directory;
    const QString source = directory.filePath("triangle.obj");
    writeFile(source, kTriangle);
    Dialog dialog({});
    check(prepare(dialog, source), "input admission fixture prepares");
    for ( const QString& scale : { QString("0"), QString("-1"), QString("nan"), QString("1e99"), QString("1000001") } )
    {
        widget<QLineEdit>(dialog, "modelImportScale")->setText(scale);
        widget<QPushButton>(dialog, "modelImportPrepare")->click();
        check(!widget<QPushButton>(dialog, "modelImportCreate")->isEnabled()
                && widget<QLabel>(dialog, "modelImportStatus")->text().contains("Scale"),
            "invalid scale is refused explicitly without silently clamping");
    }
    widget<QLineEdit>(dialog, "modelImportScale")->setText("1");
    auto* table = widget<QTableWidget>(dialog, "modelImportMaterials");
    table->setItem(0, 2, new QTableWidgetItem("4097"));
    widget<QPushButton>(dialog, "modelImportPrepare")->click();
    check(!widget<QPushButton>(dialog, "modelImportCreate")->isEnabled(), "out-of-range explicit MAT dimensions refuse");
    table->setItem(0, 2, new QTableWidgetItem("64"));
    table->setItem(0, 1, new QTableWidgetItem("../crate.mat"));
    widget<QPushButton>(dialog, "modelImportPrepare")->click();
    waitReady(dialog);
    check(!widget<QPushButton>(dialog, "modelImportCreate")->isEnabled()
            && widget<QLabel>(dialog, "modelImportStatus")->text().contains("objInvalidOptions"),
        "core material-name refusal is preserved in the human diagnostic");
    table->setItem(0, 1, new QTableWidgetItem("crate.mat"));
    writeFile(source, "v 0 0 0\nf 1 2 3\n");
    widget<QPushButton>(dialog, "modelImportPrepare")->click();
    waitReady(dialog);
    check(!widget<QPushButton>(dialog, "modelImportCreate")->isEnabled(), "malformed source has no publishable result");
    mapping(dialog, "second", "second.mat");
    table->setCurrentCell(0, 0);
    widget<QPushButton>(dialog, "modelImportRemoveMaterial")->click();
    check(table->rowCount() == 1 && table->item(0, 0)->text() == "second", "Remove selected preserves other material mappings");
    while ( table->rowCount() < 256 )
    {
        widget<QPushButton>(dialog, "modelImportAddMaterial")->click();
    }
    check(!widget<QPushButton>(dialog, "modelImportAddMaterial")->isEnabled(), "mapping Add stops at the agent's 256-entry limit");
    widget<QPushButton>(dialog, "modelImportRemoveMaterial")->click();
    check(widget<QPushButton>(dialog, "modelImportAddMaterial")->isEnabled(), "removing a mapping frees one bounded row");
}

void chooserOwnerAndGeneration()
{
    const bool nativeDialogsDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QTemporaryDir directory;
    Dialog dialog({ .initialDirectory = directory.path() });
    widget<QLineEdit>(dialog, "modelImportOutput")->setText(directory.filePath("old.3do"));
    bool chooserObserved = false;
    QTimer::singleShot(0, &dialog, [&]
    {
        auto* chooser = dialog.findChild<QFileDialog*>();
        chooserObserved = chooser != nullptr;
        widget<QLineEdit>(dialog, "modelImportOutput")->setText(directory.filePath("new.3do"));
        if ( chooser )
        {
            chooser->reject();
        }
    });
    widget<QPushButton>(dialog, "modelImportBrowseOutput")->click();
    check(chooserObserved && widget<QLineEdit>(dialog, "modelImportOutput")->text() == directory.filePath("new.3do"),
        "nested output chooser does not replace a changed destination");

    const QString existing = directory.filePath("exists.3do");
    writeFile(existing, "preserve");
    bool replacePrompt = false;
    QTimer::singleShot(0, &dialog, [&]
    {
        const QPointer<QFileDialog> chooser = dialog.findChild<QFileDialog*>();
        check(!chooser.isNull(), "existing-output chooser is observable");
        if ( !chooser )
        {
            return;
        }
        chooser->selectFile(existing);
        QTimer::singleShot(0, &dialog, [&, chooser]
        {
            if ( auto* confirmation = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()) )
            {
                replacePrompt = true;
                confirmation->reject();
                if ( chooser )
                {
                    QTimer::singleShot(0, chooser, &QDialog::reject);
                }
            }
        });
        QMetaObject::invokeMethod(chooser, "accept", Qt::DirectConnection);
    });
    widget<QPushButton>(dialog, "modelImportBrowseOutput")->click();
    QCoreApplication::processEvents();
    check(!replacePrompt && readFile(existing) == "preserve",
        "create-only output chooser never offers permission to replace an existing file");

    QPointer<Dialog> owner = new Dialog({ .initialDirectory = directory.path() });
    bool ownerChooserObserved = false;
    QTimer::singleShot(0, [&]
    {
        ownerChooserObserved = owner && owner->findChild<QFileDialog*>();
        delete owner.data();
    });
    widget<QPushButton>(*owner, "modelImportBrowseSource")->click();
    check(ownerChooserObserved && owner.isNull(), "nested source chooser resumes safely after dialog destruction");
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDialogsDisabled);
}

void numericPolicyAndUnknownHostException()
{
    const auto previous = ui::uiDecimalSeparator();
    ui::applyUiDecimalSeparator(UiDecimalSeparator::comma);
    QTemporaryDir directory;
    const QString source = directory.filePath("triangle.obj");
    writeFile(source, kTriangle);
    Dialog dialog({});
    auto* scale = widget<QLineEdit>(dialog, "modelImportScale");
    scale->setText("2,5");
    scale->setModified(true);
    check(prepare(dialog, source), "comma UI decimal policy accepts a fractional model scale");
    scale->setText("3,25");
    scale->setModified(true);
    ui::applyUiDecimalSeparator(UiDecimalSeparator::dot);
    check(scale->text() == "3,25", "live decimal preference change preserves the raw modified scale draft");
    widget<QPushButton>(dialog, "modelImportPrepare")->click();
    waitReady(dialog);
    const QString destination = directory.filePath("localized.3do");
    widget<QLineEdit>(dialog, "modelImportOutput")->setText(destination);
    widget<QPushButton>(dialog, "modelImportCreate")->click();
    waitReady(dialog);
    assets::ObjImportOptions options;
    options.materials.push_back({ "crate", "crate.mat", 64, 32 });
    options.scale = 3.25;
    check(QFile::exists(destination)
            && readFile(destination) == QByteArray::fromStdString(
                assets::saveTo3DO(assets::importModel(kTriangle.toStdString(), options).model))
            && scale->text() == "3.25",
        "raw scale commits in its original locale then adopts the new display policy without changing output bytes");
    ui::applyUiDecimalSeparator(UiDecimalSeparator::comma);
    check(scale->text() == "3,25" && widget<QPushButton>(dialog, "modelImportCreate")->isEnabled(),
        "settled scale presentation updates without invalidating the prepared model");
    ui::applyUiDecimalSeparator(previous);

    Dialog hostile({ .isCurrent = []() -> bool { throw 7; } });
    bool escaped = false;
    try
    {
        widget<QPushButton>(hostile, "modelImportPrepare")->click();
    }
    catch ( ... )
    {
        escaped = true;
    }
    check(!escaped && !widget<QPushButton>(hostile, "modelImportCreate")->isEnabled()
            && widget<QLabel>(hostile, "modelImportStatus")->text().contains("exception"),
        "unknown context exceptions become a refusal instead of escaping the Qt handler");
}
} // namespace

void runModelImportDialogTests()
{
    objConversion();
    placementAndInvalidation();
    refusedInputsAndChangedSource();
    canceledAndStaleWork();
    glbAndIndependentImage();
    matDimensionsAndProtectedInput();
    hostCallbacks();
    compactLayout();
    stagingAndOwnerLoss();
    malformedAndMappingControls();
    chooserOwnerAndGeneration();
    numericPolicyAndUnknownHostException();
}
} // namespace sed::app_tests
