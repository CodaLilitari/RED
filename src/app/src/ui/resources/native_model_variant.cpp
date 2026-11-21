#include "ui/resources/native_model_variant.hpp"

#include "../../../../core/src/assets/atomic_file_write.hpp"
#include "sed/assets/model_lighting.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QStringDecoder>
#include <QStringList>

#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace sed::ui::resources
{
namespace
{
namespace fs = std::filesystem;

void require(const bool condition, const char* message)
{
    if ( !condition )
    {
        throw std::runtime_error(message);
    }
}

QString pathText(const fs::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

void checkModelName(const QString& name)
{
    require(!name.isEmpty() && !name.contains(QChar::Null) && name.endsWith(QStringLiteral(".3do"), Qt::CaseInsensitive),
        "Choose a model name ending in .3do.");
}

std::string_view byteView(const QByteArray& bytes)
{
    return { bytes.constData(), static_cast<std::size_t>(bytes.size()) };
}

QString digest(const std::string_view bytes)
{
    const QByteArrayView view(bytes.data(), static_cast<qsizetype>(bytes.size()));
    return QString::fromLatin1(QCryptographicHash::hash(view, QCryptographicHash::Sha256).toHex());
}

QString archiveEntryLabel(const std::string& name)
{
    const QByteArray bytes = QByteArray::fromStdString(name);
    QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
    const QString text = decoder.decode(bytes);
    if ( decoder.hasError() )
    {
        return QObject::tr("entry bytes (hex) %1").arg(QString::fromLatin1(bytes.toHex()));
    }
    return QObject::tr("entry %1").arg(text);
}

QString originLabel(const assets::ResourceReadOrigin& origin)
{
    using Kind = assets::ResourceReadOrigin::Kind;
    switch ( origin.kind )
    {
    case Kind::PrimaryOverride:
        return QObject::tr("Resource override %1").arg(origin.overrideRevision);
    case Kind::LooseFile:
        return pathText(fs::absolute(origin.path));
    case Kind::Archive:
        return QObject::tr("Archive %1 (%2), %3").arg(origin.archiveIndex)
            .arg(pathText(origin.path), archiveEntryLabel(origin.archiveEntry));
    case Kind::None:
        break;
    }
    throw std::runtime_error("Could not identify the resource that supplied this model.");
}

fs::path resolveDestination(const fs::path& absolute)
{
    // Resolve the existing parent, never the leaf: a dangling symlink also occupies the leaf.
    const auto parent = fs::canonical(absolute.parent_path());
    require(fs::is_directory(parent), "The destination parent must be a directory.");
    return parent / absolute.filename();
}

void requireUnused(const fs::path& destination)
{
    require(fs::symlink_status(destination).type() == fs::file_type::not_found,
        "Choose an unused destination. The selected directory entry already exists.");
}

QString changedUvRows(const assets::Model3doMesh& before, const assets::Model3doMesh& after)
{
    QStringList ranges;
    std::size_t row = 0;
    const auto changed = [&](const std::size_t index)
    {
        return before.texVertices[index].u != after.texVertices[index].u
            || before.texVertices[index].v != after.texVertices[index].v;
    };
    while ( row < before.texVertices.size() )
    {
        if ( !changed(row) )
        {
            ++row;
            continue;
        }
        const auto first = row++;
        while ( row < before.texVertices.size() && changed(row) )
        {
            ++row;
        }
        ranges.push_back(row == first + 1 ? QString::number(first) : QObject::tr("%1–%2").arg(first).arg(row - 1));
    }
    return ranges.join(QStringLiteral(", "));
}

QString cumulativeSummary(const assets::NativeModelDocument& baseline, const assets::NativeModelDocument& candidate)
{
    const auto original = baseline.geosets();
    const auto models = candidate.geosets();
    require(original.size() == models.size(), "The candidate has different LODs from the source.");
    QStringList summary{ QObject::tr("%1 source LOD(s) and their hierarchy are preserved.").arg(models.size()) };
    for ( std::size_t lod = 0; lod < models.size(); ++lod )
    {
        require(original[lod].meshes.size() == models[lod].meshes.size(), "The candidate has different source meshes.");
        for ( std::size_t mesh = 0; mesh < models[lod].meshes.size(); ++mesh )
        {
            const auto& before = original[lod].meshes[mesh];
            const auto& after = models[lod].meshes[mesh];
            require(before.vertices.size() == after.vertices.size() && before.texVertices.size() == after.texVertices.size(),
                "The candidate has different vertex or UV row counts.");
            std::size_t positions = 0;
            for ( std::size_t row = 0; row < before.vertices.size(); ++row )
            {
                const auto& oldPosition = before.vertices[row];
                const auto& newPosition = after.vertices[row];
                if ( oldPosition.x != newPosition.x || oldPosition.y != newPosition.y || oldPosition.z != newPosition.z )
                {
                    ++positions;
                }
            }
            const QString uvRows = changedUvRows(before, after);
            if ( positions || !uvRows.isEmpty() )
            {
                QStringList edits;
                if ( positions )
                {
                    edits.push_back(QObject::tr("%1 changed position(s), retained mesh radius %2").arg(positions)
                        .arg(after.authoredRadius, 0, 'g', std::numeric_limits<double>::max_digits10));
                }
                if ( !uvRows.isEmpty() )
                {
                    edits.push_back(QObject::tr("changed UV row(s) %1").arg(uvRows));
                }
                summary.push_back(QObject::tr("LOD %1, mesh %2 (%3): %4").arg(lod).arg(mesh)
                    .arg(QString::fromStdString(after.name), edits.join(QStringLiteral("; "))));
            }
        }
    }
    if ( summary.size() == 1 )
    {
        summary.push_back(QObject::tr("No mesh positions or UV rows changed."));
    }
    summary.push_back(QObject::tr("Authored radii and stored lighting are retained. Recheck bounds, lighting and external "
        "bindings for changed geometry. Target-game rendering, animation and collision have not been verified."));
    return summary.join(QLatin1Char('\n'));
}
} // namespace

NativeModelSourceSnapshot readNativeModelSource(NativeModelSource source)
{
    checkModelName(source.name);
    const auto maximum = assets::ModelLightingLimits{}.sourceBytes;
    QByteArray bytes;
    assets::ResourceReadOrigin origin;
    if ( source.resource )
    {
        require(static_cast<bool>(source.resources), "No resource context is available for the selected model.");
        const auto name = source.name.toStdString();
        const auto payload = source.resources->tryReadCurrentBinary(name, maximum, origin);
        require(payload.has_value(), "The selected model resource is missing or exceeds the source limit.");
        bytes = QByteArray(reinterpret_cast<const char*>(payload->data()), static_cast<qsizetype>(payload->size()));
    }
    else
    {
        const auto nativePath = fs::absolute(QFileInfo(source.name).filesystemFilePath());
        source.name = pathText(nativePath);
        origin.kind = assets::ResourceReadOrigin::Kind::LooseFile;
        origin.path = nativePath;
        require(fs::is_regular_file(nativePath), "The selected model source must be a regular file.");
        QFile file(source.name);
        if ( !file.open(QIODevice::ReadOnly) )
        {
            throw std::runtime_error(file.errorString().toStdString());
        }
        bytes = file.read(static_cast<qint64>(maximum) + 1);
        require(file.error() == QFile::NoError && file.atEnd() && bytes.size() == file.size()
                && static_cast<std::size_t>(bytes.size()) <= maximum,
            "The complete model could not be read within the source-byte limit.");
    }
    fs::path diskOrigin;
    using Kind = assets::ResourceReadOrigin::Kind;
    if ( origin.kind == Kind::LooseFile || (origin.kind == Kind::Archive && !origin.capturedArchive) )
    {
        diskOrigin = fs::canonical(origin.path);
    }
    auto identity = originLabel(origin);
    const QString sha256 = digest(byteView(bytes));
    return { std::move(source), std::move(bytes), sha256, std::move(origin), std::move(diskOrigin), std::move(identity) };
}

void revalidateNativeModelSource(const NativeModelSourceSnapshot& original)
{
    const auto current = readNativeModelSource(original.source);
    require(current.origin == original.origin && current.diskOrigin == original.diskOrigin
            && current.bytes == original.bytes,
        "The source or its winning resource changed. Review the original model again.");
}

std::shared_ptr<const NativeModelVariantReview> reviewNativeModelVariant(
    const NativeModelSourceSnapshot& original, assets::NativeModelDocument candidate, const QString& destination)
{
    revalidateNativeModelSource(original);
    checkModelName(destination);
    const auto selected = fs::absolute(QFileInfo(destination).filesystemFilePath());
    require(!selected.extension().empty(), "Choose a model filename with a stem before .3do.");
    const auto target = resolveDestination(selected);
    requireUnused(target);
    // A session proposal has already passed validation. Reopen exactly what this review will publish.
    auto reopened = assets::NativeModelDocument::open(candidate.source());
    const auto baseline = assets::NativeModelDocument::open(byteView(original.bytes));
    const auto summary = cumulativeSummary(baseline, reopened);
    const auto sha256 = digest(reopened.source());
    return std::make_shared<const NativeModelVariantReview>(NativeModelVariantReview{
        original, std::move(reopened), target, pathText(selected), sha256, summary });
}

NativeModelVariantStage::NativeModelVariantStage(std::shared_ptr<const NativeModelVariantReview> review)
    : review_(std::move(review)), file_(std::make_unique<assets::detail::StagedAssetFile>(review_->destination))
{
    const auto bytes = review_->candidate.source();
    file_->writeAndClose(std::as_bytes(std::span(bytes.data(), bytes.size())));
}

NativeModelVariantStage::~NativeModelVariantStage() = default;

bool NativeModelVariantStage::published() const noexcept
{
    return published_;
}

std::shared_ptr<NativeModelVariantStage> stageNativeModelVariant(std::shared_ptr<const NativeModelVariantReview> review)
{
    require(static_cast<bool>(review), "A reviewed candidate is required before staging.");
    revalidateNativeModelSource(review->original);
    require(resolveDestination(QFileInfo(review->destinationText).filesystemFilePath()) == review->destination,
        "The reviewed destination directory changed.");
    requireUnused(review->destination);
    return std::shared_ptr<NativeModelVariantStage>(new NativeModelVariantStage(std::move(review)));
}

void publishNativeModelVariant(NativeModelVariantStage& stage)
{
    require(!stage.published_, "This variant has already been published.");
    revalidateNativeModelSource(stage.review_->original);
    require(resolveDestination(QFileInfo(stage.review_->destinationText).filesystemFilePath()) == stage.review_->destination,
        "The reviewed destination directory changed.");
    stage.file_->publishNew(stage.review_->destination);
    stage.published_ = true;
}

} // namespace sed::ui::resources
