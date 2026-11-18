#pragma once

#include <atomic>
#include <chrono>
#include <thread>

namespace sed::ui::diagnostics
{
bool admitInstallationOwner(std::chrono::milliseconds timeout);
// Starts only after admission. Controller EOF retires this exact diagnostic
// process even if its GUI/driver thread is blocked; it never signals another PID.
class InstallationOwnerWatch
{
public:
    InstallationOwnerWatch();
    ~InstallationOwnerWatch();
private:
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};
}
