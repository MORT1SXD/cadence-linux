#pragma once
#include <atomic>
#include <functional>
#include <libwebsockets.h>
#include <mutex>
#include <string>
#include <thread>

namespace cadence {

// connects to pulsoid's real-time heart rate websocket api and reports bpm updates.
// runs its own event loop on a background thread so the ui never blocks on it
class PulsoidClient {
public:
    using UpdateCallback = std::function<void(int bpm)>;
    using StatusCallback = std::function<void(const std::string&)>;

    PulsoidClient();
    ~PulsoidClient();

    void set_token(const std::string& token);
    void set_on_update(UpdateCallback cb);
    void set_on_status(StatusCallback cb);

    void start();
    void stop();
    bool is_connected() const { return connected_; }

private:
    std::string token_;
    UpdateCallback on_update_;
    StatusCallback on_status_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread worker_;
    std::mutex context_mutex_;
    lws_context* context_ = nullptr;

    struct Retry {
        lws_sorted_usec_list_t timer{};
        PulsoidClient* owner = nullptr;
    };
    Retry retry_;
    bool retry_pending_ = false;
    int reconnect_attempts_ = 0;
    std::string receive_buffer_;

    void run_event_loop();
    void connect_once();
    void schedule_retry();
    static void on_retry(lws_sorted_usec_list_t* timer);
    static int callback(lws* wsi, lws_callback_reasons reason, void* user, void* in, size_t len);
};

} // namespace cadence
