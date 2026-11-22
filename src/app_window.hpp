#pragma once
#include "clock_source.hpp"
#include "formatter.hpp"
#include "lyrics_source.hpp"
#include "media_reader.hpp"
#include "osc_sender.hpp"
#include "pulsoid_client.hpp"
#include "settings.hpp"
#include "session_timer.hpp"
#include "tray.hpp"
#include <adwaita.h>
#include <atomic>
#include <deque>
#include <gtk/gtk.h>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cadence {

class AppWindow {
public:
    explicit AppWindow(AdwApplication* app);
    void present(const std::string& startup_id = {});

private:
    AdwApplication* app_;
    GtkWidget* window_ = nullptr;
    GtkWidget* preview_label_ = nullptr;
    GtkWidget* stats_label_ = nullptr;
    GtkWidget* pause_button_ = nullptr;
    GtkWidget* copy_button_ = nullptr;
    GtkWidget* send_once_button_ = nullptr;
    AdwBanner* status_banner_ = nullptr;
    AdwPreferencesGroup* status_group_ = nullptr;
    GtkWidget* add_status_button_ = nullptr;
    GtkWidget* empty_status_row_ = nullptr;
    std::vector<GtkWidget*> status_rows_;
    std::optional<size_t> active_status_; // Session-only; never restored from settings.
    bool syncing_status_ = false;
    GtkWidget* timer_action_ = nullptr;
    GtkWidget* timer_status_ = nullptr;
    GtkWidget* weather_city_row_ = nullptr;
    GtkWidget* weather_results_row_ = nullptr;
    GtkWidget* weather_clear_button_ = nullptr;
    GtkWidget* vrchat_listener_row_ = nullptr;
    GtkWidget* vrchat_retry_button_ = nullptr;
    GtkWidget* lyrics_preview_row_ = nullptr;
    std::vector<WeatherPlace> weather_results_;
    std::optional<WeatherReading> weather_reading_;
    std::string weather_error_;
    bool weather_loading_ = false;
    unsigned weather_generation_ = 0;
    gint64 weather_next_request_ = 0;
    gint64 weather_last_success_ = 0;
    gint64 weather_cycle_start_ = 0;
    GtkWidget* pulsoid_pill_ = nullptr;
    GtkWidget* osc_pill_ = nullptr;
    GtkWidget* pulsoid_status_row_ = nullptr;
    GtkWidget* connect_btn_ = nullptr;
    GtkWidget* disconnect_btn_ = nullptr;
    GtkWidget* toast_overlay_ = nullptr;
    bool update_running_ = false;
    AdwToast* update_toast_ = nullptr;
    guint update_toast_expiry_ = 0;
    std::shared_ptr<Tray> tray_;
    GtkWidget* close_behavior_button_ = nullptr;
    AdwAlertDialog* close_dialog_ = nullptr;
    bool held_in_tray_ = false;
    bool quitting_ = false;

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
    struct LyricsCacheEntry {
        LyricsQuery query;
        LyricsResult result;
        gint64 expires;
    };
    std::deque<LyricsCacheEntry> lyrics_cache_;
    std::optional<LyricsQuery> lyrics_query_;
    LyricsResult lyrics_result_;
    GCancellable* lyrics_job_ = nullptr;
    bool lyrics_attempted_ = false;
    gint64 lyrics_retry_at_ = 0;
    gint64 lyrics_online_after_ = 0;
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
    VrchatStatusListener vrchat_listener_;

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
    GtkWidget* build_status_page();
    void rebuild_status_rows();
    void sync_status_controls();
    void set_active_status(std::optional<size_t> index);
    void edit_status(std::optional<size_t> index);
    void move_status(size_t from, size_t to);
    void confirm_delete_status(size_t index);
    void add_pulsoid_connection(AdwPreferencesPage* page);
    GtkWidget* build_connect_page();
    GtkWidget* build_settings_menu();
    void set_sending(bool sending);
    gboolean request_close();
    void hide_to_tray();
    void quit();
    void prepare_to_quit();
    void tray_availability_changed(bool available);
    void refresh_close_dialog();
    void edit_close_behavior();
    void confirm_appimage_update(std::string path, std::string installer);
    void run_appimage_update(const std::string& path, const std::string& installer);
    void show_update_toast(const char* title, guint timeout = 4);

    void create_source_row(int component);
    void add_weather_settings(AdwExpanderRow* row);
    void add_lyrics_settings(AdwExpanderRow* row);
    void reset_lyrics();
    void refresh_lyrics(gint64 now);
    void reset_weather();
    void refresh_weather(gint64 now);
    void reset_vrchat_listener();
    void move_component(int component, int direction);
    void update_move_buttons();

    void refresh_preview(bool send = false);
    SessionTimer::Seconds timer_limit() const;
    void send_message(const std::string& text, bool manual = false);
    void restart_timer();
    void reset_session_stats();
    int smoothed_bpm() const;
    void connect_pulsoid(const std::string& token);
    void disconnect_pulsoid();
    void on_pulsoid_status(const std::string& status);

    static gboolean on_tick(gpointer user_data);
};

} // namespace cadence
