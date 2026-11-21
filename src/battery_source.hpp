#pragma once
#include <string>

namespace cadence {

// A snapshot of the machine's battery. present is false on desktops and when
// the kernel exposes no battery, so callers can hide the block cleanly.
struct BatteryInfo {
    bool present = false;
    int percent = 0;
    std::string state;
};

// Reads /sys/class/power_supply directly. No D-Bus service, no network, and no
// access to anything outside the user's own machine.
BatteryInfo current_battery();

} // namespace cadence
