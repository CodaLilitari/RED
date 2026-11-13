#include "app_settings.hpp"
#include "test_common.hpp"

#include <QApplication>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace sed::app_tests
{
void runGameTestExtraResourcesTests();
}

int main(int argc, char** argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir settings;
    if ( !settings.isValid() )
    {
        return 2;
    }
    const QString iniPath = settings.filePath(QStringLiteral("red.ini"));
    QFile ini(iniPath);
    if ( !ini.open(QIODevice::WriteOnly | QIODevice::NewOnly) )
    {
        return 2;
    }
    ini.close();
    sed::setAppSettingsIniPathOverrideForTest(std::filesystem::path(iniPath.toStdWString()));
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("RED extra resource regression"));
    sed::app_tests::runGameTestExtraResourcesTests();
    std::cout << "Extra test resource failures: " << sed::app_tests::failures << '\n';
    return sed::app_tests::failures ? 1 : 0;
}
