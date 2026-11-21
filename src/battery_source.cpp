#include "battery_source.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace cadence {

BatteryInfo current_battery() {
    BatteryInfo out;
    std::error_code ec;
    const std::filesystem::path root("/sys/class/power_supply");
    if (!std::filesystem::is_directory(root, ec)) return out;

    for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const auto& entry = *it;
        std::string type, scope;
        std::ifstream(entry.path() / "type") >> type;
        std::ifstream(entry.path() / "scope") >> scope;
        if (type != "Battery" || scope == "Device") continue;

        std::ifstream cap(entry.path() / "capacity");
        int pct = -1;
        if (!cap || !(cap >> pct) || pct < 0 || pct > 100) continue;

        out.present = true;
        out.percent = pct;

        std::ifstream status(entry.path() / "status");
        if (status) std::getline(status, out.state);
        std::transform(out.state.begin(), out.state.end(), out.state.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        break;
    }
    return out;
}

} // namespace cadence
