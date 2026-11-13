#include "test_common.hpp"
#include "fitting_dialog_geometry.hpp"

#include "app_settings.hpp"
#include "main_window/main_window.hpp"

#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QOpenGLWidget>
#include <QLayout>
#include <QWindow>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>

#include <filesystem>
#include <iostream>
#include <memory>

namespace sed::app_tests
{
namespace
{
class HiddenGeometryGlGuard final : public QObject
{
public:
    HiddenGeometryGlGuard() { qApp->installEventFilter(this); }
    ~HiddenGeometryGlGuard() override { qApp->removeEventFilter(this); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if ( qobject_cast<QOpenGLWidget*>(watched)
            && (event->type() == QEvent::Show || event->type() == QEvent::Resize
                || event->type() == QEvent::Paint) )
        {
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
};

class GeometrySettingsOverride final
{
public:
    explicit GeometrySettingsOverride(const std::filesystem::path& path)
        : previous_(appSettingsIniPath())
    {
        setAppSettingsIniPathOverrideForTest(path);
    }

    ~GeometrySettingsOverride() { setAppSettingsIniPathOverrideForTest(previous_); }

private:
    std::filesystem::path previous_;
};

void printGeometryRect(const QRect& rect)
{
    std::cout << rect.x() << ',' << rect.y() << ',' << rect.width() << ',' << rect.height();
}

void checkProductionGeometry(const char* name, const QRect& requested, const bool fitting)
{
    std::cerr << "template-geometry stage: " << name << " seed begin" << std::endl;
    const QString key = QStringLiteral("templates/editor_geometry");
    QDialog seed;
    seed.setAttribute(Qt::WA_DontShowOnScreen);
    seed.setGeometry(requested);
    const QByteArray saved = seed.saveGeometry();
    QSettings settings = settingsStore();
    settings.setValue(key, saved);
    settings.sync();
    check(settings.status() == QSettings::NoError, "template geometry fixture writes its isolated INI");

    // Deferred placement, a hidden parent, no event pumping and the offscreen
    // precondition prevent native window/input interaction. Empty templates and
    // the locator-free factory do not load preview resources.
    std::cerr << "template-geometry stage: " << name << " owner construction begin" << std::endl;
    MainWindow owner(nullptr, true);
    std::cerr << "template-geometry stage: " << name << " owner construction complete" << std::endl;
    owner.setAttribute(Qt::WA_DontShowOnScreen);
    QDialog control(&owner);
    control.setAttribute(Qt::WA_DontShowOnScreen);
    control.setMinimumSize(1120, 640);
    const bool accepted = control.restoreGeometry(saved);
    std::cerr << "template-geometry stage: " << name << " production factory begin" << std::endl;
    std::unique_ptr<QDialog> editor(owner.createTemplateEditorDialogForTest({}));
    std::cerr << "template-geometry stage: " << name << " production factory complete" << std::endl;
    check(accepted && editor, "production geometry fixture and plain Qt control are constructed");
    if ( !editor )
    {
        return;
    }
    editor->setAttribute(Qt::WA_DontShowOnScreen);
    std::cout << "template-geometry " << name << ": requested=";
    printGeometryRect(requested);
    std::cout << " seed=";
    printGeometryRect(seed.geometry());
    std::cout << " control=";
    printGeometryRect(control.geometry());
    std::cout << " production=";
    printGeometryRect(editor->geometry());
    std::cout << " minimum=" << editor->minimumWidth() << ',' << editor->minimumHeight()
              << " savedBytes=" << saved.size()
              << " savedSha256=" << QCryptographicHash::hash(saved, QCryptographicHash::Sha256).toHex().constData()
              << " controlSizeExact=" << (control.size() == seed.size())
              << " productionSizeExact=" << (editor->size() == seed.size()) << std::endl;
    check(editor->geometry() == control.geometry(),
        "production constructor restores the same geometry as the equally constrained Qt control");
    if ( fitting )
    {
        check(editor->size() == seed.size(), "production constructor preserves a screen-fitting saved size");
    }
    check(!owner.isVisible() && !editor->isVisible() && !control.isVisible(),
        "production geometry inspection leaves top-level fixtures hidden");
    settings.sync();
    check(settings.value(key).toByteArray() == saved,
        "production geometry construction does not rewrite the saved application geometry bytes");
    std::cerr << "template-geometry stage: " << name << " editor destruction begin" << std::endl;
    editor.reset();
    std::cerr << "template-geometry stage: " << name << " editor destruction complete" << std::endl;
    settings.sync();
    check(settings.value(key).toByteArray() == saved,
        "hidden production dialog destruction does not rewrite the saved application geometry bytes");
    std::cerr << "template-geometry stage: " << name << " owner scope exit" << std::endl;
}
}

void runTemplateGeometryRestoreTests()
{
    std::cerr << "template-geometry stage: entry reached after QApplication startup" << std::endl;
    const bool offscreen = QGuiApplication::platformName() == QStringLiteral("offscreen");
    check(offscreen, "production template geometry diagnostics require the offscreen platform");
    if ( !offscreen )
    {
        return;
    }
    const bool isolated = appSettingsIniPathIsOverriddenForTest();
    check(isolated, "production geometry diagnostics require the main-window runner's isolated settings");
    if ( !isolated )
    {
        return;
    }
    const QScreen* const screen = QGuiApplication::primaryScreen();
    check(screen != nullptr, "production geometry diagnostics have an offscreen screen");
    if ( !screen )
    {
        return;
    }
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("geometry.ini"));
    settingsStore().sync();
    const bool copied = directory.isValid()
        && QFile::copy(QString::fromStdWString(appSettingsIniPath().wstring()), path);
    check(copied, "production geometry diagnostics copy the isolated runner baseline into a private INI");
    if ( !copied )
    {
        return;
    }
    std::cerr << "template-geometry stage: isolated settings copy complete" << std::endl;
    GeometrySettingsOverride settingsGuard(std::filesystem::path(path.toStdWString()));
    HiddenGeometryGlGuard glGuard;
    const QRect available = screen->availableGeometry();
    std::cout << "template-geometry Qt=" << qVersion() << " available=";
    printGeometryRect(available);
    std::cout << " dpr=" << screen->devicePixelRatio() << std::endl;

    // Query the hidden production widget's effective minimum without creating a
    // native handle, showing a top-level window, or pumping events.
    MainWindow probeOwner(nullptr, true);
    probeOwner.setAttribute(Qt::WA_DontShowOnScreen);
    std::unique_ptr<QDialog> probe(probeOwner.createTemplateEditorDialogForTest({}));
    check(probe != nullptr, "geometry planner has a hidden production minimum probe");
    if ( !probe )
    {
        return;
    }
    QSize effectiveMinimum = probe->minimumSize();
    if ( probe->layout() )
    {
        probe->layout()->activate();
        effectiveMinimum = effectiveMinimum.expandedTo(probe->layout()->minimumSize());
    }
    if ( probe->minimumSizeHint().isValid() )
    {
        effectiveMinimum = effectiveMinimum.expandedTo(probe->minimumSizeHint());
    }
    const QMargins margins = probe->windowHandle() ? probe->windowHandle()->frameMargins() : QMargins{};
    const auto pair = fittingDialogGeometries(available, effectiveMinimum, margins);
    check(pair.has_value(), "offscreen screen fits distinct production geometry fixtures with effective minima");
    if ( pair )
    {
        checkProductionGeometry("planned-application", pair->application, true);
        checkProductionGeometry("planned-workspace", pair->workspace, true);
        check(available.adjusted(2, 2, -2, -2).contains(pair->application.marginsAdded(margins))
                && available.adjusted(2, 2, -2, -2).contains(pair->workspace.marginsAdded(margins)),
            "planned offscreen frames retain the required screen inset");
    }
    check(!probeOwner.isVisible() && !probe->isVisible(), "geometry planner leaves production probes hidden");
    checkProductionGeometry("original-application", QRect(72, 64, 1280, 740), false);
    checkProductionGeometry("original-workspace", QRect(110, 96, 1390, 780), false);
    check(QApplication::activeWindow() == nullptr, "production geometry diagnostics never activate a window");
    std::cerr << "template-geometry stage: all production controls complete" << std::endl;
}
}
