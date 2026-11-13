#include "installation_check_contract.hpp"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QScopeGuard>

#include <algorithm>
#include <array>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace sed::ui::diagnostics
{
namespace
{

bool refuse(QString* const error, const QString& detail)
{
    if ( error )
    {
        *error = detail;
    }
    return false;
}

bool regionMatches(const QImage& frame, const QPoint center, const bool bright)
{
    for ( int y = center.y() - 3; y <= center.y() + 3; ++y )
    {
        for ( int x = center.x() - 3; x <= center.x() + 3; ++x )
        {
            const QColor color = frame.pixelColor(x, y);
            if ( color.alpha() < 250
                || (bright && (color.red() < 200 || color.green() < 200 || color.blue() < 200))
                || (!bright && (color.red() > 12 || color.green() > 12 || color.blue() > 12)) )
            {
                return false;
            }
        }
    }
    return true;
}

} // namespace

std::optional<QStringList> installationProcessArguments(const int argc, char** argv)
{
    QStringList arguments;
#ifdef Q_OS_WIN
    (void)argc;
    (void)argv;
    int count{};
    wchar_t** words = CommandLineToArgvW(GetCommandLineW(), &count);
    if ( !words )
    {
        return std::nullopt;
    }
    const auto cleanup = qScopeGuard([words]() { LocalFree(words); });
    // Windows command lines are limited to 32767 UTF-16 code units. Keep
    // admission bounded even if an unexpected platform response violates it.
    if ( count < 1 || count > 32768 )
    {
        return std::nullopt;
    }
    for ( int index = 1; index < count; ++index )
    {
        arguments.append(QString::fromWCharArray(words[index]));
    }
#else
    for ( int index = 1; index < argc; ++index )
    {
        arguments.append(QString::fromLocal8Bit(argv[index]));
    }
#endif
    return arguments;
}

InstallationArguments parseInstallationArguments(const QStringList& arguments)
{
    const QString publicMode = QStringLiteral("--check-installation");
    const QString childMode = QStringLiteral("--installation-check-child");
    if ( !arguments.contains(publicMode) && !arguments.contains(childMode) )
    {
        return {};
    }
    if ( arguments.size() != 3
        || (arguments.front() != publicMode && arguments.front() != childMode)
        || arguments[1] != QStringLiteral("--report-directory")
        || arguments[2].trimmed().isEmpty() || arguments[2].size() > 4096 )
    {
        return {InstallationStartupMode::refused, {}, QStringLiteral(
            "Use --check-installation --report-directory <new absolute directory>.")};
    }
    return {arguments.front() == publicMode
        ? InstallationStartupMode::controller : InstallationStartupMode::child, arguments[2], {}};
}

bool createInstallationReportDirectory(const QString& directory, QString* const error)
{
    if ( error )
    {
        error->clear();
    }
    const QString path = QDir::fromNativeSeparators(directory);
    const QFileInfo target(path);
    const QString name = target.fileName();
    const QRegularExpression device(QStringLiteral(
        "^(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\\..*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    const bool unsafeName = name.isEmpty() || name.endsWith(QLatin1Char(' '))
        || name.endsWith(QLatin1Char('.')) || device.match(name).hasMatch()
        || std::any_of(name.begin(), name.end(), [](const QChar character)
        {
            return character.unicode() < 32
                || QStringLiteral("\\:<>\"|?*").contains(character);
        });
    if ( path.size() > 4096 || !QDir::isAbsolutePath(path)
        || QDir::cleanPath(path) != path || unsafeName )
    {
        return refuse(error, QStringLiteral("The report needs an exact absolute directory with a portable new name."));
    }
    const QFileInfo parent(target.absolutePath());
    if ( target.exists() || target.isSymLink() || !parent.isDir()
        || !QDir(parent.absoluteFilePath()).mkdir(name) )
    {
        return refuse(error, QStringLiteral("The report directory must be new and its parent must already exist."));
    }
    const QFileInfo created(path);
    if ( created.isSymLink() || !created.isDir()
        || QFileInfo(created.canonicalFilePath()).absolutePath() != parent.canonicalFilePath() )
    {
        return refuse(error, QStringLiteral("The created report directory does not belong to the requested parent."));
    }
    return true;
}

bool assessInstallationFrames(
    const QImage& before, const QImage& after,
    const InstallationFrameProbes& probes, QString* const error)
{
    if ( error )
    {
        error->clear();
    }
    if ( before.isNull() || after.isNull() || before.size() != after.size()
        || before.width() < 64 || before.height() < 64
        || before.width() > 4096 || before.height() > 4096 )
    {
        return refuse(error, QStringLiteral("The two native frames must have matching bounded physical dimensions."));
    }
    const std::array<QPointF, 3> normalized{probes.firstFace, probes.secondFace, probes.background};
    std::array<QPoint, 3> pixels{};
    for ( std::size_t index = 0; index < normalized.size(); ++index )
    {
        const auto point = normalized[index];
        if ( !std::isfinite(point.x()) || !std::isfinite(point.y())
            || point.x() <= 0.0 || point.x() >= 1.0 || point.y() <= 0.0 || point.y() >= 1.0 )
        {
            return refuse(error, QStringLiteral("A synthetic geometry probe falls outside the frame."));
        }
        pixels[index] = QPoint(qRound(point.x() * (before.width() - 1)),
            qRound(point.y() * (before.height() - 1)));
        if ( !before.rect().adjusted(3, 3, -3, -3).contains(pixels[index]) )
        {
            return refuse(error, QStringLiteral("A synthetic geometry probe is too close to a frame edge."));
        }
        for ( std::size_t previous = 0; previous < index; ++previous )
        {
            if ( std::abs(pixels[index].x() - pixels[previous].x()) <= 6
                && std::abs(pixels[index].y() - pixels[previous].y()) <= 6 )
            {
                return refuse(error, QStringLiteral("Synthetic geometry probe regions must be distinct."));
            }
        }
    }
    if ( !regionMatches(before, pixels[0], true) || !regionMatches(before, pixels[1], false)
        || !regionMatches(after, pixels[0], false) || !regionMatches(after, pixels[1], true)
        || !regionMatches(before, pixels[2], false) || !regionMatches(after, pixels[2], false) )
    {
        return refuse(error, QStringLiteral(
            "The native frames did not show the expected moving bright face and stable dark background."));
    }
    return true;
}

} // namespace sed::ui::diagnostics
