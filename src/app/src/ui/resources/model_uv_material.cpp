#include "ui/resources/model_uv_material.hpp"

#include "sed/assets/bounded_stream_reader.hpp"
#include "sed/text.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QObject>

#include <cstring>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace sed::ui::resources
{
namespace
{

constexpr std::size_t kMaterialByteLimit = 16 * 1024 * 1024;
constexpr std::size_t kPaletteByteLimit = 1024 * 1024;

QString nativePathLabel(const std::filesystem::path& path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.native());
#else
    const auto& bytes = path.native();
    const auto label = QFile::decodeName(QByteArray::fromStdString(bytes));
    if ( QFile::encodeName(label).toStdString() != bytes )
    {
        return QObject::tr("Native path (hex) %1").arg(QString::fromLatin1(QByteArray::fromStdString(bytes).toHex()));
    }
    return label;
#endif
}

QString describe(const assets::ResourceReadOrigin& origin)
{
    using Kind = assets::ResourceReadOrigin::Kind;
    switch ( origin.kind )
    {
    case Kind::PrimaryOverride:
        return QObject::tr("Project resource override %1").arg(origin.overrideRevision);
    case Kind::LooseFile:
        return nativePathLabel(origin.path);
    case Kind::Archive:
    {
        const auto bytes = QByteArray::fromStdString(origin.archiveEntry);
        QString entry = QString::fromUtf8(bytes);
        if ( entry.toUtf8() != bytes )
        {
            entry = QObject::tr("entry bytes (hex) %1").arg(QString::fromLatin1(bytes.toHex()));
        }
        return QObject::tr("Archive %1 (%2), %3").arg(origin.archiveIndex).arg(nativePathLabel(origin.path), entry);
    }
    case Kind::None:
        break;
    }
    throw std::runtime_error("The source of the captured resource is unknown.");
}

// The locator also accepts absolute disk names. Only a separately supplied loose path
// represents a choice to inspect a native file outside the project resource search.
void requireProjectName(std::string_view name)
{
    name = text::trimView(name);
    if ( name.empty() || name.starts_with('/') || name.starts_with('\\') || name.find(':') != name.npos
        || name.find('\0') != name.npos )
    {
        throw std::runtime_error("Choose a relative project resource name or an explicit loose file.");
    }
    while ( !name.empty() )
    {
        const auto separator = name.find_first_of("/\\");
        if ( name.substr(0, separator) == ".." )
        {
            throw std::runtime_error("Project resource names cannot traverse parent directories.");
        }
        if ( separator == name.npos )
        {
            return;
        }
        name.remove_prefix(separator + 1);
    }
}

std::vector<std::byte> captureBytes(const assets::ResourceLocator* resources, const std::string& name,
    const std::filesystem::path& path, std::size_t limit, ModelUvMaterialSource& source)
{
    assets::ResourceReadOrigin origin;
    std::optional<std::vector<std::byte>> bytes;
    if ( !path.empty() )
    {
        origin.kind = assets::ResourceReadOrigin::Kind::LooseFile;
        origin.path = std::filesystem::absolute(path);
        if ( !std::filesystem::is_regular_file(origin.path) )
        {
            throw std::runtime_error("The selected loose source is missing or is not a regular file.");
        }
        const auto size = std::filesystem::file_size(origin.path);
        if ( size <= limit )
        {
            std::ifstream stream(origin.path, std::ios::binary);
            if ( !stream )
            {
                throw std::runtime_error("Could not open the selected loose source.");
            }
            bool exceeded = false;
            bytes = assets::detail::readBinaryStreamWithinBudget<std::vector<std::byte>>(
                stream, limit, static_cast<std::size_t>(size), exceeded, "Could not read the selected loose source.");
        }
    }
    else
    {
        if ( !resources || name.empty() )
        {
            throw std::runtime_error("No project source was supplied.");
        }
        requireProjectName(name);
        bytes = resources->tryReadCurrentBinary(name, limit, origin);
    }
    if ( !bytes )
    {
        throw std::runtime_error("The selected source is missing or exceeds its byte limit.");
    }
    const auto view = QByteArrayView(reinterpret_cast<const char*>(bytes->data()), static_cast<qsizetype>(bytes->size()));
    source.sha256 = QString::fromLatin1(QCryptographicHash::hash(view, QCryptographicHash::Sha256).toHex());
    source.description = describe(origin);
    source.origin = std::move(origin);
    return std::move(*bytes);
}

} // namespace

ModelUvMaterialCapture captureModelUvMaterial(const ModelUvMaterialRequest& request)
{
    ModelUvMaterialCapture result;
    const auto cel = assets::MatFile::inspectPreviewCel(captureBytes(request.resources.get(), request.materialName,
        request.materialPath, kMaterialByteLimit, result.material));
    result.kind = cel.kind;
    result.sourceSize = QSize(cel.width, cel.height);
    result.solidColorIndex = cel.solidColorIndex;
    if ( cel.kind == assets::MatPreviewKind::TrueColor )
    {
        // The temporary view borrows the decoder's tightly packed RGBA bytes. Return an owned image.
        const QImage view(cel.rgba.rgba.data(), cel.width, cel.height, cel.width * 4, QImage::Format_RGBA8888);
        result.image = view.copy();
    }
    else
    {
        std::optional<assets::CmpFile> cmp;
        try
        {
            const auto bytes = captureBytes(request.resources.get(), request.paletteName, request.palettePath,
                kPaletteByteLimit, result.palette);
            cmp = assets::parseCmp(bytes);
            if ( !cmp )
            {
                throw std::runtime_error("The captured CMP is invalid or incomplete.");
            }
        }
        catch ( const std::exception& error )
        {
            result.colorsUnavailable = QObject::tr("Colors unavailable. %1").arg(QString::fromUtf8(error.what()));
            return result;
        }
        if ( cel.kind == assets::MatPreviewKind::SolidIndexed )
        {
            const auto& color = cmp->palette[cel.solidColorIndex];
            result.solidColor = QColor(color.r, color.g, color.b);
            return result;
        }
        result.image = QImage(cel.width, cel.height, QImage::Format_Indexed8);
        if ( !result.image.isNull() )
        {
            QList<QRgb> colors;
            colors.reserve(static_cast<qsizetype>(cmp->palette.size()));
            for ( const auto& color : cmp->palette )
            {
                const int alpha = cel.keyed && colors.empty() ? 0 : 255;
                colors.push_back(qRgba(color.r, color.g, color.b, alpha));
            }
            result.image.setColorTable(colors);
            for ( int y = 0; y < cel.height; ++y )
            {
                std::memcpy(result.image.scanLine(y), cel.indices.data() + static_cast<std::size_t>(y) * cel.width,
                    static_cast<std::size_t>(cel.width));
            }
        }
    }
    if ( result.image.isNull() )
    {
        throw std::runtime_error("Could not allocate the material preview image.");
    }
    return result;
}

}
