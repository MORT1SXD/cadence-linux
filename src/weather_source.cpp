#include "weather_source.hpp"
#include "system_ca.hpp"

#include <cmath>
#include <curl/curl.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <limits>
#include <memory>
#include <utility>

namespace cadence {

bool WeatherPlace::valid() const {
    return !label.empty() && std::isfinite(latitude) && std::isfinite(longitude) &&
           latitude >= -90 && latitude <= 90 && longitude >= -180 && longitude <= 180;
}

namespace {

struct Condition {
    const char* text;
    const char* icon;
};

Condition describe(int code, bool day) {
    switch (code) {
        case 0: return day ? Condition{"Sunny", "☀️"} : Condition{"Clear", "🌙"};
        case 1: return day ? Condition{"Mostly sunny", "🌤️"} : Condition{"Mostly clear", "🌙"};
        case 2: return {"Partly cloudy", day ? "⛅" : "☁️"};
        case 3: return {"Overcast", "☁️"};
        case 45: case 48: return {"Fog", "🌫️"};
        case 51: case 53: case 55: return {"Drizzle", "🌦️"};
        case 56: case 57: return {"Freezing drizzle", "🌧️"};
        case 61: case 63: case 65: return {"Rain", "🌧️"};
        case 66: case 67: return {"Freezing rain", "🌧️"};
        case 71: case 73: case 75: case 77: return {"Snow", "❄️"};
        case 80: case 81: case 82: return {"Rain showers", "🌧️"};
        case 85: case 86: return {"Snow showers", "🌨️"};
        case 95: return {"Thunderstorms", "⛈️"};
        case 96: case 99: return {"Thunderstorms with hail", "⛈️"};
        default: return {"", ""};
    }
}

constexpr size_t kResponseLimit = 128 * 1024;

size_t append_response(char* data, size_t size, size_t count, void* userdata) {
    auto& body = *static_cast<std::string*>(userdata);
    if (size && count > (kResponseLimit - body.size()) / size) return 0;
    const size_t bytes = size * count;
    body.append(data, bytes);
    return bytes;
}

struct Response {
    std::string body;
    std::string error;
};

// Run curl on GLib's worker pool, not the GTK thread.
void request(std::string url, std::function<void(Response)> done) {
    struct Request {
        std::string url;
        std::function<void(Response)> done;
    };
    GTask* task = g_task_new(nullptr, nullptr, +[](GObject*, GAsyncResult* result, gpointer) {
        auto* task = G_TASK(result);
        auto* work = static_cast<Request*>(g_task_get_task_data(task));
        std::unique_ptr<Response> response(static_cast<Response*>(g_task_propagate_pointer(task, nullptr)));
        work->done(std::move(*response));
    }, nullptr);
    g_task_set_task_data(task, new Request{std::move(url), std::move(done)},
        +[](gpointer data) { delete static_cast<Request*>(data); });
    g_task_run_in_thread(task, +[](GTask* task, gpointer, gpointer data, GCancellable*) {
        const auto& work = *static_cast<Request*>(data);
        auto result = std::make_unique<Response>();
        static const CURLcode init = curl_global_init(CURL_GLOBAL_DEFAULT);
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(
            init == CURLE_OK ? curl_easy_init() : nullptr, &curl_easy_cleanup);
        if (!curl) {
            result->error = "Could not start the weather request";
        } else {
            curl_easy_setopt(curl.get(), CURLOPT_URL, work.url.c_str());
            curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
            curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 5L);
            curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 12L);
            curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "Cadence");
            curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, &append_response);
            curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &result->body);
            if (const char* ca = host_ca_bundle()) curl_easy_setopt(curl.get(), CURLOPT_CAINFO, ca);
            const auto status = curl_easy_perform(curl.get());
            long code = 0;
            curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &code);
            if (status == CURLE_PEER_FAILED_VERIFICATION || status == CURLE_SSL_CACERT_BADFILE)
                result->error = "Weather certificate verification failed";
            else if (status != CURLE_OK)
                result->error = "Could not reach Open-Meteo";
            else if (code == 429)
                result->error = "Weather service is rate limited; try again later";
            else if (code != 200)
                result->error = "Weather service returned HTTP " + std::to_string(code);
        }
        g_task_return_pointer(task, result.release(), +[](gpointer p) { delete static_cast<Response*>(p); });
    });
    g_object_unref(task);
}

JsonObject* parse_object(JsonParser* parser, const std::string& body) {
    if (!json_parser_load_from_data(parser, body.data(), body.size(), nullptr)) return nullptr;
    JsonNode* root = json_parser_get_root(parser);
    return root && JSON_NODE_HOLDS_OBJECT(root) ? json_node_get_object(root) : nullptr;
}

std::string text(JsonObject* object, const char* key) {
    JsonNode* node = json_object_get_member(object, key);
    if (!node || json_node_get_value_type(node) != G_TYPE_STRING) return {};
    const auto* value = json_node_get_string(node);
    return value ? value : "";
}

double number(JsonObject* object, const char* key) {
    JsonNode* node = json_object_get_member(object, key);
    if (node && JSON_NODE_HOLDS_VALUE(node)) {
        const auto type = json_node_get_value_type(node);
        if (type == G_TYPE_INT64 || type == G_TYPE_DOUBLE) return json_node_get_double(node);
    }
    return std::numeric_limits<double>::quiet_NaN();
}

} // namespace

WeatherDisplay display_weather(const WeatherReading& reading, WeatherUnits units, bool alternate_phase) {
    const bool fahrenheit = units == WeatherUnits::Fahrenheit ||
                             (units == WeatherUnits::Alternate && alternate_phase);
    const auto temp = std::lround(fahrenheit ? reading.celsius * 9.0 / 5.0 + 32.0 : reading.celsius);
    const auto status = describe(reading.code, reading.daytime);
    return {std::to_string(temp) + (fahrenheit ? "°F" : "°C"), status.icon, status.text};
}

void search_weather_city(const std::string& query, WeatherPlacesCallback done) {
    if (query.empty()) {
        done({}, "Enter a city name");
        return;
    }
    gchar* escaped = g_uri_escape_string(query.c_str(), nullptr, FALSE);
    std::string url = std::string("https://geocoding-api.open-meteo.com/v1/search?count=8&language=en&name=") + escaped;
    g_free(escaped);
    request(std::move(url), [done = std::move(done)](Response response) {
        if (!response.error.empty()) { done({}, std::move(response.error)); return; }
        std::unique_ptr<JsonParser, decltype(&g_object_unref)> parser(json_parser_new(), &g_object_unref);
        JsonObject* root = parse_object(parser.get(), response.body);
        if (!root) { done({}, "Could not read city results"); return; }
        JsonNode* results = json_object_get_member(root, "results");
        if (!results || !JSON_NODE_HOLDS_ARRAY(results)) { done({}, {}); return; }
        JsonArray* array = json_node_get_array(results);
        std::vector<WeatherPlace> places;
        for (guint i = 0; i < json_array_get_length(array); ++i) {
            JsonNode* node = json_array_get_element(array, i);
            if (!node || !JSON_NODE_HOLDS_OBJECT(node)) continue;
            auto* place = json_node_get_object(node);
            const auto name = text(place, "name");
            const auto region = text(place, "admin1");
            const auto country = text(place, "country");
            if (name.empty()) continue;
            auto label = name;
            if (!region.empty() && region != name) label += ", " + region;
            if (!country.empty() && country != region && country != name) label += ", " + country;
            WeatherPlace city{std::move(label), number(place, "latitude"), number(place, "longitude")};
            if (city.valid()) places.push_back(std::move(city));
        }
        done(std::move(places), {});
    });
}

void fetch_weather(const WeatherPlace& place, WeatherReadingCallback done) {
    if (!place.valid()) { done({}, "Choose a city first"); return; }
    char latitude[G_ASCII_DTOSTR_BUF_SIZE], longitude[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_formatd(latitude, sizeof latitude, "%.6f", place.latitude);
    g_ascii_formatd(longitude, sizeof longitude, "%.6f", place.longitude);
    std::string url = std::string("https://api.open-meteo.com/v1/forecast?latitude=") + latitude +
        "&longitude=" + longitude + "&current=temperature_2m,weather_code,is_day&temperature_unit=celsius";
    request(std::move(url), [done = std::move(done)](Response response) {
        if (!response.error.empty()) { done({}, std::move(response.error)); return; }
        std::unique_ptr<JsonParser, decltype(&g_object_unref)> parser(json_parser_new(), &g_object_unref);
        JsonObject* root = parse_object(parser.get(), response.body);
        JsonNode* node = root ? json_object_get_member(root, "current") : nullptr;
        if (!node || !JSON_NODE_HOLDS_OBJECT(node)) { done({}, "No current weather available"); return; }
        JsonObject* current = json_node_get_object(node);
        const double temp = number(current, "temperature_2m");
        const double code = number(current, "weather_code");
        const double day = number(current, "is_day");
        if (!std::isfinite(temp) || temp < -150 || temp > 100) {
            done({}, "No temperature available");
            return;
        }
        const int weather_code = code >= 0 && code <= 99 && code == std::floor(code)
            ? static_cast<int>(code) : -1;
        done(WeatherReading{temp, weather_code, day != 0}, {});
    });
}

} // namespace cadence
