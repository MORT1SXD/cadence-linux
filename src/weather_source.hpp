#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cadence {

struct WeatherPlace {
    std::string label;
    double latitude = 0;
    double longitude = 0;

    bool valid() const;
};

struct WeatherReading {
    double celsius;
    int code;
    bool daytime;
};

struct WeatherDisplay {
    std::string temperature;
    std::string icon;
    std::string condition;
};

enum class WeatherUnits { Celsius, Fahrenheit, Alternate };

WeatherDisplay display_weather(const WeatherReading& reading, WeatherUnits units, bool alternate_phase);

// Completion callbacks run on the GLib main context. Neither entrypoint does
// location detection: search requires typed text; a forecast requires a place.
using WeatherPlacesCallback = std::function<void(std::vector<WeatherPlace>, std::string)>;
using WeatherReadingCallback = std::function<void(std::optional<WeatherReading>, std::string)>;
void search_weather_city(const std::string& query, WeatherPlacesCallback done);
void fetch_weather(const WeatherPlace& place, WeatherReadingCallback done);

} // namespace cadence
