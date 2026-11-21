#include "media_reader.hpp"
#include <utility>

namespace cadence {

namespace {

constexpr int kRequestTimeoutMs = 300;

// finds an mpris-compatible bus name that is currently playing, falling back
// to the first mpris player found on the bus if nothing is actively playing
std::pair<std::string, bool> find_active_player(GDBusConnection* bus) {
    GVariant* reply = g_dbus_connection_call_sync(
        bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "ListNames", nullptr, G_VARIANT_TYPE("(as)"),
        G_DBUS_CALL_FLAGS_NONE, kRequestTimeoutMs, nullptr, nullptr);
    if (!reply) return {};

    GVariantIter* iter = nullptr;
    g_variant_get(reply, "(as)", &iter);

    gchar* name = nullptr;
    std::string fallback;

    while (g_variant_iter_loop(iter, "s", &name)) {
        std::string n(name);
        if (n.rfind("org.mpris.MediaPlayer2.", 0) != 0) continue;
        if (fallback.empty()) fallback = n;

        GVariant* status_reply = g_dbus_connection_call_sync(
            bus, n.c_str(), "/org/mpris/MediaPlayer2",
            "org.freedesktop.DBus.Properties", "Get",
            g_variant_new("(ss)", "org.mpris.MediaPlayer2.Player", "PlaybackStatus"),
            G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, kRequestTimeoutMs, nullptr, nullptr);
        if (!status_reply) continue;

        GVariant* v = nullptr;
        g_variant_get(status_reply, "(v)", &v);
        bool playing = g_variant_is_of_type(v, G_VARIANT_TYPE_STRING) &&
                       std::string(g_variant_get_string(v, nullptr)) == "Playing";
        g_variant_unref(v);
        g_variant_unref(status_reply);

        if (playing) {
            g_variant_iter_free(iter);
            g_variant_unref(reply);
            return {n, true};
        }
    }

    g_variant_iter_free(iter);
    g_variant_unref(reply);
    return {fallback, false};
}

} // namespace

MediaReader::MediaReader() {
    bus_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
}

MediaReader::~MediaReader() {
    if (bus_) g_object_unref(bus_);
}

std::optional<TrackInfo> MediaReader::current_track(bool read_progress) {
    if (!bus_) return std::nullopt;

    auto [player, playing] = find_active_player(bus_);
    if (player.empty()) return std::nullopt;

    GVariant* reply = g_dbus_connection_call_sync(
        bus_, player.c_str(), "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", "org.mpris.MediaPlayer2.Player", "Metadata"),
        G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, kRequestTimeoutMs, nullptr, nullptr);
    if (!reply) return std::nullopt;

    GVariant* metadata = nullptr;
    g_variant_get(reply, "(v)", &metadata);
    g_variant_unref(reply);
    if (!g_variant_is_of_type(metadata, G_VARIANT_TYPE_VARDICT)) {
        g_variant_unref(metadata);
        return std::nullopt;
    }

    TrackInfo info;
    info.playing = playing;
    if (GVariant* title_v = g_variant_lookup_value(metadata, "xesam:title", G_VARIANT_TYPE_STRING)) {
        info.title = g_variant_get_string(title_v, nullptr);
        g_variant_unref(title_v);
    }

    if (GVariant* artist_v = g_variant_lookup_value(metadata, "xesam:artist", nullptr)) {
        if (g_variant_is_of_type(artist_v, G_VARIANT_TYPE("as"))) {
            GVariantIter it;
            g_variant_iter_init(&it, artist_v);
            const gchar* a = nullptr;
            if (g_variant_iter_next(&it, "&s", &a)) info.artist = a;
        }
        g_variant_unref(artist_v);
    }

    gint64 length = 0;
    if (read_progress) {
        g_variant_lookup(metadata, "mpris:length", "x", &length);
        if (length >= G_USEC_PER_SEC) info.duration = std::chrono::seconds(length / G_USEC_PER_SEC);
        const gchar* album = nullptr;
        if (g_variant_lookup(metadata, "xesam:album", "&s", &album)) info.album = album;
    }
    g_variant_unref(metadata);
    if (info.title.empty()) return std::nullopt;

    if (read_progress) {
        GVariant* position = g_dbus_connection_call_sync(
            bus_, player.c_str(), "/org/mpris/MediaPlayer2",
            "org.freedesktop.DBus.Properties", "Get",
            g_variant_new("(ss)", "org.mpris.MediaPlayer2.Player", "Position"),
            G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, kRequestTimeoutMs, nullptr, nullptr);
        if (position) {
            GVariant* value = nullptr;
            g_variant_get(position, "(v)", &value);
            if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64)) {
                const gint64 elapsed = g_variant_get_int64(value);
                if (elapsed >= 0 && (length <= 0 || elapsed <= length)) {
                    info.position = std::chrono::seconds(elapsed / G_USEC_PER_SEC);
                    info.position_ms = std::chrono::milliseconds(elapsed / 1000);
                }
            }
            g_variant_unref(value);
            g_variant_unref(position);
        }
    }
    return info;
}

} // namespace cadence
