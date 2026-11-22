#include "app_window.hpp"

#include <utility>

namespace cadence {

void AppWindow::set_sending(bool sending) {
    if (quitting_ || settings_.send_automatically == sending) return;
    settings_.send_automatically = sending;
    settings_.save();
    if (tray_) tray_->set_sending(sending);
    refresh_close_dialog();
    refresh_preview();
}

void AppWindow::present(const std::string& startup_id) {
    if (!window_ || quitting_) return;
    if (!startup_id.empty()) gtk_window_set_startup_id(GTK_WINDOW(window_), startup_id.c_str());
    gtk_window_present(GTK_WINDOW(window_));
    if (std::exchange(held_in_tray_, false)) g_application_release(G_APPLICATION(app_));
}

void AppWindow::hide_to_tray() {
    if (!window_ || quitting_ || held_in_tray_ || !tray_ || !tray_->available()) return;
    g_application_hold(G_APPLICATION(app_));
    held_in_tray_ = true;
    gtk_widget_set_visible(window_, FALSE);
}

void AppWindow::prepare_to_quit() {
    if (std::exchange(quitting_, true)) return;
    if (tray_) tray_->stop();
    g_clear_handle_id(&tick_id_, g_source_remove);
    vrchat_listener_.stop();
    if (lyrics_job_) g_cancellable_cancel(lyrics_job_);
    if (std::exchange(held_in_tray_, false)) g_application_release(G_APPLICATION(app_));
}

void AppWindow::quit() {
    prepare_to_quit();
    g_application_quit(G_APPLICATION(app_));
}

void AppWindow::tray_availability_changed(bool available) {
    if (quitting_) return;
    gtk_widget_set_visible(close_behavior_button_, available);
    refresh_close_dialog();
    if (!available && held_in_tray_) present();
}

void AppWindow::refresh_close_dialog() {
    if (!close_dialog_) return;
    const bool available = tray_ && tray_->available();
    adw_alert_dialog_set_response_enabled(close_dialog_, "hide", available);
    adw_alert_dialog_set_body(close_dialog_, !available
        ? "The system tray is no longer available. You can quit or keep Cadence open."
        : settings_.send_automatically
            ? "If you hide Cadence, it will keep sending to your VRChat chatbox."
            : "Sending is paused and will stay paused if you hide Cadence.");
}

gboolean AppWindow::request_close() {
    if (quitting_) return FALSE;
    if (close_dialog_) return TRUE;
    if (!tray_ || !tray_->available() || settings_.close_action == CloseAction::Quit) {
        prepare_to_quit();
        return FALSE;
    }
    if (settings_.close_action == CloseAction::HideToTray) {
        hide_to_tray();
        return TRUE;
    }

    close_dialog_ = ADW_ALERT_DIALOG(adw_alert_dialog_new("Close Cadence?", nullptr));
    auto* remember = gtk_check_button_new_with_label("Remember my choice");
    adw_alert_dialog_set_extra_child(close_dialog_, remember);
    g_object_set_data(G_OBJECT(close_dialog_), "remember-close-choice", remember);
    adw_alert_dialog_add_responses(close_dialog_, "cancel", "Cancel", "quit", "Quit",
        "hide", "Hide to Tray", nullptr);
    adw_alert_dialog_set_close_response(close_dialog_, "cancel");
    adw_alert_dialog_set_default_response(close_dialog_, "cancel");
    adw_alert_dialog_set_response_appearance(close_dialog_, "hide", ADW_RESPONSE_SUGGESTED);
    refresh_close_dialog();
    g_signal_connect(close_dialog_, "closed", G_CALLBACK(+[](AdwDialog* dialog, gpointer data) {
        auto& self = *static_cast<AppWindow*>(data);
        if (self.close_dialog_ && ADW_DIALOG(self.close_dialog_) == dialog) self.close_dialog_ = nullptr;
    }), this);
    g_signal_connect(close_dialog_, "response", G_CALLBACK(+[](AdwAlertDialog* dialog, const char* response, gpointer data) {
        auto& self = *static_cast<AppWindow*>(data);
        self.close_dialog_ = nullptr;
        const bool quit = g_strcmp0(response, "quit") == 0;
        const bool hide = g_strcmp0(response, "hide") == 0 && self.tray_ && self.tray_->available();
        if (!quit && !hide) return;
        auto* remember = GTK_CHECK_BUTTON(g_object_get_data(G_OBJECT(dialog), "remember-close-choice"));
        if (gtk_check_button_get_active(remember)) {
            self.settings_.close_action = quit ? CloseAction::Quit : CloseAction::HideToTray;
            self.settings_.save();
        }
        if (quit) self.quit();
        else self.hide_to_tray();
    }), this);
    adw_dialog_present(ADW_DIALOG(close_dialog_), window_);
    return TRUE;
}

void AppWindow::edit_close_behavior() {
    if (!window_ || !tray_ || !tray_->available()) return;
    auto* dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new("When Closing Cadence", nullptr));
    const char* current = settings_.close_action == CloseAction::Quit ? "Quit" :
        settings_.close_action == CloseAction::HideToTray ? "Hide to Tray" : "Ask";
    adw_alert_dialog_format_body(dialog, "Current choice: %s", current);
    adw_alert_dialog_add_responses(dialog, "cancel", "Cancel", "ask", "Ask", "quit", "Quit",
        "hide", "Hide to Tray", nullptr);
    adw_alert_dialog_set_close_response(dialog, "cancel");
    adw_alert_dialog_set_default_response(dialog, "cancel");
    g_signal_connect(dialog, "response", G_CALLBACK(+[](AdwAlertDialog*, const char* response, gpointer data) {
        auto& self = *static_cast<AppWindow*>(data);
        if (g_strcmp0(response, "ask") == 0) self.settings_.close_action = CloseAction::Ask;
        else if (g_strcmp0(response, "quit") == 0) self.settings_.close_action = CloseAction::Quit;
        else if (g_strcmp0(response, "hide") == 0 && self.tray_ && self.tray_->available())
            self.settings_.close_action = CloseAction::HideToTray;
        else return;
        self.settings_.save();
    }), this);
    adw_dialog_present(ADW_DIALOG(dialog), window_);
}

} // namespace cadence
