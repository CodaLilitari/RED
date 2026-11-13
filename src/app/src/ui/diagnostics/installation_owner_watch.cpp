#include "installation_owner_watch.hpp"
#include <cstdlib>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace sed::ui::diagnostics
{
namespace
{
bool inputIsPipe()
{
#ifdef _WIN32
    return GetFileType(GetStdHandle(STD_INPUT_HANDLE)) == FILE_TYPE_PIPE;
#else
    struct stat state{};
    return ::fstat(STDIN_FILENO, &state) == 0 && S_ISFIFO(state.st_mode);
#endif
}

// 0: open and waiting, 1: bytes available, -1: owner closed or pipe failed.
int inputState()
{
#ifdef _WIN32
    DWORD available{};
    if ( !PeekNamedPipe(GetStdHandle(STD_INPUT_HANDLE), nullptr, 0, nullptr, &available, nullptr) )
    {
        return -1;
    }
    return available > 0 ? 1 : 0;
#else
    pollfd input{STDIN_FILENO, POLLIN, 0};
    const int result = ::poll(&input, 1, 0);
    if ( result < 0 )
    {
        return errno == EINTR ? 0 : -1;
    }
    if ( input.revents & POLLIN )
    {
        return 1;
    }
    return input.revents & (POLLHUP | POLLERR | POLLNVAL) ? -1 : 0;
#endif
}

bool readCharacter(char& character)
{
#ifdef _WIN32
    DWORD count{};
    return ReadFile(GetStdHandle(STD_INPUT_HANDLE), &character, 1, &count, nullptr) && count == 1;
#else
    return ::read(STDIN_FILENO, &character, 1) == 1;
#endif
}
}

bool admitInstallationOwner(const std::chrono::milliseconds timeout)
{
    if ( !inputIsPipe() || timeout.count() <= 0 || timeout > std::chrono::seconds(5) )
    {
        return false;
    }
    constexpr std::string_view expected = "start\n";
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::size_t received{};
    while ( std::chrono::steady_clock::now() < deadline )
    {
        const int state = inputState();
        if ( state < 0 )
        {
            return false;
        }
        if ( state > 0 )
        {
            char character{};
            if ( !readCharacter(character) || character != expected[received] )
            {
                return false;
            }
            if ( ++received == expected.size() )
            {
                return true;
            }
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    return false;
}

InstallationOwnerWatch::InstallationOwnerWatch()
    : thread_([](const std::stop_token stop)
    {
        while ( !stop.stop_requested() )
        {
            if ( inputState() != 0 )
            {
                // No Qt or driver calls from this watchdog. Only this exact
                // child exits; the parent writes the cancellation/failure receipt.
                std::_Exit(125);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    })
{
}
}
