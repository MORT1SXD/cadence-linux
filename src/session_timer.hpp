#pragma once

#include <chrono>
#include <cstdio>
#include <optional>
#include <string>

namespace cadence {

inline std::string format_duration(std::chrono::seconds duration) {
    const auto seconds = static_cast<long long>(duration.count());
    char text[32];
    if (seconds >= 3600)
        std::snprintf(text, sizeof text, "%lld:%02lld:%02lld",
                      seconds / 3600, seconds / 60 % 60, seconds % 60);
    else
        std::snprintf(text, sizeof text, "%02lld:%02lld", seconds / 60, seconds % 60);
    return text;
}

class SessionTimer {
public:
    using Clock = std::chrono::steady_clock;
    using Seconds = std::chrono::seconds;

    Seconds elapsed(Clock::time_point now) const {
        return std::chrono::duration_cast<Seconds>(accumulated_ + (started_ ? now - *started_ : Clock::duration{}));
    }

    bool finished(Clock::time_point now, Seconds limit) const {
        return limit > Seconds::zero() && elapsed(now) >= limit;
    }

    bool running(Clock::time_point now, Seconds limit) const {
        return started_.has_value() && !finished(now, limit);
    }

    void toggle(Clock::time_point now, Seconds limit) {
        if (finished(now, limit)) reset();
        if (started_) {
            accumulated_ += now - *started_;
            started_.reset();
        } else {
            started_ = now;
        }
    }

    void reset() {
        accumulated_ = {};
        started_.reset();
    }

    Seconds display(Clock::time_point now, Seconds limit) const {
        const auto spent = elapsed(now);
        return limit > Seconds::zero() ? (spent < limit ? limit - spent : Seconds{}) : spent;
    }

private:
    Clock::duration accumulated_{};
    std::optional<Clock::time_point> started_;
};

} // namespace cadence
