#include "app_window.hpp"
#include "formatter.hpp"
#include "heart_stats.hpp"

#include <cstdio>
#include <cstdlib>
#include <functional>
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
    adw_view_stack_add_titled(ADW_VIEW_STACK(stack), build_connect_page(), "connect", "Connectivity");
    GtkWidget* switcher = adw_view_switcher_new();
    adw_view_switcher_set_stack(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_STACK(stack));
    adw_view_switcher_set_policy(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_SWITCHER_POLICY_WIDE);

    // Display preferences are separate from chatbox sources and connections.
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

    apply_animation_setting();
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

    preview_revealer_ = gtk_revealer_new();
    gtk_revealer_set_child(GTK_REVEALER(preview_revealer_), preview_label_);
    gtk_revealer_set_transition_type(GTK_REVEALER(preview_revealer_), GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
    gtk_revealer_set_reveal_child(GTK_REVEALER(preview_revealer_), TRUE);
    gtk_box_append(GTK_BOX(card), preview_revealer_);
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
        "Heart Rate", "Music", "Clock", "Custom Text", "Battery", "System Stats", "Session Timer"
    };
    static const char* icons[COMP_COUNT] = {
        "emblem-favorite-symbolic", "audio-x-generic-symbolic", "preferences-system-time-symbolic",
        "accessories-text-editor-symbolic", "battery-symbolic", "utilities-system-monitor-symbolic",
        "alarm-symbolic",
    };
    static constexpr bool AppSettings::*enabled[COMP_COUNT] = {
        &AppSettings::show_heart_component, &AppSettings::show_song_component,
        &AppSettings::show_clock_component, &AppSettings::show_custom_component,
        &AppSettings::show_battery_component, &AppSettings::show_system_component,
        &AppSettings::show_timer_component,
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
        default:
            break;
    }
    gtk_list_box_append(GTK_LIST_BOX(component_list_), source.row);
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

// the cog menu. this replaced the Display tab, which was two toggles on a
// mostly-empty page. checkmarks are the natural control for this in a menu,
// and it keeps the tab bar down to things that genuinely need a page.
GtkWidget* AppWindow::build_settings_menu() {
    GtkWidget* popover = gtk_popover_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);

    auto add_check = [&](const char* label, bool active, std::function<void(bool)> cb) {
        GtkWidget* check = gtk_check_button_new_with_label(label);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(check), active);
        g_signal_connect_data(check, "toggled",
            G_CALLBACK(+[](GtkCheckButton* c, gpointer d) {
                (*static_cast<std::function<void(bool)>*>(d))(
                    gtk_check_button_get_active(c));
            }), new std::function<void(bool)>(std::move(cb)),
            +[](gpointer d, GClosure*) { delete static_cast<std::function<void(bool)>*>(d); },
            static_cast<GConnectFlags>(0));
        gtk_box_append(GTK_BOX(box), check);
    };

    add_check("Box drawing", settings_.box_drawing, [this](bool v) {
        settings_.box_drawing = v; settings_.save(); refresh_preview();
    });
    add_check("Animations", settings_.animations_enabled, [this](bool v) {
        settings_.animations_enabled = v; settings_.save(); apply_animation_setting();
    });

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    return popover;
}

GtkWidget* AppWindow::build_connect_page() {
    GtkWidget* page = adw_preferences_page_new();

    AdwPreferencesGroup* vg = group("VRChat OSC", "Where chatbox messages are sent.");
    GtkWidget* port = spin_row("Input port", "VRChat's default is 9000.",
                               1, 65535, 1, settings_.osc_port, [this](int v) {
        settings_.osc_port = v;
        osc_sender_.set_port((uint16_t)v);
        settings_.save();
    });
    adw_preferences_group_add(vg, port);
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
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), vg);

    // the setup steps that are easy to miss and impossible to debug from here,
    // so they belong in the app rather than only in a readme
    AdwPreferencesGroup* cg = group("Before it shows up in VRChat",
        "The green OSC pill only means cadence wrote packets without error — UDP is "
        "fire-and-forget, so it can't tell you whether VRChat received them.");
    const char* steps[][2] = {
        {"Enable OSC", "VRChat action menu → Options → OSC → Enabled."},
         {"Load Into a World", "Enter a world before testing chatbox output."},
    };
    for (const auto& s : steps) {
        GtkWidget* row = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), s[0]);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), s[1]);
        adw_preferences_group_add(cg, row);
    }
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), cg);

    // --- output cadence ---
    AdwPreferencesGroup* og = group("Output",
        "How often the chatbox is refreshed, and how noisy it is.");
    adw_preferences_group_add(og, state_row("Notification sound",
        "Play VRChat's chat notification with each update", settings_.notification_sfx,
        [this](bool v) {
            settings_.notification_sfx = v;
            settings_.save();
        }));

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

    // --- discord rich presence ---
    if (DiscordPresence::supported()) {
        AdwPreferencesGroup* dg = group("Discord",
            "Show your current song and heart rate on your Discord profile.");

        // the gate. acknowledging once is remembered, so this dialog doesn't
        // reappear on every launch
        GtkWidget* gate_row = state_row("Share with Discord",
            "Required before anything can be published", settings_.discord_acknowledged,
            [this](bool v) {
                if (updating_ui_) return;
                if (v) {
                    show_discord_warning();
                } else {
                    settings_.discord_acknowledged = false;
                    settings_.save();
                    discord_.disconnect();
                    sync_discord_ui();
                }
            });
        discord_toggle_row_ = gate_row;
        adw_preferences_group_add(dg, gate_row);

        struct Field {
            const char* title;
            const char* sub;
            bool AppSettings::*member;
            GtkWidget** row;
        };
        const Field fields[] = {
            {"Music",      "Show what you're listening to", &AppSettings::discord_music,
             &discord_music_row_},
            {"Heart rate", "Show your bpm alongside it",     &AppSettings::discord_heart_rate,
             &discord_hr_row_},
        };
        for (const auto& f : fields) {
            GtkWidget* row = state_row(f.title, f.sub, settings_.*(f.member),
                [this, f](bool v) {
                    if (updating_ui_) return;
                    settings_.*(f.member) = v;
                    settings_.save();
                    // nothing selected means there's nothing left to publish
                    if (!settings_.discord_music && !settings_.discord_heart_rate)
                        discord_.clear_presence();
                });
            *f.row = row;
            adw_preferences_group_add(dg, row);
        }

        discord_status_row_ = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(discord_status_row_), "Status");
        adw_action_row_set_subtitle(ADW_ACTION_ROW(discord_status_row_), "off");
        adw_preferences_group_add(dg, discord_status_row_);

        sync_discord_ui();
        adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), dg);
    }

    return page;
}
// shown once, before anything is ever sent. deliberately only two buttons:
// accept or decline. *what* gets shared is then left to the two toggles, so
// the dialog never makes that decision on the user's behalf.
void AppWindow::show_discord_warning() {
    AdwDialog* dlg = adw_alert_dialog_new("Share with Discord?", nullptr);
    auto* dialog = ADW_ALERT_DIALOG(dlg);
    adw_alert_dialog_format_body(dialog,
        "Your Discord status is visible to anyone who can see your profile, and "
        "other apps can read it through Discord's API.\n\n"
        "After this you can choose what to share - song, heart rate, both or "
        "neither - and change it whenever you like.");
    adw_alert_dialog_add_responses(dialog,
        "nope", "No thanks",
        "acknowledge", "Acknowledge",
        nullptr);
    adw_alert_dialog_set_response_appearance(dialog, "acknowledge", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(dialog, "nope");
    adw_alert_dialog_set_close_response(dialog, "nope");

    g_signal_connect_data(dialog, "response",
        G_CALLBACK(+[](AdwAlertDialog* d, const char* response, gpointer) {
            auto* self = static_cast<AppWindow*>(g_object_get_data(G_OBJECT(d), "cadence-self"));
            if (!self) return;
            if (std::string(response ? response : "") == "acknowledge") {
                self->settings_.discord_acknowledged = true;
                self->settings_.save();
            }
            // declined or dismissed: snap the switch back either way
            self->sync_discord_ui();
        }),
        nullptr, nullptr, static_cast<GConnectFlags>(0));

    g_object_set_data(G_OBJECT(dlg), "cadence-self", this);
    adw_dialog_present(dlg, GTK_WIDGET(window_));
}

// pushes settings into the discord rows. guarded by updating_ui_ so writing the
// switch state doesn't re-enter the handler that opened the dialog.
void AppWindow::sync_discord_ui() {
    updating_ui_ = true;

    const bool acked = settings_.discord_acknowledged;
    adw_switch_row_set_active(ADW_SWITCH_ROW(discord_toggle_row_), acked);
    adw_switch_row_set_active(ADW_SWITCH_ROW(discord_music_row_), settings_.discord_music);
    adw_switch_row_set_active(ADW_SWITCH_ROW(discord_hr_row_), settings_.discord_heart_rate);
    gtk_widget_set_sensitive(discord_music_row_, acked);
    gtk_widget_set_sensitive(discord_hr_row_, acked);

    updating_ui_ = false;
}

void AppWindow::update_discord() {
    if (!DiscordPresence::supported()) return;

    if (!settings_.discord_acknowledged) {
        if (discord_.connected()) discord_.disconnect();
        if (discord_status_row_)
            adw_action_row_set_subtitle(ADW_ACTION_ROW(discord_status_row_), "off");
        return;
    }

    if (settings_.discord_app_id.empty()) {
        if (discord_status_row_)
            adw_action_row_set_subtitle(ADW_ACTION_ROW(discord_status_row_),
                                         "needs an Application ID");
        return;
    }

    if (!discord_.connected()) {
        if (!discord_.connect(settings_.discord_app_id)) return;
    }
    discord_.pump();

    // nothing selected means nothing gets published
    if (!settings_.discord_music && !settings_.discord_heart_rate) {
        discord_.clear_presence();
        if (discord_status_row_)
            adw_action_row_set_subtitle(ADW_ACTION_ROW(discord_status_row_),
                                         "nothing selected");
        return;
    }

    // discord renders `state` as the first line, `details` beneath it. with only
    // one thing shared it takes the top line rather than leaving a blank row
    // underneath it.
    std::string song;
    if (settings_.discord_music && last_track_) {
        song = last_track_->title;
        if (!last_track_->artist.empty()) song += " \u2014 " + last_track_->artist;
        // discord caps each field at 128 bytes; trim well short of that so the
        // status never gets cut off mid-word
        if (g_utf8_strlen(song.c_str(), -1) > 64) {
            const char* cut = g_utf8_offset_to_pointer(song.c_str(), 61);
            song = std::string(song.c_str(), cut - song.c_str()) + "\u2026";
        }
    }
    std::string heart;
    if (settings_.discord_heart_rate && last_hr_.has_data)
        heart = "\u2665 " + std::to_string(last_hr_.current) + " bpm";

    std::string top = song, bottom = heart;
    if (top.empty()) { top = bottom; bottom.clear(); }

    discord_.set_presence(top, bottom);

    if (discord_status_row_) {
        std::string s = "sharing ";
        if (settings_.discord_music && settings_.discord_heart_rate) s += "song + heart rate";
        else if (settings_.discord_music) s += "song";
        else s += "heart rate";
        adw_action_row_set_subtitle(ADW_ACTION_ROW(discord_status_row_), s.c_str());
    }
}

void AppWindow::apply_animation_setting() {
    gtk_revealer_set_transition_duration(GTK_REVEALER(preview_revealer_),
                                          settings_.animations_enabled ? 200 : 0);
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
    } else if (osc_sender_.send_chatbox(text, true, settings_.notification_sfx)) {
        last_osc_send_ = now;
        status = "OSC packet sent";
    } else {
        status = "Could not send the OSC packet";
    }
    if (manual) adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(toast_overlay_), adw_toast_new(status));
}

void AppWindow::refresh_preview(bool send) {
    const bool raw = settings_.use_raw_template && !settings_.raw_template.empty();
    last_track_ = media_.current_track(settings_.show_music_progress ||
                                        (raw && settings_.raw_template.find("music_progress") != std::string::npos));

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
    const TemplateData data{&last_hr_, last_track_ ? &*last_track_ : nullptr,
                            &last_clock_, &last_battery_, &last_system_, timer};

    preview_text_ = render_chatbox(data, settings_);
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

    // discord gets the same data cadence already had. it never learns anything
    // about vrchat beyond what the user chose to publish.
    update_discord();
}

gboolean AppWindow::on_tick(gpointer user_data) {
    static_cast<AppWindow*>(user_data)->refresh_preview(true);
    return G_SOURCE_CONTINUE;
}

} // namespace cadence
