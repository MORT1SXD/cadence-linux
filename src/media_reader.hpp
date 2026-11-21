#pragma once
#include <chrono>
#include <gio/gio.h>
#include <optional>
#include <string>

namespace cadence {

struct TrackInfo {
    std::string title;
    std::string artist;
    std::optional<std::chrono::seconds> position;
    std::optional<std::chrono::seconds> duration;
};

// polls mpris (media player remote interfacing spec) over dbus for the active player
// uses glib/gio directly since we already link it via gtk, no extra dependency needed
class MediaReader {
public:
    MediaReader();
    ~MediaReader();

    std::optional<TrackInfo> current_track(bool read_progress = false);

private:
    GDBusConnection* bus_ = nullptr;
};

} // namespace cadence
