#pragma once
#include <string>

namespace cadence {

// Discord rich presence. A thin wrapper so the rest of the app doesn't depend on
// discord-rpc's C API, and so the whole feature can be compiled out.
//
// Nothing here ever touches VRChat. The "In VRChat" state is a manual toggle the
// user flips themselves - auto-detecting it would mean inspecting the game
// process or its sockets, which this project does not do.
class DiscordPresence {
public:
    DiscordPresence() = default;
    ~DiscordPresence();

    DiscordPresence(const DiscordPresence&) = delete;
    DiscordPresence& operator=(const DiscordPresence&) = delete;

    // compiled out entirely when ENABLE_DISCORD is off
    static bool supported();

    // connecting is best-effort: discord may simply not be running
    bool connect(const std::string& app_id);
    void disconnect();
    bool connected() const { return connected_; }

    // state/detail are the two lines discord shows, in that order on screen
    // (state renders first, details beneath it). empty strings are dropped
    // rather than sent blank. image_key is optional; "logo" resolves to the
    // application's own uploaded icon without any extra asset work.
    void set_presence(const std::string& state, const std::string& detail,
                      const std::string& image_key = "logo");
    void clear_presence();

    // must be called regularly or discord considers the connection dead
    void pump();

private:
    bool connected_ = false;
    std::string app_id_;
};

} // namespace cadence
