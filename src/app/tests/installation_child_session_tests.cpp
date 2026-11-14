#include "installation_child_session.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <iostream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace
{
int failures{};
void check(const bool passed, const char* message)
{
    if ( !passed )
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
int childStatus(const bool alreadySessionLeader)
{
    const pid_t child = ::fork();
    if ( child < 0 )
    {
        return -1;
    }
    if ( child == 0 )
    {
        ::alarm(2);
        if ( alreadySessionLeader )
        {
            if ( ::setsid() < 0 )
            {
                ::_exit(3);
            }
            // A process-group leader cannot establish another session. This
            // creates a real setsid failure without syscall interposition.
            ::_exit(sed::ui::diagnostics::detail::prepareInstallationChildSession() ? 1 : 0);
        }
        struct sigaction ignored{};
        ignored.sa_handler = SIG_IGN;
        if ( sigemptyset(&ignored.sa_mask) != 0 || ::sigaction(SIGUSR1, &ignored, nullptr) != 0 )
        {
            ::_exit(3);
        }
        sigset_t blocked{};
        if ( sigemptyset(&blocked) != 0 || sigaddset(&blocked, SIGUSR1) != 0
            || ::sigprocmask(SIG_BLOCK, &blocked, nullptr) != 0 )
        {
            ::_exit(3);
        }
        if ( !sed::ui::diagnostics::detail::prepareInstallationChildSession() )
        {
            ::_exit(2);
        }
        struct sigaction after{};
        sigset_t mask{};
        const bool owned = ::getsid(0) == ::getpid() && ::getpgrp() == ::getpid();
        const bool restored = ::sigaction(SIGUSR1, nullptr, &after) == 0 && after.sa_handler == SIG_DFL
            && ::sigprocmask(SIG_SETMASK, nullptr, &mask) == 0 && sigismember(&mask, SIGUSR1) == 0;
        ::_exit(owned && restored ? 0 : 1);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    int status{};
    while ( std::chrono::steady_clock::now() < deadline )
    {
        const pid_t result = ::waitpid(child, &status, WNOHANG);
        if ( result == child )
        {
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
        if ( result < 0 && errno != EINTR )
        {
            return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ::kill(child, SIGKILL);
    while ( ::waitpid(child, &status, 0) < 0 && errno == EINTR )
    {
    }
    return -1;
}
}

int main()
{
    check(childStatus(false) == 0, "forked child owns its new session/group and restores ignored/blocked signals");
    check(childStatus(true) == 0, "real setsid failure refuses before the executable can be admitted");
    std::cout << "installation child session failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
