#pragma once
#include "battery_source.hpp"
#include "clock_source.hpp"
#include "heart_stats.hpp"
#include "media_reader.hpp"
#include "settings.hpp"
#include "system_source.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace cadence {

// everything a template can interpolate. either side may be absent - a missing
// heart session or a missing track is a normal state, not an error
struct TemplateData {
    const HeartStats* hr = nullptr;
    const TrackInfo* track = nullptr;
    const ClockInfo* clock = nullptr;
    const BatteryInfo* battery = nullptr;
    const SystemInfo* system = nullptr;
    std::string timer;
    const WeatherDisplay* weather = nullptr;
    const VrchatStatus* vrchat = nullptr;
    std::string lyric;
};

// a token the user can drop into their template
struct TokenInfo {
    const char* token;  // inserted verbatim, e.g. {bpm}
    const char* label;  // short caption for the button
    const char* help;   // tooltip
};

// ready-made starting points so nobody has to write a template from scratch
struct PresetInfo {
    const char* name;
    const char* tmpl;
};

// live metrics for the outgoing message, used to warn about vrchat's limits
struct ChatboxStats {
    int chars = 0;
    int lines = 0;
    bool over_limit = false;
};

// the template implied by the component layout + per-field toggles. this is
// generated rather than typed, so the ui only has to offer reorder + toggles.
std::string component_template(const AppSettings& st);

// expands tmpl against data. unknown tokens render as empty rather than
// erroring, so a typo degrades quietly instead of blanking the chatbox
std::string render_template(const std::string& tmpl, const TemplateData& data, const AppSettings& st);

// the real entry point: honours a raw template override if one is set,
// otherwise renders the component layout
std::string render_chatbox(const TemplateData& data, const AppSettings& st);
std::string format_lyric(std::string_view lyric, const AppSettings& st);

// one component rendered in isolation, for the layout cards' live samples
std::string render_component(int component, const TemplateData& data, const AppSettings& st);

ChatboxStats stats_for(const std::string& text);

std::string default_template();
const std::vector<TokenInfo>& token_catalog();
const std::vector<PresetInfo>& template_presets();

} // namespace cadence
