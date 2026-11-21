#include "lyrics_source.hpp"
#include "system_ca.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <curl/curl.h>
#include <json-glib/json-glib.h>
#include <memory>
#include <optional>
#include <utility>

namespace cadence {

namespace {

constexpr size_t kMaxLrcBytes = 256 * 1024;
constexpr size_t kMaxResponseBytes = 512 * 1024;
constexpr size_t kMaxLines = 2048;

std::optional<int> integer(std::string_view text) {
    if (text.empty()) return {};
    int number;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (error != std::errc{} || end != text.data() + text.size()) return {};
    return number;
}

std::optional<std::int64_t> timestamp(std::string_view value) {
    const auto colon = value.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon > 3) return {};
    auto minutes = integer(value.substr(0, colon));
    value.remove_prefix(colon + 1);
    const auto dot = value.find('.');
    auto seconds = integer(value.substr(0, dot));
    if (!minutes || *minutes < 0 || !seconds || *seconds < 0 || *seconds >= 60) return {};

    int fraction = 0;
    if (dot != std::string_view::npos) {
        auto digits = value.substr(dot + 1);
        if (digits.empty() || digits.size() > 3) return {};
        auto parsed = integer(digits);
        if (!parsed || *parsed < 0) return {};
        fraction = *parsed * (digits.size() == 1 ? 100 : digits.size() == 2 ? 10 : 1);
    }
    return (static_cast<std::int64_t>(*minutes) * 60 + *seconds) * 1000 + fraction;
}

std::string clean_line(std::string_view text) {
    std::string clean;
    while (!text.empty() && clean.size() < 4096) {
        if (text.front() == '<') {
            const auto close = text.substr(0, 12).find('>');
            if (close != std::string_view::npos && timestamp(text.substr(1, close - 1))) {
                text.remove_prefix(close + 1); // Ignore enhanced/word timing.
                continue;
            }
        }
        const auto byte = static_cast<unsigned char>(text.front());
        if (byte >= 32 && byte != 127) clean += text.front();
        else if (byte == '\t') clean += ' ';
        text.remove_prefix(1);
    }
    const auto first = clean.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    clean = clean.substr(first, clean.find_last_not_of(' ') - first + 1);
    gchar* valid = g_utf8_make_valid(clean.data(), clean.size());
    std::string result;
    for (const gchar* p = valid; *p;) {
        const auto symbol = g_utf8_get_char(p);
        const auto* next = g_utf8_next_char(p);
        if (symbol == 0x85 || symbol == 0x2028 || symbol == 0x2029) result += ' ';
        else if (!g_unichar_iscntrl(symbol)) result.append(p, next - p);
        p = next;
    }
    g_free(valid);
    return result;
}

std::string folded(std::string_view name) {
    if (!g_utf8_validate(name.data(), name.size(), nullptr)) return {};
    gchar* lower = g_utf8_casefold(name.data(), name.size());
    gchar* normalized = g_utf8_normalize(lower, -1, G_NORMALIZE_NFC);
    std::string result(normalized ? normalized : lower);
    g_free(normalized);
    g_free(lower);
    return result;
}

std::string escape(const std::string& text) {
    gchar* encoded = g_uri_escape_string(text.c_str(), nullptr, FALSE);
    std::string result(encoded);
    g_free(encoded);
    return result;
}

size_t append_response(char* data, size_t size, size_t count, void* userdata) {
    auto& body = *static_cast<std::string*>(userdata);
    if (size && count > (kMaxResponseBytes - body.size()) / size) return 0;
    const auto bytes = size * count;
    body.append(data, bytes);
    return bytes;
}

std::string json_text(JsonObject* root, const char* name) {
    auto* node = json_object_get_member(root, name);
    if (!node || json_node_get_value_type(node) != G_TYPE_STRING) return {};
    const auto* value = json_node_get_string(node);
    return value ? value : "";
}

LyricsResult load_online(const LyricsQuery& query, GCancellable* cancellable) {
    if (query.duration_seconds < 1 || query.duration_seconds > 3600)
        return {{}, "Online lookup needs a track length of 1–3600 seconds"};
    static const CURLcode init = curl_global_init(CURL_GLOBAL_DEFAULT);
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(
        init == CURLE_OK ? curl_easy_init() : nullptr, &curl_easy_cleanup);
    if (!curl) return {{}, "Could not start the lyrics lookup"};

    std::string url = "https://lrclib.net/api/get?track_name=" + escape(query.title) +
        "&artist_name=" + escape(query.artist) + "&duration=" + std::to_string(query.duration_seconds);
    if (!query.album.empty()) url += "&album_name=" + escape(query.album);
    std::string body;
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "Cadence/1.4 (https://github.com/MORT1SXD/cadence-linux)");
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, &append_response);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, cancellable);
    curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION,
        +[](void* data, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
            return g_cancellable_is_cancelled(G_CANCELLABLE(data)) ? 1 : 0;
        });
    if (const char* ca = host_ca_bundle()) curl_easy_setopt(curl.get(), CURLOPT_CAINFO, ca);
    if (g_cancellable_is_cancelled(cancellable)) return {};
    const auto response = curl_easy_perform(curl.get());
    if (response == CURLE_ABORTED_BY_CALLBACK) return {};
    if (response != CURLE_OK) return {{}, "Could not reach LRCLIB"};
    long code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &code);
    if (code == 429 || code == 503) {
        curl_off_t retry_after = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RETRY_AFTER, &retry_after);
        return {{}, "LRCLIB asked us to wait", retry_after > 0 ? retry_after : 60};
    }
    if (code == 404) return {{}, "No synced lyrics matched this song", 0, true};
    if (code != 200) return {{}, "LRCLIB returned HTTP " + std::to_string(code)};

    std::unique_ptr<JsonParser, decltype(&g_object_unref)> parser(json_parser_new(), &g_object_unref);
    if (!json_parser_load_from_data(parser.get(), body.data(), body.size(), nullptr))
        return {{}, "Could not read the lyrics response"};
    auto* root = json_parser_get_root(parser.get());
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) return {{}, "Could not read the lyrics response"};
    auto* track = json_node_get_object(root);
    auto* duration = json_object_get_member(track, "duration");
    const auto type = duration ? json_node_get_value_type(duration) : G_TYPE_INVALID;
    const double seconds = type == G_TYPE_DOUBLE || type == G_TYPE_INT64 ? json_node_get_double(duration) : -1;
    if (!std::isfinite(seconds) || std::abs(seconds - query.duration_seconds) > 2 ||
        folded(json_text(track, "trackName")) != folded(query.title) ||
        folded(json_text(track, "artistName")) != folded(query.artist)) {
        return {{}, "No reliable lyrics match for this recording", 0, true};
    }
    const auto album = json_text(track, "albumName");
    if (!query.album.empty() && !album.empty() && folded(album) != folded(query.album))
        return {{}, "No reliable lyrics match for this recording", 0, true};

    auto lines = parse_lrc(json_text(track, "syncedLyrics"));
    if (lines.empty()) return {{}, "No synced lyrics matched this song", 0, true};
    return {std::move(lines), "Synced lyrics from LRCLIB", 0, true};
}

} // namespace

std::vector<LyricLine> parse_lrc(std::string_view text) {
    if (text.size() > kMaxLrcBytes) return {};
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    std::vector<LyricLine> lines;
    size_t lyric_bytes = 0;
    int offset = 0;
    while (!text.empty() && lines.size() < kMaxLines) {
        const auto end = text.find_first_of("\r\n");
        auto line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        if (line.starts_with("[offset:") && line.ends_with(']')) {
            auto value = line.substr(8, line.size() - 9);
            if (value.starts_with('+')) value.remove_prefix(1);
            offset = std::clamp(integer(value).value_or(0), -600000, 600000);
            continue;
        }
        const auto first = lines.size();
        while (line.starts_with('[') && lines.size() < kMaxLines) {
            const auto close = line.find(']');
            if (close == std::string_view::npos || close > 11) break;
            const auto at = timestamp(line.substr(1, close - 1));
            if (!at) break;
            lines.push_back({*at, {}});
            line.remove_prefix(close + 1);
        }
        if (first == lines.size()) continue;
        const auto words = clean_line(line);
        lyric_bytes += (lines.size() - first) * words.size();
        if (lyric_bytes > kMaxLrcBytes) return {};
        for (size_t i = first; i < lines.size(); ++i) lines[i].text = words;
    }
    if (std::none_of(lines.begin(), lines.end(), [](const auto& line) { return !line.text.empty(); }))
        return {};
    for (auto& line : lines) line.milliseconds -= offset;
    std::stable_sort(lines.begin(), lines.end(), [](const auto& a, const auto& b) {
        return a.milliseconds < b.milliseconds;
    });
    return lines;
}

std::string_view lyric_at(const std::vector<LyricLine>& lines, std::int64_t milliseconds) {
    auto next = std::upper_bound(lines.begin(), lines.end(), milliseconds,
        [](auto at, const LyricLine& line) { return at < line.milliseconds; });
    if (next == lines.begin()) return {};
    const auto& current = *std::prev(next);
    if (next == lines.end() && milliseconds - current.milliseconds > 10000) return {};
    return current.text;
}

void lookup_synced_lyrics(LyricsQuery query, GCancellable* cancellable, LyricsCallback done) {
    struct Work { LyricsQuery query; LyricsCallback done; };
    GTask* task = g_task_new(nullptr, cancellable, +[](GObject*, GAsyncResult* result, gpointer) {
        auto* task = G_TASK(result);
        auto* work = static_cast<Work*>(g_task_get_task_data(task));
        std::unique_ptr<LyricsResult> response(static_cast<LyricsResult*>(g_task_propagate_pointer(task, nullptr)));
        work->done(response ? std::move(*response) : LyricsResult{});
    }, nullptr);
    g_task_set_check_cancellable(task, FALSE);
    g_task_set_task_data(task, new Work{std::move(query), std::move(done)},
        +[](gpointer data) { delete static_cast<Work*>(data); });
    g_task_run_in_thread(task, +[](GTask* task, gpointer, gpointer data, GCancellable* cancellable) {
        const auto& query = static_cast<Work*>(data)->query;
        auto result = std::make_unique<LyricsResult>();
        if (!g_cancellable_is_cancelled(cancellable)) *result = load_online(query, cancellable);
        g_task_return_pointer(task, result.release(), +[](gpointer p) { delete static_cast<LyricsResult*>(p); });
    });
    g_object_unref(task);
}

} // namespace cadence
