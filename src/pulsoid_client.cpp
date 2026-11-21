#include "pulsoid_client.hpp"

#include <algorithm>
#include <cstdlib>
#include <libwebsockets.h>
#include <string_view>
#include <unistd.h>

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

// The AppImage's OpenSSL was built on Ubuntu. Its compiled-in CA path
// need not exist on the machine running the AppImage.
const char* host_ca_bundle() {
    if (const char* path = std::getenv("SSL_CERT_FILE"); path && *path) return path;
    for (const char* path : {"/etc/ssl/certs/ca-certificates.crt",
                             "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem"}) {
        if (access(path, R_OK) == 0) return path;
    }
    return nullptr;
}

} // namespace

PulsoidClient::PulsoidClient() { g_instance = this; }

PulsoidClient::~PulsoidClient() {
    stop();
    g_instance = nullptr;
}

void PulsoidClient::set_token(const std::string& token) {
    const auto first = token.find_first_not_of(" \t\r\n");
    token_ = first == std::string::npos ? "" :
        token.substr(first, token.find_last_not_of(" \t\r\n") - first + 1);
}
void PulsoidClient::set_on_update(UpdateCallback cb) { on_update_ = std::move(cb); }
void PulsoidClient::set_on_status(StatusCallback cb) { on_status_ = std::move(cb); }

int PulsoidClient::callback(lws* wsi, lws_callback_reasons reason, void*, void* in, size_t len) {
    if (!g_instance) return 0;
    auto& self = *g_instance;

    switch (reason) {
        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
            if (!self.running_) return -1;
            const std::string bearer = "Bearer " + self.token_;
            auto** cursor = static_cast<unsigned char**>(in);
            return lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_AUTHORIZATION,
                reinterpret_cast<const unsigned char*>(bearer.data()), static_cast<int>(bearer.size()),
                cursor, *cursor + len) ? -1 : 0;
        }
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
            if (lws_is_final_fragment(wsi) && !lws_remaining_packet_payload(wsi)) {
                int bpm = extract_heart_rate(g_instance->receive_buffer_);
                if (bpm > 0 && g_instance->on_update_) g_instance->on_update_(bpm);
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
            self.connected_ = false;
            if (!self.running_) break;
            const unsigned code = wsi ? lws_http_client_http_response(wsi) : 0;
            const std::string_view detail = in ? static_cast<const char*>(in) : "";
            if (code == 400 || code == 401 || code == 402 || code == 403) {
                if (self.on_status_) self.on_status_(code == 402
                    ? "Pulsoid rejected the plan for this token"
                    : code == 403 ? "Pulsoid returned HTTP 403; check token scope or network filtering"
                                  : "Pulsoid rejected the token; check expiry and data:heart_rate:read");
                self.running_ = false;
            } else {
                const bool tls = detail.find("SSL") != std::string_view::npos ||
                                 detail.find("certificate") != std::string_view::npos;
                if (self.on_status_) self.on_status_(tls
                    ? "TLS verification failed; check system certificates"
                    : "Connection error; retrying");
                self.schedule_retry();
            }
            break;
        }
        case LWS_CALLBACK_CLIENT_CLOSED:
            self.connected_ = false;
            if (self.running_) {
                if (self.on_status_) self.on_status_("Connection lost; retrying");
                self.schedule_retry();
            }
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
    info.client_ssl_ca_filepath = host_ca_bundle();
    info.connect_timeout_secs = 12;
    info.timeout_secs = 12;

    auto* context = lws_create_context(&info);
    if (!context) {
        running_ = false;
        if (on_status_) on_status_("failed to create context");
        return;
    }
    {
        std::lock_guard lock(context_mutex_);
        context_ = context;
    }

    retry_ = {};
    retry_.owner = this;
    retry_pending_ = false;
    reconnect_attempts_ = 0;
    if (running_) connect_once();
    while (running_) {
        if (lws_service(context, 0) < 0) {
            if (running_ && on_status_) on_status_("Connection error; reconnect from Connectivity");
            break;
        }
    }
    running_ = false;
    lws_sul_cancel(&retry_.timer);
    {
        std::lock_guard lock(context_mutex_);
        context_ = nullptr;
    }
    lws_context_destroy(context);
    connected_ = false;
}

void PulsoidClient::connect_once() {
    struct lws_client_connect_info conn {};
    conn.context = context_;
    conn.address = "dev.pulsoid.net";
    conn.port = 443;
    conn.path = "/api/v1/data/real_time";
    conn.host = conn.address;
    conn.local_protocol_name = "pulsoid-protocol";
    conn.ssl_connection = LCCSCF_USE_SSL;

    if (!lws_client_connect_via_info(&conn) && running_ && !retry_pending_) {
        if (on_status_) on_status_("Connection error; retrying");
        schedule_retry();
    }
}

void PulsoidClient::schedule_retry() {
    if (!running_ || retry_pending_) return;
    const auto delay = std::min(1 << std::min(reconnect_attempts_, 5), 30);
    if (reconnect_attempts_ < 5) ++reconnect_attempts_;
    retry_pending_ = true;
    lws_sul_schedule(context_, 0, &retry_.timer, &PulsoidClient::on_retry,
                     static_cast<lws_usec_t>(delay) * LWS_US_PER_SEC);
}

void PulsoidClient::on_retry(lws_sorted_usec_list_t* timer) {
    auto& self = *reinterpret_cast<Retry*>(timer)->owner; // timer is the first member
    self.retry_pending_ = false;
    if (self.running_) self.connect_once();
}

void PulsoidClient::start() {
    if (running_ || token_.empty()) return;
    if (worker_.joinable()) worker_.join();
    if (token_.find_first_of(" \t\r\n") != std::string::npos) {
        if (on_status_) on_status_("Invalid token format");
        return;
    }
    running_ = true;
    if (on_status_) on_status_("connecting");
    worker_ = std::thread(&PulsoidClient::run_event_loop, this);
}

void PulsoidClient::stop() {
    running_ = false;
    {
        std::lock_guard lock(context_mutex_);
        if (context_) lws_cancel_service(context_);
    }
    if (worker_.joinable()) worker_.join();
}

} // namespace cadence
