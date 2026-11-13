#pragma once

#include <QMargins>
#include <QRect>
#include <QSize>

#include <optional>
#include <limits>

namespace sed::app_tests
{

struct FittingDialogGeometries
{
    QRect application;
    QRect workspace;
};

// Test-only client rectangles on one selected screen, in logical pixels. Both
// native frames must retain a two-pixel inset from availableGeometry. Workspace
// size and position differ by four pixels on both axes, so the screen must fit
// the effective minimum, native frame margins, and twelve pixels of slack.
// Invalid input or insufficient space returns no pair; callers must fail their
// fixture precondition rather than skip persistence assertions.
inline std::optional<FittingDialogGeometries> fittingDialogGeometries(
    const QRect& availableGeometry, const QSize& effectiveMinimum,
    const QMargins& frameMargins)
{
    if ( !availableGeometry.isValid() || effectiveMinimum.width() <= 0
        || effectiveMinimum.height() <= 0 || frameMargins.left() < 0
        || frameMargins.top() < 0 || frameMargins.right() < 0 || frameMargins.bottom() < 0 )
    {
        return std::nullopt;
    }

    // Widen before adding so oversized minima/margins reject without overflow.
    const qint64 requiredWidth = qint64(effectiveMinimum.width())
        + frameMargins.left() + frameMargins.right() + 12;
    const qint64 requiredHeight = qint64(effectiveMinimum.height())
        + frameMargins.top() + frameMargins.bottom() + 12;
    const qint64 screenRight = qint64(availableGeometry.x()) + availableGeometry.width() - 1;
    const qint64 screenBottom = qint64(availableGeometry.y()) + availableGeometry.height() - 1;
    if ( requiredWidth > availableGeometry.width() || requiredHeight > availableGeometry.height()
        || screenRight > std::numeric_limits<int>::max()
        || screenBottom > std::numeric_limits<int>::max() )
    {
        return std::nullopt;
    }

    // Full frame containment above also proves the following int coordinates and
    // dimensions representable. There is no mutable output until both fit.
    const int x = static_cast<int>(qint64(availableGeometry.x()) + frameMargins.left() + 2);
    const int y = static_cast<int>(qint64(availableGeometry.y()) + frameMargins.top() + 2);
    return FittingDialogGeometries{
        QRect(QPoint(x, y), effectiveMinimum),
        QRect(x + 4, y + 4, effectiveMinimum.width() + 4, effectiveMinimum.height() + 4)
    };
}

} // namespace sed::app_tests
