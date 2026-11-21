#include "vrchat_status.hpp"

#include <arpa/inet.h>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <glib-unix.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cadence {

namespace {

std::optional<std::string_view> osc_string(std::string_view& packet) {
    const auto end = packet.find('\0');
    if (end == std::string_view::npos) return {};
    const auto padded = (end + 4) & ~size_t{3};
    if (padded > packet.size()) return {};
    for (size_t i = end; i < padded; ++i)
        if (packet[i] != '\0') return {};
    const auto text = packet.substr(0, end);
    packet.remove_prefix(padded);
    return text;
}

std::uint32_t osc_word(std::string_view bytes) {
    std::uint32_t result = 0;
    for (const unsigned char byte : bytes) result = (result << 8) | byte;
    return result;
}

constexpr std::string_view kPrefix = "/avatar/parameters/";
constexpr gint64 kStatusLifetime = 60 * G_USEC_PER_SEC;
constexpr gint64 kMovementLifetime = 5 * G_USEC_PER_SEC;

} // namespace

VrchatStatusListener::~VrchatStatusListener() { stop(); }

void VrchatStatusListener::start(int port) {
    stop();
    if (port < 1 || port > 65535) {
        error_ = "Invalid VRChat output port";
        return;
    }
    fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0) {
        error_ = "Could not open the local OSC listener";
        return;
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(fd_, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) < 0) {
        const int reason = errno;
        stop();
        error_ = reason == EADDRINUSE ? "Port in use; close the other listener or change the port"
                                      : "Could not bind the local OSC port";
        return;
    }
    source_ = g_unix_fd_add(fd_, static_cast<GIOCondition>(G_IO_IN | G_IO_ERR | G_IO_NVAL | G_IO_HUP),
                           &VrchatStatusListener::on_ready, this);
}

void VrchatStatusListener::stop() {
    if (source_) g_source_remove(source_);
    source_ = 0;
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
    status_ = {};
    last_report_ = last_speed_ = 0;
    error_.clear();
}

VrchatStatus VrchatStatusListener::snapshot(gint64 now) const {
    if (!listening() || !last_report_ || now - last_report_ > kStatusLifetime) return {};
    auto result = status_;
    // Don't continue to advertise a running speed when motion updates stop.
    if (result.speed && *result.speed != 0 && now - last_speed_ > kMovementLifetime)
        result.speed.reset();
    return result;
}

gboolean VrchatStatusListener::on_ready(int fd, GIOCondition condition, gpointer data) {
    auto& listener = *static_cast<VrchatStatusListener*>(data);
    if (condition & (G_IO_ERR | G_IO_NVAL | G_IO_HUP)) {
        listener.source_ = 0;
        listener.stop();
        listener.error_ = "Local OSC listener stopped";
        return G_SOURCE_REMOVE;
    }
    // Bound work per callback even if another local process floods the port.
    char buffer[1024];
    for (int i = 0; i < 32; ++i) {
        const auto size = recv(fd, buffer, sizeof(buffer), MSG_TRUNC);
        if (size < 0) break;
        if (static_cast<size_t>(size) <= sizeof(buffer))
            listener.receive(std::string_view(buffer, size), g_get_monotonic_time());
    }
    return G_SOURCE_CONTINUE;
}

void VrchatStatusListener::receive(std::string_view packet, gint64 now) {
    auto address = osc_string(packet);
    auto type = osc_string(packet);
    if (!address || !type) return;
    if (*address == "/avatar/change" && *type == ",s") {
        const auto avatar = osc_string(packet);
        if (avatar && !avatar->empty() && packet.empty()) {
            status_ = {};
            last_report_ = last_speed_ = 0;
        }
        return;
    }
    if (!address->starts_with(kPrefix)) return;
    const auto name = address->substr(kPrefix.size());
    VrchatStatus update;
    if ((name == "MuteSelf" || name == "Earmuffs") && (*type == ",T" || *type == ",F") && packet.empty()) {
        (name == "MuteSelf" ? update.muted : update.earmuffs) = *type == ",T";
    } else if ((name == "GestureLeft" || name == "GestureRight") && *type == ",i" && packet.size() == 4) {
        const auto gesture = osc_word(packet);
        if (gesture > 7) return;
        (name == "GestureLeft" ? update.left_gesture : update.right_gesture) = static_cast<int>(gesture);
    } else if (name == "VelocityMagnitude" && *type == ",f" && packet.size() == 4) {
        const auto speed = std::bit_cast<float>(osc_word(packet));
        if (!std::isfinite(speed) || speed < 0 || speed > 100000) return;
        update.speed = speed == 0 ? 0 : speed;
        last_speed_ = now;
    } else return;

    if (!last_report_ || now - last_report_ > kStatusLifetime) status_ = {};
    if (update.muted) status_.muted = update.muted;
    if (update.earmuffs) status_.earmuffs = update.earmuffs;
    if (update.left_gesture) status_.left_gesture = update.left_gesture;
    if (update.right_gesture) status_.right_gesture = update.right_gesture;
    if (update.speed) status_.speed = update.speed;
    last_report_ = now;
}

} // namespace cadence
