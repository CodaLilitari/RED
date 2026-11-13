#include <QApplication>
#include <QByteArray>
#include <QDialog>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QSize>

#include <iostream>

// Standalone Qt control, not a replacement for the Template Editor native test.
// No application settings, MainWindow, event loop, OpenGL or native input.
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

void printRect(const QRect& rect)
{
    std::cout << rect.x() << ',' << rect.y() << ',' << rect.width() << ',' << rect.height();
}

QRect restoreControl(const char* name, const QRect& requested, const QSize& minimum)
{
    QDialog seed;
    seed.setAttribute(Qt::WA_DontShowOnScreen);
    seed.setGeometry(requested);
    const QByteArray bytes = seed.saveGeometry();

    QDialog restored;
    restored.setAttribute(Qt::WA_DontShowOnScreen);
    restored.setMinimumSize(minimum);
    const bool accepted = restored.restoreGeometry(bytes);
    std::cout << name << ": requested=";
    printRect(requested);
    std::cout << " seed=";
    printRect(seed.geometry());
    std::cout << " restored=";
    printRect(restored.geometry());
    std::cout << " minimum=" << minimum.width() << ',' << minimum.height()
              << " accepted=" << accepted << '\n';
    check(!bytes.isEmpty() && accepted, "a valid hidden QDialog geometry record is accepted");
    check(!seed.isVisible() && !restored.isVisible(), "geometry controls remain hidden");
    return restored.geometry();
}
}

int main()
{
    // Enforce the non-native platform before constructing QApplication. Do not
    // forward caller arguments that could override it with -platform windows.
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    int argc = 1;
    char name[] = "sed_app_dialog_geometry_restore_tests";
    char* argv[] = {name, nullptr};
    QApplication application(argc, argv);
    if ( QGuiApplication::platformName() != QStringLiteral("offscreen") )
    {
        std::cerr << "FAIL: geometry controls require the offscreen platform\n";
        return 1;
    }
    const QScreen* const screen = QGuiApplication::primaryScreen();
    if ( !screen || screen->availableGeometry().width() < 160
        || screen->availableGeometry().height() < 160 )
    {
        std::cerr << "FAIL: offscreen geometry fixture needs at least 160x160 logical pixels\n";
        return 1;
    }
    const QRect available = screen->availableGeometry();
    std::cout << "Qt=" << qVersion() << " platform=offscreen available=";
    printRect(available);
    std::cout << " dpr=" << screen->devicePixelRatio() << '\n';

    const QSize fittingSize(available.width() / 3, available.height() / 3);
    const QRect fitting(available.topLeft() + QPoint(32, 32), fittingSize);
    const QRect fitted = restoreControl("fitting", fitting, QSize(1, 1));
    check(fitted.size() == fitting.size(), "restore preserves a fitting saved client size");
    check(available.contains(fitted), "a fitting restored client rectangle remains on screen");

    const QRect oversized(available.topLeft() + QPoint(32, 32),
        QSize(available.width() + 400, available.height() + 300));
    const QRect clamped = restoreControl("oversized", oversized, QSize(1, 1));
    check(clamped.size() != oversized.size(), "restore does not preserve an oversized saved client size");
    check(available.contains(clamped), "restore fits an unconstrained oversized client rectangle to the screen");

    // Recorded test vectors, with the production constructor's minimum size.
    // Diagnostic only: the offscreen screen may be smaller than that minimum,
    // and these controls do not instantiate the production dialog or its layout.
    restoreControl("template-application-vector", QRect(72, 64, 1280, 740), QSize(1120, 640));
    restoreControl("template-workspace-vector", QRect(110, 96, 1390, 780), QSize(1120, 640));
    check(QApplication::activeWindow() == nullptr, "hidden controls do not activate a window");
    return failures == 0 ? 0 : 1;
}
