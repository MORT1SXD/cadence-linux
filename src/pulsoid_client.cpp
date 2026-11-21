#include "pulsoid_client.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <libwebsockets.h>

namespace cadence {

namespace {

// only one client is ever instantiated by this app, so a single static pointer
// is enough to bridge lws's c-style callback back into the instance
PulsoidClient* g_instance = nullptr;

// pulsoid sends: {"measured_at": ..., "data": {"heart_rate": 40}}
// pulls the "heart_rate" integer out without a full json parser
int extract_heart_rate(const std::string& payload) {
    auto pos = payload.find("\"heart_rate\"");
    if (pos == std::string::npos) return -1;
    pos = payload.find(':', pos);
    if (pos == std::string::npos) return -1;
    return std::atoi(payload.c_str() + pos + 1);
}

} // namespace

PulsoidClient::PulsoidClient() { g_instance = this; }

PulsoidClient::~PulsoidClient() {
    stop();
    g_instance = nullptr;
}

void PulsoidClient::set_token(const std::string& token) { token_ = token; }
void PulsoidClient::set_on_update(UpdateCallback cb) { on_update_ = std::move(cb); }
void PulsoidClient::set_on_status(StatusCallback cb) { on_status_ = std::move(cb); }

int PulsoidClient::callback(lws* wsi, lws_callback_reasons reason, void*, void* in, size_t len) {
    (void)wsi;
    if (!g_instance) return 0;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            g_instance->connected_ = true;
            g_instance->reconnect_attempts_ = 0;
            if (g_instance->on_status_) g_instance->on_status_("connected");
            break;
        case LWS_CALLBACK_CLIENT_RECEIVE: {
            // accumulate fragments - small json messages usually arrive in one
            // frame, but handle fragmentation properly just in case
            if (lws_is_first_fragment(wsi)) {
                g_instance->receive_buffer_.clear();
            }
            g_instance->receive_buffer_.append(static_cast<const char*>(in), len);
            if (lws_is_final_fragment(wsi)) {
                int bpm = extract_heart_rate(g_instance->receive_buffer_);
                if (bpm > 0 && g_instance->on_update_) g_instance->on_update_(bpm);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            g_instance->connected_ = false;
            if (g_instance->on_status_) g_instance->on_status_("connection error");
            break;
        case LWS_CALLBACK_CLIENT_CLOSED:
            g_instance->connected_ = false;
            if (g_instance->on_status_) g_instance->on_status_("disconnected");
            break;
        default:
            break;
    }
    return 0;
}

void PulsoidClient::run_event_loop() {
    static struct lws_protocols protocols[] = {
        {"pulsoid-protocol", &PulsoidClient::callback, 0, 4096, 0, nullptr, 0},
        LWS_PROTOCOL_LIST_TERM};

    struct lws_context_creation_info info {};
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    context_ = lws_create_context(&info);
    if (!context_) {
        running_ = false;
        if (on_status_) on_status_("failed to create context");
        return;
    }

    std::string path = "/api/v1/data/real_time?access_token=" + token_;

    struct lws_client_connect_info conn {};
    conn.context = context_;
    conn.address = "dev.pulsoid.net";
    conn.port = 443;
    conn.path = path.c_str();
    conn.host = conn.address;
    conn.origin = conn.address;
    conn.protocol = protocols[0].name;
    conn.ssl_connection = LCCSCF_USE_SSL;

    lws_client_connect_via_info(&conn);

    while (running_) {
        lws_service(context_, 100);

        // reconnect with exponential backoff when the connection drops
        if (running_ && !connected_ && !token_.empty()) {
            int delay = std::min(1000 * (1 << std::min(reconnect_attempts_, 5)), 30000);
            for (int i = 0; i < delay / 100 && running_; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!running_) break;
            reconnect_attempts_++;
            lws_client_connect_via_info(&conn);
        } else if (connected_) {
            reconnect_attempts_ = 0;
        }
    }

    lws_context_destroy(context_);
    context_ = nullptr;
    connected_ = false;
}

void PulsoidClient::start() {
    if (running_ || token_.empty()) return;
    running_ = true;
    if (on_status_) on_status_("connecting");
    worker_ = std::thread(&PulsoidClient::run_event_loop, this);
}

void PulsoidClient::stop() {
    if (!running_) return;
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

} // namespace cadence
