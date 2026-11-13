#include "app_settings.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <filesystem>
#include <iostream>

namespace
{
int failures = 0;

void check(const bool condition, const char* message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("REDPathRegression"));
    QCoreApplication::setApplicationName(QStringLiteral("REDPathRegression"));
    QStandardPaths::setTestModeEnabled(true);
    using Path = std::filesystem::path;
    const Path bundle = Path("/Applications/RED.app/Contents/MacOS");
    const Path user = Path("/Users/designer/Library/Preferences/RED");
    check(sed::appSettingsIniPath(bundle, user, true) == user / "red.ini",
        "macOS app bundle stores settings outside its signed contents");
    check(sed::appSettingsIniPath(Path("/Applications/RED Preview.app/Contents/MacOS"), user, true)
            == user / "red.ini",
        "macOS bundle detection accepts spaces in the application name");
    check(sed::appSettingsIniPath(Path("/portable/RED"), user, false) == Path("/portable/RED/red.ini"),
        "Windows and Linux portable settings remain beside the executable");
    check(sed::appSettingsIniPath(bundle, user, false) == bundle / "red.ini",
        "a similarly named directory does not change non-macOS portable behavior");
    check(sed::appSettingsIniPath(Path("/local/bin"), user, true) == Path("/local/bin/red.ini"),
        "macOS unbundled development executable keeps portable behavior");
    check(sed::appSettingsIniPath(Path("/local/Contents/MacOS"), user, true)
            == Path("/local/Contents/MacOS/red.ini"),
        "Contents/MacOS without an app ancestor is not a bundle");
    bool missingUserPathRefused = false;
    try
    {
        (void)sed::appSettingsIniPath(bundle, {}, true);
    }
    catch ( const std::exception& )
    {
        missingUserPathRefused = true;
    }
    check(missingUserPathRefused, "missing macOS user configuration path never falls back into the bundle");

    QTemporaryDir scratch;
    check(scratch.isValid(), "settings regression uses an isolated temporary directory");
    if ( !scratch.isValid() )
    {
        return 1;
    }
    const Path baseline = Path(scratch.path().toStdWString()) / "red.ini";
    const Path override = Path(scratch.path().toStdWString()) / "override.ini";
    sed::setAppSettingsIniPathBaselineOverrideForTest(baseline);
    sed::setAppSettingsIniPathOverrideForTest(override);
    check(sed::appSettingsIniPath() == override, "explicit test settings override wins");
    sed::setAppSettingsIniPathOverrideForTest({});
    check(sed::appSettingsIniPath() == baseline, "baseline test settings override remains isolated");
    const QString legacyPath = scratch.filePath(QStringLiteral("sed.ini"));
    const QByteArray legacyBytes("[game]\npath=synthetic-retail\n");
    {
        QFile legacy(legacyPath);
        check(legacy.open(QIODevice::WriteOnly) && legacy.write(legacyBytes) == legacyBytes.size(),
            "legacy settings fixture is complete");
    }
    {
        auto settings = sed::settingsStore();
        check(settings.value(QStringLiteral("game/path")).toString() == QStringLiteral("synthetic-retail"),
            "portable SED settings import remains available");
        settings.setValue(QStringLiteral("game/path"), QStringLiteral("updated"));
        settings.sync();
        check(settings.status() == QSettings::NoError, "active settings remain writable");
    }
    QFile legacy(legacyPath);
    check(legacy.open(QIODevice::ReadOnly) && legacy.readAll() == legacyBytes,
        "importing and editing RED settings preserves original SED bytes");
    check(sed::settingsStore().value(QStringLiteral("game/path")).toString() == QStringLiteral("updated"),
        "existing RED settings are not imported over again");

    const QString oldDirectory = scratch.filePath(QStringLiteral("RED.app/Contents/MacOS"));
    check(QDir().mkpath(oldDirectory), "synthetic legacy bundle directory exists");
    const QString oldRedPath = oldDirectory + QStringLiteral("/red.ini");
    const QByteArray oldRedBytes("[game]\npath=prior-red-install\n");
    {
        QFile oldRed(oldRedPath);
        check(oldRed.open(QIODevice::WriteOnly) && oldRed.write(oldRedBytes) == oldRedBytes.size(),
            "synthetic old bundled RED settings exist");
    }
    const Path migratedPath = Path(scratch.path().toStdWString()) / "new-user" / "red.ini";
    check(sed::importLegacyAppSettingsIni(migratedPath, Path(oldDirectory.toStdWString()), true),
        "first macOS launch imports existing bundled RED settings into a new user directory");
    {
        QFile migrated(QString::fromStdWString(migratedPath.wstring()));
        check(migrated.open(QIODevice::ReadOnly) && migrated.readAll() == oldRedBytes,
            "bundled RED import preserves exact bytes");
    }
    const QByteArray currentUserBytes("[game]\npath=current-user-preference\n");
    {
        QFile migrated(QString::fromStdWString(migratedPath.wstring()));
        check(migrated.open(QIODevice::WriteOnly) && migrated.write(currentUserBytes) == currentUserBytes.size(),
            "updated per-user settings fixture is complete");
    }
    check(!sed::importLegacyAppSettingsIni(migratedPath, Path(oldDirectory.toStdWString()), true),
        "existing per-user settings are never overwritten by migration");
    {
        QFile migrated(QString::fromStdWString(migratedPath.wstring()));
        check(migrated.open(QIODevice::ReadOnly) && migrated.readAll() == currentUserBytes,
            "migration retains a newer per-user preference byte for byte");
    }
    QFile oldRed(oldRedPath);
    check(oldRed.open(QIODevice::ReadOnly) && oldRed.readAll() == oldRedBytes,
        "migration never writes to the old application bundle");
    oldRed.close();
    const Path otherPlatformOutput = Path(scratch.path().toStdWString()) / "other-platform" / "red.ini";
    check(!sed::importLegacyAppSettingsIni(otherPlatformOutput, Path(oldDirectory.toStdWString()), false)
            && !std::filesystem::exists(otherPlatformOutput),
        "non-macOS migration does not read another application's bundle");
    const QString sedOnlyDirectory = scratch.filePath(QStringLiteral("SED.app/Contents/MacOS"));
    check(QDir().mkpath(sedOnlyDirectory), "synthetic SED-only bundle directory exists");
    {
        QFile sedOnly(sedOnlyDirectory + QStringLiteral("/sed.ini"));
        check(sedOnly.open(QIODevice::WriteOnly) && sedOnly.write(legacyBytes) == legacyBytes.size(),
            "synthetic SED-only bundle settings exist");
    }
    const Path sedOnlyOutput = Path(scratch.path().toStdWString()) / "sed-only-user" / "red.ini";
    check(sed::importLegacyAppSettingsIni(sedOnlyOutput, Path(sedOnlyDirectory.toStdWString()), true),
        "macOS imports a legacy SED bundle when prior RED settings are absent");
    {
        QFile migrated(QString::fromStdWString(sedOnlyOutput.wstring()));
        check(migrated.open(QIODevice::ReadOnly) && migrated.readAll() == legacyBytes,
            "legacy SED bundle import preserves exact bytes");
    }
    sed::setAppSettingsIniPathBaselineOverrideForTest({});
    return failures == 0 ? 0 : 1;
}
