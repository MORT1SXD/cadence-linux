#pragma once
#include <cstdint>
#include <optional>

namespace cadence {

struct SystemInfo {
    std::optional<int> cpu_percent;
    std::optional<int> memory_percent;
};

class SystemSource {
public:
    SystemInfo sample();

private:
    struct CpuCounters {
        std::uint64_t total;
        std::uint64_t idle;
    };
    std::optional<CpuCounters> previous_cpu_;
};

} // namespace cadence
