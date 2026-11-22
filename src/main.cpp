#include "app_window.hpp"
#include <adwaita.h>

namespace {

// GTK4's stock look is what made the UI feel unfinished. libadwaita supplies the
// GNOME HIG structure (view switcher, preference pages, clamp); this stylesheet
// handles the rest so the app reads as one deliberate piece rather than a
// default-widget assembly job.
//
// registered on activate, not in main(): before that there is no default
// GdkDisplay and the call fails silently - which is exactly how an earlier
// version ended up shipping styles that never applied.
const char* css_data = R"CSS(
/* status pills in the preview header */
.status-pill {
    background: alpha(currentColor, 0.07);
    border-radius: 999px;
    padding: 3px 11px;
    font-size: 0.85em;
    font-weight: 500;
}
.status-pill .dot { font-size: 0.7em; opacity: 0.35; }
.status-pill.is-ok .dot    { color: #30a46c; opacity: 1; }
.status-pill.is-error .dot { color: #e01b24; opacity: 1; }
.is-error { color: #e01b24; }

/* the preview sits inside a preferences group, which already supplies the
   surface and margins - so it adds no background of its own and can't read as a
   foreign panel bolted onto the page */
.preview-card { padding: 2px 0; }

/* the message itself gets an inset panel, which is what makes it read as a
   readout of the app's output rather than another settings label */
.preview-output {
    background: alpha(@window_fg_color, 0.055);
    border: 1px solid alpha(@window_fg_color, 0.08);
    border-radius: 8px;
    padding: 16px 14px;
    font-size: 1.02em;
}


/* pill buttons for connect/disconnect. kept small so they read as a pair of
   actions rather than the loudest thing in the row */
.pill {
    border-radius: 999px;
    padding: 2px 12px;
    min-height: 24px;
    font-size: 0.9em;
    font-weight: 500;
}
.flat { background: none; border: none; }
.flat:hover { background: alpha(currentColor, 0.07); }

/* numeric stepper: small square buttons flanking a right-aligned value.
   text labels rather than icons so they render on every theme. */
.stepper {
    min-width: 30px;
    min-height: 26px;
    padding: 0 8px;
    border-radius: 8px;
    font-size: 1.05em;
    font-weight: 600;
}
.stepper:hover { background: alpha(currentColor, 0.12); }
.stepper:active { background: alpha(currentColor, 0.2); }

textview, textview > stack > scrolledwindow > viewport > box {
    background: alpha(currentColor, 0.03);
}
/* the template text was sitting flush against the frame edge */
textview {
    padding: 10px 12px;
}
)CSS";

void load_css() {
    static GtkCssProvider* provider = [] {
        GtkCssProvider* p = gtk_css_provider_new();
        gtk_css_provider_load_from_string(p, css_data);
        return p;
    }();
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

void on_activate(AdwApplication* app, gpointer) {
    load_css();
    // intentionally left alive: it owns everything and lives as long as the window does
    new cadence::AppWindow(app);
}

} // namespace

int main(int argc, char** argv) {
    AdwApplication* app = adw_application_new("dev.cadence.app", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
