#include "fitting_dialog_geometry.hpp"

#include <iostream>
#include <limits>

namespace
{

int failures = 0;

void check(const bool ok, const char* message)
{
    if ( !ok )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void checkFittingPair(const QRect& available, const QSize& minimum,
    const QMargins& margins, const QRect& expectedApplication,
    const QRect& expectedWorkspace)
{
    const auto pair = sed::app_tests::fittingDialogGeometries(available, minimum, margins);
    check(pair.has_value(), "screen admitting minimum, frames, and slack produces both geometries");
    if ( !pair )
    {
        return;
    }
    check(pair->application == expectedApplication, "application client geometry matches the exact vector");
    check(pair->workspace == expectedWorkspace, "workspace client geometry matches the exact vector");
    check(pair->application.size() != pair->workspace.size()
            && pair->application.topLeft() != pair->workspace.topLeft(),
        "fallback and workspace have independently distinguishable size and position");
    for ( const QRect& client : { pair->application, pair->workspace } )
    {
        check(client.width() >= minimum.width() && client.height() >= minimum.height(),
            "both client sizes respect the effective dialog minimum");
        check(available.adjusted(2, 2, -2, -2).contains(client.marginsAdded(margins)),
            "each native frame stays inside its selected screen with a two-pixel inset");
    }
}

void checkRejected(const QRect& available, const QSize& minimum, const QMargins& margins)
{
    const QRect savedAvailable = available;
    const QSize savedMinimum = minimum;
    const QMargins savedMargins = margins;
    const auto rejected = sed::app_tests::fittingDialogGeometries(available, minimum, margins);
    check(!rejected.has_value(), "rejection publishes neither application nor workspace geometry");
    check(available == savedAvailable && minimum == savedMinimum && margins == savedMargins,
        "rejection leaves all caller inputs unchanged");
}

} // namespace

int main()
{
    // Regression: 4096x2160 at 3x DPR exposes about 1365x720 logical pixels;
    // its taskbar leaves 692 available. Do not multiply these Qt coordinates by DPR.
    checkFittingPair(QRect(0, 0, 1365, 692), QSize(1120, 640), QMargins(8, 30, 8, 8),
        QRect(10, 32, 1120, 640), QRect(14, 36, 1124, 644));
    checkFittingPair(QRect(0, 0, 1920, 1040), QSize(1120, 640), QMargins(8, 30, 8, 8),
        QRect(10, 32, 1120, 640), QRect(14, 36, 1124, 644));

    // Each call selects exactly one monitor, not a virtual desktop union. Include
    // left/above-primary and positive origins, plus asymmetric frame extents.
    checkFittingPair(QRect(-1600, -900, 1365, 692), QSize(1120, 640), QMargins(5, 27, 11, 9),
        QRect(-1593, -871, 1120, 640), QRect(-1589, -867, 1124, 644));
    checkFittingPair(QRect(1920, 48, 1280, 720), QSize(1120, 640), QMargins(5, 27, 11, 9),
        QRect(1927, 77, 1120, 640), QRect(1931, 81, 1124, 644));
    checkRejected(QRect(1920, 0, 1000, 700), QSize(1120, 640), QMargins{});

    // Exact boundary and one-pixel failures on each axis. A minimum-only fit
    // cannot silently collapse two fixtures to the same geometry.
    checkFittingPair(QRect(20, 40, 1148, 690), QSize(1120, 640), QMargins(8, 30, 8, 8),
        QRect(30, 72, 1120, 640), QRect(34, 76, 1124, 644));
    checkRejected(QRect(20, 40, 1147, 690), QSize(1120, 640), QMargins(8, 30, 8, 8));
    checkRejected(QRect(20, 40, 1148, 689), QSize(1120, 640), QMargins(8, 30, 8, 8));
    checkRejected(QRect(0, 0, 1136, 678), QSize(1120, 640), QMargins(8, 30, 8, 8));
    checkFittingPair(QRect(-10, 7, 1212, 712), QSize(1200, 700), QMargins{},
        QRect(-8, 9, 1200, 700), QRect(-4, 13, 1204, 704));

    checkRejected(QRect{}, QSize(1120, 640), QMargins{});
    checkRejected(QRect(0, 0, -1, 692), QSize(1120, 640), QMargins{});
    checkRejected(QRect(0, 0, 1365, 692), QSize{}, QMargins{});
    checkRejected(QRect(0, 0, 1365, 692), QSize(0, 640), QMargins{});
    checkRejected(QRect(0, 0, 1365, 692), QSize(1120, 640), QMargins(-1, 0, 0, 0));
    checkRejected(QRect(0, 0, 1365, 692), QSize(1120, 640), QMargins(0, -1, 0, 0));
    checkRejected(QRect(0, 0, 1365, 692), QSize(1120, 640), QMargins(0, 0, -1, 0));
    checkRejected(QRect(0, 0, 1365, 692), QSize(1120, 640), QMargins(0, 0, 0, -1));
    checkRejected(QRect(0, 0, 1365, 692), QSize(std::numeric_limits<int>::max(), 640), QMargins{});
    checkRejected(QRect(0, 0, 1365, 692), QSize(1120, 640),
        QMargins(std::numeric_limits<int>::max(), 0, std::numeric_limits<int>::max(), 0));
    checkRejected(QRect(0, 0, 1365, 692), QSize(1120, std::numeric_limits<int>::max()), QMargins{});
    checkRejected(QRect(0, 0, 1365, 692), QSize(1120, 640),
        QMargins(0, std::numeric_limits<int>::max(), 0, std::numeric_limits<int>::max()));
    const int largest = std::numeric_limits<int>::max();
    const int smallest = std::numeric_limits<int>::min();
    checkFittingPair(QRect(largest - 12, largest - 12, 13, 13), QSize(1, 1), QMargins{},
        QRect(largest - 10, largest - 10, 1, 1), QRect(largest - 6, largest - 6, 5, 5));
    checkFittingPair(QRect(smallest, smallest, 13, 13), QSize(1, 1), QMargins{},
        QRect(smallest + 2, smallest + 2, 1, 1), QRect(smallest + 6, smallest + 6, 5, 5));

    // A rejected call has no externally retained partial result or poisoned state.
    checkFittingPair(QRect(0, 0, 1365, 692), QSize(1120, 640), QMargins(8, 30, 8, 8),
        QRect(10, 32, 1120, 640), QRect(14, 36, 1124, 644));
    if ( failures == 0 )
    {
        std::cout << "Fitting dialog geometry tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
