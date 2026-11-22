#include "app_window.hpp"

#include <algorithm>
#include <functional>
#include <utility>

namespace cadence {

namespace {

using Action = std::function<void()>;

void on_click(GtkWidget* button, Action action) {
    g_signal_connect_data(button, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        // An action can remove its own row, and with it the original closure.
        auto run = *static_cast<Action*>(data);
        run();
    }), new Action(std::move(action)), +[](gpointer data, GClosure*) { delete static_cast<Action*>(data); },
        static_cast<GConnectFlags>(0));
}

void on_response(AdwAlertDialog* dialog, const char* signal, Action action) {
    g_signal_connect_data(dialog, signal, G_CALLBACK(+[](AdwAlertDialog*, const char*, gpointer data) {
        auto run = *static_cast<Action*>(data);
        run();
    }), new Action(std::move(action)), +[](gpointer data, GClosure*) { delete static_cast<Action*>(data); },
        static_cast<GConnectFlags>(0));
}

std::string text_from_buffer(GtkTextBuffer* buffer) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar* value = gtk_text_buffer_get_text(buffer, &start, &end, TRUE);
    std::string text(value);
    g_free(value);
    return text;
}

const char* status_problem(const std::string& text) {
    if (text.size() > AppSettings::kMaxStatusBytes) return "Text is too large";
    if (!g_utf8_validate(text.data(), text.size(), nullptr)) return "Invalid text";

    bool has_text = false;
    for (const char* p = text.c_str(); *p; p = g_utf8_next_char(p)) {
        const auto ch = g_utf8_get_char(p);
        if ((g_unichar_iscntrl(ch) && ch != '\n' && ch != '\t') || ch == 0x2028 || ch == 0x2029)
            return "Use plain text and regular line breaks";
        if (!g_unichar_isspace(ch) && !g_unichar_iszerowidth(ch)) has_text = true;
    }
    if (!has_text) return "Enter a status";
    if (stats_for(text).over_limit) return "Over the chatbox limit";
    return nullptr;
}

} // namespace

GtkWidget* AppWindow::build_status_page() {
    GtkWidget* page = adw_preferences_page_new();
    status_group_ = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(status_group_, "Saved Statuses");
    adw_preferences_group_set_description(status_group_,
        "Turn on one status to replace your usual chatbox. Pause Sending still applies.");

    add_status_button_ = gtk_button_new_with_label("Add Status");
    gtk_widget_set_valign(add_status_button_, GTK_ALIGN_CENTER);
    adw_preferences_group_set_header_suffix(status_group_, add_status_button_);
    on_click(add_status_button_, [this] { edit_status(std::nullopt); });

    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), status_group_);
    rebuild_status_rows();
    return page;
}

void AppWindow::rebuild_status_rows() {
    if (empty_status_row_) {
        adw_preferences_group_remove(status_group_, empty_status_row_);
        empty_status_row_ = nullptr;
    }
    // Guard against any signals emitted during widget removal.
    syncing_status_ = true;
    for (auto* row : status_rows_) adw_preferences_group_remove(status_group_, row);
    status_rows_.clear();

    const auto& phrases = settings_.status_phrases;
    gtk_widget_set_sensitive(add_status_button_, phrases.size() < AppSettings::kMaxStatuses);
    if (phrases.empty()) {
        empty_status_row_ = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(empty_status_row_), "No Saved Statuses");
        adw_action_row_set_subtitle(ADW_ACTION_ROW(empty_status_row_), "Add a message like “Be right back”.");
        adw_preferences_group_add(status_group_, empty_status_row_);
    }

    for (size_t index = 0; index < phrases.size(); ++index) {
        GtkWidget* row = adw_action_row_new();
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), phrases[index].c_str());
        adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
        adw_action_row_set_title_lines(ADW_ACTION_ROW(row), 2);

        GtkWidget* toggle = gtk_switch_new();
        gtk_widget_set_valign(toggle, GTK_ALIGN_CENTER);
        gtk_accessible_update_property(GTK_ACCESSIBLE(toggle), GTK_ACCESSIBLE_PROPERTY_LABEL,
            phrases[index].c_str(), -1);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), toggle);
        struct Toggle { AppWindow* window; size_t index; };
        g_signal_connect_data(toggle, "notify::active",
            G_CALLBACK(+[](GtkSwitch* toggle, GParamSpec*, gpointer data) {
                const auto& context = *static_cast<Toggle*>(data);
                if (context.window->syncing_status_) return;
                if (gtk_switch_get_active(toggle)) context.window->set_active_status(context.index);
                else if (context.window->active_status_ == context.index) context.window->set_active_status(std::nullopt);
            }), new Toggle{this, index}, +[](gpointer data, GClosure*) { delete static_cast<Toggle*>(data); },
            static_cast<GConnectFlags>(0));

        GtkWidget* menu = gtk_menu_button_new();
        gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu), "view-more-symbolic");
        gtk_widget_set_tooltip_text(menu, "Manage Status");
        gtk_widget_set_valign(menu, GTK_ALIGN_CENTER);
        gtk_widget_add_css_class(menu, "flat");
        GtkWidget* popover = gtk_popover_new();
        GtkWidget* menu_items = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        auto item = [popover, menu_items](const char* label, bool enabled, Action action) {
            GtkWidget* button = gtk_button_new_with_label(label);
            gtk_widget_add_css_class(button, "flat");
            gtk_widget_set_sensitive(button, enabled);
            on_click(button, [popover, action = std::move(action)] {
                gtk_popover_popdown(GTK_POPOVER(popover));
                action();
            });
            gtk_box_append(GTK_BOX(menu_items), button);
        };
        item("Edit…", true, [this, index] { edit_status(index); });
        item("Move Up", index > 0, [this, index] { if (index) move_status(index, index - 1); });
        item("Move Down", index + 1 < phrases.size(), [this, index] { move_status(index, index + 1); });
        item("Delete…", true, [this, index] { confirm_delete_status(index); });
        gtk_popover_set_child(GTK_POPOVER(popover), menu_items);
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu), popover);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), menu);

        // The switch, not the row or its editing menu, changes the active status.
        g_object_set_data(G_OBJECT(row), "cadence-status-switch", toggle);
        adw_preferences_group_add(status_group_, row);
        status_rows_.push_back(row);
    }
    sync_status_controls();
}

void AppWindow::sync_status_controls() {
    syncing_status_ = true;
    for (size_t i = 0; i < status_rows_.size(); ++i) {
        auto* row = status_rows_[i];
        auto* toggle = GTK_SWITCH(g_object_get_data(G_OBJECT(row), "cadence-status-switch"));
        const char* problem = status_problem(settings_.status_phrases[i]);
        gtk_switch_set_active(toggle, active_status_ == i);
        gtk_widget_set_sensitive(GTK_WIDGET(toggle), !problem);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), problem ? problem : active_status_ == i ? "Active" : "");
    }
    syncing_status_ = false;
    adw_banner_set_revealed(status_banner_, active_status_.has_value());
}

void AppWindow::set_active_status(std::optional<size_t> index) {
    if (index && (*index >= settings_.status_phrases.size() || status_problem(settings_.status_phrases[*index]))) {
        sync_status_controls();
        return;
    }
    active_status_ = index;
    sync_status_controls();
    refresh_preview();
}

void AppWindow::edit_status(std::optional<size_t> index) {
    if (index ? *index >= settings_.status_phrases.size()
              : settings_.status_phrases.size() >= AppSettings::kMaxStatuses) return;
    auto* dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(index ? "Edit Status" : "Add Status",
        index && index == active_status_ ? "Saving changes updates the active status." : nullptr));
    auto* contents = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    auto* scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroller), 100);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    auto* editor = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(editor), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(editor), FALSE);
    gtk_accessible_update_property(GTK_ACCESSIBLE(editor), GTK_ACCESSIBLE_PROPERTY_LABEL, "Status text", -1);
    auto* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
    if (index) gtk_text_buffer_set_text(buffer, settings_.status_phrases[*index].c_str(), -1);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), editor);
    gtk_box_append(GTK_BOX(contents), scroller);
    auto* counter = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(counter), 0.0);
    gtk_label_set_wrap(GTK_LABEL(counter), TRUE);
    gtk_widget_add_css_class(counter, "dim-label");
    gtk_box_append(GTK_BOX(contents), counter);
    adw_alert_dialog_set_extra_child(dialog, contents);

    adw_alert_dialog_add_responses(dialog, "cancel", "Cancel", "save", "Save", nullptr);
    adw_alert_dialog_set_close_response(dialog, "cancel");
    adw_alert_dialog_set_response_appearance(dialog, "save", ADW_RESPONSE_SUGGESTED);
    auto update = [buffer, counter, dialog] {
        const std::string text = text_from_buffer(buffer);
        const char* problem = status_problem(text);
        adw_alert_dialog_set_response_enabled(dialog, "save", !problem);
        if (text.size() > AppSettings::kMaxStatusBytes) {
            gtk_label_set_text(GTK_LABEL(counter), problem);
            return;
        }
        const auto s = stats_for(text);
        const std::string count = std::to_string(s.chars) + " / 144 characters · " +
            std::to_string(s.lines) + " / 9 lines" + (problem && !text.empty() ? std::string(" · ") + problem : "");
        gtk_label_set_text(GTK_LABEL(counter), count.c_str());
    };
    update();
    g_signal_connect_data(buffer, "changed", G_CALLBACK(+[](GtkTextBuffer*, gpointer data) {
        (*static_cast<Action*>(data))();
    }), new Action(std::move(update)), +[](gpointer data, GClosure*) { delete static_cast<Action*>(data); },
        static_cast<GConnectFlags>(0));

    on_response(dialog, "response::save", [this, index, buffer] {
        std::string text = text_from_buffer(buffer);
        if (status_problem(text)) return;
        auto& phrases = settings_.status_phrases;
        if (index) {
            if (*index >= phrases.size()) return;
            phrases[*index] = std::move(text);
        } else {
            if (phrases.size() >= AppSettings::kMaxStatuses) return;
            phrases.push_back(std::move(text));
        }
        settings_.save();
        rebuild_status_rows();
        refresh_preview();
    });
    adw_dialog_set_focus(ADW_DIALOG(dialog), editor);
    adw_dialog_present(ADW_DIALOG(dialog), window_);
}

void AppWindow::move_status(size_t from, size_t to) {
    auto& phrases = settings_.status_phrases;
    if (from >= phrases.size() || to >= phrases.size() || from == to) return;
    std::swap(phrases[from], phrases[to]);
    if (active_status_ == from) active_status_ = to;
    else if (active_status_ == to) active_status_ = from;
    settings_.save();
    rebuild_status_rows();
}

void AppWindow::confirm_delete_status(size_t index) {
    if (index >= settings_.status_phrases.size()) return;
    auto* dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new("Delete This Status?",
        active_status_ == index ? "Your chatbox will return to its usual sources." : nullptr));
    adw_alert_dialog_add_responses(dialog, "cancel", "Cancel", "delete", "Delete", nullptr);
    adw_alert_dialog_set_close_response(dialog, "cancel");
    adw_alert_dialog_set_default_response(dialog, "cancel");
    adw_alert_dialog_set_response_appearance(dialog, "delete", ADW_RESPONSE_DESTRUCTIVE);
    on_response(dialog, "response::delete", [this, index] {
        auto& phrases = settings_.status_phrases;
        if (index >= phrases.size()) return;
        phrases.erase(phrases.begin() + index);
        if (active_status_ == index) active_status_.reset();
        else if (active_status_ && *active_status_ > index) --*active_status_;
        settings_.save();
        rebuild_status_rows();
        refresh_preview();
    });
    adw_dialog_present(ADW_DIALOG(dialog), window_);
}

} // namespace cadence
