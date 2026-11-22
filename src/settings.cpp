#include "settings.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <glib.h>
#include <limits>

namespace cadence {

namespace {

// internal state file the app owns entirely - not meant to be hand-edited
std::string config_path() {
    std::string dir = std::string(g_get_user_config_dir()) + "/cadence";
    std::filesystem::create_directories(dir);
    return dir + "/state.ini";
}

// g_key_file_get_boolean returns false (with an error set) for a missing key,
// which would silently stomp a true default - so fall back explicitly instead
bool get_bool(GKeyFile* kf, const char* group, const char* key, bool fallback) {
    GError* err = nullptr;
    gboolean v = g_key_file_get_boolean(kf, group, key, &err);
    if (err) {
        g_error_free(err);
        return fallback;
    }
    return v;
}

std::string get_string(GKeyFile* kf, const char* group, const char* key, const std::string& fallback) {
    gchar* v = g_key_file_get_string(kf, group, key, nullptr);
    if (!v) return fallback;
    std::string result = v;
    g_free(v);
    return result;
}

int get_int(GKeyFile* kf, const char* group, const char* key, int fallback) {
    GError* err = nullptr;
    int v = g_key_file_get_integer(kf, group, key, &err);
    if (err) {
        g_error_free(err);
        return fallback;
    }
    return v;
}

// Reads a stored block order, dropping unknown/duplicate ids and appending any
// missing ones. An empty result means "no stored order", so the caller can
// migrate from the old heart_first key instead.
std::vector<int> parse_order(const std::string& raw) {
    std::vector<int> out;
    if (raw.empty()) return out;

    gchar** parts = g_strsplit(raw.c_str(), ",", -1);
    for (int i = 0; parts[i]; ++i) {
        const std::string part = g_strstrip(parts[i]);
        int v = 0;
        const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), v);
        if (error != std::errc{} || end != part.data() + part.size()
            || v < COMP_HEART || v >= COMP_COUNT) continue;
        if (std::find(out.begin(), out.end(), v) != out.end()) continue;
        out.push_back(v);
    }
    g_strfreev(parts);
    if (out.empty()) return out;

    for (int v = COMP_HEART; v < COMP_COUNT; ++v)
        if (v != COMP_LYRICS && std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
    if (std::find(out.begin(), out.end(), COMP_LYRICS) == out.end())
        out.insert(std::find(out.begin(), out.end(), COMP_SONG) + 1, COMP_LYRICS);
    return out;
}

} // namespace

AppSettings AppSettings::load() {
    AppSettings s;
    GKeyFile* kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, config_path().c_str(), G_KEY_FILE_NONE, nullptr)) {
        s.component_order = parse_order(get_string(kf, "layout", "order", ""));
        if (s.component_order.empty()) {
            // migrate the old two-block layout key
            const bool heart_first = get_bool(kf, "layout", "heart_first", true);
            s.component_order = parse_order(heart_first ? "0,1,2" : "1,0,2");
        }

        s.show_heart_component = get_bool(kf, "heart", "enabled", s.show_heart_component);
        s.show_bpm_current = get_bool(kf, "heart", "show_current", s.show_bpm_current);
        s.show_bpm_min = get_bool(kf, "heart", "show_min", s.show_bpm_min);
        s.show_bpm_avg = get_bool(kf, "heart", "show_avg", s.show_bpm_avg);
        s.show_bpm_max = get_bool(kf, "heart", "show_max", s.show_bpm_max);
        s.heart_icon = get_string(kf, "heart", "heart_icon", s.heart_icon);

        s.show_song_component = get_bool(kf, "song", "enabled", s.show_song_component);
        s.show_title = get_bool(kf, "song", "show_title", s.show_title);
        s.show_artist = get_bool(kf, "song", "show_artist", s.show_artist);
        s.show_music_progress = get_bool(kf, "song", "show_progress", s.show_music_progress);
        s.note_icon = get_string(kf, "song", "note_icon", s.note_icon);
        s.separator = get_string(kf, "song", "separator", s.separator);
        s.truncate_length = get_int(kf, "song", "truncate_length", s.truncate_length);

        s.show_clock_component = get_bool(kf, "clock", "enabled", s.show_clock_component);
        s.clock_24h = get_bool(kf, "clock", "format_24h", s.clock_24h);
        s.clock_show_seconds = get_bool(kf, "clock", "show_seconds", s.clock_show_seconds);
        s.clock_show_date = get_bool(kf, "clock", "show_date", s.clock_show_date);
        s.clock_icon = get_string(kf, "clock", "icon", s.clock_icon);

        s.show_custom_component = get_bool(kf, "custom", "enabled", s.show_custom_component);
        s.custom_text = get_string(kf, "custom", "text", s.custom_text);
        s.custom_icon = get_string(kf, "custom", "icon", s.custom_icon);

        s.show_battery_component = get_bool(kf, "battery", "enabled", s.show_battery_component);
        s.battery_icon = get_string(kf, "battery", "icon", s.battery_icon);
        s.battery_show_state = get_bool(kf, "battery", "show_state", s.battery_show_state);

        s.show_system_component = get_bool(kf, "system", "enabled", s.show_system_component);
        s.system_show_cpu = get_bool(kf, "system", "show_cpu", s.system_show_cpu);
        s.system_show_memory = get_bool(kf, "system", "show_memory", s.system_show_memory);

        s.show_timer_component = get_bool(kf, "timer", "enabled", s.show_timer_component);
        s.timer_countdown = get_bool(kf, "timer", "countdown", s.timer_countdown);
        s.timer_minutes = std::clamp(get_int(kf, "timer", "minutes", s.timer_minutes), 1, 180);
        s.timer_icon = get_string(kf, "timer", "icon", s.timer_icon);

        s.show_weather_component = get_bool(kf, "weather", "enabled", s.show_weather_component);
        s.weather_show_icon = get_bool(kf, "weather", "show_icon", s.weather_show_icon);
        s.weather_show_condition = get_bool(kf, "weather", "show_condition", s.weather_show_condition);
        const auto unit = get_string(kf, "weather", "units", "celsius");
        if (unit == "fahrenheit") s.weather_units = WeatherUnits::Fahrenheit;
        if (unit == "alternate") s.weather_units = WeatherUnits::Alternate;
        auto coordinate = [kf](const char* key) {
            GError* error = nullptr;
            const double value = g_key_file_get_double(kf, "weather", key, &error);
            if (!error) return value;
            g_error_free(error);
            return std::numeric_limits<double>::quiet_NaN();
        };
        WeatherPlace place{get_string(kf, "weather", "city", ""), coordinate("latitude"), coordinate("longitude")};
        if (place.valid()) s.weather_place = place;

        s.show_vrchat_component = get_bool(kf, "vrchat_status", "enabled", s.show_vrchat_component);
        s.vrchat_show_mic = get_bool(kf, "vrchat_status", "show_mic", s.vrchat_show_mic);
        s.vrchat_show_earmuffs = get_bool(kf, "vrchat_status", "show_earmuffs", s.vrchat_show_earmuffs);
        s.vrchat_show_left = get_bool(kf, "vrchat_status", "show_left", s.vrchat_show_left);
        s.vrchat_show_right = get_bool(kf, "vrchat_status", "show_right", s.vrchat_show_right);
        s.vrchat_show_speed = get_bool(kf, "vrchat_status", "show_speed", s.vrchat_show_speed);
        s.vrchat_active_only = get_bool(kf, "vrchat_status", "active_only", s.vrchat_active_only);
        s.vrchat_separator = get_string(kf, "vrchat_status", "separator", s.vrchat_separator);
        const auto speed_unit = get_string(kf, "vrchat_status", "speed_units", "m/s");
        if (speed_unit == "km/h") s.vrchat_speed_units = SpeedUnits::KilometersPerHour;
        if (speed_unit == "mph") s.vrchat_speed_units = SpeedUnits::MilesPerHour;

        s.show_lyrics_component = get_bool(kf, "lyrics", "enabled", s.show_lyrics_component);
        s.lyrics_online = get_bool(kf, "lyrics", "online", s.lyrics_online);
        s.lyrics_show_icon = get_bool(kf, "lyrics", "show_icon", s.lyrics_show_icon);
        s.lyrics_icon = get_string(kf, "lyrics", "icon", s.lyrics_icon);
        s.lyrics_max_width = std::clamp(get_int(kf, "lyrics", "max_width", s.lyrics_max_width), 20, 120);
        s.lyrics_offset_ms = std::clamp(get_int(kf, "lyrics", "offset_ms", s.lyrics_offset_ms), -5000, 5000);

        s.compact_layout = get_bool(kf, "layout", "compact", s.compact_layout);
        s.compact_separator = get_string(kf, "layout", "separator", s.compact_separator);


        s.send_interval_ms = get_int(kf, "output", "send_interval_ms", s.send_interval_ms);
        if (s.send_interval_ms != 2000 && s.send_interval_ms != 5000) s.send_interval_ms = 1000;
        s.bpm_smoothing = get_int(kf, "output", "bpm_smoothing", s.bpm_smoothing);
        if (s.send_interval_ms < 250) s.send_interval_ms = 1000;

        s.pulsoid_token = get_string(kf, "pulsoid", "token", s.pulsoid_token);
        s.osc_port = get_int(kf, "osc", "port", s.osc_port);
        s.vrchat_status_port = std::clamp(get_int(kf, "osc", "status_port", s.vrchat_status_port), 1, 65535);

        s.raw_template = get_string(kf, "format", "raw_template", "");
        s.use_raw_template = get_bool(kf, "format", "use_raw_template", s.use_raw_template);
        s.send_automatically = get_bool(kf, "output", "send_automatically", s.send_automatically);
        const auto close = get_string(kf, "window", "close_action", "ask");
        if (close == "quit") s.close_action = CloseAction::Quit;
        else if (close == "tray") s.close_action = CloseAction::HideToTray;

        gsize count = 0;
        if (gchar** phrases = g_key_file_get_string_list(kf, "status", "phrases", &count, nullptr)) {
            for (gsize i = 0; i < std::min(count, kMaxStatuses); ++i) {
                const std::string phrase = phrases[i];
                if (phrase.size() <= kMaxStatusBytes && g_utf8_validate(phrase.data(), phrase.size(), nullptr))
                    s.status_phrases.push_back(phrase);
            }
            g_strfreev(phrases);
        }
    }
    g_key_file_free(kf);
    return s;
}

void AppSettings::save() const {
    GKeyFile* kf = g_key_file_new();

    std::string order;
    for (size_t i = 0; i < component_order.size(); ++i) {
        if (i) order += ",";
        order += std::to_string(component_order[i]);
    }
    g_key_file_set_string(kf, "layout", "order", order.c_str());
    g_key_file_set_boolean(kf, "layout", "compact", compact_layout);
    g_key_file_set_string(kf, "layout", "separator", compact_separator.c_str());

    g_key_file_set_boolean(kf, "heart", "enabled", show_heart_component);
    g_key_file_set_boolean(kf, "heart", "show_current", show_bpm_current);
    g_key_file_set_boolean(kf, "heart", "show_min", show_bpm_min);
    g_key_file_set_boolean(kf, "heart", "show_avg", show_bpm_avg);
    g_key_file_set_boolean(kf, "heart", "show_max", show_bpm_max);
    g_key_file_set_string(kf, "heart", "heart_icon", heart_icon.c_str());

    g_key_file_set_boolean(kf, "song", "enabled", show_song_component);
    g_key_file_set_boolean(kf, "song", "show_title", show_title);
    g_key_file_set_boolean(kf, "song", "show_artist", show_artist);
    g_key_file_set_boolean(kf, "song", "show_progress", show_music_progress);
    g_key_file_set_string(kf, "song", "note_icon", note_icon.c_str());
    g_key_file_set_string(kf, "song", "separator", separator.c_str());
    g_key_file_set_integer(kf, "song", "truncate_length", truncate_length);

    g_key_file_set_boolean(kf, "clock", "enabled", show_clock_component);
    g_key_file_set_boolean(kf, "clock", "format_24h", clock_24h);
    g_key_file_set_boolean(kf, "clock", "show_seconds", clock_show_seconds);
    g_key_file_set_boolean(kf, "clock", "show_date", clock_show_date);
    g_key_file_set_string(kf, "clock", "icon", clock_icon.c_str());

    g_key_file_set_boolean(kf, "custom", "enabled", show_custom_component);
    g_key_file_set_string(kf, "custom", "text", custom_text.c_str());
    g_key_file_set_string(kf, "custom", "icon", custom_icon.c_str());

    g_key_file_set_boolean(kf, "battery", "enabled", show_battery_component);
    g_key_file_set_string(kf, "battery", "icon", battery_icon.c_str());
    g_key_file_set_boolean(kf, "battery", "show_state", battery_show_state);

    g_key_file_set_boolean(kf, "system", "enabled", show_system_component);
    g_key_file_set_boolean(kf, "system", "show_cpu", system_show_cpu);
    g_key_file_set_boolean(kf, "system", "show_memory", system_show_memory);

    g_key_file_set_boolean(kf, "timer", "enabled", show_timer_component);
    g_key_file_set_boolean(kf, "timer", "countdown", timer_countdown);
    g_key_file_set_integer(kf, "timer", "minutes", timer_minutes);
    g_key_file_set_string(kf, "timer", "icon", timer_icon.c_str());

    g_key_file_set_boolean(kf, "weather", "enabled", show_weather_component);
    g_key_file_set_boolean(kf, "weather", "show_icon", weather_show_icon);
    g_key_file_set_boolean(kf, "weather", "show_condition", weather_show_condition);
    g_key_file_set_string(kf, "weather", "units", weather_units == WeatherUnits::Fahrenheit ? "fahrenheit" :
                          weather_units == WeatherUnits::Alternate ? "alternate" : "celsius");
    if (weather_place.valid()) {
        g_key_file_set_string(kf, "weather", "city", weather_place.label.c_str());
        g_key_file_set_double(kf, "weather", "latitude", weather_place.latitude);
        g_key_file_set_double(kf, "weather", "longitude", weather_place.longitude);
    }

    g_key_file_set_boolean(kf, "vrchat_status", "enabled", show_vrchat_component);
    g_key_file_set_boolean(kf, "vrchat_status", "show_mic", vrchat_show_mic);
    g_key_file_set_boolean(kf, "vrchat_status", "show_earmuffs", vrchat_show_earmuffs);
    g_key_file_set_boolean(kf, "vrchat_status", "show_left", vrchat_show_left);
    g_key_file_set_boolean(kf, "vrchat_status", "show_right", vrchat_show_right);
    g_key_file_set_boolean(kf, "vrchat_status", "show_speed", vrchat_show_speed);
    g_key_file_set_boolean(kf, "vrchat_status", "active_only", vrchat_active_only);
    g_key_file_set_string(kf, "vrchat_status", "separator", vrchat_separator.c_str());
    g_key_file_set_string(kf, "vrchat_status", "speed_units",
        vrchat_speed_units == SpeedUnits::KilometersPerHour ? "km/h" :
        vrchat_speed_units == SpeedUnits::MilesPerHour ? "mph" : "m/s");

    g_key_file_set_boolean(kf, "lyrics", "enabled", show_lyrics_component);
    g_key_file_set_boolean(kf, "lyrics", "online", lyrics_online);
    g_key_file_set_boolean(kf, "lyrics", "show_icon", lyrics_show_icon);
    g_key_file_set_string(kf, "lyrics", "icon", lyrics_icon.c_str());
    g_key_file_set_integer(kf, "lyrics", "max_width", lyrics_max_width);
    g_key_file_set_integer(kf, "lyrics", "offset_ms", lyrics_offset_ms);


    g_key_file_set_integer(kf, "output", "send_interval_ms", send_interval_ms);
    g_key_file_set_integer(kf, "output", "bpm_smoothing", bpm_smoothing);

    g_key_file_set_string(kf, "pulsoid", "token", pulsoid_token.c_str());
    g_key_file_set_integer(kf, "osc", "port", osc_port);
    g_key_file_set_integer(kf, "osc", "status_port", vrchat_status_port);

    if (!raw_template.empty())
        g_key_file_set_string(kf, "format", "raw_template", raw_template.c_str());
    g_key_file_set_boolean(kf, "format", "use_raw_template", use_raw_template);
    g_key_file_set_boolean(kf, "output", "send_automatically", send_automatically);
    g_key_file_set_string(kf, "window", "close_action", close_action == CloseAction::Quit ? "quit" :
        close_action == CloseAction::HideToTray ? "tray" : "ask");

    if (!status_phrases.empty()) {
        std::vector<const char*> phrases;
        phrases.reserve(status_phrases.size());
        for (const auto& phrase : status_phrases) phrases.push_back(phrase.c_str());
        g_key_file_set_string_list(kf, "status", "phrases", phrases.data(), phrases.size());
    }

    g_key_file_save_to_file(kf, config_path().c_str(), nullptr);
    g_key_file_free(kf);
}

} // namespace cadence
