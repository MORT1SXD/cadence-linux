#include "app_window.hpp"
#include "formatter.hpp"
#include "heart_stats.hpp"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <filesystem>
#include <memory>
#include <algorithm>
#include <utility>
#include <vector>

namespace cadence {

namespace {

// ---- signal bridges ------------------------------------------------------
// gtk/adw signals are C callbacks; these wrap them so the builders below read
// as plain "when this changes, do that"

void on_click(GtkWidget* b, std::function<void()> cb) {
    g_signal_connect_data(b, "clicked", G_CALLBACK(+[](GtkButton*, gpointer d) {
        (*static_cast<std::function<void()>*>(d))();
    }), new std::function<void()>(std::move(cb)),
       +[](gpointer d, GClosure*) { delete static_cast<std::function<void()>*>(d); },
       static_cast<GConnectFlags>(0));
}

void on_text(GtkWidget* e, std::function<void(const std::string&)> cb) {
    g_signal_connect_data(e, "changed", G_CALLBACK(+[](GtkEditable* ed, gpointer d) {
        const char* t = gtk_editable_get_text(ed);
        (*static_cast<std::function<void(const std::string&)>*>(d))(t ? t : "");
    }), new std::function<void(const std::string&)>(std::move(cb)),
        +[](gpointer d, GClosure*) { delete static_cast<std::function<void(const std::string&)>*>(d); },
        static_cast<GConnectFlags>(0));
}

GtkWidget* state_row(const char* title, const char* sub, bool active,
                     std::function<void(bool)> cb) {
    GtkWidget* row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (sub) adw_action_row_set_subtitle(ADW_ACTION_ROW(row), sub);
    adw_switch_row_set_active(ADW_SWITCH_ROW(row), active);
    g_signal_connect_data(row, "notify::active", G_CALLBACK(+[](AdwSwitchRow* r, GParamSpec*, gpointer d) {
         (*static_cast<std::function<void(bool)>*>(d))(adw_switch_row_get_active(r));
    }), new std::function<void(bool)>(std::move(cb)),
        +[](gpointer d, GClosure*) { delete static_cast<std::function<void(bool)>*>(d); },
        static_cast<GConnectFlags>(0));

    return row;
}

// a combo row from a fixed set of options. options are few and meaningful, so
// picking from a list beats typing a number
GtkWidget* combo_row(const char* title, const char* sub,
                     std::vector<std::string> options, int selected,
                     std::function<void(int)> on_changed) {
    GtkWidget* row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (sub) adw_action_row_set_subtitle(ADW_ACTION_ROW(row), sub);

    // GtkStringList copies the strings into its own GValues, so a local buffer
    // is fine - no need to keep these alive
    std::vector<const char*> raw;
    raw.reserve(options.size() + 1);
    for (const auto& o : options) raw.push_back(o.c_str());
    raw.push_back(nullptr);

    // model and selection are set before connecting, so seeding the current
    // value doesn't fire the handler and write the setting straight back
    adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(gtk_string_list_new(raw.data())));
    adw_combo_row_set_selected(ADW_COMBO_ROW(row), selected);

    g_signal_connect_data(row, "notify::selected",
        G_CALLBACK(+[](GObject* o, GParamSpec*, gpointer d) {
            (*static_cast<std::function<void(int)>*>(d))(
                adw_combo_row_get_selected(ADW_COMBO_ROW(o)));
        }), new std::function<void(int)>(std::move(on_changed)),
        +[](gpointer d, GClosure*) { delete static_cast<std::function<void(int)>*>(d); },
        static_cast<GConnectFlags>(0));
    return row;
}

int interval_index(int ms) { return ms == 2000 ? 1 : ms == 5000 ? 2 : 0; }
int smoothing_index(int n) { return n == 3 ? 1 : n == 5 ? 2 : n == 8 ? 3 : 0; }

GtkWidget* entry_row(const char* title, const std::string& text) {
    GtkWidget* row = adw_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    gtk_editable_set_text(GTK_EDITABLE(row), text.c_str());
    return row;
}

AdwPreferencesGroup* group(const char* title, const char* description = nullptr) {
    AdwPreferencesGroup* g = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    if (title) adw_preferences_group_set_title(g, title);
    if (description) adw_preferences_group_set_description(g, description);
    return g;
}

// Every group must be given a description. AdwPreferencesGroup always creates
// its description label, and when that label is left empty it renders as a
// small stray speck next to the group title. Passing "" here is what caused it.

// a numeric row built by hand rather than with AdwSpinRow, whose stepper arrows
// are theme-drawn and render as empty circles on themes that don't style
// GtkSpinButton. text-labelled buttons are legible everywhere, and the value
// stays typable.
GtkWidget* spin_row(const char* title, const char* sub,
                    int min, int max, int step, int value,
                    std::function<void(int)> on_change) {
    GtkWidget* row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (sub) adw_action_row_set_subtitle(ADW_ACTION_ROW(row), sub);

    auto current = std::make_shared<int>(value);
    auto clamp = [=](int v) { return v < min ? min : (v > max ? max : v); };

    GtkWidget* entry = gtk_entry_new();
    gtk_editable_set_width_chars(GTK_EDITABLE(entry), 6);
    gtk_entry_set_alignment(GTK_ENTRY(entry), 1.0);
    {
        char buf[16];
        snprintf(buf, sizeof buf, "%d", *current);
        gtk_editable_set_text(GTK_EDITABLE(entry), buf);
    }

    auto commit = [current, clamp, entry, on_change] {
        const char* t = gtk_editable_get_text(GTK_EDITABLE(entry));
        const int v = clamp(atoi(t ? t : "0"));
        const bool changed = v != *current;
        *current = v;
        if (changed) on_change(v);
    };

    g_signal_connect_data(entry, "activate", G_CALLBACK(+[](GtkEntry*, gpointer d) {
        (*static_cast<std::function<void()>*>(d))();
    }), new std::function<void()>(commit),
        +[](gpointer d, GClosure*) { delete static_cast<std::function<void()>*>(d); },
        static_cast<GConnectFlags>(0));

    GtkWidget* dec = gtk_button_new_with_label("\xE2\x88\x92");  // U+2212 MINUS SIGN
    GtkWidget* inc = gtk_button_new_with_label("+");
    gtk_widget_add_css_class(dec, "stepper");
    gtk_widget_add_css_class(inc, "stepper");
    on_click(dec, [current, clamp, entry, on_change, step] {
        const int v = clamp(*current - step);
        *current = v;
        char buf[16];
        snprintf(buf, sizeof buf, "%d", v);
        gtk_editable_set_text(GTK_EDITABLE(entry), buf);
        on_change(v);
    });
    on_click(inc, [current, clamp, entry, on_change, step] {
        const int v = clamp(*current + step);
        *current = v;
        char buf[16];
        snprintf(buf, sizeof buf, "%d", v);
        gtk_editable_set_text(GTK_EDITABLE(entry), buf);
        on_change(v);
    });

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), dec);
    gtk_box_append(GTK_BOX(box), entry);
    gtk_box_append(GTK_BOX(box), inc);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), box);
    return row;
}

} // namespace

AppWindow::AppWindow(AdwApplication* app)
    : settings_(AppSettings::load()),
      osc_sender_("127.0.0.1", static_cast<uint16_t>(settings_.osc_port)) {

    pulsoid_.set_token(settings_.pulsoid_token);
    pulsoid_.set_on_update([this](int bpm) {
        latest_bpm_ = bpm;

        // keep the raw sample for smoothing; the session stats below stay on raw
        // values so min/avg/max remain truthful
        ring_[ring_pos_.fetch_add(1) % kRingSize] = bpm;
        ring_count_ += 1;

        int prev_min = min_bpm_.load();
        while (bpm < prev_min && !min_bpm_.compare_exchange_weak(prev_min, bpm)) {}
        int prev_max = max_bpm_.load();
        while (bpm > prev_max && !max_bpm_.compare_exchange_weak(prev_max, bpm)) {}
        sum_bpm_ += bpm;
        count_bpm_ += 1;
    });
    pulsoid_.set_on_status([this](const std::string& status) {
        // status arrives on pulsoid's thread; hand it to the main loop
        auto* data = new std::pair<AppWindow*, std::string>(this, status);
        g_idle_add(+[](gpointer d) -> gboolean {
            auto* p = static_cast<std::pair<AppWindow*, std::string>*>(d);
            p->first->on_pulsoid_status(p->second);
            delete p;
            return G_SOURCE_REMOVE;
        }, data);
    });
    if (!settings_.pulsoid_token.empty()) pulsoid_.start();

    window_ = adw_application_window_new(GTK_APPLICATION(app));
    g_object_add_weak_pointer(G_OBJECT(window_), reinterpret_cast<gpointer*>(&window_));
    g_signal_connect(window_, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer data) -> gboolean {
        auto& self = *static_cast<AppWindow*>(data);
        self.vrchat_listener_.stop();
        if (self.lyrics_job_) g_cancellable_cancel(self.lyrics_job_);
        return FALSE;
    }), this);
    gtk_window_set_title(GTK_WINDOW(window_), "cadence");
    gtk_window_set_default_size(GTK_WINDOW(window_), 680, 680);

    // toast overlay is outermost so transient confirmations float above
    // everything without needing a page to live in
    toast_overlay_ = adw_toast_overlay_new();
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(root, TRUE);
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(toast_overlay_), root);

    GtkWidget* header = adw_header_bar_new();
    gtk_box_append(GTK_BOX(root), header);

    GtkWidget* stack = adw_view_stack_new();
    gtk_widget_set_vexpand(stack, TRUE);
    // text-only titles on purpose: add_titled_with_icon prints the icon's *name*
    // when the theme lacks it, which produced unreadable tabs
    adw_view_stack_add_titled(ADW_VIEW_STACK(stack), build_chatbox_page(), "chatbox", "Chatbox");
    adw_view_stack_add_titled(ADW_VIEW_STACK(stack), build_status_page(), "status", "Status");
    adw_view_stack_add_titled(ADW_VIEW_STACK(stack), build_connect_page(), "connect", "Connectivity");
    GtkWidget* switcher = adw_view_switcher_new();
    adw_view_switcher_set_stack(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_STACK(stack));
    adw_view_switcher_set_policy(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_SWITCHER_POLICY_WIDE);

    // Keep app updates separate from chatbox sources and connections.
    GtkWidget* cog = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(cog), "open-menu-symbolic");
    gtk_widget_set_tooltip_text(cog, "Settings");
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(cog), build_settings_menu());
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), cog);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), switcher);

    // no AdwClamp here on purpose. AdwPreferencesPage already constrains its
    // content to a readable width and supplies its own margins - wrapping it in
    // a clamp collapsed those, which is what made the rows run edge-to-edge in a
    // non-maximised window.
    gtk_box_append(GTK_BOX(root), stack);


    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window_),
                                       GTK_WIDGET(toast_overlay_));

    reset_vrchat_listener();
    refresh_preview(true);
    restart_timer();
    gtk_window_present(GTK_WINDOW(window_));
}

void AppWindow::restart_timer() {
    if (tick_id_) g_source_remove(tick_id_);
    tick_id_ = g_timeout_add((guint)settings_.send_interval_ms, AppWindow::on_tick, this);
}

void AppWindow::reset_session_stats() {
    min_bpm_ = std::numeric_limits<int>::max();
    max_bpm_ = 0;
    sum_bpm_ = 0;
    count_bpm_ = 0;
    ring_pos_ = 0;
    ring_count_ = 0;
    latest_bpm_ = 0;
    for (int i = 0; i < kRingSize; ++i) ring_[i] = 0;
    refresh_preview();
}

// mean of the most recent N raw samples, or the raw value when smoothing is off
int AppWindow::smoothed_bpm() const {
    const int window = settings_.bpm_smoothing;
    if (window <= 1) return latest_bpm_.load();

    const int have = std::min(window, static_cast<int>(ring_count_.load()));
    if (have <= 0) return latest_bpm_.load();

    long long total = 0;
    int n = 0;
    // walk backwards from the newest sample
    const int pos = ring_pos_.load();
    for (int i = 1; i <= have; ++i) {
        total += ring_[(pos - i + kRingSize * 2) % kRingSize];
        ++n;
    }
    return n ? static_cast<int>(total / n) : latest_bpm_.load();
}

// the output panel lives on the Chatbox page inside a preferences group, so
// it inherits the same surface and margins as everything else rather than
// looking like a widget that was dropped in from another app
GtkWidget* AppWindow::build_preview_card() {
    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand(card, TRUE);
    gtk_widget_add_css_class(card, "preview-card");

    status_banner_ = ADW_BANNER(adw_banner_new("Status active — normal sources are hidden"));
    gtk_widget_add_css_class(GTK_WIDGET(status_banner_), "status-override");
    adw_banner_set_button_label(status_banner_, "Turn Off");
    g_signal_connect(status_banner_, "button-clicked", G_CALLBACK(+[](AdwBanner*, gpointer self) {
        static_cast<AppWindow*>(self)->set_active_status(std::nullopt);
    }), this);
    gtk_box_append(GTK_BOX(card), GTK_WIDGET(status_banner_));

    GtkWidget* head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    auto make_pill = [](const char* text) {
        GtkWidget* p = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        GtkWidget* dot = gtk_label_new("●");
        gtk_widget_add_css_class(dot, "dot");
        GtkWidget* lbl = gtk_label_new(text);
        gtk_box_append(GTK_BOX(p), dot);
        gtk_box_append(GTK_BOX(p), lbl);
        gtk_widget_add_css_class(p, "status-pill");
        return p;
    };
    pulsoid_pill_ = make_pill("Pulsoid");
    osc_pill_ = make_pill("OSC");
    gtk_widget_set_tooltip_text(osc_pill_, "Indicates whether cadence has sent an OSC packet, not whether VRChat received it.");
    gtk_box_append(GTK_BOX(head), pulsoid_pill_);
    gtk_box_append(GTK_BOX(head), osc_pill_);

    stats_label_ = gtk_label_new("");
    gtk_widget_add_css_class(stats_label_, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(stats_label_), TRUE);
    gtk_label_set_xalign(GTK_LABEL(stats_label_), 1.0);
    gtk_widget_set_hexpand(stats_label_, TRUE);
    gtk_box_append(GTK_BOX(head), stats_label_);
    gtk_box_append(GTK_BOX(card), head);

    // the message sits in its own inset panel, so it reads as a readout rather
    // than as a label floating among the settings
    preview_label_ = gtk_label_new("…");
    gtk_widget_add_css_class(preview_label_, "monospace");
    gtk_widget_add_css_class(preview_label_, "preview-output");
    gtk_label_set_wrap(GTK_LABEL(preview_label_), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(preview_label_), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_justify(GTK_LABEL(preview_label_), GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(GTK_LABEL(preview_label_), 0.5);

    gtk_box_append(GTK_BOX(card), preview_label_);
    return card;
}

GtkWidget* AppWindow::build_chatbox_page() {
    GtkWidget* page = adw_preferences_page_new();
    auto* preview = group("Preview", nullptr);
    adw_preferences_group_add(preview, build_preview_card());

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    pause_button_ = gtk_button_new();
    gtk_widget_set_valign(pause_button_, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(pause_button_, "Pause automatic updates to edit or send a single preview");
    on_click(pause_button_, [this] {
        settings_.send_automatically = !settings_.send_automatically;
        settings_.save();
        refresh_preview();
    });
    gtk_box_append(GTK_BOX(actions), pause_button_);

    send_once_button_ = gtk_button_new_with_label("Send Once");
    gtk_widget_set_valign(send_once_button_, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(send_once_button_, "Send this preview without resuming automatic sending");
    on_click(send_once_button_, [this] {
        if (!settings_.send_automatically) send_message(preview_text_, true);
    });
    gtk_box_append(GTK_BOX(actions), send_once_button_);

    copy_button_ = gtk_button_new_from_icon_name("edit-copy-symbolic");
    gtk_widget_set_valign(copy_button_, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(copy_button_, "Copy preview");
    gtk_accessible_update_property(GTK_ACCESSIBLE(copy_button_), GTK_ACCESSIBLE_PROPERTY_LABEL, "Copy preview", -1);
    on_click(copy_button_, [this] {
        if (preview_text_.empty()) return;
        gdk_clipboard_set_text(gtk_widget_get_clipboard(copy_button_), preview_text_.c_str());
        adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(toast_overlay_), adw_toast_new("Preview copied"));
    });
    gtk_box_append(GTK_BOX(actions), copy_button_);
    adw_preferences_group_set_header_suffix(preview, actions);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), preview);

    auto* format = group("Message Format", nullptr);
    GtkWidget* layout = adw_expander_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(layout), "Layout Options");
    auto* layout_row = ADW_EXPANDER_ROW(layout);
    adw_expander_row_set_subtitle(layout_row, "Use separate lines or fit all sources on one line");
    GtkWidget* separator = entry_row("Separator", settings_.compact_separator);
    gtk_widget_set_sensitive(separator, settings_.compact_layout);
    on_text(separator, [this](const std::string& value) {
        settings_.compact_separator = value;
        settings_.save();
        refresh_preview();
    });
    adw_expander_row_add_row(layout_row, state_row("Compact Layout", nullptr, settings_.compact_layout,
        [this, separator](bool enabled) {
            settings_.compact_layout = enabled;
            gtk_widget_set_sensitive(separator, enabled);
            settings_.save();
            refresh_preview();
        }));
    adw_expander_row_add_row(layout_row, separator);
    gtk_widget_set_sensitive(layout, settings_.raw_template.empty() || !settings_.use_raw_template);
    if (!settings_.raw_template.empty()) {
        adw_preferences_group_add(format, state_row("Use saved template",
            "Turn off to use the sources below instead.", settings_.use_raw_template,
            [this, layout](bool enabled) {
                settings_.use_raw_template = enabled;
                gtk_widget_set_sensitive(component_list_, !enabled);
                gtk_widget_set_sensitive(layout, !enabled);
                settings_.save();
                refresh_preview();
            }));
    }
    adw_preferences_group_add(format, layout);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), format);

    auto* group_sources = group("Sources", "Expand a source for settings. Use the arrows to change its position.");
    component_list_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(component_list_), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(component_list_, "boxed-list");
    for (const int component : settings_.component_order) create_source_row(component);
    update_move_buttons();
    gtk_widget_set_sensitive(component_list_, settings_.raw_template.empty() || !settings_.use_raw_template);
    adw_preferences_group_add(group_sources, component_list_);
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), group_sources);
    return page;
}

void AppWindow::create_source_row(int component) {
    static const char* titles[COMP_COUNT] = {
        "Heart Rate", "Music", "Clock", "Custom Text", "Battery", "System Stats", "Session Timer", "Weather", "VRChat Status",
        "Lyrics"
    };
    static const char* icons[COMP_COUNT] = {
        "emblem-favorite-symbolic", "audio-x-generic-symbolic", "preferences-system-time-symbolic",
        "accessories-text-editor-symbolic", "battery-symbolic", "utilities-system-monitor-symbolic",
        "alarm-symbolic", "weather-clear-symbolic", "input-gaming-symbolic", "media-view-subtitles-symbolic",
    };
    static constexpr bool AppSettings::*enabled[COMP_COUNT] = {
        &AppSettings::show_heart_component, &AppSettings::show_song_component,
        &AppSettings::show_clock_component, &AppSettings::show_custom_component,
        &AppSettings::show_battery_component, &AppSettings::show_system_component,
        &AppSettings::show_timer_component,
        &AppSettings::show_weather_component,
        &AppSettings::show_vrchat_component,
        &AppSettings::show_lyrics_component,
    };
    auto& source = sources_[component];
    source.enabled = enabled[component];
    source.row = adw_expander_row_new();
    auto* row = ADW_EXPANDER_ROW(source.row);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), titles[component]);
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    adw_expander_row_set_subtitle_lines(row, 1);
    GtkWidget* icon = gtk_image_new_from_icon_name(icons[component]);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    adw_expander_row_add_prefix(row, icon);

    auto arrow = [this, row, component](const char* icon, const char* label, int direction) {
        GtkWidget* button = gtk_button_new_from_icon_name(icon);
        gtk_widget_add_css_class(button, "flat");
        gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(button, label);
        gtk_accessible_update_property(GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
        on_click(button, [this, component, direction] { move_component(component, direction); });
        adw_expander_row_add_suffix(row, button);
        return button;
    };
    source.up = arrow("go-up-symbolic", "Move up", -1);
    source.down = arrow("go-down-symbolic", "Move down", 1);

    auto changed = [this] { settings_.save(); refresh_preview(); };
    auto toggle = [this, row, changed](const char* title, bool AppSettings::*member) {
        adw_expander_row_add_row(row, state_row(title, nullptr, settings_.*member,
            [this, member, changed](bool active) { settings_.*member = active; changed(); }));
    };
    auto entry = [this, row, changed](const char* title, std::string AppSettings::*member) {
        GtkWidget* field = entry_row(title, settings_.*member);
        on_text(field, [this, member, changed](const std::string& text) {
            settings_.*member = text;
            changed();
        });
        adw_expander_row_add_row(row, field);
    };

    if (component != COMP_WEATHER && component != COMP_VRCHAT)
        toggle("Include in Chatbox", source.enabled);
    switch (component) {
        case COMP_HEART: {
            toggle("Current BPM", &AppSettings::show_bpm_current);
            toggle("Session Minimum", &AppSettings::show_bpm_min);
            toggle("Session Average", &AppSettings::show_bpm_avg);
            toggle("Session Maximum", &AppSettings::show_bpm_max);
            adw_expander_row_add_row(row, combo_row("BPM Smoothing", nullptr,
                {"Off", "Last 3 readings", "Last 5 readings", "Last 8 readings"},
                smoothing_index(settings_.bpm_smoothing), [this, changed](int index) {
                    constexpr int values[] = {0, 3, 5, 8};
                    if (index < 0 || index > 3) return;
                    settings_.bpm_smoothing = values[index];
                    changed();
                }));
            entry("Icon", &AppSettings::heart_icon);
            GtkWidget* reset = adw_action_row_new();
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(reset), "Session Statistics");
            GtkWidget* button = gtk_button_new_with_label("Reset");
            gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
            on_click(button, [this] { reset_session_stats(); });
            adw_action_row_add_suffix(ADW_ACTION_ROW(reset), button);
            adw_expander_row_add_row(row, reset);
            break;
        }
        case COMP_SONG:
            toggle("Track Title", &AppSettings::show_title);
            toggle("Artist", &AppSettings::show_artist);
            toggle("Playback Progress", &AppSettings::show_music_progress);
            entry("Icon", &AppSettings::note_icon);
            entry("Title and Artist Separator", &AppSettings::separator);
            adw_expander_row_add_row(row, spin_row("Maximum Field Length", nullptr,
                10, 100, 5, settings_.truncate_length, [this, changed](int value) {
                    settings_.truncate_length = value;
                    changed();
                }));
            break;
        case COMP_CLOCK:
            adw_expander_row_add_row(row, combo_row("Time Format", nullptr,
                {"24-hour", "12-hour"}, settings_.clock_24h ? 0 : 1,
                [this, changed](int index) { settings_.clock_24h = index == 0; changed(); }));
            toggle("Show Seconds", &AppSettings::clock_show_seconds);
            toggle("Show Date", &AppSettings::clock_show_date);
            entry("Icon", &AppSettings::clock_icon);
            break;
        case COMP_CUSTOM:
            entry("Text", &AppSettings::custom_text);
            entry("Icon", &AppSettings::custom_icon);
            break;
        case COMP_BATTERY:
            toggle("Show Charging Status", &AppSettings::battery_show_state);
            entry("Icon", &AppSettings::battery_icon);
            break;
        case COMP_SYSTEM:
            toggle("Show CPU Usage", &AppSettings::system_show_cpu);
            toggle("Show Memory Usage", &AppSettings::system_show_memory);
            break;
        case COMP_TIMER: {
            timer_status_ = adw_action_row_new();
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(timer_status_), "Timer");
            GtkWidget* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
            gtk_widget_set_valign(controls, GTK_ALIGN_CENTER);
            timer_action_ = gtk_button_new_with_label("Start");
            GtkWidget* reset = gtk_button_new_with_label("Reset");
            gtk_box_append(GTK_BOX(controls), timer_action_);
            gtk_box_append(GTK_BOX(controls), reset);
            adw_action_row_add_suffix(ADW_ACTION_ROW(timer_status_), controls);
            adw_expander_row_add_row(row, timer_status_);
            on_click(timer_action_, [this] {
                session_timer_.toggle(SessionTimer::Clock::now(), timer_limit());
                refresh_preview();
            });
            on_click(reset, [this] {
                session_timer_.reset();
                refresh_preview();
            });

            GtkWidget* length = spin_row("Countdown Minutes", nullptr, 1, 180, 1,
                settings_.timer_minutes, [this, changed](int minutes) {
                    settings_.timer_minutes = minutes;
                    session_timer_.reset();
                    changed();
                });
            gtk_widget_set_sensitive(length, settings_.timer_countdown);
            adw_expander_row_add_row(row, combo_row("Mode", "Changing mode resets the timer",
                {"Stopwatch", "Countdown"}, settings_.timer_countdown ? 1 : 0,
                [this, changed, length](int mode) {
                    if (mode < 0 || mode > 1) return;
                    settings_.timer_countdown = mode == 1;
                    gtk_widget_set_sensitive(length, settings_.timer_countdown);
                    session_timer_.reset();
                    changed();
                }));
            adw_expander_row_add_row(row, length);
            entry("Icon", &AppSettings::timer_icon);
            break;
        }
        case COMP_WEATHER:
            add_weather_settings(row);
            break;
        case COMP_VRCHAT:
            adw_expander_row_add_row(row, state_row("Include in Chatbox",
                "Listen for your own status via VRChat's local OSC output",
                settings_.show_vrchat_component, [this, changed](bool enabled) {
                    settings_.show_vrchat_component = enabled;
                    reset_vrchat_listener();
                    changed();
                }));
            toggle("Mic Mute State", &AppSettings::vrchat_show_mic);
            toggle("Earmuff Mode", &AppSettings::vrchat_show_earmuffs);
            toggle("Left Hand Gesture", &AppSettings::vrchat_show_left);
            toggle("Right Hand Gesture", &AppSettings::vrchat_show_right);
            toggle("Movement Speed", &AppSettings::vrchat_show_speed);
            adw_expander_row_add_row(row, state_row("Only Show Active States",
                "Hide unmuted mic, off earmuffs, neutral gestures, and zero speed",
                settings_.vrchat_active_only, [this, changed](bool enabled) {
                    settings_.vrchat_active_only = enabled;
                    changed();
                }));
            adw_expander_row_add_row(row, combo_row("Speed Units", nullptr,
                {"Meters per second", "Kilometers per hour", "Miles per hour"},
                static_cast<int>(settings_.vrchat_speed_units), [this, changed](int index) {
                    if (index < 0 || index > 2) return;
                    settings_.vrchat_speed_units = static_cast<SpeedUnits>(index);
                    changed();
                }));
            entry("Field Separator", &AppSettings::vrchat_separator);
            break;
        case COMP_LYRICS:
            add_lyrics_settings(row);
            break;
        default:
            break;
    }
    gtk_list_box_append(GTK_LIST_BOX(component_list_), source.row);
}

void AppWindow::add_lyrics_settings(AdwExpanderRow* row) {
    lyrics_preview_row_ = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(lyrics_preview_row_), "Lyric Preview");
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(lyrics_preview_row_), FALSE);
    adw_action_row_set_subtitle_lines(ADW_ACTION_ROW(lyrics_preview_row_), 2);
    adw_expander_row_add_row(row, lyrics_preview_row_);

    adw_expander_row_add_row(row, state_row("Look Up Lyrics Online",
        "Send title, artist, album and duration to LRCLIB, even for preview only",
        settings_.lyrics_online, [this](bool enabled) {
            settings_.lyrics_online = enabled;
            settings_.save();
            reset_lyrics();
        }));
    GtkWidget* icon = entry_row("Lyric Icon", settings_.lyrics_icon);
    gtk_widget_set_sensitive(icon, settings_.lyrics_show_icon);
    on_text(icon, [this](const std::string& value) {
        settings_.lyrics_icon = value;
        settings_.save();
        refresh_preview();
    });
    adw_expander_row_add_row(row, state_row("Show Lyric Icon", nullptr, settings_.lyrics_show_icon,
        [this, icon](bool enabled) {
            settings_.lyrics_show_icon = enabled;
            gtk_widget_set_sensitive(icon, enabled);
            settings_.save();
            refresh_preview();
        }));
    adw_expander_row_add_row(row, icon);
    adw_expander_row_add_row(row, spin_row("Timing Offset (ms)", "A positive offset shows lines earlier",
        -5000, 5000, 250, settings_.lyrics_offset_ms, [this](int value) {
            settings_.lyrics_offset_ms = value;
            settings_.save();
            refresh_preview();
        }));
    adw_expander_row_add_row(row, spin_row("Maximum Line Width", "The line may shrink further to fit the chatbox",
        20, 120, 5, settings_.lyrics_max_width, [this](int value) {
            settings_.lyrics_max_width = value;
            settings_.save();
            refresh_preview();
        }));

    auto* provider = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(provider), "Lyrics Source");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(provider), "Synced lyrics only");
    auto* link = gtk_link_button_new_with_label("https://lrclib.net/", "LRCLIB");
    gtk_widget_set_valign(link, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(provider), link);
    auto* reload = gtk_button_new_with_label("Reload");
    gtk_widget_set_valign(reload, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(provider), reload);
    on_click(reload, [this] { reset_lyrics(); });
    adw_expander_row_add_row(row, provider);
}

void AppWindow::reset_lyrics() {
    if (lyrics_job_) g_cancellable_cancel(lyrics_job_);
    lyrics_cache_.clear();
    lyrics_query_.reset();
    lyrics_result_ = {};
    lyrics_attempted_ = false;
    lyrics_retry_at_ = 0;
    refresh_preview();
}

void AppWindow::refresh_lyrics(gint64 now) {
    std::optional<LyricsQuery> next;
    if (settings_.lyrics_online &&
        last_track_ && !last_track_->artist.empty() && last_track_->artist.size() <= 512 &&
        last_track_->title.size() <= 512 && last_track_->album.size() <= 512) {
        const auto duration = last_track_->duration ? last_track_->duration->count() : 0;
        next = LyricsQuery{last_track_->title, last_track_->artist, last_track_->album,
            duration >= 1 && duration <= 3600 ? static_cast<int>(duration) : 0};
    }
    if (next != lyrics_query_) {
        if (lyrics_job_) g_cancellable_cancel(lyrics_job_);
        lyrics_query_ = std::move(next);
        lyrics_result_ = {};
        lyrics_attempted_ = false;
        lyrics_retry_at_ = 0;
    }
    if (!lyrics_query_ || !last_track_->playing || !last_track_->position_ms || lyrics_job_) return;
    if (lyrics_retry_at_ && now >= lyrics_retry_at_) {
        lyrics_attempted_ = false;
        lyrics_retry_at_ = 0;
    }
    if (lyrics_attempted_) return;

    const auto query = *lyrics_query_;
    for (const auto& cached : lyrics_cache_) {
        if (cached.query == query && cached.expires > now) {
            lyrics_result_ = cached.result;
            lyrics_attempted_ = true;
            return;
        }
    }
    lyrics_attempted_ = true;
    if (now < lyrics_online_after_) {
        lyrics_result_.status = "Waiting before the next LRCLIB lookup";
        lyrics_retry_at_ = lyrics_online_after_;
        return;
    }
    lyrics_online_after_ = now + 2 * G_USEC_PER_SEC;
    lyrics_job_ = g_cancellable_new();
    lookup_synced_lyrics(query, lyrics_job_, [this, query](LyricsResult result) {
        const bool cancelled = g_cancellable_is_cancelled(lyrics_job_);
        g_clear_object(&lyrics_job_);
        if (!window_) return;
        const auto now = g_get_monotonic_time();
        if (result.retry_after_seconds > 0) {
            const auto delay = std::min<gint64>(result.retry_after_seconds,
                (G_MAXINT64 - now) / G_USEC_PER_SEC - 10);
            lyrics_online_after_ = now + (delay + g_random_int_range(0, 6)) * G_USEC_PER_SEC;
        }
        if (!cancelled && lyrics_query_ && *lyrics_query_ == query) {
            if (result.retry_after_seconds > 0) {
                lyrics_retry_at_ = lyrics_online_after_;
            }
            if (result.cacheable) {
                if (lyrics_cache_.size() >= 16) lyrics_cache_.pop_front();
                const auto ttl = result.lines.empty() ? 600LL : 3600LL;
                lyrics_cache_.push_back({query, result, now + ttl * G_USEC_PER_SEC});
            }
            lyrics_result_ = std::move(result);
        }
        refresh_preview();
    });
}

void AppWindow::add_weather_settings(AdwExpanderRow* row) {
    adw_expander_row_add_row(row, state_row("Include in Chatbox", "Only checks weather for a city you select",
        settings_.show_weather_component, [this](bool value) {
            settings_.show_weather_component = value;
            reset_weather();
        }));

    GtkWidget* city = entry_row("City Name", "");
    GtkWidget* search = gtk_button_new_with_label("Search");
    gtk_widget_set_valign(search, GTK_ALIGN_CENTER);
    adw_entry_row_add_suffix(ADW_ENTRY_ROW(city), search);
    adw_expander_row_add_row(row, city);

    weather_results_row_ = combo_row("Matching Cities", "Search for a city first", {"Select a city"}, 0,
        [this](int index) {
            if (index < 1 || static_cast<size_t>(index) > weather_results_.size()) return;
            settings_.weather_place = weather_results_[index - 1];
            reset_weather();
        });
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(weather_results_row_), FALSE);
    adw_expander_row_add_row(row, weather_results_row_);

    auto clear_results = [this] {
        weather_results_.clear();
        const char* labels[] = {"Select a city", nullptr};
        GtkStringList* model = gtk_string_list_new(labels);
        adw_combo_row_set_model(ADW_COMBO_ROW(weather_results_row_), G_LIST_MODEL(model));
        g_object_unref(model);
        adw_combo_row_set_selected(ADW_COMBO_ROW(weather_results_row_), 0);
    };
    on_click(search, [this, city, search, clear_results] {
        std::string query = gtk_editable_get_text(GTK_EDITABLE(city));
        const auto first = query.find_first_not_of(" \t\r\n");
        query = first == std::string::npos ? "" : query.substr(first, query.find_last_not_of(" \t\r\n") - first + 1);
        clear_results();
        if (query.empty() || g_utf8_strlen(query.c_str(), -1) > 100) {
            adw_action_row_set_subtitle(ADW_ACTION_ROW(weather_results_row_), "Enter a city name (up to 100 characters)");
            return;
        }
        gtk_widget_set_sensitive(search, FALSE);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(weather_results_row_), "Searching…");
        search_weather_city(query, [this, search](std::vector<WeatherPlace> places, std::string error) {
            if (!window_) return;
            gtk_widget_set_sensitive(search, TRUE);
            if (!error.empty() || places.empty()) {
                adw_action_row_set_subtitle(ADW_ACTION_ROW(weather_results_row_),
                    error.empty() ? "No matching cities" : error.c_str());
                return;
            }
            GtkStringList* model = gtk_string_list_new(nullptr);
            gtk_string_list_append(model, "Select a city");
            for (const auto& place : places) gtk_string_list_append(model, place.label.c_str());
            auto* row = ADW_COMBO_ROW(weather_results_row_);
            // The placeholder stays selected: even a single match is never picked automatically.
            adw_combo_row_set_model(row, G_LIST_MODEL(model));
            adw_combo_row_set_selected(row, 0);
            g_object_unref(model);
            weather_results_ = std::move(places);
            adw_action_row_set_subtitle(ADW_ACTION_ROW(row), "Choose the city and region you want");
        });
    });

    weather_city_row_ = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(weather_city_row_), "Selected City");
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(weather_city_row_), FALSE);
    weather_clear_button_ = gtk_button_new_with_label("Clear");
    gtk_widget_set_valign(weather_clear_button_, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(weather_city_row_), weather_clear_button_);
    on_click(weather_clear_button_, [this] {
        settings_.weather_place = {};
        adw_combo_row_set_selected(ADW_COMBO_ROW(weather_results_row_), 0);
        reset_weather();
    });
    adw_expander_row_add_row(row, weather_city_row_);

    adw_expander_row_add_row(row, combo_row("Temperature Units", "Alternating units switch every 10 seconds",
        {"Celsius (°C)", "Fahrenheit (°F)", "Alternate °C / °F"}, static_cast<int>(settings_.weather_units),
        [this](int units) {
            if (units < 0 || units > 2) return;
            settings_.weather_units = static_cast<WeatherUnits>(units);
            weather_cycle_start_ = g_get_monotonic_time();
            settings_.save();
            refresh_preview();
        }));
    adw_expander_row_add_row(row, state_row("Show Weather Icon", nullptr, settings_.weather_show_icon,
        [this](bool value) { settings_.weather_show_icon = value; settings_.save(); refresh_preview(); }));
    adw_expander_row_add_row(row, state_row("Show Condition", "Add text like Sunny, Rain, or Snow",
        settings_.weather_show_condition,
        [this](bool value) { settings_.weather_show_condition = value; settings_.save(); refresh_preview(); }));

    GtkWidget* provider = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(provider), "Weather Data");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(provider), "Looks up the city you pick, not your current location");
    GtkWidget* link = gtk_link_button_new_with_label("https://open-meteo.com/", "Open-Meteo");
    gtk_widget_set_valign(link, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(provider), link);
    adw_expander_row_add_row(row, provider);
}

void AppWindow::reset_weather() {
    ++weather_generation_;
    weather_reading_.reset();
    weather_error_.clear();
    weather_next_request_ = weather_last_success_ = 0;
    weather_cycle_start_ = g_get_monotonic_time();
    settings_.save();
    refresh_preview();
}

void AppWindow::refresh_weather(gint64 now) {
    if (!settings_.show_weather_component || !settings_.weather_place.valid() ||
        weather_loading_ || now < weather_next_request_) return;
    weather_loading_ = true;
    const auto generation = weather_generation_;
    fetch_weather(settings_.weather_place, [this, generation](std::optional<WeatherReading> reading, std::string error) {
        if (!window_) return;
        weather_loading_ = false;
        if (generation == weather_generation_) {
            const auto now = g_get_monotonic_time();
            weather_next_request_ = now + (reading ? 15 : 5) * 60 * G_USEC_PER_SEC;
            weather_error_ = std::move(error);
            if (reading) {
                if (!weather_reading_) weather_cycle_start_ = now;
                weather_reading_ = std::move(reading);
                weather_last_success_ = now;
            }
        }
        refresh_preview();
    });
}

void AppWindow::update_move_buttons() {
    const auto& order = settings_.component_order;
    for (size_t i = 0; i < order.size(); ++i) {
        const auto& source = sources_[order[i]];
        gtk_widget_set_sensitive(source.up, i > 0);
        gtk_widget_set_sensitive(source.down, i + 1 < order.size());
    }
}

void AppWindow::move_component(int component, int direction) {
    auto& order = settings_.component_order;
    auto from = std::find(order.begin(), order.end(), component);
    const int to = static_cast<int>(from - order.begin()) + direction;
    if (from == order.end() || to < 0 || to >= static_cast<int>(order.size())) return;
    std::iter_swap(from, order.begin() + to);

    auto& source = sources_[component];
    auto* list = GTK_LIST_BOX(component_list_);
    g_object_ref(source.row);
    gtk_list_box_remove(list, source.row);
    gtk_list_box_insert(list, source.row, to);
    g_object_unref(source.row);
    update_move_buttons();
    gtk_widget_grab_focus(direction < 0 && to > 0 ? source.up :
                          direction > 0 && to + 1 < static_cast<int>(order.size()) ? source.down :
                          direction < 0 ? source.down : source.up);
    settings_.save();
    refresh_preview();
}

void AppWindow::add_pulsoid_connection(AdwPreferencesPage* page) {
    // --- connection ---
    AdwPreferencesGroup* cg = group("Pulsoid", "Connect your live heart rate feed.");

    GtkWidget* token = adw_password_entry_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(token), "Access token");
    gtk_editable_set_text(GTK_EDITABLE(token), settings_.pulsoid_token.c_str());
    adw_preferences_group_add(cg, token);

    connect_btn_ = gtk_button_new_with_label("Connect");
    disconnect_btn_ = gtk_button_new_with_label("Disconnect");
    gtk_widget_add_css_class(connect_btn_, "pill");
    gtk_widget_add_css_class(connect_btn_, "suggested-action");
    gtk_widget_add_css_class(disconnect_btn_, "pill");

    on_click(connect_btn_, [this, token] {
        const char* t = gtk_editable_get_text(GTK_EDITABLE(token));
        connect_pulsoid(t ? t : "");
    });
    on_click(disconnect_btn_, [this] { disconnect_pulsoid(); });

    GtkWidget* btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_valign(btns, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(btns), connect_btn_);
    gtk_box_append(GTK_BOX(btns), disconnect_btn_);

    GtkWidget* btn_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(btn_row), "Actions");
    adw_action_row_add_suffix(ADW_ACTION_ROW(btn_row), btns);
    adw_preferences_group_add(cg, btn_row);

    pulsoid_status_row_ = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(pulsoid_status_row_), "Status");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(pulsoid_status_row_), "disconnected");
    adw_preferences_group_add(cg, pulsoid_status_row_);

    adw_preferences_page_add(page, cg);
}

GtkWidget* AppWindow::build_settings_menu() {
    GtkWidget* popover = gtk_popover_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);

    const char* running_appimage = g_getenv("APPIMAGE");
    const char* installer = g_getenv("CADENCE_INSTALLER");
    if (running_appimage && g_path_is_absolute(running_appimage) &&
        installer && g_path_is_absolute(installer) && g_file_test(installer, G_FILE_TEST_IS_EXECUTABLE)) {
        GtkWidget* update = gtk_button_new_with_label("Update This AppImage");
        gtk_widget_add_css_class(update, "flat");
        on_click(update, [this, popover, path = std::string(running_appimage), script = std::string(installer)] {
            gtk_popover_popdown(GTK_POPOVER(popover));
            confirm_appimage_update(path, script);
        });
        gtk_box_append(GTK_BOX(box), update);
    } else {
        GtkWidget* updates = gtk_link_button_new_with_label(
            "https://github.com/MORT1SXD/cadence-linux/releases/latest", "Check for Updates");
        gtk_widget_set_tooltip_text(updates, "Open the latest release on GitHub");
        gtk_box_append(GTK_BOX(box), updates);
    }

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    return popover;
}

void AppWindow::show_update_toast(const char* title, guint timeout) {
    g_clear_handle_id(&update_toast_expiry_, g_source_remove);
    if (auto* previous = std::exchange(update_toast_, nullptr)) {
        adw_toast_dismiss(previous);
        g_object_unref(previous);
    }
    if (!title || !window_) return;

    auto* toast = adw_toast_new(title);
    update_toast_ = ADW_TOAST(g_object_ref(toast));
    adw_toast_set_timeout(toast, 0);
    g_signal_connect(toast, "dismissed", G_CALLBACK(+[](AdwToast* dismissed, gpointer data) {
        auto& self = *static_cast<AppWindow*>(data);
        if (self.update_toast_ != dismissed) return;
        g_clear_handle_id(&self.update_toast_expiry_, g_source_remove);
        g_clear_object(&self.update_toast_);
    }), this);
    adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(toast_overlay_), toast);
    // A result has no action to complete. Clear it even if the pointer stayed
    // over the toast, which would otherwise suspend Adwaita's countdown.
    if (timeout) {
        update_toast_expiry_ = g_timeout_add(timeout * 1000, +[](gpointer data) -> gboolean {
            auto& self = *static_cast<AppWindow*>(data);
            self.update_toast_expiry_ = 0;
            self.show_update_toast(nullptr);
            return G_SOURCE_REMOVE;
        }, this);
    }
}

void AppWindow::confirm_appimage_update(std::string path, std::string installer) {
    if (update_running_) {
        if (!update_toast_) show_update_toast("Updating the AppImage…", 0);
        return;
    }
    std::error_code error;
    const auto target = std::filesystem::canonical(path, error);
    if (error || !std::filesystem::is_regular_file(target, error)) {
        show_update_toast("Could not locate the running AppImage");
        return;
    }

    show_update_toast(nullptr);
    GtkWidget* parent = window_;
    auto* dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new("Update This AppImage?", nullptr));
    gchar* display_path = g_filename_display_name(target.c_str());
    adw_alert_dialog_format_body(dialog,
        "Download the latest published release and replace this file?\n\n%s\n\n"
        "A development build may be newer than the release. Your settings are kept.", display_path);
    g_free(display_path);
    adw_alert_dialog_add_responses(dialog, "cancel", "Cancel", "update", "Download and Replace", nullptr);
    adw_alert_dialog_set_response_appearance(dialog, "update", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(dialog, "cancel");
    adw_alert_dialog_set_close_response(dialog, "cancel");
    using Update = std::function<void()>;
    g_signal_connect_data(dialog, "response::update",
        G_CALLBACK(+[](AdwAlertDialog*, const char*, gpointer data) { (*static_cast<Update*>(data))(); }),
        new Update([this, path = target.string(), installer = std::move(installer)] {
            run_appimage_update(path, installer);
        }), +[](gpointer data, GClosure*) { delete static_cast<Update*>(data); },
        static_cast<GConnectFlags>(0));
    adw_dialog_present(ADW_DIALOG(dialog), parent);
}

void AppWindow::run_appimage_update(const std::string& path, const std::string& installer) {
    if (update_running_) return;
    auto* launcher = g_subprocess_launcher_new(static_cast<GSubprocessFlags>(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE));
    // The installer runs host tools, not executables linked against the AppImage.
    g_subprocess_launcher_unsetenv(launcher, "LD_LIBRARY_PATH");
    GError* error = nullptr;
    const char* args[] = {installer.c_str(), "--replace", path.c_str(), nullptr};
    GSubprocess* process = g_subprocess_launcher_spawnv(launcher, args, &error);
    g_object_unref(launcher);
    if (!process) {
        g_clear_error(&error);
        show_update_toast("Could not start the installer");
        return;
    }
    update_running_ = true;
    show_update_toast("Updating the AppImage…", 0);
    g_subprocess_communicate_utf8_async(process, nullptr, nullptr,
        +[](GObject* object, GAsyncResult* result, gpointer data) {
            auto& window = *static_cast<AppWindow*>(data);
            auto* process = G_SUBPROCESS(object);
            gchar* output = nullptr;
            gchar* diagnostic = nullptr;
            GError* error = nullptr;
            const bool complete = g_subprocess_communicate_utf8_finish(process, result, &output, &diagnostic, &error);
            window.update_running_ = false;
            if (complete && g_subprocess_get_successful(process)) {
                const bool current = output && g_strstr_len(output, -1, "Already up to date.");
                window.show_update_toast(current ? "Cadence is already up to date" : "Updated. Quit and reopen Cadence.");
            } else {
                window.show_update_toast(nullptr);
                if (window.window_) {
                    auto* dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new("Could Not Update", nullptr));
                    const char* reason = diagnostic && *diagnostic ? diagnostic :
                        "Download the latest AppImage from GitHub and try again.";
                    adw_alert_dialog_set_body(dialog, reason);
                    adw_alert_dialog_add_response(dialog, "close", "Close");
                    adw_dialog_present(ADW_DIALOG(dialog), window.window_);
                }
            }
            g_free(output);
            g_free(diagnostic);
            g_clear_error(&error);
        }, this);
    g_object_unref(process);
}

GtkWidget* AppWindow::build_connect_page() {
    GtkWidget* page = adw_preferences_page_new();

    AdwPreferencesGroup* vg = group("VRChat OSC", "Where chatbox messages are sent.");
    auto* advanced = ADW_EXPANDER_ROW(adw_expander_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(advanced), "Advanced OSC");
    adw_expander_row_set_subtitle(advanced, "Only needed for custom VRChat ports or an OSC router.");
    GtkWidget* port = spin_row("VRChat Input Port", "Where Cadence sends chatbox text (default 9000).",
                               1, 65535, 1, settings_.osc_port, [this](int v) {
        const bool was_same_port = settings_.osc_port == settings_.vrchat_status_port;
        settings_.osc_port = v;
        osc_sender_.set_port((uint16_t)v);
        settings_.save();
        if (was_same_port || v == settings_.vrchat_status_port) reset_vrchat_listener();
        refresh_preview();
    });
    adw_expander_row_add_row(advanced, port);
    GtkWidget* test_row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(test_row), "Test Message");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(test_row), "Check that the chatbox receives OSC.");
    GtkWidget* test = gtk_button_new_with_label("Send Test");
    gtk_widget_set_valign(test, GTK_ALIGN_CENTER);
    on_click(test, [this] {
        send_message("cadence test", true);
    });
    adw_action_row_add_suffix(ADW_ACTION_ROW(test_row), test);
    adw_preferences_group_add(vg, test_row);
    adw_expander_row_add_row(advanced, spin_row("VRChat Output Port", "Where Cadence listens for VRChat Status (default 9001).",
        1, 65535, 1, settings_.vrchat_status_port, [this](int value) {
            settings_.vrchat_status_port = value;
            settings_.save();
            reset_vrchat_listener();
            refresh_preview();
        }));
    vrchat_listener_row_ = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(vrchat_listener_row_), "Local Listener");
    vrchat_retry_button_ = gtk_button_new_with_label("Retry");
    gtk_widget_set_valign(vrchat_retry_button_, GTK_ALIGN_CENTER);
    on_click(vrchat_retry_button_, [this] { reset_vrchat_listener(); refresh_preview(); });
    adw_action_row_add_suffix(ADW_ACTION_ROW(vrchat_listener_row_), vrchat_retry_button_);
    adw_expander_row_add_row(advanced, vrchat_listener_row_);
    adw_preferences_group_add(vg, GTK_WIDGET(advanced));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), vg);

    AdwPreferencesGroup* og = group("Chatbox Updates");

    // combos rather than a free spin box: these are the values that make sense
    // against vrchat's rate limit
    adw_preferences_group_add(og, combo_row("Update interval",
        "How often the chatbox is rewritten",
         {"Every Second", "Every 2 Seconds", "Every 5 Seconds"},
        interval_index(settings_.send_interval_ms),
        [this](int idx) {
             static const int vals[] = {1000, 2000, 5000};
             if (idx < 0 || idx > 2) return;
            settings_.send_interval_ms = vals[idx];
            settings_.save();
            restart_timer();
        }));

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), og);
    add_pulsoid_connection(ADW_PREFERENCES_PAGE(page));

    return page;
}

void AppWindow::connect_pulsoid(const std::string& token) {
    settings_.pulsoid_token = token;
    settings_.save();

    pulsoid_.stop();
    reset_session_stats();

    pulsoid_.set_token(token);
    if (!token.empty()) pulsoid_.start();

    refresh_preview();
}

void AppWindow::disconnect_pulsoid() {
    pulsoid_.stop();
    on_pulsoid_status("disconnected");
    refresh_preview();
}

void AppWindow::on_pulsoid_status(const std::string& status) {
    if (!window_) return;
    adw_action_row_set_subtitle(ADW_ACTION_ROW(pulsoid_status_row_), status.c_str());

    // state classes on the pill rather than inline styling, so css stays the
    // single source of truth for how status looks
    gtk_widget_remove_css_class(pulsoid_pill_, "is-ok");
    gtk_widget_remove_css_class(pulsoid_pill_, "is-error");
    if (status == "connected") gtk_widget_add_css_class(pulsoid_pill_, "is-ok");
    if (status != "connected" && status != "connecting" && status != "disconnected")
        gtk_widget_add_css_class(pulsoid_pill_, "is-error");
}

SessionTimer::Seconds AppWindow::timer_limit() const {
    return settings_.timer_countdown ? std::chrono::minutes(settings_.timer_minutes) : SessionTimer::Seconds{};
}

void AppWindow::send_message(const std::string& text, bool manual) {
    if (text.empty() || stats_for(text).over_limit) return;
    const gint64 now = g_get_monotonic_time();
    const char* status = nullptr;
    if (last_osc_send_ != 0 && now - last_osc_send_ < G_USEC_PER_SEC) {
        status = "Wait a moment before sending again";
    } else if (osc_sender_.send_chatbox(text)) {
        last_osc_send_ = now;
        status = "OSC packet sent";
    } else {
        status = "Could not send the OSC packet";
    }
    if (manual) adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(toast_overlay_), adw_toast_new(status));
}

void AppWindow::refresh_preview(bool send) {
    const bool raw = settings_.use_raw_template && !settings_.raw_template.empty();
    last_track_ = media_.current_track(settings_.show_music_progress || settings_.lyrics_online ||
                                        (raw && settings_.raw_template.find("music_progress") != std::string::npos));
    refresh_lyrics(g_get_monotonic_time());
    std::string lyric;
    std::string lyric_preview;
    if (!settings_.lyrics_online) lyric_preview = "Enable online lookup to preview lyrics";
    else if (!last_track_ || !last_track_->playing) lyric_preview = "Waiting for music to play";
    else if (!last_track_->position_ms) lyric_preview = "Your player doesn’t report playback position";
    else if (!lyrics_query_) lyric_preview = "No usable track title or artist";
    else {
        lyric = lyric_at(lyrics_result_.lines, last_track_->position_ms->count() + settings_.lyrics_offset_ms);
        if (!lyric.empty()) {
            lyric_preview = format_lyric(lyric, settings_);
        } else if (lyrics_job_) lyric_preview = "Looking for synced lyrics…";
        else if (!lyrics_result_.lines.empty()) lyric_preview = "No lyric at this moment";
        else lyric_preview = lyrics_result_.status.empty() ? "No synced lyrics available" : lyrics_result_.status;
    }
    adw_action_row_set_subtitle(ADW_ACTION_ROW(lyrics_preview_row_), lyric_preview.c_str());

    last_hr_ = HeartStats{};
    const long long count = count_bpm_;
    last_hr_.has_data = count > 0;
    // display the smoothed reading; the session stats below stay raw
    last_hr_.current = smoothed_bpm();
    if (last_hr_.has_data) {
        last_hr_.min = min_bpm_;
        last_hr_.max = max_bpm_;
        last_hr_.avg = static_cast<double>(sum_bpm_) / count;
    }

    last_clock_ = current_clock(settings_);
    last_battery_ = current_battery();
    // Only sample on the timer (not on every edit); CPU usage is a delta.
    if (send) last_system_ = system_source_.sample();
    const auto now = SessionTimer::Clock::now();
    const auto limit = timer_limit();
    const std::string timer = format_duration(session_timer_.display(now, limit));
    const bool running = session_timer_.running(now, limit);
    const bool finished = session_timer_.finished(now, limit);
    const std::string timer_status = timer + (finished ? " · Finished" : running ? " · Running" : "");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(timer_status_), timer_status.c_str());
    const char* timer_action = running ? "Pause" : finished ? "Restart" :
        session_timer_.elapsed(now) > SessionTimer::Seconds::zero() ? "Resume" : "Start";
    if (g_strcmp0(gtk_button_get_label(GTK_BUTTON(timer_action_)), timer_action) != 0)
        gtk_button_set_label(GTK_BUTTON(timer_action_), timer_action);

    const gint64 weather_now = g_get_monotonic_time();
    if (weather_reading_ && weather_now - weather_last_success_ > 45LL * 60 * G_USEC_PER_SEC)
        weather_reading_.reset();
    refresh_weather(weather_now);
    const bool has_city = settings_.weather_place.valid();
    std::string city_status = has_city ? settings_.weather_place.label : "No city selected";
    if (has_city && settings_.show_weather_component) {
        if (weather_loading_ && !weather_reading_) city_status += " · Checking weather…";
        else if (!weather_error_.empty()) city_status += " · " + weather_error_;
    }
    adw_action_row_set_subtitle(ADW_ACTION_ROW(weather_city_row_), city_status.c_str());
    gtk_widget_set_sensitive(weather_clear_button_, has_city);
    std::optional<WeatherDisplay> weather;
    if (settings_.show_weather_component && weather_reading_) {
        const bool alternate_phase = ((weather_now - weather_cycle_start_) / (10 * G_USEC_PER_SEC)) % 2 != 0;
        weather = display_weather(*weather_reading_, settings_.weather_units, alternate_phase);
    }
    const auto vrchat = vrchat_listener_.snapshot(g_get_monotonic_time());
    const bool has_vrchat_state = vrchat.muted || vrchat.earmuffs || vrchat.left_gesture ||
                                  vrchat.right_gesture || vrchat.speed;
    const bool same_port = settings_.osc_port == settings_.vrchat_status_port;
    const std::string vrchat_status = !settings_.show_vrchat_component ? "Off" :
        same_port ? "Use different ports for sending and receiving" :
        !vrchat_listener_.error().empty() ? vrchat_listener_.error() :
        has_vrchat_state ? "Receiving status from local OSC" : "Waiting for VRChat status";
    adw_action_row_set_subtitle(ADW_ACTION_ROW(vrchat_listener_row_), vrchat_status.c_str());
    gtk_widget_set_sensitive(vrchat_retry_button_,
        settings_.show_vrchat_component && !same_port && !vrchat_listener_.listening());
    const TemplateData data{&last_hr_, last_track_ ? &*last_track_ : nullptr,
                            &last_clock_, &last_battery_, &last_system_, timer,
                            weather ? &*weather : nullptr, &vrchat, std::move(lyric)};

    preview_text_ = active_status_ ? settings_.status_phrases[*active_status_] : render_chatbox(data, settings_);
    const auto& text = preview_text_;
    const auto s = stats_for(text);
    const char* pause_label = settings_.send_automatically ? "Pause Sending" : "Resume Sending";
    if (g_strcmp0(gtk_button_get_label(GTK_BUTTON(pause_button_)), pause_label) != 0)
        gtk_button_set_label(GTK_BUTTON(pause_button_), pause_label);
    gtk_widget_set_sensitive(copy_button_, !text.empty());
    gtk_widget_set_visible(send_once_button_, !settings_.send_automatically);
    gtk_widget_set_sensitive(send_once_button_, !text.empty() && !s.over_limit);
    gtk_label_set_text(GTK_LABEL(preview_label_),
                       text.empty() ? "Nothing to send yet" : text.c_str());

    for (const int component : settings_.component_order) {
        const auto& source = sources_[component];
        std::string summary = render_component(component, data, settings_);
        if (!(settings_.*(source.enabled))) summary = "Off";
        else if (summary.empty()) {
            switch (component) {
                case COMP_HEART: summary = last_hr_.has_data ? "No BPM fields selected" : "Waiting for a Pulsoid reading"; break;
                case COMP_SONG: summary = last_track_ ? "No track fields available" : "Nothing playing"; break;
                case COMP_CLOCK: summary = "Local time unavailable"; break;
                case COMP_CUSTOM: summary = "Enter text to add a line"; break;
                case COMP_BATTERY: summary = "No system battery detected"; break;
                case COMP_SYSTEM:
                    summary = settings_.system_show_cpu || settings_.system_show_memory
                        ? "Waiting for system readings" : "No metrics selected";
                    break;
                case COMP_WEATHER:
                    if (!has_city) summary = "Choose a city";
                    else if (weather_loading_) summary = "Checking weather…";
                    else summary = weather_error_.empty() ? "No current weather available" : weather_error_;
                    break;
                case COMP_VRCHAT:
                    summary = has_vrchat_state ? "No selected fields to show" : vrchat_status;
                    break;
                case COMP_LYRICS: summary = lyric_preview; break;
                default: break;
            }
        }
        std::replace(summary.begin(), summary.end(), '\n', ' ');
        adw_expander_row_set_subtitle(ADW_EXPANDER_ROW(source.row), summary.c_str());
    }

    // green means "we wrote packets without error", NOT "vrchat is listening" -
    // udp is fire-and-forget so a wrong port looks identical from here
    gtk_widget_remove_css_class(osc_pill_, "is-ok");
    if (osc_sender_.has_sent()) gtk_widget_add_css_class(osc_pill_, "is-ok");

    char buf[96];
    snprintf(buf, sizeof(buf), "%d / 144 characters · %d / 9 lines%s", s.chars, s.lines,
             s.over_limit ? " · Too long to send" : !settings_.send_automatically ? " · Sending paused" : "");
    gtk_label_set_text(GTK_LABEL(stats_label_), buf);
    gtk_widget_remove_css_class(stats_label_, "is-error");
    if (s.over_limit) gtk_widget_add_css_class(stats_label_, "is-error");

    // this is the app's one and only outbound path to vrchat: the official
    // /chatbox/input osc endpoint. nothing here ever touches the game itself.
    if (send && settings_.send_automatically) send_message(text);

}

void AppWindow::reset_vrchat_listener() {
    if (settings_.show_vrchat_component && settings_.osc_port != settings_.vrchat_status_port)
        vrchat_listener_.start(settings_.vrchat_status_port);
    else
        vrchat_listener_.stop();
}

gboolean AppWindow::on_tick(gpointer user_data) {
    auto* self = static_cast<AppWindow*>(user_data);
    if (!self->window_) return G_SOURCE_REMOVE;
    self->refresh_preview(true);
    return G_SOURCE_CONTINUE;
}

} // namespace cadence
