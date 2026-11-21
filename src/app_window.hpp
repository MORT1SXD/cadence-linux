#pragma once
#include "clock_source.hpp"
#include "formatter.hpp"
#include "discord_presence.hpp"
#include "media_reader.hpp"
#include "osc_sender.hpp"
#include "pulsoid_client.hpp"
#include "settings.hpp"
#include "session_timer.hpp"
#include <adwaita.h>
#include <atomic>
#include <gtk/gtk.h>
#include <limits>
#include <optional>
#include <string>

namespace cadence {

class AppWindow {
public:
    explicit AppWindow(AdwApplication* app);

private:
    GtkWidget* window_ = nullptr;
    GtkWidget* preview_label_ = nullptr;
    GtkWidget* preview_revealer_ = nullptr;
    GtkWidget* stats_label_ = nullptr;
    GtkWidget* pause_button_ = nullptr;
    GtkWidget* copy_button_ = nullptr;
    GtkWidget* send_once_button_ = nullptr;
    GtkWidget* timer_action_ = nullptr;
    GtkWidget* timer_status_ = nullptr;
    GtkWidget* pulsoid_pill_ = nullptr;
    GtkWidget* osc_pill_ = nullptr;
    GtkWidget* pulsoid_status_row_ = nullptr;
    GtkWidget* connect_btn_ = nullptr;
    GtkWidget* disconnect_btn_ = nullptr;
    GtkWidget* toast_overlay_ = nullptr;
    GtkWidget* discord_status_row_ = nullptr;
    GtkWidget* discord_toggle_row_ = nullptr;
    GtkWidget* discord_music_row_ = nullptr;
    GtkWidget* discord_hr_row_ = nullptr;
    DiscordPresence discord_;

    // set while writing settings back into widgets, so the handler that opened
    // the privacy dialog isn't re-entered by the update it triggers
    bool updating_ui_ = false;

    GtkWidget* component_list_ = nullptr;
    struct SourceRow {
        GtkWidget* row = nullptr;
        GtkWidget* up = nullptr;
        GtkWidget* down = nullptr;
        bool AppSettings::*enabled = nullptr;
    };
    SourceRow sources_[COMP_COUNT]{};

    // last snapshot, so the layout cards can show live samples without each one
    // going back out to d-bus for the track
    HeartStats last_hr_{};
    std::optional<TrackInfo> last_track_;
    ClockInfo last_clock_;
    BatteryInfo last_battery_;
    SystemSource system_source_;
    SystemInfo last_system_;
    std::string preview_text_;
    SessionTimer session_timer_;
    gint64 last_osc_send_ = 0;

    AppSettings settings_;
    OscSender osc_sender_;
    PulsoidClient pulsoid_;
    MediaReader media_;

    std::atomic<int> latest_bpm_{0};
    std::atomic<int> min_bpm_{std::numeric_limits<int>::max()};
    std::atomic<int> max_bpm_{0};
    std::atomic<long long> sum_bpm_{0};
    std::atomic<long long> count_bpm_{0};

    // ring of recent raw samples, written from pulsoid's thread and averaged on
    // the main thread. kept as atomics rather than a deque because the producer
    // is a different thread and a lock here would be pointless at this rate
    static constexpr int kRingSize = 8;
    std::atomic<int> ring_[kRingSize]{};
    std::atomic<int> ring_pos_{0};
    std::atomic<int> ring_count_{0};

    guint tick_id_ = 0;

    GtkWidget* build_preview_card();
    GtkWidget* build_chatbox_page();
    void add_pulsoid_connection(AdwPreferencesPage* page);
    GtkWidget* build_connect_page();
    GtkWidget* build_settings_menu();

    void create_source_row(int component);
    void move_component(int component, int direction);
    void update_move_buttons();

    void refresh_preview(bool send = false);
    SessionTimer::Seconds timer_limit() const;
    void send_message(const std::string& text, bool manual = false);
    void apply_animation_setting();
    void restart_timer();
    void reset_session_stats();
    int smoothed_bpm() const;
    void show_discord_warning();
    void sync_discord_ui();
    void update_discord();
    void connect_pulsoid(const std::string& token);
    void disconnect_pulsoid();
    void on_pulsoid_status(const std::string& status);

    static gboolean on_tick(gpointer user_data);
};

} // namespace cadence
