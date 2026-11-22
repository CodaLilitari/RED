#include "ui/resources/model_uv_material.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace sed::ui::resources;

void check(bool value, const char* message)
{
    if ( !value )
    {
        throw std::runtime_error(message);
    }
}

void word(QByteArray& bytes, int offset, unsigned value)
{
    for ( int i = 0; i < 4; ++i )
    {
        bytes[offset + i] = static_cast<char>(value >> (8 * i));
    }
}

QByteArray material(bool solid)
{
    QByteArray bytes(solid ? 100 : 146, '\0');
    bytes.replace(0, 4, "MAT ");
    word(bytes, 4, 0x32);
    word(bytes, 8, solid ? 0 : 2);
    word(bytes, 12, 1);
    word(bytes, 16, solid ? 0 : 1);
    word(bytes, 24, 8);
    if ( !solid )
    {
        word(bytes, 76, 8);
        word(bytes, 116, 3);
        word(bytes, 120, 2);
        word(bytes, 124, 1);
        word(bytes, 136, 1);
        bytes.replace(140, 6, QByteArray::fromHex("000102020100"));
    }
    return bytes;
}

QByteArray palette()
{
    QByteArray bytes(64 + 256 * 3 + 64 * 256, '\0');
    bytes.replace(0, 4, "CMP ");
    word(bytes, 4, 30);
    bytes.replace(64, 9, QByteArray::fromHex("112233f01020405060"));
    return bytes;
}

void write(const std::filesystem::path& path, const QByteArray& bytes)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "fixture write");
}

QString hash(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

void run()
{
    QTemporaryDir temp;
    check(temp.isValid(), "temporary directory");
    const auto root = QFileInfo(temp.path()).filesystemFilePath();
    const auto mat = material(false);
    const auto cmp = palette();
    auto locator = std::make_shared<sed::assets::ResourceLocator>();
    locator->addRoot(root);
    const std::string gob = sed::assets::buildGobBytes({{"MaT/Row.MAT", mat.toStdString()}});
    std::vector<std::byte> archive(gob.size());
    std::memcpy(archive.data(), gob.data(), gob.size());
    locator->mountGob(sed::assets::GobArchive::fromBytes(std::move(archive), "owned.gob"));
    write(root / "master.cmp", cmp);
    ModelUvMaterialRequest request;
    request.resources = locator;
    request.materialName = "row.mat";
    request.paletteName = "master.cmp";
    const auto captured = captureModelUvMaterial(request);
    using Origin = sed::assets::ResourceReadOrigin;
    check(captured.material.origin.kind == Origin::Kind::Archive && captured.material.origin.capturedArchive
        && captured.material.origin.archiveEntry == "MaT\\Row.MAT" && captured.material.sha256 == hash(mat),
        "typed captured archive winner and byte digest");
    check(captured.palette.origin.kind == Origin::Kind::LooseFile && captured.palette.sha256 == hash(cmp)
        && captured.sourceSize == QSize(3, 2) && captured.colorsUnavailable.isEmpty(), "captured palette and real dimensions");
    check(captured.image.pixelColor(0, 0).alpha() == 0
        && captured.image.pixelColor(1, 0) == QColor(240, 16, 32)
        && captured.image.pixelColor(0, 1) == QColor(64, 80, 96), "direct unlit palette, key and row order");

    // The same immutable mount configuration must observe a newly winning loose file.
    auto unkeyed = mat;
    word(unkeyed, 124, 0);
    write(root / "row.mat", unkeyed);
    const auto winner = captureModelUvMaterial(request);
    check(winner.material.origin.kind == Origin::Kind::LooseFile && winner.material.sha256 == hash(unkeyed)
        && winner.image.pixelColor(0, 0) == QColor(17, 34, 51), "current loose winner overrides archive");
    check(QFile::remove(root / "row.mat"), "remove winning loose MAT");

    auto untrusted = request;
    untrusted.materialName = (root / "named.mat").generic_string();
    write(root / "named.mat", mat);
    bool refusedAbsoluteName = false;
    try
    {
        (void)captureModelUvMaterial(untrusted);
    }
    catch ( const std::runtime_error& )
    {
        refusedAbsoluteName = true;
    }
    check(refusedAbsoluteName, "an authored resource name cannot stand in for an explicit loose override");

    request.palettePath = root / "replacement.cmp";
    auto alternate = cmp;
    alternate[67] = 19;
    write(request.palettePath, alternate);
    const auto changed = captureModelUvMaterial(request);
    check(changed.material.sha256 == captured.material.sha256 && changed.palette.sha256 == hash(alternate)
        && changed.image.pixelColor(1, 0).red() == 19 && captured.image.pixelColor(1, 0).red() == 240,
        "palette-only replacement creates an independent immutable image");
    write(request.palettePath, "broken");
    const auto invalid = captureModelUvMaterial(request);
    check(invalid.image.isNull() && invalid.sourceSize == QSize(3, 2) && !invalid.colorsUnavailable.isEmpty()
        && invalid.palette.sha256 == hash("broken"), "invalid palette keeps dimensions and its actual hash");
    check(QFile::remove(request.palettePath), "remove palette");
    const auto missing = captureModelUvMaterial(request);
    check(missing.image.isNull() && missing.sourceSize == QSize(3, 2) && !missing.colorsUnavailable.isEmpty()
        && missing.palette.sha256.isEmpty(), "missing explicit CMP never falls back to project palette");
    write(request.palettePath, cmp + QByteArray(1024 * 1024 + 1 - cmp.size(), '\0'));
    const auto oversized = captureModelUvMaterial(request);
    check(oversized.image.isNull() && oversized.sourceSize == QSize(3, 2) && oversized.palette.sha256.isEmpty()
        && !oversized.colorsUnavailable.isEmpty(), "oversized CMP never installs colors");

    auto brokenContext = std::make_shared<sed::assets::ResourceLocator>(*locator);
    brokenContext->setPrimaryResourceOverride("master.cmp", []() -> std::optional<std::vector<std::byte>>
    {
        throw std::invalid_argument("Palette capture failed.");
    });
    auto brokenRequest = request;
    brokenRequest.resources = brokenContext;
    brokenRequest.palettePath.clear();
    const auto failedPalette = captureModelUvMaterial(brokenRequest);
    check(failedPalette.sourceSize == QSize(3, 2) && !failedPalette.colorsUnavailable.isEmpty()
        && failedPalette.image.isNull(), "project palette read exception preserves captured MAT dimensions");

    request.materialPath = root / "explicit.mat";
    request.palettePath.clear();
    const auto solid = material(true);
    write(request.materialPath, solid);
    const auto flat = captureModelUvMaterial(request);
    check(flat.kind == sed::assets::MatPreviewKind::SolidIndexed && flat.sourceSize == QSize(0, 0)
        && flat.solidColorIndex == 0 && flat.solidColor == QColor(17, 34, 51)
        && flat.image.isNull() && flat.material.sha256 == hash(solid)
        && flat.material.origin.path == request.materialPath, "opaque solid zero has no tile or texture size");

    QByteArray rgb = mat.left(140);
    word(rgb, 20, 1);
    word(rgb, 24, 16);
    word(rgb, 28, 5);
    word(rgb, 32, 6);
    word(rgb, 36, 5);
    word(rgb, 40, 11);
    word(rgb, 44, 5);
    word(rgb, 120, 1);
    word(rgb, 124, 0);
    rgb += QByteArray::fromHex("00f8e0071f00");
    write(request.materialPath, rgb);
    request.palettePath = root / "unused.cmp";
    const auto direct = captureModelUvMaterial(request);
    check(direct.kind == sed::assets::MatPreviewKind::TrueColor && direct.sourceSize == QSize(3, 1)
        && direct.image.pixelColor(0, 0) == QColor(Qt::red) && direct.image.pixelColor(1, 0) == QColor(Qt::green)
        && direct.image.pixelColor(2, 0) == QColor(Qt::blue) && direct.palette.sha256.isEmpty()
        && direct.colorsUnavailable.isEmpty(), "packed RGB565 retains channel order independently of CMP");

    for ( const auto& bad : {QByteArray("bad"), mat + QByteArray(16 * 1024 * 1024 + 1 - mat.size(), '\0')} )
    {
        write(request.materialPath, bad);
        bool refused = false;
        try
        {
            (void)captureModelUvMaterial(request);
        }
        catch ( const std::runtime_error& )
        {
            refused = true;
        }
        check(refused, "invalid or oversized loose MAT refuses without shadow fallback");
    }
}
}

int main()
{
    try
    {
        run();
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
