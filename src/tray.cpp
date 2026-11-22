#include "tray.hpp"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <cstring>
#include <utility>
#include <vector>

namespace cadence {
namespace {

constexpr char kWatcher[] = "org.kde.StatusNotifierWatcher";
constexpr char kWatcherPath[] = "/StatusNotifierWatcher";
constexpr char kItemPath[] = "/StatusNotifierItem";
constexpr char kMenuPath[] = "/MenuBar";
constexpr char kMenu[] = "com.canonical.dbusmenu";
constexpr char kProperties[] = "org.freedesktop.DBus.Properties";
constexpr char kHostProperty[] = "IsStatusNotifierHostRegistered";

constexpr char kInterfaces[] = R"xml(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <method name="ProvideXdgActivationToken"><arg type="s" direction="in"/></method>
    <method name="ContextMenu"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
    <method name="Activate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
    <method name="SecondaryActivate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
    <method name="Scroll"><arg type="i" direction="in"/><arg type="s" direction="in"/></method>
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="WindowId" type="i" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="IconThemePath" type="s" access="read"/>
    <property name="IconPixmap" type="a(iiay)" access="read"/>
    <property name="OverlayIconName" type="s" access="read"/>
    <property name="OverlayIconPixmap" type="a(iiay)" access="read"/>
    <property name="AttentionIconName" type="s" access="read"/>
    <property name="AttentionIconPixmap" type="a(iiay)" access="read"/>
    <property name="AttentionMovieName" type="s" access="read"/>
    <property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
    <property name="Menu" type="o" access="read"/>
    <signal name="NewToolTip"/>
  </interface>
  <interface name="com.canonical.dbusmenu">
    <property name="Version" type="u" access="read"/>
    <property name="TextDirection" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconThemePath" type="as" access="read"/>
    <method name="GetLayout">
      <arg type="i" direction="in"/><arg type="i" direction="in"/><arg type="as" direction="in"/>
      <arg type="u" direction="out"/><arg type="(ia{sv}av)" direction="out"/>
    </method>
    <method name="GetGroupProperties">
      <arg type="ai" direction="in"/><arg type="as" direction="in"/>
      <arg type="a(ia{sv})" direction="out"/>
    </method>
    <method name="GetProperty">
      <arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="out"/>
    </method>
    <method name="Event">
      <arg type="i" direction="in"/><arg type="s" direction="in"/>
      <arg type="v" direction="in"/><arg type="u" direction="in"/>
    </method>
    <method name="EventGroup"><arg type="a(isvu)" direction="in"/><arg type="ai" direction="out"/></method>
    <method name="AboutToShow"><arg type="i" direction="in"/><arg type="b" direction="out"/></method>
    <method name="AboutToShowGroup">
      <arg type="ai" direction="in"/><arg type="ai" direction="out"/><arg type="ai" direction="out"/>
    </method>
    <signal name="LayoutUpdated"><arg type="u"/><arg type="i"/></signal>
    <signal name="ItemsPropertiesUpdated"><arg type="a(ia{sv})"/><arg type="a(ias)"/></signal>
  </interface>
</node>)xml";

enum Item { Root = 0, Show = 1, ToggleSending = 2, Quit = 3 };

struct PendingCall {
    std::weak_ptr<Tray> tray;
    std::uint64_t query;
};

GVariant* empty_array(const GVariantType* type) {
    return g_variant_new_array(type, nullptr, 0);
}

// Tray hosts run outside the AppImage, so they can't see its private icon theme.
GVariant* icon_pixmaps() {
    GVariantBuilder sizes;
    g_variant_builder_init(&sizes, G_VARIANT_TYPE("a(iiay)"));
    for (const int size : {16, 22, 32, 48}) {
        auto* image = gdk_pixbuf_new_from_resource_at_scale(
            "/dev/cadence/app/icons/48x48/apps/dev.cadence.app.png", size, size, TRUE, nullptr);
        if (!image) continue;

        const int w = gdk_pixbuf_get_width(image), h = gdk_pixbuf_get_height(image);
        const int channels = gdk_pixbuf_get_n_channels(image), stride = gdk_pixbuf_get_rowstride(image);
        const auto* source = gdk_pixbuf_read_pixels(image);
        std::vector<guint8> argb;
        argb.reserve(w * h * 4);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const auto* px = source + y * stride + x * channels;
                argb.push_back(channels == 4 ? px[3] : 255);
                argb.insert(argb.end(), px, px + 3);
            }
        }
        g_variant_builder_add(&sizes, "(ii@ay)", w, h,
            g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, argb.data(), argb.size(), 1));
        g_object_unref(image);
    }
    return g_variant_ref_sink(g_variant_builder_end(&sizes));
}

bool same(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

} // namespace

Tray::Tray(Actions actions) : actions_(std::move(actions)) {}
Tray::~Tray() {
    stop();
    g_clear_pointer(&pixels_, g_variant_unref);
}

void Tray::start(bool sending) {
    if (bus_) return;
    sending_ = sending;
    bus_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!bus_) return;

    static const GDBusInterfaceVTable item_table{&Tray::item_call, &Tray::item_property, nullptr, {}};
    static const GDBusInterfaceVTable menu_table{&Tray::menu_call, &Tray::menu_bus_property, nullptr, {}};
    auto* info = g_dbus_node_info_new_for_xml(kInterfaces, nullptr);
    if (!info) { stop(); return; }
    item_ = g_dbus_connection_register_object(bus_, kItemPath, info->interfaces[0], &item_table, this, nullptr, nullptr);
    menu_ = g_dbus_connection_register_object(bus_, kMenuPath, info->interfaces[1], &menu_table, this, nullptr, nullptr);
    g_dbus_node_info_unref(info);
    if (!item_ || !menu_) { stop(); return; }

    watch_ = g_bus_watch_name_on_connection(bus_, kWatcher, G_BUS_NAME_WATCHER_FLAGS_NONE,
        +[](GDBusConnection*, const gchar*, const gchar* owner, gpointer data) {
            static_cast<Tray*>(data)->watcher_changed(owner);
        }, +[](GDBusConnection*, const gchar*, gpointer data) {
            static_cast<Tray*>(data)->watcher_changed(nullptr);
        }, this, nullptr);
}

void Tray::stop() {
    g_clear_handle_id(&watch_, g_bus_unwatch_name);
    watcher_changed(nullptr);
    if (item_) g_dbus_connection_unregister_object(bus_, std::exchange(item_, 0));
    if (menu_) g_dbus_connection_unregister_object(bus_, std::exchange(menu_, 0));
    g_clear_object(&bus_);
}

void Tray::watcher_changed(const char* owner) {
    if (host_signals_) g_dbus_connection_signal_unsubscribe(bus_, std::exchange(host_signals_, 0));
    if (property_signals_) g_dbus_connection_signal_unsubscribe(bus_, std::exchange(property_signals_, 0));
    watcher_ = owner ? owner : "";
    activation_token_.clear();
    ++host_query_;
    ++registration_query_;
    registered_ = registering_ = false;
    set_available(false);
    if (watcher_.empty()) return;

    host_signals_ = g_dbus_connection_signal_subscribe(bus_, owner, kWatcher, nullptr, kWatcherPath, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, +[](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* signal,
                                     GVariant*, gpointer data) {
            if (same(signal, "StatusNotifierHostRegistered") || same(signal, "StatusNotifierHostUnregistered"))
                static_cast<Tray*>(data)->check_host();
        }, this, nullptr);
    property_signals_ = g_dbus_connection_signal_subscribe(bus_, owner, kProperties, "PropertiesChanged", kWatcherPath, kWatcher,
        G_DBUS_SIGNAL_FLAGS_NONE, +[](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                                     GVariant*, gpointer data) { static_cast<Tray*>(data)->check_host(); },
        this, nullptr);
    check_host();
}

void Tray::check_host() {
    if (watcher_.empty()) return;
    g_dbus_connection_call(bus_, watcher_.c_str(), kWatcherPath, kProperties, "Get",
        g_variant_new("(ss)", kWatcher, kHostProperty), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NO_AUTO_START, 1500,
        nullptr, +[](GObject* object, GAsyncResult* result, gpointer data) {
            std::unique_ptr<PendingCall> call(static_cast<PendingCall*>(data));
            auto* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object), result, nullptr);
            bool has_host = false;
            if (reply) {
                GVariant* value = nullptr;
                g_variant_get(reply, "(v)", &value);
                has_host = g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN) && g_variant_get_boolean(value);
                g_variant_unref(value);
                g_variant_unref(reply);
            }
            auto self = call->tray.lock();
            if (!self || call->query != self->host_query_) return;
            if (!has_host) {
                ++self->registration_query_;
                self->registered_ = self->registering_ = false;
                self->set_available(false);
            } else if (!self->registered_ && !self->registering_) {
                self->register_item();
            }
        }, new PendingCall{weak_from_this(), ++host_query_});
}

void Tray::register_item() {
    registering_ = true;
    g_dbus_connection_call(bus_, watcher_.c_str(), kWatcherPath, kWatcher, "RegisterStatusNotifierItem",
        g_variant_new("(s)", kItemPath), G_VARIANT_TYPE_UNIT, G_DBUS_CALL_FLAGS_NO_AUTO_START, 1500,
        nullptr, +[](GObject* object, GAsyncResult* result, gpointer data) {
            std::unique_ptr<PendingCall> call(static_cast<PendingCall*>(data));
            auto* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object), result, nullptr);
            const bool registered = reply != nullptr;
            if (reply) g_variant_unref(reply);
            auto self = call->tray.lock();
            if (!self || call->query != self->registration_query_) return;
            self->registering_ = false;
            self->registered_ = registered;
            self->set_available(registered);
        }, new PendingCall{weak_from_this(), ++registration_query_});
}

void Tray::set_available(bool available) {
    if (available_ == available) return;
    available_ = available;
    if (actions_.availability_changed) actions_.availability_changed(available);
}

void Tray::set_sending(bool sending) {
    if (sending_ == sending) return;
    sending_ = sending;
    if (!bus_ || !menu_) return;
    GVariantBuilder changes, props;
    g_variant_builder_init(&changes, G_VARIANT_TYPE("a(ia{sv})"));
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "label", menu_property(ToggleSending, "label"));
    g_variant_builder_add(&changes, "(i@a{sv})", ToggleSending, g_variant_builder_end(&props));
    g_dbus_connection_emit_signal(bus_, nullptr, kMenuPath, kMenu, "ItemsPropertiesUpdated",
        g_variant_new("(@a(ia{sv})@a(ias))", g_variant_builder_end(&changes), empty_array(G_VARIANT_TYPE("(ias)"))), nullptr);
    g_dbus_connection_emit_signal(bus_, nullptr, kMenuPath, kMenu, "LayoutUpdated", g_variant_new("(ui)", ++revision_, Root), nullptr);
    g_dbus_connection_emit_signal(bus_, nullptr, kItemPath, "org.kde.StatusNotifierItem", "NewToolTip", nullptr, nullptr);
}

GVariant* Tray::item_property(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* name,
                             GError**, gpointer data) {
    auto& self = *static_cast<Tray*>(data);
    if (same(name, "Category")) return g_variant_new_string("ApplicationStatus");
    if (same(name, "Id")) return g_variant_new_string("cadence");
    if (same(name, "Title")) return g_variant_new_string("Cadence");
    if (same(name, "Status")) return g_variant_new_string("Active");
    if (same(name, "WindowId")) return g_variant_new_int32(0);
    if (!self.pixels_) self.pixels_ = icon_pixmaps();
    if (same(name, "IconName"))
        return g_variant_new_string(g_variant_n_children(self.pixels_) ? "" : "applications-utilities");
    if (same(name, "IconPixmap")) return g_variant_ref(self.pixels_);
    if (same(name, "ToolTip")) return g_variant_new("(s@a(iiay)ss)", "", g_variant_ref(self.pixels_), "Cadence",
        self.sending_ ? "Automatic sending is on" : "Sending is paused");
    if (same(name, "ItemIsMenu")) return g_variant_new_boolean(FALSE);
    if (same(name, "Menu")) return g_variant_new_object_path(kMenuPath);
    if (g_str_has_suffix(name, "Pixmap")) return empty_array(G_VARIANT_TYPE("(iiay)"));
    return g_variant_new_string("");
}

GVariant* Tray::menu_bus_property(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* name,
                                 GError**, gpointer) {
    if (same(name, "Version")) return g_variant_new_uint32(3);
    if (same(name, "TextDirection")) return g_variant_new_string("ltr");
    if (same(name, "Status")) return g_variant_new_string("normal");
    return empty_array(G_VARIANT_TYPE_STRING);
}

void Tray::item_call(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                     GVariant* args, GDBusMethodInvocation* invocation, gpointer data) {
    auto& self = *static_cast<Tray*>(data);
    g_dbus_method_invocation_return_value(invocation, nullptr);
    if (same(method, "Activate") || same(method, "SecondaryActivate")) self.activate_menu_item(Show);
    else if (same(method, "ProvideXdgActivationToken") && self.available_) {
        const char* token;
        g_variant_get(args, "(&s)", &token);
        if (std::strlen(token) <= 4096) {
            self.activation_token_ = token;
            self.token_received_ = g_get_monotonic_time();
        }
    }
}

GVariant* Tray::menu_property(int id, const char* name) const {
    if (same(name, "children-display")) return id == Root ? g_variant_new_string("submenu") : nullptr;
    if (same(name, "visible") || same(name, "enabled")) return g_variant_new_boolean(TRUE);
    if (!same(name, "label")) return nullptr;
    if (id == Show) return g_variant_new_string("Show Cadence");
    if (id == ToggleSending) return g_variant_new_string(sending_ ? "Pause Sending" : "Resume Sending");
    if (id == Quit) return g_variant_new_string("Quit");
    return nullptr;
}

GVariant* Tray::menu_properties(int id, GVariant* names) const {
    GVariantBuilder properties;
    g_variant_builder_init(&properties, G_VARIANT_TYPE("a{sv}"));
    auto add = [&](const char* name) {
        if (auto* value = menu_property(id, name)) g_variant_builder_add(&properties, "{sv}", name, value);
    };
    if (g_variant_n_children(names) == 0) {
        for (auto* name : {"label", "visible", "enabled", "children-display"}) add(name);
    } else {
        GVariantIter iter;
        g_variant_iter_init(&iter, names);
        const char* name;
        while (g_variant_iter_next(&iter, "&s", &name)) add(name);
    }
    return g_variant_builder_end(&properties);
}

GVariant* Tray::layout(int id, int depth, GVariant* names) const {
    GVariantBuilder children;
    g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
    if (id == Root && depth != 0)
        for (const int child : {Show, ToggleSending, Quit})
            g_variant_builder_add(&children, "v", layout(child, 0, names));
    return g_variant_new("(i@a{sv}@av)", id, menu_properties(id, names), g_variant_builder_end(&children));
}

void Tray::activate_menu_item(int id) {
    if (!available_) return;
    if (id == Show) {
        auto token = std::exchange(activation_token_, {});
        if (g_get_monotonic_time() - token_received_ > 5 * G_USEC_PER_SEC) token.clear();
        if (actions_.show) actions_.show(token);
        return;
    }
    const auto action = id == ToggleSending ? actions_.toggle_sending :
                        id == Quit ? actions_.quit : std::function<void()>{};
    if (action) action();
}

void Tray::menu_call(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* method,
                     GVariant* args, GDBusMethodInvocation* invocation, gpointer data) {
    auto& self = *static_cast<Tray*>(data);
    auto invalid = [&] {
        g_dbus_method_invocation_return_error_literal(invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Unknown menu item");
    };
    if (same(method, "GetLayout")) {
        int id, depth;
        GVariant* names = nullptr;
        g_variant_get(args, "(ii@as)", &id, &depth, &names);
        if (id < Root || id > Quit) invalid();
        else g_dbus_method_invocation_return_value(invocation, g_variant_new("(u@(ia{sv}av))",
            self.revision_, self.layout(id, depth, names)));
        g_variant_unref(names);
    } else if (same(method, "GetGroupProperties")) {
        GVariant *ids, *names;
        g_variant_get(args, "(@ai@as)", &ids, &names);
        GVariantBuilder group;
        g_variant_builder_init(&group, G_VARIANT_TYPE("a(ia{sv})"));
        GVariantIter iter;
        g_variant_iter_init(&iter, ids);
        int id;
        while (g_variant_iter_next(&iter, "i", &id))
            if (id >= Root && id <= Quit) g_variant_builder_add(&group, "(i@a{sv})", id, self.menu_properties(id, names));
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&group)));
        g_variant_unref(ids);
        g_variant_unref(names);
    } else if (same(method, "GetProperty")) {
        int id;
        const char* name;
        g_variant_get(args, "(i&s)", &id, &name);
        auto* value = id >= Root && id <= Quit ? self.menu_property(id, name) : nullptr;
        if (!value) invalid();
        else g_dbus_method_invocation_return_value(invocation, g_variant_new("(v)", value));
    } else if (same(method, "Event")) {
        int id;
        const char* event;
        g_variant_get_child(args, 0, "i", &id);
        g_variant_get_child(args, 1, "&s", &event);
        g_dbus_method_invocation_return_value(invocation, nullptr);
        if (same(event, "clicked")) self.activate_menu_item(id);
    } else if (same(method, "EventGroup")) {
        GVariantBuilder errors;
        g_variant_builder_init(&errors, G_VARIANT_TYPE("ai"));
        auto* events = g_variant_get_child_value(args, 0);
        std::vector<int> clicked;
        for (gsize index = 0; index < g_variant_n_children(events); ++index) {
            auto* packet = g_variant_get_child_value(events, index);
            int id;
            const char* event;
            g_variant_get_child(packet, 0, "i", &id);
            g_variant_get_child(packet, 1, "&s", &event);
            if (id < Root || id > Quit) g_variant_builder_add(&errors, "i", id);
            else if (same(event, "clicked")) clicked.push_back(id);
            g_variant_unref(packet);
        }
        g_variant_unref(events);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai)", g_variant_builder_end(&errors)));
        for (const int id : clicked) self.activate_menu_item(id);
    } else if (same(method, "AboutToShow")) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
    } else {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ai@ai)",
            empty_array(G_VARIANT_TYPE_INT32), empty_array(G_VARIANT_TYPE_INT32)));
    }
}

} // namespace cadence
