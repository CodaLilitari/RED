#include "ui/episode/gameplay_publication.hpp"

#include "../../../../core/src/assets/atomic_file_write.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

#include <memory>
#include <set>
#include <stdexcept>

namespace sed::ui::episode
{
namespace
{
namespace fs = std::filesystem;
using Staged = assets::detail::StagedAssetFile;
constexpr qsizetype maxFileBytes = 16 * 1024 * 1024;
constexpr qsizetype maxPlanBytes = 32 * 1024 * 1024;
constexpr qsizetype maxJournalBytes = 48 * 1024 * 1024;
constexpr std::size_t maxFiles = 32;

QString pathString(const fs::path& path)
{
    return QString::fromStdU16String(path.generic_u16string());
}

QByteArray digest(std::string_view bytes)
{
    return QCryptographicHash::hash(QByteArrayView(bytes.data(), static_cast<qsizetype>(bytes.size())),
                                    QCryptographicHash::Sha256)
        .toHex();
}

bool legacyStrings(const fs::path& path)
{
    return path == "cogstrings.uni" || path == "ui/cogstrings.uni";
}

bool metadata(const fs::path& path)
{
    return path == "episode.jk" || path == "misc/cogstrings.uni" || legacyStrings(path);
}

void checkPath(const fs::path& root, const fs::path& relative)
{
    const QString name = pathString(relative);
    if ( relative.empty() || relative.has_root_path() || name.contains(QChar::Null) || name.contains(':') ||
         name.contains('\\') || relative.lexically_normal() != relative )
    {
        throw std::runtime_error("Invalid gameplay project path.");
    }
    const bool generated = (relative.parent_path().empty() && name.endsWith(QStringLiteral("_start.cog"))) ||
                           (relative.parent_path() == "cog" && relative.extension() == ".cog") ||
                           relative == "templates/swoop-enemy-pursuit.tpl" ||
                           relative == "templates/swoop-enemy-pursuit.md";
    if ( !metadata(relative) && !generated )
    {
        throw std::runtime_error("Unknown gameplay file role.");
    }
    auto resolved = root;
    for ( const auto& component : relative )
    {
        if ( component == "." || component == ".." || component.empty() )
        {
            throw std::runtime_error("Invalid gameplay path component.");
        }
        resolved /= component;
        if ( fs::is_symlink(fs::symlink_status(resolved)) )
        {
            throw std::runtime_error("Gameplay publication refuses a symbolic link: " + resolved.string());
        }
    }
}

QByteArray readBounded(const fs::path& path, const qsizetype limit)
{
    QFile file(pathString(path));
    if ( !fs::is_regular_file(fs::symlink_status(path)) || !file.open(QIODevice::ReadOnly) || file.size() > limit )
    {
        throw std::runtime_error("Cannot read a complete bounded regular file: " + path.string());
    }
    const auto bytes = file.read(limit + 1);
    if ( bytes.size() > limit || file.error() != QFileDevice::NoError || !file.atEnd() || file.size() != bytes.size() )
    {
        throw std::runtime_error("File changed or exceeded the read limit: " + path.string());
    }
    return bytes;
}

fs::path journalPath(const fs::path& root, const fs::path& recovery)
{
    QString identity = pathString(fs::canonical(root));
#ifdef _WIN32
    identity = identity.toCaseFolded();
#endif
    return recovery / "gameplay" / (digest(identity.toStdString()).toStdString() + ".json");
}

QByteArray encode(const std::vector<GameplayFileChange>& files)
{
    QJsonArray rows;
    for ( const auto& file : files )
    {
        QJsonObject row{{"path", pathString(file.relativePath)}};
        if ( file.previousSha256 )
        {
            row["previous"] = QString::fromLatin1(*file.previousSha256);
        }
        if ( file.nextBytes )
        {
            row["next"] = QString::fromLatin1(QByteArray::fromStdString(*file.nextBytes).toBase64());
        }
        rows.append(row);
    }
    const auto body = QJsonDocument(QJsonObject{{"version", 1}, {"files", rows}}).toJson(QJsonDocument::Compact);
    return QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex() + '\n' + body;
}

std::vector<GameplayFileChange> decode(const QByteArray& bytes)
{
    const QByteArray body = bytes.mid(65);
    if ( bytes.indexOf('\n') != 64 ||
         QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex() != bytes.left(64) )
    {
        throw std::runtime_error("Gameplay recovery journal checksum failed. The project files were preserved.");
    }
    const auto record = QJsonDocument::fromJson(body).object();
    const auto rows = record.value("files").toArray();
    if ( record.size() != 2 || record.value("version").toInt() != 1 || rows.isEmpty() ||
         rows.size() > static_cast<qsizetype>(maxFiles) )
    {
        throw std::runtime_error("Invalid gameplay recovery journal.");
    }
    std::vector<GameplayFileChange> files;
    for ( const auto& item : rows )
    {
        const auto row = item.toObject();
        GameplayFileChange file{};
        if ( !row.value("path").isString() || row.size() != 1 + row.contains("previous") + row.contains("next") )
        {
            throw std::runtime_error("Invalid gameplay recovery file.");
        }
        file.relativePath = fs::path(row.value("path").toString().toStdU16String());
        if ( row.contains("previous") )
        {
            const auto hash = row.value("previous").toString().toLatin1();
            if ( hash.size() != 64 || QByteArray::fromHex(hash).toHex() != hash )
            {
                throw std::runtime_error("Invalid previous gameplay file hash.");
            }
            file.previousSha256 = hash;
        }
        if ( row.contains("next") )
        {
            if ( !row.value("next").isString() )
            {
                throw std::runtime_error("Invalid staged gameplay bytes.");
            }
            const auto decoded = QByteArray::fromBase64Encoding(row.value("next").toString().toLatin1(),
                                                                QByteArray::AbortOnBase64DecodingErrors);
            if ( !decoded || decoded.decoded.size() > maxFileBytes )
            {
                throw std::runtime_error("Invalid staged gameplay bytes.");
            }
            file.nextBytes = decoded.decoded.toStdString();
        }
        files.push_back(std::move(file));
    }
    return files;
}

struct PreparedFile
{
    std::optional<QByteArray> desired;
    std::unique_ptr<Staged> staged;
};

void complete(const fs::path& root, const fs::path& journal, const std::vector<GameplayFileChange>& files,
              const bool replay)
{
    if ( files.empty() || files.size() > maxFiles )
    {
        throw std::runtime_error("Invalid gameplay file count.");
    }
    std::vector<PreparedFile> prepared(files.size());
    std::set<fs::path> paths;
    std::size_t total = 0;
    for ( std::size_t i = 0; i < files.size(); ++i )
    {
        const auto& file = files[i];
        const auto& path = file.relativePath;
        if ( !paths.insert(path).second || (!metadata(path) && file.previousSha256) ||
             (!file.nextBytes && !legacyStrings(path)) )
        {
            throw std::runtime_error("Invalid gameplay file change.");
        }
        if ( file.nextBytes )
        {
            total += file.nextBytes->size();
            if ( file.nextBytes->size() > maxFileBytes || total > maxPlanBytes )
            {
                throw std::runtime_error("Gameplay file plan exceeds its byte budget.");
            }
            prepared[i].desired = digest(*file.nextBytes);
        }
        const auto actual = gameplayFileRevision(root, path);
        if ( actual == prepared[i].desired )
        {
            continue;
        }
        if ( actual != file.previousSha256 )
        {
            throw std::runtime_error("Gameplay file differs from the reviewed revision: " + (root / path).string());
        }
        if ( file.nextBytes )
        {
            fs::create_directories((root / path).parent_path());
            auto staged = std::make_unique<Staged>(root / path);
            staged->writeAndClose(std::as_bytes(std::span(file.nextBytes->data(), file.nextBytes->size())));
            prepared[i].staged = std::move(staged);
        }
    }
    if ( !replay )
    {
        QSaveFile file(pathString(journal));
        const auto bytes = encode(files);
        if ( !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit() )
        {
            throw std::runtime_error("Could not record the gameplay operation before publication: " + journal.string());
        }
        if ( readBounded(journal, maxJournalBytes) != bytes )
        {
            throw std::runtime_error("Gameplay journal verification failed before publishing any file: " +
                                     journal.string());
        }
    }
    for ( std::size_t i = 0; i < files.size(); ++i )
    {
        const auto& file = files[i];
        const auto path = root / file.relativePath;
        const auto actual = gameplayFileRevision(root, file.relativePath);
        if ( actual == prepared[i].desired )
        {
            continue;
        }
        if ( actual != file.previousSha256 )
        {
            throw std::runtime_error("Gameplay file changed during publication: " + path.string());
        }
        if ( !file.nextBytes )
        {
            fs::remove(path);
        }
        else if ( !prepared[i].staged )
        {
            throw std::runtime_error("Gameplay file changed after staging: " + path.string());
        }
        else if ( actual )
        {
            prepared[i].staged->publish(path);
        }
        else
        {
            prepared[i].staged->publishNew(path);
        }
    }
    for ( std::size_t i = 0; i < files.size(); ++i )
    {
        if ( gameplayFileRevision(root, files[i].relativePath) != prepared[i].desired )
        {
            throw std::runtime_error("Gameplay publication readback failed: " +
                                     (root / files[i].relativePath).string());
        }
    }
    if ( !fs::remove(journal) )
    {
        throw std::runtime_error("Could not finish the gameplay recovery record.");
    }
}

void lockJournal(QLockFile& lock)
{
    lock.setStaleLockTime(0);
    if ( !lock.tryLock(0) )
    {
        throw std::runtime_error("Another RED session is publishing gameplay files in this project.");
    }
}
} // namespace

std::optional<QByteArray> gameplayFileRevision(const fs::path& project, const fs::path& relativePath)
{
    checkPath(project, relativePath);
    const auto path = project / relativePath;
    if ( !fs::exists(fs::symlink_status(path)) )
    {
        return std::nullopt;
    }
    return QCryptographicHash::hash(readBounded(path, maxFileBytes), QCryptographicHash::Sha256).toHex();
}

void publishGameplayFiles(const fs::path& project, const fs::path& recovery,
                          const std::vector<GameplayFileChange>& files)
{
    const auto journal = journalPath(project, recovery);
    fs::create_directories(journal.parent_path());
    QLockFile lock(pathString(journal) + QStringLiteral(".lock"));
    lockJournal(lock);
    if ( fs::exists(journal) )
    {
        throw std::runtime_error("An earlier gameplay operation is pending. Reopen the project to recover it.");
    }
    try
    {
        complete(project, journal, files, false);
    }
    catch ( const std::exception& error )
    {
        if ( fs::exists(journal) )
        {
            throw std::runtime_error(
                std::string(error.what()) +
                " The operation record was retained. Reopen the project to finish the approved files.");
        }
        throw;
    }
}

bool recoverGameplayFiles(const fs::path& project, const fs::path& recovery)
{
    const auto journal = journalPath(project, recovery);
    if ( !fs::exists(journal) )
    {
        return false;
    }
    QLockFile lock(pathString(journal) + QStringLiteral(".lock"));
    lockJournal(lock);
    if ( !fs::exists(journal) )
    {
        return false;
    }
    complete(project, journal, decode(readBounded(journal, maxJournalBytes)), true);
    return true;
}
} // namespace sed::ui::episode
