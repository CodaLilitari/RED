#include "app_icon.hpp"
#include "app_logging.hpp"
#include "app_settings.hpp"
#include "app_version.hpp"
#include "main_window/main_window.hpp"
#include "render/viewport_aa_policy.hpp"
#include "ui/components/shared_ui_styles.hpp"
#include "ui/components/ui_decimal_format.hpp"
#include "ui/recovery/windows_recovery_hooks.hpp"
#include "ui/diagnostics/installation_check_contract.hpp"
#include "ui/diagnostics/installation_check_native.hpp"

#include <QApplication>
#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QSurfaceFormat>
#include <QSessionManager>
#include <QSettings>

#include <exception>
#include <cstdio>

int main(int argc, char** argv)
{
    const auto arguments = sed::ui::diagnostics::installationProcessArguments(argc, argv);
    if ( !arguments )
    {
        std::fprintf(stderr, "Could not read the process command line.\n");
        return 64;
    }
    const auto diagnostic = sed::ui::diagnostics::parseInstallationArguments(*arguments);
    using sed::ui::diagnostics::InstallationStartupMode;
    if ( diagnostic.mode == InstallationStartupMode::refused )
    {
        std::fprintf(stderr, "%s\n", diagnostic.error.toUtf8().constData());
        return 64;
    }
    if ( diagnostic.mode == InstallationStartupMode::controller )
    {
        return sed::ui::diagnostics::runInstallationCheckController(argc, argv, diagnostic.reportDirectory);
    }
    if ( diagnostic.mode == InstallationStartupMode::child )
    {
        return sed::ui::diagnostics::runInstallationCheckChild(argc, argv, diagnostic.reportDirectory);
    }
    // Supply the normal default before QApplication; persisted preferences
    // are read just after construction and before any viewport context exists.
    QSurfaceFormat fmt = sed::render::viewportSurfaceFormat(
        sed::render::kDefaultViewportMsaaSamples);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setApplicationName("RED");
    QApplication::setApplicationVersion(QString::fromLatin1(sed::app::kVersion));
    QApplication::setOrganizationName("RED");
    // RED is a fork of SED (GPLv3). Native title bars end with "RED".
    QApplication::setApplicationDisplayName(QStringLiteral("RED"));

    {
        const QSettings settings = sed::settingsStore();
        sed::ui::installUiDecimalPolicy(app, sed::loadUiDecimalSeparator(settings));
        fmt = sed::render::viewportSurfaceFormat(settings);
        QSurfaceFormat::setDefaultFormat(fmt);
    }

    const QIcon appIcon = sed::loadApplicationIcon();
    if (!appIcon.isNull())
    {
        QApplication::setWindowIcon(appIcon);
    }
    sed::installApplicationLogging(app);
    sed::ui::recovery::installWindowsRecoveryHooks(
        sed::appRecoveryDirectoryPath());

    try
    {
        sed::MainWindow window(nullptr, true);
        QObject::connect(
            &app, &QGuiApplication::commitDataRequest,
            &window, [&window](QSessionManager&) { window.prepareForSystemShutdown(); },
            Qt::DirectConnection);
        if (!appIcon.isNull())
        {
            window.setWindowIcon(appIcon);
        }
        const QColor titleBarColor = sed::ui::applicationChromeColor();
        sed::applyWindowsTitleBarColor(window, titleBarColor);
        // Create/apply the native large icon before the startup flow.  The
        // recovery offer is intentionally modal and can be shown while the
        // main window is still hidden; applying this early keeps the owning
        // application entry identifiable in the Windows taskbar.
        sed::applyWindowsTaskbarIcon(window);
        if (!window.runInitialStartupFlow())
        {
            return 0;
        }
        if (!window.isVisible())
        {
            window.show();
        }
        sed::applyWindowsTitleBarColor(window, titleBarColor);
        sed::applyWindowsTaskbarIcon(window);

        return app.exec();
    }
    catch (const std::exception& e)
    {
        qCritical("Fatal startup error: %s", e.what());
        return 1;
    }
    catch (...)
    {
        qCritical("Fatal startup error: unknown exception");
        return 1;
    }
}
