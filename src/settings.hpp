#pragma once
#include "weather_source.hpp"
#include "vrchat_status.hpp"
#include <cstddef>
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
    COMP_WEATHER = 7,
    COMP_VRCHAT = 8,
    COMP_LYRICS = 9,
    COMP_COUNT,
};

enum class CloseAction { Ask, Quit, HideToTray };

// all user-facing formatting/behavior knobs, edited only through the app's
// tabs and persisted silently in the background - never hand-edited
struct AppSettings {
    // layout. the chatbox is a list of independent blocks; this is their
    // display order, top to bottom. values are Component ids.
    std::vector<int> component_order = {
        COMP_CLOCK, COMP_HEART, COMP_SONG, COMP_LYRICS, COMP_CUSTOM, COMP_BATTERY,
        COMP_SYSTEM, COMP_TIMER, COMP_WEATHER, COMP_VRCHAT
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

    bool show_weather_component = false;
    WeatherPlace weather_place;
    WeatherUnits weather_units = WeatherUnits::Celsius;
    bool weather_show_icon = true;
    bool weather_show_condition = true;

    bool show_vrchat_component = false;
    bool vrchat_show_mic = true;
    bool vrchat_show_earmuffs = true;
    bool vrchat_show_left = false;
    bool vrchat_show_right = false;
    bool vrchat_show_speed = false;
    bool vrchat_active_only = false;
    SpeedUnits vrchat_speed_units = SpeedUnits::MetersPerSecond;
    std::string vrchat_separator = " · ";

    bool show_lyrics_component = false;
    bool lyrics_online = false;
    bool lyrics_show_icon = true;
    std::string lyrics_icon = "❝";
    int lyrics_offset_ms = 0;
    int lyrics_max_width = 72;

    bool compact_layout = false;
    std::string compact_separator = " · ";

    // output
    bool send_automatically = true;
    CloseAction close_action = CloseAction::Ask;

    static constexpr std::size_t kMaxStatuses = 50;
    static constexpr std::size_t kMaxStatusBytes = 1024;
    std::vector<std::string> status_phrases;

    // how often the chatbox is refreshed, in ms. vrchat rate-limits, and a
    // faster tick is not meaningfully fresher past about once a second.
    int send_interval_ms = 1000;

    // number of recent samples averaged for the displayed bpm. 0 disables
    // smoothing. raw values still feed the min/avg/max session stats.
    int bpm_smoothing = 0;

    // connections
    std::string pulsoid_token;
    int osc_port = 9000;
    int vrchat_status_port = 9001;

    // escape hatch: when set, this template is used verbatim and the component
    // toggles above are ignored. left empty unless someone deliberately writes
    // one into the state file - the ui has no editor for it on purpose.
    std::string raw_template;
    bool use_raw_template = true;

    static AppSettings load();
    void save() const;
};

} // namespace cadence
