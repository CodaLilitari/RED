#pragma once

#include <QTimer>

#include <utility>

namespace sed::app_tests
{

// A test callback may borrow stack state only while this owner remains alive.
// Destroying the owner cancels pending delivery, including after a wait times out.
class ScopedSingleShot
{
public:
    template <typename Callback>
    ScopedSingleShot(const int intervalMs, Callback&& callback)
    {
        timer_.setSingleShot(true);
        // Preserve singleShot's precise timing for the short debounce wait.
        timer_.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer_, &QTimer::timeout, &timer_,
            std::forward<Callback>(callback));
        timer_.start(intervalMs);
    }

    ScopedSingleShot(const ScopedSingleShot&) = delete;
    ScopedSingleShot& operator=(const ScopedSingleShot&) = delete;
    ScopedSingleShot(ScopedSingleShot&&) = delete;
    ScopedSingleShot& operator=(ScopedSingleShot&&) = delete;

private:
    QTimer timer_{};
};

} // namespace sed::app_tests
