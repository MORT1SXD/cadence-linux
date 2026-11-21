#pragma once

#include <glib.h>
#include <optional>
#include <string>
#include <string_view>

namespace cadence {

struct VrchatStatus {
    std::optional<bool> muted;
    std::optional<bool> earmuffs;
    std::optional<int> left_gesture;
    std::optional<int> right_gesture;
    std::optional<float> speed;
};

enum class SpeedUnits { MetersPerSecond, KilometersPerHour, MilesPerHour };

class VrchatStatusListener {
public:
    VrchatStatusListener() = default;
    ~VrchatStatusListener();
    VrchatStatusListener(const VrchatStatusListener&) = delete;
    VrchatStatusListener& operator=(const VrchatStatusListener&) = delete;

    // Localhost only. When off there is no listening socket.
    void start(int port);
    void stop();
    VrchatStatus snapshot(gint64 now) const;
    bool listening() const { return fd_ >= 0; }
    const std::string& error() const { return error_; }

private:
    static gboolean on_ready(int fd, GIOCondition condition, gpointer data);
    void receive(std::string_view packet, gint64 now);

    int fd_ = -1;
    guint source_ = 0;
    VrchatStatus status_;
    gint64 last_report_ = 0;
    gint64 last_speed_ = 0;
    std::string error_;
};

} // namespace cadence
