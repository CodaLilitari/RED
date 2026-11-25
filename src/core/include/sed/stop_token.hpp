#pragma once

#include <version>

#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
#include <stop_token>

namespace sed
{
using StopSource = std::stop_source;
using StopToken = std::stop_token;
} // namespace sed
#else
#include <atomic>
#include <memory>

namespace sed
{
// Some supported Apple SDKs lack the standard stop types. Review workers only
// poll for cancellation, and each token keeps its own job's stop state alive.
class StopToken
{
public:
    StopToken() = default;

    [[nodiscard]] bool stop_requested() const noexcept
    {
        return state_ && state_->load();
    }

private:
    friend class StopSource;
    explicit StopToken(const std::shared_ptr<std::atomic_bool>& state) noexcept : state_(state) {}

    std::shared_ptr<const std::atomic_bool> state_;
};

class StopSource
{
public:
    [[nodiscard]] StopToken get_token() const noexcept { return StopToken{state_}; }
    [[nodiscard]] bool stop_requested() const noexcept { return state_ && state_->load(); }
    bool request_stop() const noexcept { return state_ && !state_->exchange(true); }

private:
    std::shared_ptr<std::atomic_bool> state_ = std::make_shared<std::atomic_bool>(false);
};
} // namespace sed
#endif
