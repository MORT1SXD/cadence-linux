#include "system_source.hpp"

#include <array>
#include <cmath>
#include <fstream>
#include <numeric>
#include <sstream>
#include <string>

namespace cadence {

namespace {

int percent(std::uint64_t part, std::uint64_t total) {
    return static_cast<int>(std::lround(100.0 * part / total));
}

std::optional<int> memory_usage() {
    std::ifstream file("/proc/meminfo");
    std::uint64_t total = 0;
    std::optional<std::uint64_t> available;
    for (std::string line; std::getline(file, line);) {
        std::istringstream field(line);
        std::string key;
        std::uint64_t value;
        if (!(field >> key >> value)) continue;
        if (key == "MemTotal:") total = value;
        if (key == "MemAvailable:") available = value;
        if (total && available) break;
    }
    if (!total || !available || *available > total) return {};
    return percent(total - *available, total);
}

} // namespace

SystemInfo SystemSource::sample() {
    SystemInfo info;
    info.memory_percent = memory_usage();
    std::ifstream file("/proc/stat");
    std::string line, label;
    std::getline(file, line);
    std::istringstream cpu(line);
    std::array<std::uint64_t, 8> fields{};
    cpu >> label;
    // Guest time is already counted in user/nice. Only read through steal.
    for (auto& value : fields) cpu >> value;
    if (label != "cpu" || !cpu) {
        previous_cpu_.reset();
        return info;
    }
    const CpuCounters current{
        std::accumulate(fields.begin(), fields.end(), std::uint64_t{0}),
        fields[3] + fields[4],
    };
    if (previous_cpu_ && current.total > previous_cpu_->total && current.idle >= previous_cpu_->idle) {
        const auto elapsed = current.total - previous_cpu_->total;
        const auto idle = current.idle - previous_cpu_->idle;
        if (idle <= elapsed) info.cpu_percent = percent(elapsed - idle, elapsed);
    }
    previous_cpu_ = current;
    return info;
}

} // namespace cadence
