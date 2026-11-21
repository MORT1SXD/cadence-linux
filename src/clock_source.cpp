#include "clock_source.hpp"

#include <glib.h>

namespace cadence {

ClockInfo current_clock(const AppSettings& st) {
    ClockInfo out;
    GDateTime* now = g_date_time_new_now_local();
    if (!now) return out;

    const char* fmt = st.clock_24h
        ? (st.clock_show_seconds ? "%H:%M:%S" : "%H:%M")
        : (st.clock_show_seconds ? "%I:%M:%S %p" : "%I:%M %p");

    if (gchar* t = g_date_time_format(now, fmt)) {
        out.time = t;
        g_free(t);
    }
    if (st.clock_show_date) {
        if (gchar* d = g_date_time_format(now, "%Y-%m-%d")) {
            out.date = d;
            g_free(d);
        }
    }

    g_date_time_unref(now);
    return out;
}

} // namespace cadence
