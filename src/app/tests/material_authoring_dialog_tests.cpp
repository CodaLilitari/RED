#include "sed/assets/mat.hpp"
#include "ui/components/preview_worker_pool.hpp"
#include "ui/resources/material_authoring_dialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QImageWriter>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSemaphore>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThread>

#include <cstring>
#include <iostream>

namespace
{
using Dialog = sed::ui::resources::MaterialAuthoringDialog;
int failures{};

void check(const bool condition, const char* message)
{
    if ( !condition )
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <class T> T* control(Dialog& dialog, const char* name)
{
    auto* value = dialog.findChild<T*>(QString::fromLatin1(name));
    check(value != nullptr, name);
    return value;
}

QByteArray paletteBytes()
{
    QByteArray bytes(64 + 256 * 3, '\0');
    std::memcpy(bytes.data(), "CMP ", 4);
    for ( int index = 0; index < 256; ++index )
    {
        for ( int component = 0; component < 3; ++component )
        {
            bytes[64 + index * 3 + component] = static_cast<char>(index);
        }
    }
    return bytes;
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "read fixture file");
    return file.readAll();
}

void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "write fixture file");
    check(file.write(bytes) == bytes.size(), "complete fixture write");
}

QString imageFile(const QString& root, const QString& name, const int grey, const int side = 8)
{
    QImage image(side, side, QImage::Format_RGBA8888);
    image.fill(QColor(grey, grey, grey, 90));
    const QString path = root + '/' + name;
    check(image.save(path, "PNG"), "write analytic PNG fixture");
    return path;
}

bool waitReady(Dialog& dialog)
{
    auto* preview = control<QPushButton>(dialog, "materialPreview");
    if ( !preview )
    {
        return false;
    }
    QElapsedTimer deadline;
    deadline.start();
    while ( !preview->isEnabled() && deadline.elapsed() < 5000 )
    {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    check(preview->isEnabled(), "bounded worker completion");
    return preview->isEnabled();
}

bool addImage(Dialog& dialog, const QString& path)
{
    auto* input = control<QLineEdit>(dialog, "materialSourcePath");
    auto* add = control<QPushButton>(dialog, "materialAddSource");
    if ( !input || !add )
    {
        return false;
    }
    input->setText(path);
    add->click();
    return true;
}

void basicControls()
{
    Dialog dialog({});
    auto* format = control<QComboBox>(dialog, "materialFormat");
    if ( format )
    {
        check(format->currentData().toString() == "indexed8" && format->findData("rgb565") >= 0,
            "format selection preserves indexed default and exposes RGB565");
    }
    control<QListWidget>(dialog, "materialSources");
    control<QLineEdit>(dialog, "materialCmpPath");
    control<QCheckBox>(dialog, "materialResize");
    control<QComboBox>(dialog, "materialWidth");
    control<QComboBox>(dialog, "materialHeight");
    auto* mips = control<QSpinBox>(dialog, "materialMipmaps");
    auto* low = control<QSpinBox>(dialog, "materialIndexMin");
    auto* high = control<QSpinBox>(dialog, "materialIndexMax");
    control<QCheckBox>(dialog, "materialDither");
    control<QCheckBox>(dialog, "materialReserveZero");
    control<QPushButton>(dialog, "materialCancelWork");
    auto* create = control<QPushButton>(dialog, "materialCreate");
    auto* warning = control<QLabel>(dialog, "materialOpaqueWarning");
    if ( mips )
    {
        check(mips->minimum() == 1 && mips->maximum() == 4, "mip count bounds");
    }
    if ( low && high )
    {
        check(low->minimum() == 0 && high->maximum() == 255, "palette index bounds");
    }
    if ( create )
    {
        check(!create->isEnabled(), "creation requires a validated preview");
    }
    if ( warning )
    {
        check(warning->text().contains("opaque") && warning->text().contains("alpha"), "explicit alpha limitation");
    }
}

void orderedCelsAndCreateOnly()
{
    QTemporaryDir root;
    check(root.isValid(), "isolated material workspace");
    int published{};
    Dialog::Context context;
    context.paletteName = "Synthetic greyscale CMP";
    context.paletteBytes = paletteBytes();
    context.published = [&](const QString&) { ++published; };
    Dialog dialog(context);
    if ( !addImage(dialog, imageFile(root.path(), "light.png", 220)) ||
         !addImage(dialog, imageFile(root.path(), "dark.png", 20)) )
    {
        return;
    }
    auto* sources = control<QListWidget>(dialog, "materialSources");
    auto* up = control<QPushButton>(dialog, "materialMoveUp");
    auto* preview = control<QPushButton>(dialog, "materialPreview");
    auto* create = control<QPushButton>(dialog, "materialCreate");
    auto* output = control<QLineEdit>(dialog, "materialOutputPath");
    auto* report = control<QPlainTextEdit>(dialog, "materialReport");
    auto* original = control<QLabel>(dialog, "materialSourcePreview");
    auto* mapped = control<QLabel>(dialog, "materialOutputPreview");
    if ( !sources || !up || !preview || !create || !output || !report || !original || !mapped )
    {
        return;
    }
    sources->setCurrentRow(1);
    up->click();
    preview->click();
    if ( !waitReady(dialog) )
    {
        return;
    }
    check(create->isEnabled(), "successful conversion enables create");
    check(report->toPlainText().contains("Cels: 2") && report->toPlainText().contains("Mean error: 0"),
        "visible exact conversion report");
    check(!original->pixmap().isNull() && !mapped->pixmap().isNull(), "decoded source and MAT previews");
    const QString destination = root.path() + "/new.mat";
    output->setText(destination);
    create->click();
    if ( !waitReady(dialog) )
    {
        return;
    }
    check(QFile::exists(destination) && published == 1, "new file publication callback");
    if ( !QFile::exists(destination) )
    {
        return;
    }
    const QByteArray saved = readFile(destination);
    std::vector<std::byte> bytes(static_cast<std::size_t>(saved.size()));
    std::memcpy(bytes.data(), saved.constData(), bytes.size());
    const auto palette =
        sed::assets::parseCmpPalette(std::span(reinterpret_cast<const std::byte*>(context.paletteBytes.constData()),
            static_cast<std::size_t>(context.paletteBytes.size())));
    const auto mat = sed::assets::MatFile::parse(std::move(bytes), &*palette);
    check(mat.celCount() == 2 && mat.bitsPerPixel() == 8 && !mat.isTrueColor(), "actual indexed MAT readback");
    check(
        mat.decodeCelRgba8(0).rgba[0] == 20 && mat.decodeCelRgba8(1).rgba[0] == 220, "GUI cel order reaches MAT bytes");
    check(mat.decodeCelRgba8(0).rgba[3] == 255, "output is opaque despite input alpha");
    create->click();
    waitReady(dialog);
    check(readFile(destination) == saved && published == 1, "existing target remains byte identical");
}

void staleAndInvalidInputs()
{
    QTemporaryDir root;
    bool current = true;
    Dialog::Context context;
    context.paletteName = "Captured CMP";
    context.paletteBytes = paletteBytes();
    context.isCurrent = [&] { return current; };
    Dialog dialog(context);
    const QString image = imageFile(root.path(), "source.png", 40);
    if ( !addImage(dialog, image) )
    {
        return;
    }
    auto* preview = control<QPushButton>(dialog, "materialPreview");
    auto* create = control<QPushButton>(dialog, "materialCreate");
    auto* output = control<QLineEdit>(dialog, "materialOutputPath");
    auto* status = control<QLabel>(dialog, "materialStatus");
    auto* dither = control<QCheckBox>(dialog, "materialDither");
    if ( !preview || !create || !output || !status || !dither )
    {
        return;
    }
    preview->click();
    waitReady(dialog);
    dither->setChecked(!dither->isChecked());
    check(!create->isEnabled(), "options invalidate prepared bytes immediately");
    preview->click();
    waitReady(dialog);
    writeFile(image, "broken PNG");
    const QString destination = root.path() + "/must-not-exist.mat";
    output->setText(destination);
    create->click();
    waitReady(dialog);
    check(!QFile::exists(destination) && status->text().contains("changed"), "changed cel prevents publication");
    preview->click();
    waitReady(dialog);
    check(!create->isEnabled() && status->text().contains("PNG:"), "authoritative decoder diagnostic remains visible");
    current = false;
    preview->click();
    check(!create->isEnabled() && status->text().contains("context"), "stale document/resource context refused");
}

void optionsAndInputBoundaries()
{
    QTemporaryDir root;
    Dialog::Context context;
    context.paletteBytes = paletteBytes();
    Dialog dialog(context);
    const QString source = imageFile(root.path(), "source.png", 0);
    addImage(dialog, source);
    auto* preview = control<QPushButton>(dialog, "materialPreview");
    auto* create = control<QPushButton>(dialog, "materialCreate");
    auto* report = control<QPlainTextEdit>(dialog, "materialReport");
    auto* status = control<QLabel>(dialog, "materialStatus");
    control<QCheckBox>(dialog, "materialResize")->setChecked(true);
    control<QComboBox>(dialog, "materialWidth")->setCurrentText("16");
    control<QComboBox>(dialog, "materialHeight")->setCurrentText("32");
    control<QSpinBox>(dialog, "materialMipmaps")->setValue(2);
    control<QSpinBox>(dialog, "materialIndexMax")->setValue(1);
    control<QCheckBox>(dialog, "materialReserveZero")->setChecked(true);
    control<QCheckBox>(dialog, "materialDither")->setChecked(false);
    preview->click();
    waitReady(dialog);
    check(create->isEnabled() && report->toPlainText().contains("16 × 32") &&
              report->toPlainText().contains("Mipmaps: 2"),
        "resize and mip controls reach conversion");
    const QString output = root.path() + "/options.mat";
    control<QLineEdit>(dialog, "materialOutputPath")->setText(output);
    create->click();
    waitReady(dialog);
    const QByteArray saved = readFile(output);
    std::vector<std::byte> bytes(static_cast<std::size_t>(saved.size()));
    std::memcpy(bytes.data(), saved.constData(), bytes.size());
    const auto mat = sed::assets::MatFile::parse(std::move(bytes));
    check(mat.cel(0).width == 16 && mat.cel(0).height == 32 && mat.cel(0).mipmaps.size() == 2,
        "resize/mips preserved in independently decoded output");
    check(mat.cel(0).mipmaps[0].pixels.front() == 1, "reserved zero and index range force palette index one");
    control<QSpinBox>(dialog, "materialIndexMax")->setValue(0);
    preview->click();
    waitReady(dialog);
    check(!create->isEnabled() && status->text().contains("allowed"), "empty permitted palette is refused");
    for ( int index = 1; index < 17; ++index )
    {
        addImage(dialog, source);
    }
    check(control<QListWidget>(dialog, "materialSources")->count() == 16 && status->text().contains("1 to 16"),
        "seventeenth cel leaves existing ordered list intact");
}

void jpegAndPaletteBoundaries()
{
    QTemporaryDir root;
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(QColor(80, 80, 80));
    const QString baseline = root.path() + "/baseline.jpg";
    const QString progressive = root.path() + "/progressive.jpg";
    QImageWriter baselineWriter(baseline, "jpeg");
    check(baselineWriter.write(image), "generate baseline JPEG fixture");
    QImageWriter progressiveWriter(progressive, "jpeg");
    progressiveWriter.setProgressiveScanWrite(true);
    check(progressiveWriter.write(image), "generate progressive JPEG fixture");
    const QString cmp = root.path() + "/palette.cmp";
    writeFile(cmp, paletteBytes());
    Dialog dialog({});
    control<QLineEdit>(dialog, "materialCmpPath")->setText(cmp);
    addImage(dialog, baseline);
    auto* preview = control<QPushButton>(dialog, "materialPreview");
    auto* create = control<QPushButton>(dialog, "materialCreate");
    auto* status = control<QLabel>(dialog, "materialStatus");
    preview->click();
    waitReady(dialog);
    check(create->isEnabled(), "authoritative baseline JPEG decoder used by GUI");
    const QString output = root.path() + "/changed-cmp.mat";
    control<QLineEdit>(dialog, "materialOutputPath")->setText(output);
    writeFile(cmp, "changed CMP");
    create->click();
    waitReady(dialog);
    check(!QFile::exists(output) && status->text().contains("changed"),
        "loose CMP identity rechecked before publication");
    preview->click();
    waitReady(dialog);
    check(!create->isEnabled() && status->text().contains("CMP:"), "invalid CMP refused");
    writeFile(cmp, paletteBytes());
    control<QPushButton>(dialog, "materialRemoveSource")->click();
    addImage(dialog, progressive);
    preview->click();
    waitReady(dialog);
    check(!create->isEnabled() && status->text().contains("JPEG:"),
        "unsupported progressive JPEG reports authoritative diagnostic");
    Dialog::Context large;
    large.paletteBytes = QByteArray(1024 * 1024 + 1, '\0');
    Dialog oversized(std::move(large));
    check(!control<QCheckBox>(oversized, "materialUseProjectCmp")->isEnabled() &&
              control<QLabel>(oversized, "materialStatus")->text().contains("limit"),
        "captured CMP size admission");
}

class QueuedWork
{
public:
    QueuedWork() : previous_(sed::ui::detail::previewWorkerCount())
    {
        sed::ui::detail::setPreviewWorkerCount(1);
        future_ = sed::ui::detail::runPreviewWorkerTask(
            [this]
            {
                started_.release();
                released_.tryAcquire(1, 5000);
            });
        check(started_.tryAcquire(1, 5000), "fixture owns the single preview worker");
    }
    ~QueuedWork()
    {
        released_.release();
        check(sed::ui::detail::previewWorkerPool().waitForDone(5000), "queued work completes within deadline");
        sed::ui::detail::setPreviewWorkerCount(previous_);
    }

private:
    int previous_;
    QSemaphore started_, released_;
    QFuture<void> future_;
};

void rgb565WithoutPalette()
{
    QTemporaryDir root;
    QImage image(8, 8, QImage::Format_RGBA8888);
    image.fill(QColor(255, 0, 0, 0));
    image.setPixelColor(1, 0, QColor(0, 255, 0, 90));
    image.setPixelColor(2, 0, QColor(128, 128, 128, 255));
    const QString source = root.path() + "/rgb.png";
    check(image.save(source, "PNG"), "write literal true-colour fixture");
    Dialog dialog({});
    auto* format = control<QComboBox>(dialog, "materialFormat");
    if ( !format )
    {
        return;
    }
    addImage(dialog, source);
    control<QLineEdit>(dialog, "materialCmpPath")->setText(root.path() + "/must-not-read.cmp");
    format->setCurrentIndex(format->findData("rgb565"));
    auto* preview = control<QPushButton>(dialog, "materialPreview");
    auto* create = control<QPushButton>(dialog, "materialCreate");
    auto* report = control<QPlainTextEdit>(dialog, "materialReport");
    check(!control<QLineEdit>(dialog, "materialCmpPath")->isEnabled()
            && !control<QPushButton>(dialog, "materialBrowseCmp")->isEnabled()
            && !control<QCheckBox>(dialog, "materialDither")->isEnabled()
            && !control<QSpinBox>(dialog, "materialIndexMin")->isEnabled()
            && !control<QCheckBox>(dialog, "materialReserveZero")->isEnabled(),
        "RGB565 disables palette-only controls");
    preview->click();
    waitReady(dialog);
    check(create->isEnabled() && report->toPlainText().contains("16-bit RGB565")
            && !report->toPlainText().contains("CMP SHA-256"),
        "RGB565 previews without reading a palette and labels its format honestly");
    const QString output = root.path() + "/rgb.mat";
    control<QLineEdit>(dialog, "materialOutputPath")->setText(output);
    create->click();
    waitReady(dialog);
    check(QFile::exists(output), "RGB565 GUI publishes a new MAT");
    if ( QFile::exists(output) )
    {
        const QByteArray saved = readFile(output);
        check(saved.mid(140, 6) == QByteArray::fromHex("00f8e0071084"),
            "RGB565 GUI writes literal expected packed pixels");
        std::vector<std::byte> bytes(static_cast<std::size_t>(saved.size()));
        std::memcpy(bytes.data(), saved.constData(), bytes.size());
        const auto mat = sed::assets::MatFile::parse(std::move(bytes));
        check(mat.isTrueColor() && mat.bitsPerPixel() == 16 && mat.decodeCelRgba8(0).rgba[3] == 255,
            "RGB565 GUI result decodes without a palette and ignores alpha");
    }
    format->setCurrentIndex(format->findData("indexed8"));
    check(!create->isEnabled() && control<QLineEdit>(dialog, "materialCmpPath")->isEnabled(),
        "format changes invalidate prepared bytes and restore palette controls");
    {
        QueuedWork queue;
        format->setCurrentIndex(format->findData("rgb565"));
        preview->click();
        format->setCurrentIndex(format->findData("indexed8"));
    }
    waitReady(dialog);
    check(!create->isEnabled(), "queued RGB565 conversion cannot publish after format change");
    format->setCurrentIndex(format->findData("rgb565"));
    preview->click();
    waitReady(dialog);
    const QString staleOutput = root.path() + "/stale-format.mat";
    control<QLineEdit>(dialog, "materialOutputPath")->setText(staleOutput);
    {
        QueuedWork queue;
        create->click();
        format->setCurrentIndex(format->findData("indexed8"));
    }
    waitReady(dialog);
    check(!QFile::exists(staleOutput) && !create->isEnabled(),
        "format change cancels queued RGB565 publication");
}

void cancellationAndStalePublication()
{
    QTemporaryDir root;
    Dialog::Context context;
    context.paletteBytes = paletteBytes();
    bool current = true;
    context.isCurrent = [&] { return current; };
    Dialog dialog(context);
    addImage(dialog, imageFile(root.path(), "source.png", 90));
    auto* preview = control<QPushButton>(dialog, "materialPreview");
    auto* create = control<QPushButton>(dialog, "materialCreate");
    preview->click();
    waitReady(dialog);
    const QString output = root.path() + "/cancelled.mat";
    control<QLineEdit>(dialog, "materialOutputPath")->setText(output);
    {
        QueuedWork queue;
        create->click();
        control<QPushButton>(dialog, "materialCancelWork")->click();
    }
    waitReady(dialog);
    check(!QFile::exists(output) && !create->isEnabled(), "cancelled queued publication cannot write");
    {
        QueuedWork queue;
        preview->click();
        control<QCheckBox>(dialog, "materialDither")->setChecked(false);
    }
    waitReady(dialog);
    check(!create->isEnabled(), "queued conversion cannot publish after option change");
    preview->click();
    waitReady(dialog);
    const auto before = QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    create->click();
    current = false; // final GUI publication guard must reject the staged worker result.
    waitReady(dialog);
    check(!QFile::exists(output), "stale context cannot publish staged output");
    check(QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden) == before,
        "stale completion immediately removes its staged temporary file");
    current = true;
    preview->click();
    waitReady(dialog);
    {
        QueuedWork queue;
        create->click();
        writeFile(output, "Another creator owns this file");
    }
    waitReady(dialog);
    check(readFile(output) == "Another creator owns this file", "concurrent destination creation is never replaced");
    check(control<QLabel>(dialog, "materialStatus")->text().contains("exists"), "atomic writer conflict is visible");
    const QString closedOutput = root.path() + "/closed.mat";
    auto owned = std::make_unique<Dialog>(context);
    addImage(*owned, root.path() + "/source.png");
    control<QPushButton>(*owned, "materialPreview")->click();
    waitReady(*owned);
    control<QLineEdit>(*owned, "materialOutputPath")->setText(closedOutput);
    {
        QueuedWork queue;
        control<QPushButton>(*owned, "materialCreate")->click();
        owned.reset();
    }
    QCoreApplication::processEvents();
    check(!QFile::exists(closedOutput), "destroyed owner cannot publish pending output");
}

void outputPathChanges()
{
    QTemporaryDir root;
    Dialog::Context context;
    context.paletteBytes = paletteBytes();
    Dialog dialog(context);
    addImage(dialog, imageFile(root.path(), "source.png", 30));
    auto* output = control<QLineEdit>(dialog, "materialOutputPath");
    auto* preview = control<QPushButton>(dialog, "materialPreview");
    auto* create = control<QPushButton>(dialog, "materialCreate");
    const QString original = root.path() + "/original.mat";
    const QString changed = root.path() + "/changed.mat";
    {
        QueuedWork queue;
        preview->click();
        output->setText(original);
    }
    waitReady(dialog);
    check(create->isEnabled(), "output filename edit does not cancel image conversion");
    if ( !create->isEnabled() )
    {
        return;
    }
    {
        QueuedWork queue;
        create->click();
        output->setText(changed);
    }
    waitReady(dialog);
    check(!QFile::exists(original) && !QFile::exists(changed) && create->isEnabled(),
        "changed output cancels pending old publication but retains prepared conversion");
    create->click();
    waitReady(dialog);
    check(!QFile::exists(original) && QFile::exists(changed), "explicit create uses the newly selected output");
}

void pngHeaderAdmission()
{
    QTemporaryDir root;
    const QString path = imageFile(root.path(), "nonstandard-order.png", 30);
    QByteArray bytes = readFile(path);
    // An empty, CRC-correct tEXt chunk before IHDR is accepted by the core
    // decoder, but cannot bypass this dialog's pre-decode pixel admission.
    bytes.insert(8, QByteArray::fromHex("00000000744558749642c585"));
    writeFile(path, bytes);
    Dialog::Context context;
    context.paletteBytes = paletteBytes();
    Dialog dialog(context);
    addImage(dialog, path);
    control<QPushButton>(dialog, "materialPreview")->click();
    waitReady(dialog);
    check(!control<QPushButton>(dialog, "materialCreate")->isEnabled() &&
              control<QLabel>(dialog, "materialStatus")->text().contains("IHDR"),
        "PNG must expose its leading dimensions before pixel allocation");
}

void renderSnapshot(const QString& destination)
{
    QTemporaryDir root;
    Dialog::Context context;
    context.paletteName = "Synthetic greyscale palette";
    context.paletteBytes = paletteBytes();
    Dialog dialog(context);
    addImage(dialog, imageFile(root.path(), "cel-01.png", 80));
    addImage(dialog, imageFile(root.path(), "cel-02.png", 160));
    control<QLineEdit>(dialog, "materialOutputPath")->setText(root.path() + "/new-material.mat");
    control<QPushButton>(dialog, "materialPreview")->click();
    waitReady(dialog);
    dialog.ensurePolished();
    dialog.layout()->activate();
    QPixmap image(dialog.size());
    dialog.render(&image);
    check(image.save(destination, "PNG"), "render synthetic widget snapshot");
}
} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    // Optional explicit font for offscreen render evidence; normal tests do not
    // depend on a host font installation or change system/application settings.
    const QString fontFile = qEnvironmentVariable("SED_TEST_FONT_FILE");
    if ( !fontFile.isEmpty() )
    {
        const int fontId = QFontDatabase::addApplicationFont(fontFile);
        check(fontId >= 0, "load explicitly supplied offscreen evidence font");
        if ( fontId >= 0 )
        {
            application.setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).first(), 10));
        }
    }
    basicControls();
    orderedCelsAndCreateOnly();
    staleAndInvalidInputs();
    optionsAndInputBoundaries();
    jpegAndPaletteBoundaries();
    rgb565WithoutPalette();
    cancellationAndStalePublication();
    outputPathChanges();
    pngHeaderAdmission();
    if ( argc == 3 && QString::fromLocal8Bit(argv[1]) == "--snapshot" )
    {
        renderSnapshot(QString::fromLocal8Bit(argv[2]));
    }
    std::cout << "Material dialog failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
