#include "app_settings.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QUuid>

#include <iostream>

namespace
{
int failures{};
void check(const bool condition, const char* message)
{
    if ( !condition )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
std::filesystem::path path(const QString& value)
{
    return std::filesystem::path(value.toStdWString());
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    QCoreApplication::setOrganizationName("RED-Installation-Storage-Tests");
    QCoreApplication::setApplicationName(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, temporary.path());
    sed::setAppSettingsIniPathOverrideForTest(path(temporary.filePath("normal/red.ini")));
    QSettings legacy;
    legacy.setValue("sentinel", "private-native-value");
    legacy.sync();
    const QString mode = application.arguments().value(1);
    QString error;
    const auto isolated = path(temporary.filePath(QStringLiteral("isolated-\u00e9-\u0394")));
    if ( mode == "late" )
    {
        const auto original = sed::appSettingsIniPath();
        check(!sed::initializeIsolatedAppStorage(isolated, &error), "late isolation is refused after a path was exposed");
        check(sed::appSettingsIniPath() == original && !QFile::exists(QString::fromStdWString(isolated.wstring())),
            "late refusal preserves prior policy and creates nothing");
    }
    else
    {
        check(!sed::initializeIsolatedAppStorage(path("relative"), &error), "relative storage is refused");
        check(sed::initializeIsolatedAppStorage(isolated, &error), "fresh absolute storage is admitted before first use");
        if ( failures != 0 )
        {
            return 1;
        }
        check(sed::appSettingsIniPath() == isolated / "red.ini", "isolated settings override every normal path");
        auto settings = sed::settingsStore();
        check(!settings.contains("sentinel"), "isolated startup never imports native legacy settings");
        settings.setValue("diagnostic", true);
        settings.sync();
        check(settings.status() == QSettings::NoError, "isolated settings remain usable");
        check(sed::appThemePerUserDataDirectory() == isolated, "theme fallback stays inside isolated storage");
        check(sed::appRecoveryDirectoryPath() == isolated / "recovery", "recovery destination stays isolated");
        const auto search = sed::appRecoverySearchDirectoryPaths();
        check(search == std::vector<std::filesystem::path>{ isolated / "recovery" },
            "recovery searches never include user, home or legacy paths");
        const auto themes = sed::appThemeDirectorySearchPaths(sed::AppThemeKind::viewport,
            sed::appSettingsIniPath(), sed::appThemePerUserDataDirectory());
        for ( const auto& item : themes )
        {
            check(item.lexically_relative(isolated).native().find(std::filesystem::path("..").native())
                != 0, "every theme search stays inside isolated storage");
        }
        check(!QFile::exists(temporary.filePath("normal/red.ini"))
            && legacy.value("sentinel") == "private-native-value", "normal/native settings remain unchanged");
        check(!sed::initializeIsolatedAppStorage(path(temporary.filePath("second")), &error),
            "isolation cannot be redirected later");
    }
    std::cout << "installation storage failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
