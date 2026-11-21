#include "ui/resources/native_model_variant.hpp"

#include "../../core/src/assets/atomic_file_write.hpp"
#include "sed/assets/model_lighting.hpp"
#include "sed/assets/resource_locator.hpp"
#include "sed/update/updater.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <array>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
using namespace sed;
using namespace sed::ui::resources;
namespace fs = std::filesystem;

void require(const bool result, const char* message)
{
    if ( !result )
    {
        throw std::runtime_error(message);
    }
}

template <typename Operation>
void mustRefuse(Operation operation, const char* message)
{
    try
    {
        operation();
    }
    catch ( const std::exception& )
    {
        return;
    }
    throw std::runtime_error(message);
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "cannot read expected test file");
    const auto bytes = file.readAll();
    require(file.error() == QFile::NoError, "failed to read test file");
    return bytes;
}

void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "cannot write test file");
    require(file.write(bytes) == bytes.size() && file.flush(), "failed to write test file");
}

void noStages(const QDir& root)
{
    require(root.entryList({QStringLiteral(".sed-save-*.tmp")}, QDir::Files | QDir::Hidden).isEmpty(),
        "abandoned stage was left in destination directory");
}

void winnerIdentity(const QDir& root, const QByteArray& bytes)
{
    const auto leaf = QStringLiteral("sphère-星.3do");
    const auto disk = root.filePath(QStringLiteral("identity.gob"));
    const auto contents = QByteArray::fromStdString(assets::buildGobBytes({
        {"3do/" + leaf.toStdString(), bytes.toStdString()}}));
    writeFile(disk, contents);
    auto resources = std::make_shared<assets::ResourceLocator>();
    resources->addRoot(QFileInfo(root.absolutePath()).filesystemAbsoluteFilePath());
    resources->mountGob(QFileInfo(disk).filesystemAbsoluteFilePath());
    const auto archived = readNativeModelSource({leaf, true, resources});
    require(archived.bytes == bytes && archived.originIdentity.contains(leaf),
        "archive origin must display the exact UTF-8 entry name");
    require(archived.origin.kind == assets::ResourceReadOrigin::Kind::Archive && !archived.origin.capturedArchive
        && archived.origin.archiveEntry == "3do\\" + leaf.toStdString() && archived.origin.archiveIndex == 0
        && fs::equivalent(archived.diskOrigin, QFileInfo(disk).filesystemFilePath()),
        "disk archive origin must retain its physical entry spelling, mount and canonical container");
    auto relabeled = archived;
    relabeled.originIdentity = QStringLiteral("Display-only label");
    revalidateNativeModelSource(relabeled);

    const auto loose = root.filePath(leaf);
    writeFile(loose, bytes);
    const auto selected = readNativeModelSource({leaf, true, resources});
    require(fs::equivalent(selected.diskOrigin, QFileInfo(loose).filesystemFilePath()), "exact loose name must win");
    require(QFile::remove(disk), "cannot remove the lower-priority disk mount");
    revalidateNativeModelSource(selected);

#ifndef _WIN32
    const auto first = root.filePath("identity-first.gob");
    const auto second = root.filePath("identity-second.gob");
    writeFile(first, contents);
    writeFile(second, contents);
    const auto alias = QFileInfo(disk).filesystemFilePath();
    fs::create_symlink(QFileInfo(first).filesystemFilePath(), alias);
    auto linked = std::make_shared<assets::ResourceLocator>();
    linked->mountGob(alias);
    const auto linkedSource = readNativeModelSource({leaf, true, linked});
    fs::remove(alias);
    fs::create_symlink(QFileInfo(second).filesystemFilePath(), alias);
    mustRefuse([&] { revalidateNativeModelSource(linkedSource); },
        "same-byte archive at a retargeted mount alias concealed a new physical origin");
#endif
}

void diskArchiveChanges(const QDir& root, const QByteArray& bytes)
{
    const auto disk = root.filePath(QStringLiteral("mounted.gob"));
    const auto native = QFileInfo(disk).filesystemAbsoluteFilePath();
    const auto output = root.filePath(QStringLiteral("archive-variant.3do"));
    const auto lookup = QStringLiteral("sentry.3do");
    const auto encoded = [&](const std::string& entry)
    {
        return QByteArray::fromStdString(assets::buildGobBytes({{entry, bytes.toStdString()}}));
    };
    writeFile(disk, encoded("3do/sentry.3do"));
    auto mounts = std::make_shared<assets::ResourceLocator>();
    mounts->mountGob(native);
    const auto original = readNativeModelSource({lookup, true, mounts});
    require(original.bytes == bytes, "disk-backed GOB did not supply the captured model");
    const auto document = assets::NativeModelDocument::open(bytes.toStdString());
    const auto review = reviewNativeModelVariant(original, document, output);
    {
        const auto stage = stageNativeModelVariant(review);
        // Both equal-length directory names locate identical payload bytes
        // at exactly the same offset. A read through the mounted stale index
        // therefore succeeds even though this named resource is gone.
        writeFile(disk, encoded("3do/decoys.3do"));
        require(!assets::GobArchive::open(native).contains("3do/sentry.3do")
            && mounts->gobs().front().read("3do/sentry.3do").size() == static_cast<std::size_t>(bytes.size()),
            "fixture must change the live archive index while preserving the old payload");
        mustRefuse([&] { publishNativeModelVariant(*stage); }, "published after mounted archive entry was renamed");
        require(!stage->published() && !QFileInfo::exists(output), "renamed archive source left a published variant");
    }
    noStages(root);

    writeFile(disk, encoded("3do/sentry.3do"));
    const auto higher = root.filePath(QStringLiteral("higher.gob"));
    writeFile(higher, encoded("3do/decoys.3do"));
    mounts->mountGob(QFileInfo(higher).filesystemAbsoluteFilePath());
    const auto lowerWinner = readNativeModelSource({lookup, true, mounts});
    writeFile(higher, encoded("3do/sentry.3do"));
    mustRefuse([&] { revalidateNativeModelSource(lowerWinner); },
        "same bytes in a newly winning disk archive concealed changed source identity");

    const auto frozen = encoded("sentry.3do");
    const auto captured = std::as_bytes(std::span(frozen.constData(), static_cast<std::size_t>(frozen.size())));
    mounts->mountGob(assets::GobArchive::fromBytes({captured.begin(), captured.end()}, "memory-only-fixture.gob"));
    const auto inMemoryWinner = readNativeModelSource({lookup, true, mounts});
    require(inMemoryWinner.bytes == bytes && inMemoryWinner.originIdentity.contains("memory-only-fixture.gob"),
        "a mixed locator must preserve immutable archive captures without opening their display-only labels");
    revalidateNativeModelSource(inMemoryWinner);

    int materializations = 0;
    mounts->setPrimaryResourceOverride("sentry.3do", [&]
    {
        ++materializations;
        const auto model = std::as_bytes(std::span(bytes.constData(), static_cast<std::size_t>(bytes.size())));
        return std::optional{std::vector<std::byte>(model.begin(), model.end())};
    });
    const auto overrideWinner = readNativeModelSource({lookup, true, mounts});
    require(QFile::remove(higher), "cannot remove disk GOB beneath the immutable primary override");
    revalidateNativeModelSource(overrideWinner);
    require(materializations == 1 && overrideWinner.bytes == bytes && overrideWinner.originIdentity.contains("override"),
        "the frozen primary override must retain its revision and once-only payload");
}

void exercise(const QDir& root)
{
    const auto fixture = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path()
        / "tests/fixtures/relay-sentry/relay_sentry.3do";
    const auto bytes = readFile(QFileInfo(fixture).absoluteFilePath());
    winnerIdentity(root, bytes);
    diskArchiveChanges(root, bytes);
    const QString leaf = QStringLiteral("源-É.3do");
    const QString source = root.filePath(leaf);
    const QString destination = root.filePath(QStringLiteral("изменён-月.3do"));
    const auto nativeDestination = QFileInfo(destination).filesystemAbsoluteFilePath();
    writeFile(source, bytes);
    const auto original = readNativeModelSource({source, false, {}});
    require(original.bytes == bytes && fs::equivalent(original.diskOrigin, QFileInfo(source).filesystemAbsoluteFilePath())
        && original.sha256 == QString::fromStdString(update::sha256Hex(bytes.toStdString())),
        "Unicode loose source identity, bytes or SHA-256 changed");
    auto resources = std::make_shared<assets::ResourceLocator>();
    resources->addRoot(QFileInfo(root.absolutePath()).filesystemAbsoluteFilePath());
    const auto resource = readNativeModelSource({leaf, true, resources});
    require(resource.bytes == bytes && fs::equivalent(resource.diskOrigin, original.diskOrigin),
        "Unicode project resource did not identify its actual loose entry");

    const auto virtualGob = assets::buildGobBytes({ { "3do/gob-only.3do", bytes.toStdString() } });
    const auto archiveBytes = std::as_bytes(std::span(virtualGob.data(), virtualGob.size()));
    auto archives = std::make_shared<assets::ResourceLocator>();
    archives->mountGob(assets::GobArchive::fromBytes({ archiveBytes.begin(), archiveBytes.end() }, "fixture.gob"));
    const auto archived = readNativeModelSource({QStringLiteral("gob-only.3do"), true, archives});
    require(archived.bytes == bytes && archived.diskOrigin.empty()
        && archived.originIdentity.contains(QStringLiteral("fixture.gob"))
        && archived.originIdentity.contains(QStringLiteral("gob-only.3do")), "archive identity lacks its selected entry");
    revalidateNativeModelSource(archived);
    const auto oversized = root.filePath(QStringLiteral("oversized.3do"));
    QFile sparse(oversized);
    require(sparse.open(QIODevice::WriteOnly) && sparse.resize(static_cast<qint64>(assets::ModelLightingLimits{}.sourceBytes) + 1),
        "cannot create oversized model");
    sparse.close();
    mustRefuse([&] { readNativeModelSource({oversized, false, {}}); }, "loose source ignored its byte ceiling");
    mustRefuse([&] { readNativeModelSource({QStringLiteral("oversized.3do"), true, resources}); },
        "project source ignored its byte ceiling");

    const auto document = assets::NativeModelDocument::open(bytes.toStdString());
    const assets::NativeTextureVertexEdit uv[]{ { 1, 1, 2, -16.5f, 35.25f } };
    const assets::NativeMeshTransform transform[]{ { 1, 1, 1.25, { 0.125, -0.25, 0.5 } } };
    const auto cumulative = document.applyTextureVertexEdits(uv).applyMeshTransforms(transform);
    const auto exact = QByteArray::fromStdString(std::string(cumulative.source()));
    const auto review = reviewNativeModelVariant(resource, cumulative, destination);
    require(review->candidate.source() == cumulative.source() && review->destination.filename() == nativeDestination.filename()
        && fs::equivalent(review->destination.parent_path(), nativeDestination.parent_path())
        && QFileInfo(review->destinationText).filesystemAbsoluteFilePath() == nativeDestination
        && review->sha256 == QString::fromStdString(update::sha256Hex(cumulative.source())),
        "review lost the absolute native destination or cumulative reviewed bytes");
    require(review->summary.contains(QStringLiteral("LOD 1")) && review->summary.contains(QStringLiteral("mesh 1"))
        && review->summary.contains(QStringLiteral("UV")) && review->summary.contains(QStringLiteral("2"))
        && review->summary.contains(QStringLiteral("position"), Qt::CaseInsensitive),
        "review does not report both the transformed source mesh and UV row");
    mustRefuse([&] { reviewNativeModelVariant(original, cumulative, source); }, "accepted source as destination");
    mustRefuse([&] { reviewNativeModelVariant(original, cumulative, root.filePath("bad.txt")); }, "accepted wrong extension");
    mustRefuse([&] { reviewNativeModelVariant(original, cumulative, root.filePath(".3do")); },
        "accepted an extension-only destination without a model stem");
    const auto hidden = reviewNativeModelVariant(original, cumulative, root.filePath(".edited.3do"));
    require(hidden->destination.filename() == fs::path(".edited.3do"), "a hidden filename with a model stem must work");

    writeFile(source, bytes + "\n# changed before staging\n");
    mustRefuse([&] { reviewNativeModelVariant(original, cumulative, destination); }, "review accepted stale source");
    mustRefuse([&] { stageNativeModelVariant(review); }, "staged after source changed");
    require(!QFileInfo::exists(destination), "stage published changed source");
    noStages(root);
    writeFile(source, bytes);
    {
        const auto cancelled = stageNativeModelVariant(review);
        require(!cancelled->published() && !QFileInfo::exists(destination), "unpublished stage became visible");
    }
    noStages(root);
    {
        const auto stage = stageNativeModelVariant(review);
        writeFile(source, bytes + "\n# changed at publication\n");
        mustRefuse([&] { publishNativeModelVariant(*stage); }, "published after source changed");
        require(!stage->published() && !QFileInfo::exists(destination), "failed source check left a variant");
    }
    noStages(root);
    writeFile(source, bytes);
    {
        const auto stage = stageNativeModelVariant(review);
        writeFile(destination, "concurrent creator");
        mustRefuse([&] { publishNativeModelVariant(*stage); }, "replaced concurrently created destination");
        require(!stage->published() && readFile(destination) == "concurrent creator", "lost competing creator");
        mustRefuse([&] { reviewNativeModelVariant(original, cumulative, destination); }, "review accepted occupied destination");
    }
    require(QFile::remove(destination), "failed to clear test occupant");
    noStages(root);

#ifndef _WIN32
    const auto absentTarget = QFileInfo(root.filePath("missing-target")).filesystemAbsoluteFilePath();
    fs::create_symlink(absentTarget, nativeDestination);
    mustRefuse([&] { reviewNativeModelVariant(original, cumulative, destination); }, "review accepted dangling symlink");
    require(fs::is_symlink(nativeDestination) && !fs::exists(absentTarget), "dangling symlink was modified");
    fs::remove(nativeDestination);
    const auto alias = nativeDestination.parent_path() / "destination-alias";
    const auto alternateParent = nativeDestination.parent_path() / "alternate";
    fs::create_directory(alternateParent);
    fs::create_directory_symlink(nativeDestination.parent_path(), alias);
    {
        const auto aliasReview = reviewNativeModelVariant(original, cumulative,
            QFileInfo(alias / nativeDestination.filename()).absoluteFilePath());
        const auto stage = stageNativeModelVariant(aliasReview);
        fs::remove(alias);
        fs::create_directory_symlink(alternateParent, alias);
        mustRefuse([&] { publishNativeModelVariant(*stage); }, "published after reviewed parent alias was redirected");
        require(!stage->published() && !fs::exists(nativeDestination)
            && !fs::exists(alternateParent / nativeDestination.filename()), "redirected alias received output");
    }
    noStages(root);
    fs::remove(alias);
    fs::create_hard_link(original.diskOrigin, nativeDestination);
    mustRefuse([&] { reviewNativeModelVariant(original, cumulative, destination); }, "review accepted hard-linked source alias");
    require(fs::equivalent(nativeDestination, original.diskOrigin), "hard-linked source alias was modified");
    fs::remove(nativeDestination);
#endif

    using Fault = assets::detail::StagedAssetFileFaultPointForTests;
    for ( const auto point : std::array{ Fault::write, Fault::flush, Fault::close, Fault::publish } )
    {
        assets::detail::ScopedStagedAssetFileFaultForTests fault(point);
        mustRefuse([&]
        {
            const auto stage = stageNativeModelVariant(review);
            publishNativeModelVariant(*stage);
        }, "staging/publication fault succeeded");
        require(fault.triggered() && readFile(source) == bytes && !QFileInfo::exists(destination),
            "failed publication changed filesystem");
        noStages(root);
    }

    const auto higherPriority = root.filePath("overlay");
    require(QDir().mkpath(higherPriority), "cannot create loose-resource overlay");
    auto overlayLocator = std::make_shared<assets::ResourceLocator>(*resources);
    overlayLocator->addRoot(QFileInfo(higherPriority).filesystemAbsoluteFilePath());
    const auto overlayOriginal = readNativeModelSource({leaf, true, overlayLocator});
    writeFile(QDir(higherPriority).filePath(leaf), bytes);
    mustRefuse([&] { revalidateNativeModelSource(overlayOriginal); }, "accepted same-byte resource from new winning origin");

    {
        const auto stage = stageNativeModelVariant(review);
        require(!stage->published() && !QFileInfo::exists(destination), "stage exposed unreviewed output");
        publishNativeModelVariant(*stage);
        require(stage->published(), "successful publication state was lost");
        mustRefuse([&] { publishNativeModelVariant(*stage); }, "stage was published twice");
        require(stage->published(), "repeat attempt erased publication status");
    }
    noStages(root);
    require(fs::is_regular_file(nativeDestination) && readFile(destination) == exact && readFile(source) == bytes,
        "new output, native Unicode entry or original bytes differ from review");
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    try
    {
        require(directory.isValid(), "cannot create temporary directory");
        const QString nested = directory.filePath(QStringLiteral("données-星"));
        require(QDir().mkpath(nested), "cannot create Unicode parent");
        exercise(QDir(nested));
        return 0;
    }
    catch ( const std::exception& error )
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
