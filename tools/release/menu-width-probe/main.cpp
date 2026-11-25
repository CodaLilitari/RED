#include <QAction>
#include <QApplication>
#include <QColor>
#include <QColorDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSettings>
#include <QStyle>
#include <QStyleOptionMenuItem>
#include <QStyleOptionComboBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QWheelEvent>
#include <QWidgetAction>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <utility>
#include <vector>


#include "ui/shortcuts/key_sequence_display.hpp"
namespace sed::app_tests {
int failures=0;
void check(bool passed,const char* label){std::fprintf(stderr,"%s: %s\n",passed?"PASS":"FAIL",label); if (!passed)++failures;}
void showMenuForDisplayTest(QMenu& menu, const bool naturalWidth = false)
{
    menu.setStyleSheet(
        "QMenu { background: #181a20; color: #e7e9ee; border: 1px solid #343b46; }"
        "QMenu::item { padding: 7px 18px 7px 12px; }"
        "QMenu::item:selected { background: #313945; }");
    menu.adjustSize();
    if ( !naturalWidth )
    {
        menu.setFixedWidth(300);
    }
    menu.ensurePolished();
    menu.popup(QPoint(80, 80));
    QApplication::processEvents(QEventLoop::AllEvents);
}

class NativeShortcutReferenceMenu final : public QMenu
{
public:
    explicit NativeShortcutReferenceMenu(QString formattedSuffix)
        : formattedSuffix_(formattedSuffix)
    {
    }

protected:
    void initStyleOption(QStyleOptionMenuItem* const option,
        const QAction* const action) const override
    {
        QMenu::initStyleOption(option, action);
        const int tab = option->text.indexOf(QLatin1Char('\t'));
        if ( tab >= 0 )
        {
            // Retain the raw caption used to compute QMenu's native shortcut
            // column, while providing the expected painted glyphs.
            option->text = option->text.left(tab) + QLatin1Char('\t') + formattedSuffix_;
            option->reservedShortcutWidth = std::max(
                option->reservedShortcutWidth,
                option->fontMetrics.horizontalAdvance(formattedSuffix_));
        }
    }

private:
    QString formattedSuffix_;
};

class DynamicMenuPanelWidget final : public QWidget
{
public:
    explicit DynamicMenuPanelWidget(const QSize initialSize, QWidget* const parent = nullptr)
        : QWidget(parent), sizeHint_(initialSize)
    {
    }

    QSize sizeHint() const override
    {
        return sizeHint_;
    }

    void setReportedSizeHint(const QSize size)
    {
        sizeHint_ = size;
        updateGeometry();
    }

private:
    QSize sizeHint_{};
};

QImage captureShownMenuForDisplayTest(QMenu& menu)
{
    const QPixmap captured = menu.grab();
    if ( captured.isNull() )
    {
        return {};
    }
    return captured.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

class ScopedMenuAnimationDisable final
{
public:
    ScopedMenuAnimationDisable()
        : fadeEnabled_(QApplication::isEffectEnabled(Qt::UI_FadeMenu)),
          animateEnabled_(QApplication::isEffectEnabled(Qt::UI_AnimateMenu))
    {
        QApplication::setEffectEnabled(Qt::UI_FadeMenu, false);
        QApplication::setEffectEnabled(Qt::UI_AnimateMenu, false);
    }

    ~ScopedMenuAnimationDisable()
    {
        QApplication::setEffectEnabled(Qt::UI_FadeMenu, fadeEnabled_);
        QApplication::setEffectEnabled(Qt::UI_AnimateMenu, animateEnabled_);
    }

    ScopedMenuAnimationDisable(const ScopedMenuAnimationDisable&) = delete;
    ScopedMenuAnimationDisable& operator=(const ScopedMenuAnimationDisable&) = delete;

private:
    bool fadeEnabled_{};
    bool animateEnabled_{};
};

QRect imageRectForLogicalRect(const QImage& image, const QRect& logicalRect)
{
    const qreal devicePixelRatio = image.devicePixelRatio();
    const int left = qRound(logicalRect.left() * devicePixelRatio);
    const int top = qRound(logicalRect.top() * devicePixelRatio);
    const int right = qRound((logicalRect.x() + logicalRect.width()) * devicePixelRatio);
    const int bottom = qRound((logicalRect.y() + logicalRect.height()) * devicePixelRatio);
    return QRect(left, top, std::max(0, right - left), std::max(0, bottom - top));
}

QRect shortcutGlyphBounds(const QImage& image, const QRect& actionRect)
{
    const QRect row = imageRectForLogicalRect(image, actionRect).intersected(image.rect());
    const int firstColumn = row.left() + row.width() / 2;
    QRect bounds{};
    for ( int y = row.top() + 2; y < row.bottom() - 1; ++y )
    {
        for ( int x = firstColumn; x < row.right() - 2; ++x )
        {
            if ( qGray(image.pixel(x, y)) > 160 )
            {
                const QRect pixelRect(x, y, 1, 1);
                bounds = bounds.isEmpty() ? pixelRect : bounds.united(pixelRect);
            }
        }
    }
    return bounds;
}

QImage shortcutGlyphCrop(const QImage& image, const QRect& actionRect)
{
    const QRect bounds = shortcutGlyphBounds(image, actionRect);
    return bounds.isEmpty() ? QImage{} : image.copy(bounds);
}

QImage renderMenuForDisplayTest(QMenu& menu, const bool naturalWidth = false)
{
    showMenuForDisplayTest(menu, naturalWidth);
    const QImage image = captureShownMenuForDisplayTest(menu);
    menu.hide();
    QApplication::processEvents(QEventLoop::AllEvents);
    return image;
}

void testShortcutMenuNaturalWidthTracksFormattedColumn()
{
    const ScopedMenuAnimationDisable disableMenuAnimations{};
    const QKeySequence longSequence(int(Qt::KeypadModifier) | Qt::Key_F12);
    const QKeySequence shortSequence(Qt::Key_F1);
    ui::shortcuts::KeySequenceDisplayMenu menu{};
    QAction* const action = menu.addAction(QStringLiteral("Width probe"));
    action->setShortcut(longSequence);
    action->setShortcutVisibleInContextMenu(true);
    const QFontMetrics widthProbeFont(menu.font());
    const int longNativeAdvance = widthProbeFont.horizontalAdvance(
        longSequence.toString(QKeySequence::NativeText));
    const int longFormattedAdvance = widthProbeFont.horizontalAdvance(
        ui::shortcuts::keySequenceDisplayText(longSequence));
    const bool longSequenceHasPositiveWidthDelta =
        longFormattedAdvance > longNativeAdvance;

    showMenuForDisplayTest(menu, true);
    const QSize firstPopupSize = menu.size();
    const int firstMinimumWidth = menu.minimumWidth();
    menu.hide();
    QApplication::processEvents(QEventLoop::AllEvents);
    showMenuForDisplayTest(menu, true);
    const QSize repeatedPopupSize = menu.size();
    const int repeatedMinimumWidth = menu.minimumWidth();
    menu.hide();
    QApplication::processEvents(QEventLoop::AllEvents);

    const QSize longHint = menu.sizeHint();
    const int longMinimumWidth = menu.minimumWidth();
    const QSize repeatedLongHint = menu.sizeHint();
    const int repeatedLongMinimumWidth = menu.minimumWidth();
    action->setShortcut(shortSequence);
    const QSize shortHint = menu.sizeHint();
    const int shortMinimumWidth = menu.minimumWidth();

    ui::shortcuts::KeySequenceDisplayMenu visibleOnly{};
    QAction* const visibleAction = visibleOnly.addAction(QStringLiteral("Visible"));
    visibleAction->setShortcut(longSequence);
    visibleAction->setShortcutVisibleInContextMenu(true);
    const int visibleOnlyWidth = visibleOnly.sizeHint().width();
    ui::shortcuts::KeySequenceDisplayMenu withInvisible{};
    QAction* const sameVisibleAction = withInvisible.addAction(QStringLiteral("Visible"));
    sameVisibleAction->setShortcut(longSequence);
    sameVisibleAction->setShortcutVisibleInContextMenu(true);
    QAction* const hiddenAction = withInvisible.addAction(QStringLiteral("Hidden"));
    hiddenAction->setShortcut(QKeySequence(
        Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::Key_F24));
    hiddenAction->setShortcutVisibleInContextMenu(true);
    hiddenAction->setVisible(false);
    const int withInvisibleWidth = withInvisible.sizeHint().width();

    ui::shortcuts::KeySequenceDisplayMenu fixedWidth{};
    QAction* const fixedAction = fixedWidth.addAction(QStringLiteral("Fixed"));
    fixedAction->setShortcut(longSequence);
    fixedAction->setShortcutVisibleInContextMenu(true);
    fixedWidth.setFixedWidth(321);
    (void)fixedWidth.sizeHint();

    ui::shortcuts::KeySequenceDisplayMenu initialCallerMinimum{};
    initialCallerMinimum.setMinimumWidth(350);
    QAction* const initialMinimumAction =
        initialCallerMinimum.addAction(QStringLiteral("Initial minimum"));
    initialMinimumAction->setShortcut(longSequence);
    initialMinimumAction->setShortcutVisibleInContextMenu(true);
    const QSize initialCallerMinimumHint = initialCallerMinimum.sizeHint();

    ui::shortcuts::KeySequenceDisplayMenu callerMinimum{};
    QAction* const minimumAction = callerMinimum.addAction(QStringLiteral("Minimum"));
    minimumAction->setShortcut(longSequence);
    minimumAction->setShortcutVisibleInContextMenu(true);
    callerMinimum.setMinimumWidth(350);
    (void)callerMinimum.sizeHint();
    const int callerMinimumAfterLong = callerMinimum.minimumWidth();
    minimumAction->setShortcut(shortSequence);
    (void)callerMinimum.sizeHint();

    ui::shortcuts::KeySequenceDisplayMenu loweredCallerMinimum{};
    QAction* const loweredMinimumAction =
        loweredCallerMinimum.addAction(QStringLiteral("Lowered minimum"));
    loweredMinimumAction->setShortcut(longSequence);
    loweredMinimumAction->setShortcutVisibleInContextMenu(true);
    loweredCallerMinimum.setMinimumWidth(350);
    showMenuForDisplayTest(loweredCallerMinimum, true);
    const int originalCallerFloor = loweredCallerMinimum.minimumWidth();
    loweredCallerMinimum.setMinimumWidth(200);
    const QSize loweredCallerFloorHint = loweredCallerMinimum.sizeHint();
    const int loweredCallerFloorValue = loweredCallerMinimum.minimumWidth();
    loweredCallerMinimum.hide();
    QApplication::processEvents(QEventLoop::AllEvents);

    ui::shortcuts::KeySequenceDisplayMenu maximumBound{};
    QAction* const maximumAction = maximumBound.addAction(QStringLiteral("Maximum"));
    maximumAction->setShortcut(longSequence);
    maximumAction->setShortcutVisibleInContextMenu(true);
    maximumBound.setMaximumWidth(150);
    const QSize maximumBoundHint = maximumBound.sizeHint();

    // Digits 0 and 3 have matching advances in the Cocoa system font too.
    const QKeySequence originalSameWidth(int(Qt::KeypadModifier) | Qt::Key_0);
    const QKeySequence updatedSameWidth(int(Qt::KeypadModifier) | Qt::Key_3);
    QMenu nativeSameSizeWidthReference{};
    QAction* const nativeSameSizeWidthAction = nativeSameSizeWidthReference.addAction(
        QStringLiteral("Width probe\t")
            + originalSameWidth.toString(QKeySequence::NativeText));
    nativeSameSizeWidthAction->setShortcutVisibleInContextMenu(true);
    const int nativeSameSizeWidth = nativeSameSizeWidthReference.sizeHint().width();
    ui::shortcuts::KeySequenceDisplayMenu sameSizeMenu{};
    QAction* const sameSizeAction = sameSizeMenu.addAction(QStringLiteral("Width probe"));
    sameSizeAction->setShortcut(originalSameWidth);
    sameSizeAction->setShortcutVisibleInContextMenu(true);
    const QFontMetrics sameSizeMenuFont(sameSizeMenu.font());
    const int originalNativeAdvance = sameSizeMenuFont.horizontalAdvance(
        originalSameWidth.toString(QKeySequence::NativeText));
    const int updatedNativeAdvance = sameSizeMenuFont.horizontalAdvance(
        updatedSameWidth.toString(QKeySequence::NativeText));
    const int originalFormattedAdvance = sameSizeMenuFont.horizontalAdvance(
        ui::shortcuts::keySequenceDisplayText(originalSameWidth));
    const int updatedFormattedAdvance = sameSizeMenuFont.horizontalAdvance(
        ui::shortcuts::keySequenceDisplayText(updatedSameWidth));
    const bool sameWidthSequenceAdvances =
        originalNativeAdvance == updatedNativeAdvance
        && originalFormattedAdvance == updatedFormattedAdvance
        && originalFormattedAdvance > originalNativeAdvance;
    showMenuForDisplayTest(sameSizeMenu, true);
    const QSize originalSameWidthMenuSize = sameSizeMenu.size();
    const int originalSameWidthMinimum = sameSizeMenu.minimumWidth();
    const QRect originalSameWidthActionRect = sameSizeMenu.actionGeometry(sameSizeAction);
    const QImage originalSameWidthRaster = captureShownMenuForDisplayTest(sameSizeMenu);
    sameSizeMenu.hide();
    QApplication::processEvents(QEventLoop::AllEvents);
    sameSizeAction->setShortcut(updatedSameWidth);
    showMenuForDisplayTest(sameSizeMenu, true);
    const QSize updatedSameWidthMenuSize = sameSizeMenu.size();
    const int updatedSameWidthMinimum = sameSizeMenu.minimumWidth();
    const QRect updatedSameWidthActionRect = sameSizeMenu.actionGeometry(sameSizeAction);
    const QImage updatedSameWidthRaster = captureShownMenuForDisplayTest(sameSizeMenu);
    sameSizeMenu.hide();
    QApplication::processEvents(QEventLoop::AllEvents);
    NativeShortcutReferenceMenu sameSizeReference(QStringLiteral("Numpad 3"));
    QAction* const sameSizeReferenceAction = sameSizeReference.addAction(
        QStringLiteral("Width probe\t")
            + updatedSameWidth.toString(QKeySequence::NativeText));
    sameSizeReferenceAction->setShortcutVisibleInContextMenu(true);
    sameSizeReference.setFixedWidth(updatedSameWidthMenuSize.width());
    const QImage sameSizeReferenceRaster = renderMenuForDisplayTest(
        sameSizeReference, true);
    const QRect sameSizeReferenceActionRect =
        sameSizeReference.actionGeometry(sameSizeReferenceAction);
    const QImage updatedSameWidthGlyphs = shortcutGlyphCrop(
        updatedSameWidthRaster, updatedSameWidthActionRect);
    const QImage expectedSameWidthGlyphs = shortcutGlyphCrop(
        sameSizeReferenceRaster, sameSizeReferenceActionRect);
    const QImage originalSameWidthGlyphs = shortcutGlyphCrop(
        originalSameWidthRaster, originalSameWidthActionRect);
    const bool sameWidthRebindLayoutValid =
        sameWidthSequenceAdvances
        && originalSameWidthMenuSize.width() == updatedSameWidthMenuSize.width()
        && originalSameWidthMinimum > nativeSameSizeWidth
        && updatedSameWidthMinimum > nativeSameSizeWidth
        && originalSameWidthActionRect.width() == updatedSameWidthActionRect.width()
        && updatedSameWidthActionRect == sameSizeReferenceActionRect
        && !originalSameWidthGlyphs.isNull()
        && !updatedSameWidthGlyphs.isNull()
        && originalSameWidthGlyphs != updatedSameWidthGlyphs
        && updatedSameWidthGlyphs == expectedSameWidthGlyphs;

    if ( !sameWidthRebindLayoutValid )
    {
        std::fprintf(stderr,
            "same-width menu diagnostic: advances=%d native=%d/%d formatted=%d/%d "
            "minimum=%d/%d nativeHint=%d menuWidth=%d/%d rect=%d,%d,%d,%d "
            "referenceRect=%d,%d,%d,%d glyphNull=%d/%d/%d glyphChanged=%d glyphMatches=%d\n",
            sameWidthSequenceAdvances, originalNativeAdvance, updatedNativeAdvance,
            originalFormattedAdvance, updatedFormattedAdvance,
            originalSameWidthMinimum, updatedSameWidthMinimum, nativeSameSizeWidth,
            originalSameWidthMenuSize.width(), updatedSameWidthMenuSize.width(),
            updatedSameWidthActionRect.x(), updatedSameWidthActionRect.y(),
            updatedSameWidthActionRect.width(), updatedSameWidthActionRect.height(),
            sameSizeReferenceActionRect.x(), sameSizeReferenceActionRect.y(),
            sameSizeReferenceActionRect.width(), sameSizeReferenceActionRect.height(),
            originalSameWidthGlyphs.isNull(), updatedSameWidthGlyphs.isNull(),
            expectedSameWidthGlyphs.isNull(),
            originalSameWidthGlyphs != updatedSameWidthGlyphs,
            updatedSameWidthGlyphs == expectedSameWidthGlyphs);
    }

    ui::shortcuts::KeySequenceDisplayMenu dynamicWidgetMenu{};
    auto* const dynamicWidgetAction = new QWidgetAction(&dynamicWidgetMenu);
    auto* const dynamicPanel = new DynamicMenuPanelWidget(QSize(150, 24));
    dynamicWidgetAction->setDefaultWidget(dynamicPanel);
    dynamicWidgetMenu.addAction(dynamicWidgetAction);
    const QSize compactWidgetHint = dynamicWidgetMenu.sizeHint();
    const int dynamicWidgetMinimumWidth = dynamicWidgetMenu.minimumWidth();
    dynamicPanel->setReportedSizeHint(QSize(150, 72));
    // QWidgetAction panels in the app invalidate QMenu's cached row size by
    // removing and reinserting the same action after their content changes.
    dynamicWidgetMenu.removeAction(dynamicWidgetAction);
    dynamicWidgetMenu.addAction(dynamicWidgetAction);
    const QSize expandedWidgetHint = dynamicWidgetMenu.sizeHint();

    check(firstPopupSize == repeatedPopupSize
            && firstMinimumWidth == repeatedMinimumWidth
            && longHint == repeatedLongHint
            && longMinimumWidth == repeatedLongMinimumWidth,
        "repeated menu sizing and popup cycles keep the formatted minimum width stable");
    check(shortHint.width() < longHint.width()
            && shortMinimumWidth < longMinimumWidth,
        "shorter shortcut bindings release the menu's previously applied width contribution");
    check(longSequenceHasPositiveWidthDelta,
        "menu-width boundary fixture has a positive formatted shortcut-width delta");
    check(visibleOnlyWidth == withInvisibleWidth,
        "hidden menu actions do not enlarge the formatted shortcut column");
    check(fixedWidth.minimumWidth() == 321 && fixedWidth.maximumWidth() == 321,
        "formatted shortcut sizing preserves a caller's fixed menu width");
    check(initialCallerMinimum.minimumWidth() == 350
            && initialCallerMinimumHint.width() >= 350,
        "formatted shortcut sizing preserves a caller minimum set before the first action");
    check(callerMinimumAfterLong >= 350 && callerMinimum.minimumWidth() == 350,
        "formatted shortcut sizing preserves a caller's minimum width when bindings shrink");
    check(loweredCallerFloorHint.width() < originalCallerFloor
            && loweredCallerFloorValue < originalCallerFloor,
        "a caller can lower a prior menu minimum while the popup is open");
    check(maximumBound.maximumWidth() == 150
            && maximumBound.minimumWidth() <= 150
            && maximumBoundHint.width() <= 150,
        "formatted shortcut sizing preserves a caller's maximum width");
    check(sameWidthRebindLayoutValid,
        "same-width Numpad rebind keeps the formatted column and rebuilds menu action geometry");
    check(dynamicWidgetMinimumWidth == 0
            && expandedWidgetHint.height() > compactWidgetHint.height(),
        "menus without formatted shortcuts bypass width management and re-read dynamic widget-action height");
}

}
int main(int argc,char** argv){QApplication a(argc,argv);sed::app_tests::testShortcutMenuNaturalWidthTracksFormattedColumn();return sed::app_tests::failures!=0;}
