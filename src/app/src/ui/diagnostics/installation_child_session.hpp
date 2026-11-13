#pragma once

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <unistd.h>

namespace sed::ui::diagnostics::detail
{
// Runs only in the forked child, before exec: no Qt, allocation, locks, or
// parent-state mutation. Callers must _exit on false and never admit the child.
inline bool prepareInstallationChildSession() noexcept
{
    if ( ::setsid() < 0 )
    {
        return false;
    }
    struct sigaction reset{};
    reset.sa_handler = SIG_DFL;
    if ( ::sigemptyset(&reset.sa_mask) != 0 )
    {
        return false;
    }
    for ( int signal = 1; signal < NSIG; ++signal )
    {
        if ( signal == SIGKILL || signal == SIGSTOP )
        {
            continue;
        }
        if ( ::sigaction(signal, &reset, nullptr) != 0 && errno != EINVAL )
        {
            return false;
        }
        // EINVAL identifies holes or libc-reserved signal numbers. Such
        // numbers cannot have application-installed handlers to reset.
    }
    sigset_t empty{};
    return ::sigemptyset(&empty) == 0 && ::sigprocmask(SIG_SETMASK, &empty, nullptr) == 0;
}
}
#endif
