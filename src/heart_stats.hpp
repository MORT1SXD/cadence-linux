#pragma once

namespace cadence {

// a snapshot of heart rate readings collected during this session
struct HeartStats {
    bool has_data = false;
    int current = 0;
    int min = 0;
    int max = 0;
    double avg = 0.0;
};

} // namespace cadence
