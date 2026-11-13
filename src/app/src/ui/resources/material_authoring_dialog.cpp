#include "material_authoring_dialog.hpp"

#include "../../../../core/src/assets/atomic_file_write.hpp"
#include "sed/assets/cmp.hpp"
#include "sed/assets/jpeg.hpp"
#include "sed/assets/mat.hpp"
#include "sed/assets/mat_write.hpp"
#include "ui/components/preview_worker_pool.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSpinBox>
#include <QVBoxLayout>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

namespace sed::ui::resources
{
namespace
{
constexpr qint64 kImageByteLimit = 32 * 1024 * 1024;
constexpr qint64 kInputByteLimit = 128 * 1024 * 1024;
constexpr qint64 kPaletteByteLimit = 1024 * 1024;
constexpr std::uint64_t kSourcePixelLimit = 16 * 1024 * 1024;

std::span<const std::byte> byteSpan(const QByteArray& bytes)
{
    return { reinterpret_cast<const std::byte*>(bytes.constData()), static_cast<std::size_t>(bytes.size()) };
}

std::filesystem::path filePath(const QString& path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::u8path(path.toUtf8().toStdString());
#endif
}

QByteArray readBounded(const QString& path, const qint64 limit)
{
    QFile file(path);
    if ( !QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) )
    {
        throw std::runtime_error((QStringLiteral("Cannot read source file: ") + path).toStdString());
    }
    QByteArray bytes = file.read(limit + 1);
    if ( file.error() != QFileDevice::NoError )
    {
        throw std::runtime_error(file.errorString().toStdString());
    }
    if ( bytes.size() > limit || !file.atEnd() )
    {
        throw std::runtime_error(
            (QStringLiteral("Source exceeds the %1-byte limit: %2").arg(limit).arg(path)).toStdString());
    }
    return bytes;
}

struct Source
{
    QString path;
    QByteArray bytes;
    QByteArray hash;
};

Source capture(const QString& path, const qint64 limit)
{
    Source source{ QFileInfo(path).absoluteFilePath(), readBounded(path, limit), {} };
    source.hash = QCryptographicHash::hash(source.bytes, QCryptographicHash::Sha256);
    return source;
}

void requireUnchanged(const Source& source, const qint64 limit)
{
    if ( QCryptographicHash::hash(readBounded(source.path, limit), QCryptographicHash::Sha256) != source.hash )
    {
        throw std::runtime_error((QStringLiteral("Source changed; preview again: ") + source.path).toStdString());
    }
}

void requireActive(const detail::PreviewWorkerPublicationGuard& guard)
{
    if ( guard.canceled->load(std::memory_order_acquire) )
    {
        throw std::runtime_error("Cancelled.");
    }
}

QSize imageSize(const QByteArray& bytes)
{
    if ( assets::isJpeg(byteSpan(bytes)) )
    {
        const auto info = assets::readJpegInfo(byteSpan(bytes));
        return { info.width, info.height };
    }
    // Admission only; decodePng remains authoritative for CRC/chunks/pixels.
    static constexpr unsigned char signature[]{ 137, 80, 78, 71, 13, 10, 26, 10 };
    if ( bytes.size() >= 8 && std::memcmp(bytes.constData(), signature, 8) == 0 )
    {
        if ( bytes.size() < 33 || bytes.mid(12, 4) != "IHDR" || bytes.mid(8, 4) != QByteArray::fromHex("0000000d") )
        {
            throw std::runtime_error(
                "PNG: a complete 13-byte IHDR must be the first chunk for bounded image admission.");
        }
        const auto dimension = [&](const int offset)
        {
            std::uint32_t value{};
            for ( int i = 0; i < 4; ++i )
            {
                value = (value << 8) | static_cast<unsigned char>(bytes[offset + i]);
            }
            return value > 8192 ? 8193 : static_cast<int>(value);
        };
        return { dimension(16), dimension(20) };
    }
    return {};
}

QImage previewImage(const std::uint8_t* pixels, const int width, const int height)
{
    return QImage(pixels, width, height, width * 4, QImage::Format_RGBA8888)
        .copy()
        .scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

struct Prepared
{
    std::vector<Source> sources;
    std::optional<Source> paletteSource;
    QByteArray paletteHash;
    QString paletteName;
    assets::IndexedMatResult material;
    std::vector<QImage> original;
    std::vector<QImage> mapped;
    QImage palette;
};

struct Request
{
    QStringList paths;
    QByteArray paletteBytes;
    QString palettePath;
    QString paletteName;
    assets::IndexedMatOptions options;
    bool resize{};
    int width{};
    int height{};
};

struct WorkResult
{
    std::shared_ptr<Prepared> prepared;
    std::unique_ptr<assets::detail::StagedAssetFile> staged;
    QString destination;
    QString error;
};

void verifySources(const Prepared& prepared, const detail::PreviewWorkerPublicationGuard& guard)
{
    for ( const Source& source : prepared.sources )
    {
        requireActive(guard);
        requireUnchanged(source, kImageByteLimit);
    }
    if ( prepared.paletteSource )
    {
        requireUnchanged(*prepared.paletteSource, kPaletteByteLimit);
    }
    requireActive(guard);
}

std::shared_ptr<WorkResult> prepareMaterial(Request request, const detail::PreviewWorkerPublicationGuard guard)
{
    auto result = std::make_shared<WorkResult>();
    try
    {
        requireActive(guard);
        auto prepared = std::make_shared<Prepared>();
        if ( !request.palettePath.isEmpty() )
        {
            prepared->paletteSource = capture(request.palettePath, kPaletteByteLimit);
            request.paletteBytes = prepared->paletteSource->bytes;
            request.paletteName = prepared->paletteSource->path;
        }
        if ( request.paletteBytes.size() > kPaletteByteLimit )
        {
            throw std::runtime_error("CMP exceeds the 1 MiB capture limit.");
        }
        const auto palette = assets::parseCmpPalette(byteSpan(request.paletteBytes));
        if ( !palette )
        {
            throw std::runtime_error("CMP: a valid captured 256-colour palette is required.");
        }
        prepared->paletteHash = QCryptographicHash::hash(request.paletteBytes, QCryptographicHash::Sha256);
        prepared->paletteName = request.paletteName;
        prepared->palette = QImage(16, 16, QImage::Format_RGB32);
        for ( int index = 0; index < 256; ++index )
        {
            const auto& colour = (*palette)[static_cast<std::size_t>(index)];
            prepared->palette.setPixel(index % 16, index / 16, qRgb(colour.r, colour.g, colour.b));
        }
        std::vector<assets::RgbaImage> cels;
        qint64 sourceBytes{};
        std::uint64_t pixels{};
        for ( const QString& path : request.paths )
        {
            requireActive(guard);
            Source source = capture(path, kImageByteLimit);
            sourceBytes += source.bytes.size();
            if ( sourceBytes > kInputByteLimit )
            {
                throw std::runtime_error("Images exceed the 128 MiB total capture limit.");
            }
            const QSize size = imageSize(source.bytes);
            if ( size.width() > 8192 || size.height() > 8192 )
            {
                throw std::runtime_error("Image dimensions exceed the decoder limit of 8192 pixels per side.");
            }
            if ( size.width() > 0 && size.height() > 0 )
            {
                pixels += static_cast<std::uint64_t>(size.width()) * static_cast<std::uint64_t>(size.height());
                if ( pixels > kSourcePixelLimit )
                {
                    throw std::runtime_error("Images exceed the 16777216 source-pixel budget.");
                }
            }
            auto decoded = assets::isJpeg(byteSpan(source.bytes)) ? assets::decodeJpeg(byteSpan(source.bytes))
                                                                  : assets::decodePng(byteSpan(source.bytes));
            prepared->original.push_back(previewImage(decoded.rgba.data(), decoded.width, decoded.height));
            if ( request.resize )
            {
                decoded = assets::resizeRgbaArea(decoded, request.width, request.height);
            }
            cels.push_back(std::move(decoded));
            prepared->sources.push_back(std::move(source));
        }
        requireActive(guard);
        prepared->material = assets::buildIndexedMat(cels, *palette, request.options);
        requireActive(guard);
        std::vector<std::byte> bytes(prepared->material.bytes.size());
        std::memcpy(bytes.data(), prepared->material.bytes.data(), bytes.size());
        const auto readback = assets::MatFile::parse(std::move(bytes), &*palette);
        if ( readback.bitsPerPixel() != 8 || readback.celCount() != static_cast<int>(cels.size()) )
        {
            throw std::runtime_error("MAT readback did not match the prepared indexed cels.");
        }
        for ( int cel = 0; cel < readback.celCount(); ++cel )
        {
            const auto decoded = readback.decodeCelRgba8(cel);
            prepared->mapped.push_back(previewImage(decoded.rgba.data(), decoded.width, decoded.height));
        }
        verifySources(*prepared, guard);
        result->prepared = std::move(prepared);
    }
    catch ( const std::exception& error )
    {
        result->error = QString::fromUtf8(error.what());
    }
    return result;
}

std::shared_ptr<WorkResult> stageMaterial(
    std::shared_ptr<Prepared> prepared, const QString destination, const detail::PreviewWorkerPublicationGuard guard)
{
    auto result = std::make_shared<WorkResult>();
    try
    {
        verifySources(*prepared, guard);
        auto staged = std::make_unique<assets::detail::StagedAssetFile>(filePath(destination));
        staged->writeAndClose(std::as_bytes(std::span(prepared->material.bytes)));
        verifySources(*prepared, guard);
        result->destination = destination;
        result->staged = std::move(staged);
    }
    catch ( const std::exception& error )
    {
        result->error = QString::fromUtf8(error.what());
    }
    return result;
}
} // namespace

class MaterialAuthoringDialog::Impl
{
public:
    Impl(MaterialAuthoringDialog& dialog, Context context) : dialog_(dialog), context_(std::move(context))
    {
        dialog_.setWindowTitle(QStringLiteral("Create indexed MAT from images"));
        dialog_.resize(850, 760);
        auto* layout = new QVBoxLayout(&dialog_);
        auto* warning =
            new QLabel(QStringLiteral("Creates an opaque 8-bit indexed MAT. Source alpha is ignored; reserving index "
                                      "zero does not create transparency. True-colour MAT writing is not supported."),
                &dialog_);
        warning->setObjectName(QStringLiteral("materialOpaqueWarning"));
        warning->setWordWrap(true);
        layout->addWidget(warning);
        auto* sourcesRow = new QHBoxLayout;
        sourcePath_ = line("materialSourcePath");
        sourcePath_->setPlaceholderText(QStringLiteral("PNG or baseline JPEG path"));
        sourcesRow->addWidget(sourcePath_);
        auto* add = button("Add", "materialAddSource");
        auto* browse = button("Browse images…", "materialBrowseImages");
        sourcesRow->addWidget(add);
        sourcesRow->addWidget(browse);
        layout->addLayout(sourcesRow);
        sources_ = new QListWidget(&dialog_);
        sources_->setObjectName(QStringLiteral("materialSources"));
        sources_->setMaximumHeight(110);
        sources_->setToolTip(QStringLiteral("Ordered animation cels, from 1 to 16. All output cels share one size."));
        layout->addWidget(sources_);
        auto* order = new QHBoxLayout;
        auto* remove = button("Remove", "materialRemoveSource");
        auto* up = button("Move up", "materialMoveUp");
        auto* down = button("Move down", "materialMoveDown");
        order->addWidget(remove);
        order->addWidget(up);
        order->addWidget(down);
        order->addStretch();
        layout->addLayout(order);
        auto* paletteRow = new QHBoxLayout;
        useProject_ = new QCheckBox(QStringLiteral("Use captured project CMP"), &dialog_);
        useProject_->setObjectName(QStringLiteral("materialUseProjectCmp"));
        useProject_->setChecked(!context_.paletteBytes.isEmpty());
        useProject_->setEnabled(!context_.paletteBytes.isEmpty());
        useProject_->setToolTip(context_.paletteName);
        cmpPath_ = line("materialCmpPath");
        cmpPath_->setPlaceholderText(QStringLiteral("Explicit CMP file"));
        cmpPath_->setEnabled(!useProject_->isChecked());
        auto* browseCmp = button("Browse CMP…", "materialBrowseCmp");
        paletteRow->addWidget(useProject_);
        paletteRow->addWidget(cmpPath_);
        paletteRow->addWidget(browseCmp);
        layout->addLayout(paletteRow);
        auto* options = new QHBoxLayout;
        resize_ = new QCheckBox(QStringLiteral("Resize"), &dialog_);
        resize_->setObjectName(QStringLiteral("materialResize"));
        width_ = dimensions("materialWidth");
        height_ = dimensions("materialHeight");
        mips_ = spin("materialMipmaps", 1, 4, 4);
        dither_ = new QCheckBox(QStringLiteral("Dither"), &dialog_);
        dither_->setObjectName(QStringLiteral("materialDither"));
        dither_->setChecked(true);
        options->addWidget(resize_);
        options->addWidget(width_);
        options->addWidget(new QLabel("×", &dialog_));
        options->addWidget(height_);
        options->addWidget(new QLabel("Mips", &dialog_));
        options->addWidget(mips_);
        options->addWidget(dither_);
        options->addStretch();
        layout->addLayout(options);
        auto* indices = new QHBoxLayout;
        low_ = spin("materialIndexMin", 0, 255, 0);
        high_ = spin("materialIndexMax", 0, 255, 255);
        reserve_ = new QCheckBox(QStringLiteral("Reserve index zero"), &dialog_);
        reserve_->setObjectName(QStringLiteral("materialReserveZero"));
        indices->addWidget(new QLabel("Allowed palette indices", &dialog_));
        indices->addWidget(low_);
        indices->addWidget(new QLabel("through", &dialog_));
        indices->addWidget(high_);
        indices->addWidget(reserve_);
        indices->addStretch();
        layout->addLayout(indices);
        auto* images = new QHBoxLayout;
        original_ = imageLabel("Decoded input", "materialSourcePreview", images);
        mapped_ = imageLabel("Decoded MAT output", "materialOutputPreview", images);
        palette_ = imageLabel("Captured CMP", "materialPalettePreview", images);
        layout->addLayout(images);
        report_ = new QPlainTextEdit(&dialog_);
        report_->setObjectName(QStringLiteral("materialReport"));
        report_->setReadOnly(true);
        report_->setMaximumHeight(110);
        layout->addWidget(report_);
        auto* outputRow = new QHBoxLayout;
        output_ = line("materialOutputPath");
        output_->setPlaceholderText(QStringLiteral("New .mat output path (existing files are never replaced)"));
        auto* browseOutput = button("Choose output…", "materialBrowseOutput");
        outputRow->addWidget(output_);
        outputRow->addWidget(browseOutput);
        layout->addLayout(outputRow);
        status_ = new QLabel(QStringLiteral("Add 1 to 16 images and select a CMP, then preview."), &dialog_);
        status_->setTextFormat(Qt::PlainText);
        status_->setObjectName(QStringLiteral("materialStatus"));
        status_->setWordWrap(true);
        layout->addWidget(status_);
        auto* actions = new QHBoxLayout;
        preview_ = button("Preview conversion", "materialPreview");
        create_ = button("Create new MAT", "materialCreate");
        cancel_ = button("Cancel conversion", "materialCancelWork");
        create_->setEnabled(false);
        cancel_->setEnabled(false);
        actions->addWidget(preview_);
        actions->addWidget(create_);
        actions->addWidget(cancel_);
        actions->addStretch();
        auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog_);
        actions->addWidget(close);
        layout->addLayout(actions);
        watcher_ = new QFutureWatcher<std::shared_ptr<WorkResult>>(&dialog_);
        QObject::connect(
            watcher_, &QFutureWatcher<std::shared_ptr<WorkResult>>::finished, &dialog_, [this] { finish(); });
        QObject::connect(add, &QPushButton::clicked, &dialog_, [this] { append({ sourcePath_->text() }); });
        QObject::connect(browse, &QPushButton::clicked, &dialog_,
            [this]
            {
                append(QFileDialog::getOpenFileNames(
                    &dialog_, "Add image cels", context_.initialDirectory, "Images (*.png *.jpg *.jpeg)"));
            });
        QObject::connect(remove, &QPushButton::clicked, &dialog_,
            [this]
            {
                if ( sources_->currentRow() >= 0 )
                {
                    delete sources_->takeItem(sources_->currentRow());
                    invalidate();
                }
            });
        QObject::connect(up, &QPushButton::clicked, &dialog_, [this] { move(-1); });
        QObject::connect(down, &QPushButton::clicked, &dialog_, [this] { move(1); });
        QObject::connect(sources_, &QListWidget::currentRowChanged, &dialog_, [this] { showCel(); });
        QObject::connect(browseCmp, &QPushButton::clicked, &dialog_,
            [this]
            {
                const QString path = QFileDialog::getOpenFileName(
                    &dialog_, "Select CMP palette", context_.initialDirectory, "CMP palettes (*.cmp)");
                if ( !path.isEmpty() )
                {
                    useProject_->setChecked(false);
                    cmpPath_->setText(path);
                }
            });
        QObject::connect(useProject_, &QCheckBox::toggled, &dialog_,
            [this](const bool checked)
            {
                cmpPath_->setEnabled(!checked);
                invalidate();
            });
        QObject::connect(cmpPath_, &QLineEdit::textChanged, &dialog_, [this] { invalidate(); });
        for ( QCheckBox* box : { resize_, dither_, reserve_ } )
        {
            QObject::connect(box, &QCheckBox::toggled, &dialog_, [this] { invalidate(); });
        }
        for ( QComboBox* combo : { width_, height_ } )
        {
            QObject::connect(combo, &QComboBox::currentIndexChanged, &dialog_, [this] { invalidate(); });
        }
        for ( QSpinBox* number : { mips_, low_, high_ } )
        {
            QObject::connect(number, &QSpinBox::valueChanged, &dialog_, [this] { invalidate(); });
        }
        QObject::connect(output_, &QLineEdit::textChanged, &dialog_,
            [this]
            {
                if ( busy_ && publishing_ )
                {
                    invalidate(false);
                    status_->setText("Output path changed; create again after the current work finishes.");
                }
            });
        QObject::connect(browseOutput, &QPushButton::clicked, &dialog_,
            [this]
            {
                const QString path = QFileDialog::getSaveFileName(&dialog_, "Create new MAT", context_.initialDirectory,
                    "Materials (*.mat)", nullptr, QFileDialog::DontConfirmOverwrite);
                if ( !path.isEmpty() )
                {
                    output_->setText(path);
                }
            });
        QObject::connect(preview_, &QPushButton::clicked, &dialog_, [this] { preview(); });
        QObject::connect(create_, &QPushButton::clicked, &dialog_, [this] { create(); });
        QObject::connect(cancel_, &QPushButton::clicked, &dialog_,
            [this]
            {
                invalidate();
                status_->setText("Cancelled.");
            });
        QObject::connect(close, &QDialogButtonBox::rejected, &dialog_, &QDialog::reject);
        if ( context_.paletteBytes.size() > kPaletteByteLimit )
        {
            context_.paletteBytes.clear();
            useProject_->setChecked(false);
            useProject_->setEnabled(false);
            status_->setText("CMP exceeds the 1 MiB capture limit. Select a bounded CMP file.");
        }
        updateActions();
    }

    ~Impl()
    {
        detail::cancelPreviewWorkerPublication(guard_);
        QObject::disconnect(watcher_, nullptr, &dialog_, nullptr);
    }

    void close()
    {
        closed_ = true;
        invalidate();
    }

private:
    QLineEdit* line(const char* name)
    {
        auto* widget = new QLineEdit(&dialog_);
        widget->setObjectName(QString::fromLatin1(name));
        return widget;
    }
    QPushButton* button(const char* text, const char* name)
    {
        auto* widget = new QPushButton(QString::fromUtf8(text), &dialog_);
        widget->setObjectName(QString::fromLatin1(name));
        return widget;
    }
    QSpinBox* spin(const char* name, const int low, const int high, const int value)
    {
        auto* widget = new QSpinBox(&dialog_);
        widget->setObjectName(QString::fromLatin1(name));
        widget->setRange(low, high);
        widget->setValue(value);
        return widget;
    }
    QComboBox* dimensions(const char* name)
    {
        auto* widget = new QComboBox(&dialog_);
        widget->setObjectName(QString::fromLatin1(name));
        for ( int value = 8; value <= 1024; value *= 2 )
        {
            widget->addItem(QString::number(value), value);
        }
        widget->setCurrentIndex(5);
        return widget;
    }
    QLabel* imageLabel(const char* text, const char* name, QHBoxLayout* row)
    {
        auto* column = new QVBoxLayout;
        column->addWidget(new QLabel(QString::fromUtf8(text), &dialog_));
        auto* image = new QLabel(&dialog_);
        image->setObjectName(QString::fromLatin1(name));
        image->setMinimumSize(160, 140);
        image->setAlignment(Qt::AlignCenter);
        column->addWidget(image);
        row->addLayout(column);
        return image;
    }
    bool current()
    {
        try
        {
            return !context_.isCurrent || context_.isCurrent();
        }
        catch ( const std::exception& error )
        {
            status_->setText(QString::fromUtf8(error.what()));
            return false;
        }
    }
    void updateActions()
    {
        preview_->setEnabled(!busy_ && !closed_);
        create_->setEnabled(!busy_ && !closed_ && prepared_ != nullptr);
        cancel_->setEnabled(busy_);
        width_->setEnabled(resize_->isChecked());
        height_->setEnabled(resize_->isChecked());
    }
    void invalidate(const bool discard = true)
    {
        ++generation_;
        detail::cancelPreviewWorkerPublication(guard_);
        if ( discard )
        {
            prepared_.reset();
            original_->clear();
            mapped_->clear();
            palette_->clear();
            report_->clear();
            status_->setText("Inputs or options changed; preview again.");
        }
        updateActions();
    }
    void append(const QStringList& paths)
    {
        if ( paths.isEmpty() )
        {
            return;
        }
        if ( sources_->count() + paths.size() > 16 )
        {
            status_->setText("MAT: give 1 to 16 cel images");
            return;
        }
        for ( const QString& path : paths )
        {
            if ( path.trimmed().isEmpty() )
            {
                status_->setText("Choose an image path.");
                return;
            }
        }
        invalidate();
        for ( const QString& path : paths )
        {
            sources_->addItem(QFileInfo(path).absoluteFilePath());
        }
        if ( sources_->currentRow() < 0 )
        {
            sources_->setCurrentRow(0);
        }
    }
    void move(const int delta)
    {
        const int from = sources_->currentRow();
        if ( from < 0 || from + delta < 0 || from + delta >= sources_->count() )
        {
            return;
        }
        invalidate();
        auto* item = sources_->takeItem(from);
        sources_->insertItem(from + delta, item);
        sources_->setCurrentRow(from + delta);
    }
    void showCel()
    {
        const int row = sources_->currentRow();
        if ( !prepared_ || row < 0 || row >= static_cast<int>(prepared_->mapped.size()) )
        {
            return;
        }
        original_->setPixmap(QPixmap::fromImage(prepared_->original[static_cast<std::size_t>(row)]));
        mapped_->setPixmap(QPixmap::fromImage(prepared_->mapped[static_cast<std::size_t>(row)]));
    }
    void preview()
    {
        if ( busy_ )
        {
            return;
        }
        invalidate();
        if ( !current() )
        {
            status_->setText("The document/resource context changed; reopen this dialog.");
            return;
        }
        if ( sources_->count() < 1 || sources_->count() > 16 )
        {
            status_->setText("MAT: give 1 to 16 cel images");
            return;
        }
        Request request;
        for ( int i = 0; i < sources_->count(); ++i )
        {
            request.paths.push_back(sources_->item(i)->text());
        }
        if ( useProject_->isChecked() )
        {
            request.paletteBytes = context_.paletteBytes;
            request.paletteName = context_.paletteName;
        }
        else
        {
            request.palettePath = cmpPath_->text();
        }
        request.resize = resize_->isChecked();
        request.width = width_->currentData().toInt();
        request.height = height_->currentData().toInt();
        request.options.mipmaps = mips_->value();
        request.options.dither = dither_->isChecked();
        for ( int index = 0; index < 256; ++index )
        {
            request.options.allowed[static_cast<std::size_t>(index)] =
                index >= low_->value() && index <= high_->value() && !(index == 0 && reserve_->isChecked());
        }
        guard_ = detail::makePreviewWorkerPublicationGuard(generation_);
        publishing_ = false;
        busy_ = true;
        updateActions();
        status_->setText("Capturing and converting images…");
        watcher_->setFuture(detail::runPreviewWorkerTask(prepareMaterial, std::move(request), guard_));
    }
    void create()
    {
        if ( busy_ || !prepared_ )
        {
            return;
        }
        if ( !current() )
        {
            invalidate();
            status_->setText("The document/resource context changed; reopen this dialog.");
            return;
        }
        const QString entered = output_->text().trimmed();
        if ( entered.isEmpty() || !entered.endsWith(".mat", Qt::CaseInsensitive) )
        {
            status_->setText("Choose a new output filename ending in .mat.");
            return;
        }
        const QString destination = QFileInfo(entered).absoluteFilePath();
        if ( QFileInfo::exists(destination) || QFileInfo(destination).isSymLink() )
        {
            status_->setText("Output already exists; choose a new MAT filename.");
            return;
        }
        guard_ = detail::makePreviewWorkerPublicationGuard(generation_);
        publishing_ = true;
        busy_ = true;
        updateActions();
        status_->setText("Rechecking sources and staging the new MAT…");
        watcher_->setFuture(detail::runPreviewWorkerTask(stageMaterial, prepared_, destination, guard_));
    }
    void finish()
    {
        busy_ = false;
        publishing_ = false;
        try
        {
            const auto result = watcher_->result();
            const auto releaseResult = qScopeGuard(
                [&]
                {
                    // QFuture retains its result after this callback. Release owned
                    // staging files on every refusal/exception immediately.
                    result->staged.reset();
                    result->prepared.reset();
                });
            if ( closed_ || !detail::previewWorkerMayPublish(guard_, generation_) )
            {
                updateActions();
                return;
            }
            if ( !current() )
            {
                invalidate();
                status_->setText("The document/resource context changed; reopen this dialog.");
                return;
            }
            if ( !result->error.isEmpty() )
            {
                invalidate();
                status_->setText(result->error);
                return;
            }
            if ( result->staged )
            {
                result->staged->publishNew(filePath(result->destination));
                result->staged.reset();
                status_->setText(QStringLiteral("Created %1. No level assignments or resource roots were changed.")
                        .arg(result->destination));
                if ( context_.published )
                {
                    try
                    {
                        context_.published(result->destination);
                    }
                    catch ( const std::exception& error )
                    {
                        status_->setText(QStringLiteral("Created %1. Resource refresh failed: %2")
                                .arg(result->destination, QString::fromUtf8(error.what())));
                    }
                }
            }
            else
            {
                prepared_ = result->prepared;
                const auto& report = prepared_->material.report;
                QString text = QStringLiteral("Cels: %1\nOutput: %2 × %3, 8-bit opaque\nMipmaps: %4\nColours used: "
                                              "%5\nMean error: %6\nCMP: %7\nCMP SHA-256: %8\n")
                                   .arg(report.cels)
                                   .arg(report.width)
                                   .arg(report.height)
                                   .arg(report.mipmaps)
                                   .arg(report.coloursUsed)
                                   .arg(report.meanError, 0, 'g', 8)
                                   .arg(prepared_->paletteName)
                                   .arg(QString::fromLatin1(prepared_->paletteHash.toHex()));
                for ( const auto& source : prepared_->sources )
                {
                    text += source.path + "\nSHA-256: " + QString::fromLatin1(source.hash.toHex()) + '\n';
                }
                report_->setPlainText(text);
                palette_->setPixmap(QPixmap::fromImage(prepared_->palette.scaled(128, 128)));
                showCel();
                status_->setText("Ready. Choose a new output path and create the MAT.");
            }
        }
        catch ( const std::exception& error )
        {
            invalidate();
            status_->setText(QString::fromUtf8(error.what()));
        }
        catch ( ... )
        {
            invalidate();
            status_->setText("Material conversion failed with an unexpected worker exception.");
        }
        updateActions();
    }

    MaterialAuthoringDialog& dialog_;
    Context context_;
    std::uint64_t generation_{};
    bool busy_{};
    bool publishing_{};
    bool closed_{};
    detail::PreviewWorkerPublicationGuard guard_{};
    std::shared_ptr<Prepared> prepared_;
    QFutureWatcher<std::shared_ptr<WorkResult>>* watcher_{};
    QLineEdit *sourcePath_{}, *cmpPath_{}, *output_{};
    QListWidget* sources_{};
    QCheckBox *useProject_{}, *resize_{}, *dither_{}, *reserve_{};
    QComboBox *width_{}, *height_{};
    QSpinBox *mips_{}, *low_{}, *high_{};
    QLabel *original_{}, *mapped_{}, *palette_{}, *status_{};
    QPlainTextEdit* report_{};
    QPushButton *preview_{}, *create_{}, *cancel_{};
};

MaterialAuthoringDialog::MaterialAuthoringDialog(Context context, QWidget* parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(*this, std::move(context)))
{
}

MaterialAuthoringDialog::~MaterialAuthoringDialog() = default;

void MaterialAuthoringDialog::done(const int result)
{
    impl_->close();
    QDialog::done(result);
}
} // namespace sed::ui::resources
