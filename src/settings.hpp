#pragma once
#include <string>
#include <vector>

namespace cadence {

// Identity of a chatbox block. Shared so settings and the formatter agree on
// the numbering used by [layout] order in state.ini.
enum Component {
    COMP_HEART = 0,
    COMP_SONG = 1,
    COMP_CLOCK = 2,
    COMP_CUSTOM = 3,
    COMP_BATTERY = 4,
    COMP_SYSTEM = 5,
    COMP_TIMER = 6,
    COMP_COUNT,
};

// Supplied by CMake from -DCADENCE_DISCORD_APP_ID. There is intentionally no
// fallback ID in this file: source builds bring their own, prebuilt releases
// have one compiled into the binary. The guard keeps direct/standalone builds
// of this header valid outside CMake.
#ifndef CADENCE_DISCORD_APP_ID
#define CADENCE_DISCORD_APP_ID ""
#endif

// all user-facing formatting/behavior knobs, edited only through the app's
// tabs and persisted silently in the background - never hand-edited
struct AppSettings {
    // layout. the chatbox is a list of independent blocks; this is their
    // display order, top to bottom. values are Component ids.
    std::vector<int> component_order = {
        COMP_CLOCK, COMP_HEART, COMP_SONG, COMP_CUSTOM, COMP_BATTERY, COMP_SYSTEM, COMP_TIMER
    };

    // heart rate component
    bool show_heart_component = true;
    bool show_bpm_current = true;
    bool show_bpm_min = false;
    bool show_bpm_avg = false;
    bool show_bpm_max = false;
    std::string heart_icon = "♥";

    // song component
    bool show_song_component = true;
    bool show_title = true;
    bool show_artist = true;
    bool show_music_progress = false;
    std::string note_icon = "♪";
    std::string separator = " — ";
    int truncate_length = 40;

    // clock component. on by default - it is the one source that always has
    // data, so a fresh install shows something useful immediately.
    bool show_clock_component = true;
    bool clock_24h = true;
    bool clock_show_seconds = false;
    bool clock_show_date = false;
    std::string clock_icon = "⌚";

    // a plain line the user writes themselves. empty means no block; no
    // network, no parsing, just text the user typed.
    bool show_custom_component = false;
    std::string custom_text;
    std::string custom_icon = "✎";

    // battery level from /sys. hidden automatically on machines without one.
    bool show_battery_component = false;
    std::string battery_icon = "⚡";
    bool battery_show_state = false;

    bool show_system_component = false;
    bool system_show_cpu = true;
    bool system_show_memory = true;

    bool show_timer_component = false;
    bool timer_countdown = false;
    int timer_minutes = 5;
    std::string timer_icon = "⏱";

    // display
    bool box_drawing = false;
    bool animations_enabled = true;
    bool compact_layout = false;
    std::string compact_separator = " · ";

    // output
    // vrchat's /chatbox/input takes an optional third bool that suppresses the
    // notification sound. we only ever sent two, so this was always on.
    bool notification_sfx = true;
    bool send_automatically = true;

    // how often the chatbox is refreshed, in ms. vrchat rate-limits, and a
    // faster tick is not meaningfully fresher past about once a second.
    int send_interval_ms = 1000;

    // number of recent samples averaged for the displayed bpm. 0 disables
    // smoothing. raw values still feed the min/avg/max session stats.
    int bpm_smoothing = 0;

    // connections
    std::string pulsoid_token;
    int osc_port = 9000;

    // discord rich presence.
    //
    // No Application ID lives in the source tree. Builds get one through
    // -DCADENCE_DISCORD_APP_ID=<id>; users can also override it at runtime via
    // [discord] app_id in ~/.config/cadence/state.ini. The shipped binary has
    // an ID compiled in at release time, which is why it just works.
    static constexpr const char* kDefaultDiscordAppId = CADENCE_DISCORD_APP_ID;

    // the user has seen and accepted the privacy warning. separate from the
    // toggles below so the dialog is shown once, not on every launch
    bool discord_acknowledged = false;
    bool discord_music = false;
    bool discord_heart_rate = false;
    std::string discord_app_id = kDefaultDiscordAppId;

    // escape hatch: when set, this template is used verbatim and the component
    // toggles above are ignored. left empty unless someone deliberately writes
    // one into the state file - the ui has no editor for it on purpose.
    std::string raw_template;
    bool use_raw_template = true;

    static AppSettings load();
    void save() const;
};

} // namespace cadence
