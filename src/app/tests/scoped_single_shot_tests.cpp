#include "scoped_single_shot.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

#include <iostream>
#include <memory>

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

template <typename Predicate>
bool processEventsUntil(Predicate predicate, const int timeoutMs)
{
    QElapsedTimer deadline{};
    deadline.start();
    while ( !predicate() && deadline.elapsed() < timeoutMs )
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return predicate();
}

void processLaterEvents()
{
    QEventLoop laterEvents{};
    QTimer::singleShot(20, &laterEvents, &QEventLoop::quit);
    laterEvents.exec();
    QCoreApplication::processEvents(QEventLoop::AllEvents);
}

void checkSuccessfulDelivery(const int intervalMs)
{
    auto calls = std::make_shared<int>(0);
    {
        sed::app_tests::ScopedSingleShot timer(intervalMs, [calls]() { ++*calls; });
        check(processEventsUntil([&]() { return *calls != 0; }, 1000),
            "live scoped timer delivers its callback");
        check(*calls == 1, "live scoped timer delivers exactly once");
        processLaterEvents();
        check(*calls == 1, "completed scoped timer does not repeat");
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    check(*calls == 1, "successful scope exit does not deliver another callback");
}

void checkTimeoutCancellation(const int intervalMs)
{
    // Keep the observed storage alive even on the broken path. The regression
    // detects late delivery without itself dereferencing an expired stack local.
    auto calls = std::make_shared<int>(0);
    std::weak_ptr<int> capturedLifetime;
    const bool completed = [&]()
    {
        auto lifetime = std::make_shared<int>(0);
        capturedLifetime = lifetime;
        sed::app_tests::ScopedSingleShot timer(intervalMs,
            [calls, lifetime]() { ++*calls; ++*lifetime; });
        // A zero wait budget deterministically exits before event dispatch,
        // reproducing the early-return edge without relying on machine load.
        return processEventsUntil([&]() { return *calls != 0; }, 0);
    }();
    check(!completed, "exhausted wait budget reports timeout");
    check(*calls == 0, "timeout returns before callback delivery");
    check(capturedLifetime.expired(), "scope exit releases the canceled callback captures");

    // Process real later events, including a positive-delay control timer. No
    // native window, input, settings, or application singleton is involved.
    processLaterEvents();
    check(*calls == 0, "late event processing cannot deliver a canceled callback");
    check(capturedLifetime.expired(), "late event processing retains no canceled captures");
}

void checkExceptionCancellation(const int intervalMs)
{
    struct ExitScope {};
    auto calls = std::make_shared<int>(0);
    std::weak_ptr<int> capturedLifetime;
    bool caught = false;
    try
    {
        auto lifetime = std::make_shared<int>(0);
        capturedLifetime = lifetime;
        sed::app_tests::ScopedSingleShot timer(intervalMs,
            [calls, lifetime]() { ++*calls; ++*lifetime; });
        // Throw only from the caller, never through Qt's event dispatch.
        throw ExitScope{};
    }
    catch ( const ExitScope& )
    {
        caught = true;
    }
    check(caught, "caller exception unwinds the timer scope");
    check(*calls == 0, "unwinding occurs before callback delivery");
    check(capturedLifetime.expired(), "unwinding releases the canceled callback captures");
    processLaterEvents();
    check(*calls == 0, "late event processing cannot deliver an unwound callback");
    check(capturedLifetime.expired(), "late event processing retains no unwound captures");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    checkSuccessfulDelivery(0);
    checkSuccessfulDelivery(1);
    checkTimeoutCancellation(0);
    checkTimeoutCancellation(1);
    checkExceptionCancellation(0);
    checkExceptionCancellation(1);
    if ( failures == 0 )
    {
        std::cout << "Scoped single-shot lifetime tests passed.\n";
    }
    return failures == 0 ? 0 : 1;
}
