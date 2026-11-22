#include "formatter.hpp"
#include "session_timer.hpp"

#include <algorithm>
#include <cmath>
#include <glib.h>

namespace cadence {

namespace {

// vrchat's 144 limit is display width, not codepoint count. CJK and emoji occupy
// two columns, so a codepoint count happily produces a message that gets cut off
// in-game even though our own counter says it fits.
int display_width(const std::string& s) {
    int w = 0;
    for (const char* p = s.c_str(); *p; ) {
        gunichar c = g_utf8_get_char(p);
        p = g_utf8_next_char(p);
        // zero-width marks and combining characters shouldn't advance the column
        if (g_unichar_iszerowidth(c)) continue;
        w += g_unichar_iswide(c) ? 2 : 1;
    }
    return w;
}

// truncates to a display-width budget, never splitting a character
std::string truncate(const std::string& s, int max_width) {
    if (s.empty()) return s;
    if (max_width <= 0 || display_width(s) <= max_width) return s;

    // reserve one column for the ellipsis we append
    int budget = max_width - 1;
    int w = 0;
    const char* p = s.c_str();
    const char* last_fit = s.c_str();
    while (*p) {
        gunichar c = g_utf8_get_char(p);
        const int cw = g_unichar_iszerowidth(c) ? 0 : (g_unichar_iswide(c) ? 2 : 1);
        if (w + cw > budget) break;
        w += cw;
        p = g_utf8_next_char(p);
        last_fit = p;
    }
    return std::string(s.c_str(), last_fit - s.c_str()) + "…";
}

int round_bpm(double v) { return static_cast<int>(std::lround(v)); }

std::string vrchat_line(const VrchatStatus& value, const AppSettings& st) {
    std::string out;
    auto add = [&out, &st](const std::string& field) {
        if (!out.empty()) out += st.vrchat_separator;
        out += field;
    };
    if (st.vrchat_show_mic && value.muted && (*value.muted || !st.vrchat_active_only))
        add(*value.muted ? "Mic muted" : "Mic unmuted");
    if (st.vrchat_show_earmuffs && value.earmuffs && (*value.earmuffs || !st.vrchat_active_only))
        add(*value.earmuffs ? "Earmuffs on" : "Earmuffs off");

    static const char* gestures[] = {"Neutral", "Fist", "Open hand", "Point", "Victory", "Rock", "Finger gun", "Thumbs up"};
    auto hand = [&add, &st](const char* label, std::optional<int> gesture) {
        if (gesture && *gesture >= 0 && *gesture < 8 && (*gesture != 0 || !st.vrchat_active_only))
            add(std::string(label) + gestures[*gesture]);
    };
    if (st.vrchat_show_left) hand("L: ", value.left_gesture);
    if (st.vrchat_show_right) hand("R: ", value.right_gesture);
    if (st.vrchat_show_speed && value.speed && (*value.speed > 0 || !st.vrchat_active_only)) {
        double speed = *value.speed;
        const char* unit = " m/s";
        if (st.vrchat_speed_units == SpeedUnits::KilometersPerHour) {
            speed *= 3.6;
            unit = " km/h";
        } else if (st.vrchat_speed_units == SpeedUnits::MilesPerHour) {
            speed *= 2.236936;
            unit = " mph";
        }
        char text[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_formatd(text, sizeof text, "%.1f", speed);
        add(std::string(text) + unit);
    }
    return out;
}

// resolves one token name. an unresolvable token - or one with no data behind it
// right now - yields "", which is what makes {if:...} blocks self-skipping
std::string lookup(const std::string& key, const TemplateData& d, const AppSettings& st) {
    const bool have_hr = d.hr && d.hr->has_data;

    if (key == "heart_rate")       return have_hr ? std::to_string(d.hr->current) : "";
    if (key == "heart_rate_label") return have_hr ? std::to_string(d.hr->current) + " bpm" : "";
    if (key == "bpm")              return have_hr ? std::to_string(d.hr->current) : "";
    if (key == "bpm_label")        return have_hr ? std::to_string(d.hr->current) + " bpm" : "";
    if (key == "bpm_min")          return have_hr ? std::to_string(d.hr->min) : "";
    if (key == "bpm_avg")          return have_hr ? std::to_string(round_bpm(d.hr->avg)) : "";
    if (key == "bpm_max")          return have_hr ? std::to_string(d.hr->max) : "";

    // these come from settings rather than live data, so they always resolve
    if (key == "heart_icon") return st.heart_icon;
    if (key == "song_icon")  return st.note_icon;
    if (key == "separator")  return st.separator;

    if (key == "song")   return d.track ? truncate(d.track->title, st.truncate_length) : "";
    if (key == "artist") return d.track ? truncate(d.track->artist, st.truncate_length) : "";
    if (key == "music_progress" && d.track && d.track->position && d.track->duration)
        return format_duration(*d.track->position) + " / " + format_duration(*d.track->duration);
    if (key == "timer") return d.timer;
    if (key == "timer_icon") return st.timer_icon;
    if (key == "weather_temperature") return d.weather ? d.weather->temperature : "";
    if (key == "weather_icon") return d.weather && st.weather_show_icon ? d.weather->icon : "";
    if (key == "weather_condition") return d.weather && st.weather_show_condition ? d.weather->condition : "";
    if (key == "vrchat_status") return st.show_vrchat_component && d.vrchat ? vrchat_line(*d.vrchat, st) : "";
    if (key == "lyric" && st.show_lyrics_component) return format_lyric(d.lyric, st);

    if (key == "clock")      return d.clock ? d.clock->time : "";
    if (key == "clock_date") return d.clock ? d.clock->date : "";
    if (key == "clock_icon") return st.clock_icon;

    if (key == "custom")       return st.custom_text;
    if (key == "custom_icon")  return st.custom_icon;

    if (key == "battery") {
        if (!d.battery || !d.battery->present) return "";
        std::string s = std::to_string(d.battery->percent) + "%";
        return s;
    }
    if (key == "battery_percent")
        return d.battery && d.battery->present ? std::to_string(d.battery->percent) : "";
    if (key == "battery_state")
        return d.battery && d.battery->present ? d.battery->state : "";
    if (key == "battery_icon") return st.battery_icon;
    if (key == "cpu_percent" && d.system && d.system->cpu_percent)
        return std::to_string(*d.system->cpu_percent);
    if (key == "memory_percent" && d.system && d.system->memory_percent)
        return std::to_string(*d.system->memory_percent);

    return "";
}

// signals that a block bailed out, so the caller knows whether to look for the
// matching {else} / {end}
enum class Stop { None, End, Else };

// walks the template from position i. emit is false while inside a conditional
// branch that wasn't taken: we still have to parse through it to find the
// matching close tag, we just don't write anything out
Stop expand(const std::string& s, size_t& i, const TemplateData& d, const AppSettings& st,
            bool emit, std::string& out) {
    while (i < s.size()) {
        const char c = s[i];

        if (c == '\r') { ++i; continue; }
        if (c == '\n') { if (emit) out += '\n'; ++i; continue; }

        // "\n" spelled out, for people who don't want literal newlines in a field
        if (c == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
            if (emit) out += '\n';
            i += 2;
            continue;
        }

        if (c != '{') { if (emit) out += c; ++i; continue; }

        // "{{" escapes a literal brace. there's deliberately no "}}" escape - it
        // would be ambiguous with a token's own closing brace, and nothing in a
        // chatbox message needs a literal '}' often enough to earn the footgun
        if (i + 1 < s.size() && s[i + 1] == '{') { if (emit) out += '{'; i += 2; continue; }

        const size_t close = s.find('}', i);
        if (close == std::string::npos) {
            // an unclosed brace is just a literal brace, don't eat the rest
            if (emit) out += s.substr(i);
            return Stop::None;
        }

        const std::string inner = s.substr(i + 1, close - i - 1);
        i = close + 1;

        if (inner == "end")  return Stop::End;
        if (inner == "else") return Stop::Else;

        if (inner.rfind("if:", 0) == 0) {
            const bool truthy = !lookup(inner.substr(3), d, st).empty();
            if (expand(s, i, d, st, emit && truthy, out) == Stop::Else) {
                expand(s, i, d, st, emit && !truthy, out);
            }
            continue;
        }

        if (inner == "nl") { if (emit) out += '\n'; continue; }

        if (emit) out += lookup(inner, d, st);
    }
    return Stop::None;
}

// tidies up what the user's template produced: trailing spaces off each line,
// runs of blank lines collapsed to one, no leading or trailing blank lines.
// without this a template with an {if} that resolves empty leaves a ragged gap
std::string normalize(const std::string& in) {
    std::string cleaned;
    if (gchar** rows = g_strsplit(in.c_str(), "\n", -1)) {
        for (int i = 0; rows[i]; ++i) {
            std::string line = rows[i];
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
            if (!cleaned.empty()) cleaned += '\n';
            cleaned += line;
        }
        g_strfreev(rows);
    }

    std::string result;
    if (gchar** rows = g_strsplit(cleaned.c_str(), "\n", -1)) {
        bool prev_blank = true;
        for (int i = 0; rows[i]; ++i) {
            const bool blank = rows[i][0] == '\0';
            if (blank && prev_blank) continue;
            if (!result.empty()) result += '\n';
            result += rows[i];
            prev_blank = blank;
        }
        g_strfreev(rows);
    }
    while (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

} // namespace

std::string default_template() {
    return "{if:heart_rate}{heart_icon} {heart_rate_label}{end}"
           "{if:song}\n{song_icon} {song}{if:artist}{separator}{artist}{end}{end}";
}

std::string render_template(const std::string& tmpl, const TemplateData& data, const AppSettings& st) {
    const std::string source = tmpl.empty() ? default_template() : tmpl;
    size_t i = 0;
    std::string out;
    expand(source, i, data, st, true, out);

    // deliberately no "…" placeholder here. an empty result means the template
    // genuinely produced nothing (no data, or every token misspelled) and the
    // preview should show that honestly rather than dressing it up
    return normalize(out);
}

ChatboxStats stats_for(const std::string& text) {
    ChatboxStats s;
    // measured in display columns, matching what vrchat actually enforces
    s.chars = display_width(text);
    s.lines = 1;
    for (const char c : text) {
        if (c == '\n') ++s.lines;
    }
    // vrchat truncates past these, so it's worth warning before it happens
    s.over_limit = s.chars > 144 || s.lines > 9;
    return s;
}

// builds each block, then joins them in the user's chosen order. every block is
// wrapped in {if:...} so a block with no data disappears cleanly instead of
// leaving a dangling separator or a blank line behind.
static std::string heart_block(const AppSettings& st) {
    if (!st.show_heart_component) return {};

    // collect the enabled fields, then join. the icon prefixes the whole block
    // rather than the current-bpm part, so it still appears when someone turns
    // "current" off and shows only min/avg/max
    std::vector<std::string> parts;
    if (st.show_bpm_current) parts.push_back("{heart_rate_label}");

    std::vector<std::string> stats;
    if (st.show_bpm_min) stats.push_back("min {bpm_min}");
    if (st.show_bpm_avg) stats.push_back("avg {bpm_avg}");
    if (st.show_bpm_max) stats.push_back("max {bpm_max}");
    if (!stats.empty()) {
        std::string joined = "(";
        for (size_t i = 0; i < stats.size(); ++i) {
            if (i) joined += " · ";
            joined += stats[i];
        }
        parts.push_back(joined + ")");
    }

    if (parts.empty()) return {};  // every field toggled off

    std::string body = "{heart_icon} " + parts[0];
    for (size_t i = 1; i < parts.size(); ++i) body += "  " + parts[i];
    return "{if:heart_rate}" + body + "{end}";
}

static std::string song_block(const AppSettings& st) {
    if (!st.show_song_component) return {};

    std::string body;
    if (st.show_title) body += "{song_icon} {song}";
    if (st.show_artist) {
        // the separator is gated on the artist actually having a value, otherwise
        // a track with no artist renders as "Title — " with a dangling dash
        if (st.show_title) body += "{if:artist}{separator}{end}";
        body += "{artist}";
    }
    if (st.show_music_progress) {
        if (st.show_title) body += "{if:music_progress} · {end}";
        else if (st.show_artist) body += "{if:artist}{if:music_progress} · {end}{end}";
        body += "{music_progress}";
    }
    if (body.empty()) return {};
    return "{if:song}" + body + "{end}";
}

static std::string clock_block(const AppSettings& st) {
    if (!st.show_clock_component) return {};

    std::string body = "{if:clock_icon}{clock_icon} {end}{clock}";
    if (st.clock_show_date) {
        // only add the date when there is one; keeps the gate honest for a
        // template that toggles the date off later
        body += "{if:clock_date} {clock_date}{end}";
    }
    return "{if:clock}" + body + "{end}";
}

static std::string custom_block(const AppSettings& st) {
    if (!st.show_custom_component || st.custom_text.find_first_not_of(" \t\r\n") == std::string::npos)
        return {};
    return "{if:custom}{if:custom_icon}{custom_icon} {end}{custom}{end}";
}

static std::string battery_block(const AppSettings& st) {
    if (!st.show_battery_component) return {};
    std::string body = "{if:battery}{if:battery_icon}{battery_icon} {end}{battery}";
    if (st.battery_show_state) body += "{if:battery_state} · {battery_state}{end}";
    return body + "{end}";
}

static std::string system_block(const AppSettings& st) {
    if (!st.show_system_component) return {};
    std::string body;
    if (st.system_show_cpu) body = "{if:cpu_percent}CPU {cpu_percent}%{end}";
    if (st.system_show_memory) {
        body += "{if:memory_percent}";
        if (st.system_show_cpu) body += "{if:cpu_percent} · {end}";
        body += "RAM {memory_percent}%{end}";
    }
    return body;
}

static std::string block_for(int component, const AppSettings& st) {
    switch (component) {
        case COMP_HEART:   return heart_block(st);
        case COMP_SONG:    return song_block(st);
        case COMP_CLOCK:   return clock_block(st);
        case COMP_CUSTOM:  return custom_block(st);
        case COMP_BATTERY: return battery_block(st);
        case COMP_SYSTEM:  return system_block(st);
        case COMP_TIMER:   return st.show_timer_component
            ? "{if:timer}{if:timer_icon}{timer_icon} {end}{timer}{end}" : "";
        case COMP_WEATHER: return st.show_weather_component
            ? "{if:weather_temperature}{if:weather_icon}{weather_icon} {end}{weather_temperature}"
              "{if:weather_condition} · {weather_condition}{end}{end}" : "";
        case COMP_VRCHAT: return st.show_vrchat_component ? "{vrchat_status}" : "";
        case COMP_LYRICS: return st.show_lyrics_component ? "{lyric}" : "";
        default:           return {};
    }
}

std::string component_template(const AppSettings& st) {
    std::string out;
    for (const int component : st.component_order) {
        const std::string block = block_for(component, st);
        if (block.empty()) continue;
        if (!out.empty()) out += "\n";
        out += block;
    }
    return out;
}

std::string render_chatbox(const TemplateData& data, const AppSettings& st) {
    if (st.use_raw_template && !st.raw_template.empty())
        return render_template(st.raw_template, data, st);

    auto flatten = [](std::string& line) {
        for (char& ch : line)
            if (ch == '\n' || ch == '\r') ch = ' ';
    };
    std::string separator = st.compact_layout ? st.compact_separator : "\n";
    if (st.compact_layout) flatten(separator);
    std::vector<std::string> blocks;
    std::optional<size_t> lyric_index;
    for (const int component : st.component_order) {
        std::string line = render_component(component, data, st);
        if (line.empty()) continue;
        if (st.compact_layout) flatten(line);
        if (component == COMP_LYRICS) lyric_index = blocks.size();
        blocks.push_back(std::move(line));
    }
    auto combine = [&] {
        std::string text;
        for (const auto& block : blocks) {
            if (block.empty()) continue;
            if (!text.empty()) text += separator;
            text += block;
        }
        return text;
    };
    auto out = combine();
    if (lyric_index && stats_for(out).over_limit) {
        auto& block = blocks[*lyric_index];
        const std::string original = std::move(block);
        // A long lyric must not block the rest of the chatbox from sending.
        block.clear();
        const auto without_lyrics = combine();
        const int remaining = 144 - stats_for(without_lyrics).chars;
        for (int width = std::min(remaining, display_width(original) - 1); width >= 4; --width) {
            block = truncate(original, width);
            out = combine();
            if (!stats_for(out).over_limit) return out;
        }
        return without_lyrics;
    }
    return out;
}

std::string render_component(int component, const TemplateData& data, const AppSettings& st) {
    const std::string block = block_for(component, st);
    if (block.empty()) return {};
    size_t i = 0;
    std::string out;
    expand(block, i, data, st, true, out);
    return normalize(out);
}

std::string format_lyric(std::string_view lyric, const AppSettings& st) {
    if (lyric.empty()) return {};
    std::string prefix;
    if (st.lyrics_show_icon) {
        for (const char* p = st.lyrics_icon.c_str(); *p;) {
            const auto ch = g_utf8_get_char_validated(p, -1);
            if (ch == static_cast<gunichar>(-1) || ch == static_cast<gunichar>(-2)) break;
            const auto* next = g_utf8_next_char(p);
            if (!g_unichar_iscntrl(ch) && ch != 0x2028 && ch != 0x2029) prefix.append(p, next - p);
            p = next;
        }
        const auto first = prefix.find_first_not_of(' ');
        if (first != std::string::npos)
            prefix = truncate(prefix.substr(first, prefix.find_last_not_of(' ') - first + 1), 12) + " ";
        else
            prefix.clear();
    }
    prefix.append(lyric);
    return truncate(prefix, st.lyrics_max_width);
}

const std::vector<TokenInfo>& token_catalog() {
    static const std::vector<TokenInfo> catalog = {
        {"{heart_rate}",       "HR",       "current bpm, e.g. 72"},
        {"{heart_rate_label}", "HR label", "current bpm with units, e.g. 72 bpm"},
        {"{bpm_min}",          "Min",      "lowest bpm this session"},
        {"{bpm_avg}",          "Avg",      "average bpm this session"},
        {"{bpm_max}",          "Max",      "highest bpm this session"},
        {"{song}",             "Song",     "current track title"},
        {"{artist}",           "Artist",   "current track artist"},
        {"{music_progress}",   "Progress", "track position and duration"},
        {"{timer}",            "Timer",    "stopwatch or countdown"},
        {"{timer_icon}",       "Timer icon", "the session timer icon"},
        {"{weather_temperature}", "Temperature", "current temperature for the selected city"},
        {"{weather_icon}",        "Weather icon", "icon for the current condition"},
        {"{weather_condition}",   "Conditions", "weather description for the selected city"},
        {"{vrchat_status}",        "VRChat status", "selected read-only status fields"},
        {"{lyric}",                "Lyric", "current synced lyric line"},
        {"{heart_icon}",       "♥ icon",   "the heart icon you set"},
        {"{song_icon}",        "♪ icon",   "the note icon you set"},
        {"{clock}",            "Time",     "local time, e.g. 21:07"},
        {"{clock_date}",       "Date",     "local date, e.g. 2026-11-21"},
        {"{clock_icon}",       "⌚ icon",  "the clock icon you set"},
        {"{custom}",           "Note",    "your own text line"},
        {"{custom_icon}",      "✎ icon",  "the note icon you set"},
        {"{battery}",          "Battery", "battery level, e.g. 87%"},
        {"{battery_state}",    "Charging","charging / discharging / full"},
        {"{battery_icon}",     "⚡ icon",  "the battery icon you set"},
        {"{cpu_percent}",      "CPU",     "CPU utilization as a percentage"},
        {"{memory_percent}",   "RAM",     "memory utilization as a percentage"},
        {"{separator}",        "Separator","the song/artist separator you set"},
        {"{nl}",               "Newline",  "start a new chatbox line"},
    };
    return catalog;
}

const std::vector<PresetInfo>& template_presets() {
    static const std::vector<PresetInfo> presets = {
        {"Heart, then song",
         "{if:heart_rate}{heart_icon} {heart_rate_label}{end}\n"
         "{if:song}{song_icon} {song}{if:artist}{separator}{artist}{end}{end}"},
        {"One line",
         "{heart_icon} {heart_rate_label}{separator}{song}"},
        {"Heart only",
         "{heart_icon} {heart_rate_label}"},
        {"Song only",
         "{song_icon} {song}{separator}{artist}"},
        {"With session stats",
         "{if:heart_rate}{heart_icon} {heart_rate_label}  (min {bpm_min} · avg {bpm_avg} · max {bpm_max}){end}\n"
         "{if:song}{song_icon} {song}{if:artist}{separator}{artist}{end}{end}"},
        {"With clock",
         "{if:clock}{clock_icon} {clock}{end}\n"
         "{if:heart_rate}{heart_icon} {heart_rate_label}{end}\n"
         "{if:song}{song_icon} {song}{if:artist}{separator}{artist}{end}{end}"},
        {"Note + battery",
         "{if:custom}{custom_icon} {custom}{end}\n"
         "{if:battery}{battery_icon} {battery}{end}"},
    };
    return presets;
}

} // namespace cadence
