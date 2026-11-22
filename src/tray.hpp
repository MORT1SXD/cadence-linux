#pragma once

#include <cstdint>
#include <functional>
#include <gio/gio.h>
#include <memory>
#include <string>

namespace cadence {

// StatusNotifierItem and DBusMenu on the user's session bus. A watcher alone
// is not enough: hiding needs a registered host and a registered item.
class Tray : public std::enable_shared_from_this<Tray> {
public:
    struct Actions {
        std::function<void(const std::string&)> show;
        std::function<void()> toggle_sending;
        std::function<void()> quit;
        std::function<void(bool)> availability_changed;
    };

    explicit Tray(Actions actions);
    ~Tray();
    void start(bool sending);
    void stop();
    void set_sending(bool sending);
    bool available() const { return available_; }

private:
    Actions actions_;
    GDBusConnection* bus_ = nullptr;
    GVariant* pixels_ = nullptr;
    guint item_ = 0;
    guint menu_ = 0;
    guint watch_ = 0;
    guint host_signals_ = 0;
    guint property_signals_ = 0;
    std::string watcher_;
    std::uint64_t host_query_ = 0;
    std::uint64_t registration_query_ = 0;
    guint revision_ = 1;
    bool registering_ = false;
    bool registered_ = false;
    bool available_ = false;
    bool sending_ = true;
    std::string activation_token_;
    gint64 token_received_ = 0;

    void watcher_changed(const char* owner);
    void check_host();
    void register_item();
    void set_available(bool available);
    void activate_menu_item(int id);
    GVariant* menu_property(int id, const char* name) const;
    GVariant* menu_properties(int id, GVariant* names) const;
    GVariant* layout(int id, int depth, GVariant* names) const;

    static GVariant* item_property(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GError**, gpointer);
    static GVariant* menu_bus_property(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GError**, gpointer);
    static void item_call(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant*, GDBusMethodInvocation*, gpointer);
    static void menu_call(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant*, GDBusMethodInvocation*, gpointer);
};

} // namespace cadence
