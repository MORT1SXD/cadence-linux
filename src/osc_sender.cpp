#include "osc_sender.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cadence {

// osc strings are null-terminated and padded to a 4-byte boundary
void OscMessage::append_padded_string(std::vector<uint8_t>& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
    out.push_back('\0');
    while (out.size() % 4 != 0) out.push_back('\0');
}

OscMessage::OscMessage(std::string address) : address_(std::move(address)) {}

void OscMessage::add_string(const std::string& value) {
    type_tags_ += 's';
    append_padded_string(arg_bytes_, value);
}

void OscMessage::add_bool(bool value) {
    // osc booleans carry no payload bytes, only a type tag ('T' or 'F')
    type_tags_ += value ? 'T' : 'F';
}

void OscMessage::add_int32(int32_t value) {
    type_tags_ += 'i';
    int32_t be = htonl(static_cast<uint32_t>(value));
    const auto* bytes = reinterpret_cast<const uint8_t*>(&be);
    arg_bytes_.insert(arg_bytes_.end(), bytes, bytes + 4);
}

std::vector<uint8_t> OscMessage::encode() const {
    std::vector<uint8_t> packet;
    append_padded_string(packet, address_);
    append_padded_string(packet, type_tags_);
    packet.insert(packet.end(), arg_bytes_.begin(), arg_bytes_.end());
    return packet;
}

OscSender::OscSender(const std::string& host, uint16_t port) {
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) return;

    dest_.sin_family = AF_INET;
    dest_.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &dest_.sin_addr) != 1) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

OscSender::~OscSender() {
    if (socket_fd_ >= 0) close(socket_fd_);
}

bool OscSender::send_raw(const std::vector<uint8_t>& packet) {
    if (socket_fd_ < 0) return false;
    ssize_t sent = sendto(socket_fd_, packet.data(), packet.size(), 0,
                           reinterpret_cast<const sockaddr*>(&dest_), sizeof(dest_));
    if (sent == static_cast<ssize_t>(packet.size())) {
        sent_count_++;
        return true;
    }
    return false;
}

bool OscSender::send_chatbox(const std::string& text) {
    OscMessage msg("/chatbox/input");
    msg.add_string(text);
    msg.add_bool(true);  // Send immediately.
    msg.add_bool(false); // No notification sound, including manual sends.
    return send_raw(msg.encode());
}

bool OscSender::send_typing(bool typing) {
    OscMessage msg("/chatbox/typing");
    msg.add_bool(typing);
    return send_raw(msg.encode());
}

} // namespace cadence
