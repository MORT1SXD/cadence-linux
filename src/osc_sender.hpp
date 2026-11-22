#pragma once
#include <cstdint>
#include <netinet/in.h>
#include <string>
#include <vector>

namespace cadence {

// builds a single osc message (address + typed args) into a byte packet
class OscMessage {
public:
    explicit OscMessage(std::string address);

    void add_string(const std::string& value);
    void add_bool(bool value);
    void add_int32(int32_t value);

    std::vector<uint8_t> encode() const;

private:
    std::string address_;
    std::string type_tags_ = ",";
    std::vector<uint8_t> arg_bytes_;

    static void append_padded_string(std::vector<uint8_t>& out, const std::string& s);
};

// minimal udp sender, just enough for vrchat's official chatbox osc endpoint
class OscSender {
public:
    OscSender(const std::string& host, uint16_t port);
    ~OscSender();

    OscSender(const OscSender&) = delete;
    OscSender& operator=(const OscSender&) = delete;

    // Send immediately without triggering VRChat's notification sound.
    bool send_chatbox(const std::string& text);

    // sends "/chatbox/typing" b
    bool send_typing(bool typing);

    // true only once at least one packet actually left the socket successfully.
    // note this does NOT mean vrchat is listening - udp sendto succeeds even when
    // nothing is bound to the port, so this is "our side is working", not "vrchat got it"
    bool has_sent() const { return sent_count_ > 0; }
    bool is_ready() const { return socket_fd_ >= 0; }
    void set_port(uint16_t port) { dest_.sin_port = htons(port); }

private:
    int socket_fd_ = -1;
    sockaddr_in dest_{};
    long sent_count_ = 0;

    bool send_raw(const std::vector<uint8_t>& packet);
};

} // namespace cadence
