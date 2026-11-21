#pragma once
#include "settings.hpp"
#include <string>

namespace cadence {

// What the clock block can contribute right now. Both fields are empty when
// the relevant setting is off, which makes {if:...} skip them cleanly.
struct ClockInfo {
    std::string time;
    std::string date;
};

// Reads the local system clock. No network, no external service, no VRChat
// inspection - the only input is the user's own machine time.
ClockInfo current_clock(const AppSettings& st);

} // namespace cadence
