#include "discord_presence.hpp"

#include <cstdio>

#ifdef CADENCE_DISCORD
extern "C" {
#include <discord_rpc.h>
}
#endif

namespace cadence {

#ifdef CADENCE_DISCORD

namespace {

// discord-rpc's callbacks carry no user-data pointer, so there's nothing to
// bridge. everything here runs on the gtk main thread, so no locking is needed.
void handle_ready(const DiscordUser* user) {
    if (user && user->username)
        std::fprintf(stderr, "cadence: discord ready as %s\n", user->username);
}

void handle_disconnected(int code, const char* message) {
    std::fprintf(stderr, "cadence: discord disconnected (%d: %s)\n", code,
                 message ? message : "?");
}

} // namespace

DiscordPresence::~DiscordPresence() { disconnect(); }

bool DiscordPresence::supported() { return true; }

bool DiscordPresence::connect(const std::string& app_id) {
    if (app_id.empty()) return false;
    if (connected_) disconnect();

    DiscordEventHandlers handlers{};
    handlers.ready = handle_ready;
    handlers.disconnected = handle_disconnected;

    // 3.4 returns void: it starts its own IO thread and returns successfully
    // even when discord isn't running. so this only means "we tried" - the
    // ready/disconnected callbacks are what actually report the truth.
    Discord_Initialize(app_id.c_str(), &handlers, /*autoRegister=*/0, nullptr);
    connected_ = true;
    app_id_ = app_id;
    return true;
}

void DiscordPresence::disconnect() {
    if (!connected_) return;
    Discord_ClearPresence();
    Discord_Shutdown();
    connected_ = false;
    app_id_.clear();
}

void DiscordPresence::set_presence(const std::string& state, const std::string& detail,
                                   const std::string& image_key) {
    if (!connected_) return;
    // discord copies everything during the call, so passing temporaries is safe
    DiscordRichPresence rp{};
    rp.state   = state.empty()  ? nullptr : state.c_str();
    rp.details = detail.empty() ? nullptr : detail.c_str();
    // "logo" is a reserved key: discord substitutes the app's own icon, so
    // there's no asset to upload or ship
    rp.largeImageKey = image_key.empty() ? nullptr : image_key.c_str();
    Discord_UpdatePresence(&rp);
}

void DiscordPresence::clear_presence() {
    if (!connected_) return;
    Discord_ClearPresence();
}

void DiscordPresence::pump() {
    if (!connected_) return;
    // with the library's own IO thread enabled this is cheap, but it's the
    // documented way to dispatch callbacks and costs almost nothing
    Discord_RunCallbacks();
}

#else  // !CADENCE_DISCORD

// compiled out. every entry point is a no-op so callers need no ifdefs.
DiscordPresence::~DiscordPresence() = default;
bool DiscordPresence::supported() { return false; }
bool DiscordPresence::connect(const std::string&) { return false; }
void DiscordPresence::disconnect() {}
void DiscordPresence::set_presence(const std::string&, const std::string&, const std::string&) {}
void DiscordPresence::clear_presence() {}
void DiscordPresence::pump() {}

#endif

} // namespace cadence
