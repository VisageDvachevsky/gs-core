/// webrtc_test.cpp — Day 9: DataChannel smoke test (manual SDP copy-paste)
///
/// Validates the full WebRTC signaling path WITHOUT a real browser:
///   1. Creates WebRTCHost and generates a local offer
///   2. User pastes the offer into the browser (or another WebRTC peer)
///   3. User pastes the remote answer back here
///   4. ICE candidates are exchanged through the console
///   5. Once DataChannel opens, sends a test message and waits for echo
///
/// Usage:
///   webrtc_test.exe [stun_uri]
///   Default stun_uri = stun:stun.l.google.com:19302
///
/// Not a unit test — requires network and manual interaction.

#include "webrtc_host.h"
#include "iwebrtc_observer.h"
#include "webrtc_types.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

using namespace gamestream;

// ---------------------------------------------------------------------------
// TestObserver — prints events to the console and unblocks waiting threads
// ---------------------------------------------------------------------------

class TestObserver final : public IWebRTCObserver {
public:
    // --- IWebRTCObserver ---

    void on_local_sdp_created(SessionDescription sdp) override {
        spdlog::info("=== LOCAL SDP ({}) ===", to_string(sdp.type));
        std::cout << "\n" << sdp.sdp << "\n";
        spdlog::info("=== END LOCAL SDP ===");

        {
            std::lock_guard<std::mutex> lk(local_sdp_mtx_);
            local_sdp_ = std::move(sdp);
        }
        local_sdp_cv_.notify_all();
    }

    void on_ice_candidate(IceCandidateInfo candidate) override {
        spdlog::info("[ICE] New candidate: {} (mid={} mline={})",
                     candidate.sdp, candidate.sdp_mid, candidate.sdp_mline_index);

        std::lock_guard<std::mutex> lk(candidates_mtx_);
        local_candidates_.push_back(std::move(candidate));
        candidates_cv_.notify_all();
    }

    void on_connection_state_changed(PeerConnectionState state) override {
        spdlog::info("[State] PeerConnection → {}", to_string(state));
        last_state_ = state;
        state_cv_.notify_all();
    }

    void on_ice_connection_state_changed(IceConnectionState state) override {
        spdlog::info("[State] ICE → {}", to_string(state));
    }

    void on_data_channel_open() override {
        spdlog::info("[DataChannel] OPEN");
        channel_open_ = true;
        channel_cv_.notify_all();
    }

    void on_data_channel_message(const uint8_t* data, size_t size) override {
        std::string msg(reinterpret_cast<const char*>(data), size);
        spdlog::info("[DataChannel] Message ({} bytes): {}", size, msg);
    }

    void on_data_channel_closed() override {
        spdlog::info("[DataChannel] CLOSED");
        channel_open_ = false;
    }

    // --- Blocking wait helpers ---

    SessionDescription wait_for_local_sdp(int timeout_sec = 10) {
        std::unique_lock<std::mutex> lk(local_sdp_mtx_);
        local_sdp_cv_.wait_for(lk, std::chrono::seconds(timeout_sec),
            [this] { return local_sdp_.has_value(); });
        return local_sdp_.value_or(SessionDescription{SessionDescriptionType::kOffer, ""});
    }

    bool wait_for_channel_open(int timeout_sec = 30) {
        std::unique_lock<std::mutex> lk(channel_mtx_);
        return channel_cv_.wait_for(lk, std::chrono::seconds(timeout_sec),
            [this] { return channel_open_.load(); });
    }

    bool wait_for_connected(int timeout_sec = 30) {
        std::unique_lock<std::mutex> lk(state_mtx_);
        return state_cv_.wait_for(lk, std::chrono::seconds(timeout_sec),
            [this] {
                return last_state_ == PeerConnectionState::kConnected
                    || last_state_ == PeerConnectionState::kFailed
                    || last_state_ == PeerConnectionState::kClosed;
            });
    }

    PeerConnectionState last_state() const { return last_state_; }

private:
    std::mutex local_sdp_mtx_;
    std::condition_variable local_sdp_cv_;
    std::optional<SessionDescription> local_sdp_;

    std::mutex candidates_mtx_;
    std::condition_variable candidates_cv_;
    std::vector<IceCandidateInfo> local_candidates_;

    std::mutex state_mtx_;
    std::condition_variable state_cv_;
    std::atomic<PeerConnectionState> last_state_{PeerConnectionState::kNew};

    std::mutex channel_mtx_;
    std::condition_variable channel_cv_;
    std::atomic<bool> channel_open_{false};
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string read_multiline(const std::string& prompt) {
    std::cout << "\n" << prompt << "\n";
    std::cout << "(Paste content, then enter a line with just '.' to finish)\n> ";

    std::string result;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == ".") break;
        result += line + "\n";
    }
    return result;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    // Setup logging
    auto console = spdlog::stdout_color_mt("webrtc_test");
    spdlog::set_default_logger(console);
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%T.%e] [%^%l%$] %v");

    const std::string stun_uri = (argc > 1)
        ? argv[1]
        : "stun:stun.l.google.com:19302";

    spdlog::info("WebRTC DataChannel smoke test");
    spdlog::info("STUN: {}", stun_uri);

    // --- Configure and initialize ------------------------------------------
    WebRTCConfig config;
    config.ice_servers.push_back(IceServer{stun_uri, "", ""});

    auto observer = std::make_unique<TestObserver>();
    WebRTCHost host;

    auto r = host.initialize(config, observer.get());
    if (!r) {
        spdlog::error("initialize() failed: {}", r.error());
        return 1;
    }

    // --- Create offer -------------------------------------------------------
    r = host.create_offer();
    if (!r) {
        spdlog::error("create_offer() failed: {}", r.error());
        return 1;
    }

    spdlog::info("Waiting for local SDP (offer)...");
    auto local_sdp = observer->wait_for_local_sdp(10);
    if (local_sdp.sdp.empty()) {
        spdlog::error("Timed out waiting for local SDP");
        return 1;
    }

    // --- Get remote answer from user ----------------------------------------
    std::string answer_sdp = read_multiline(
        "Paste the remote SDP ANSWER from the browser:");

    if (answer_sdp.empty()) {
        spdlog::error("No answer SDP provided");
        return 1;
    }

    r = host.set_remote_description(
        SessionDescription{SessionDescriptionType::kAnswer, answer_sdp});
    if (!r) {
        spdlog::error("set_remote_description() failed: {}", r.error());
        return 1;
    }

    // --- Get remote ICE candidates from user --------------------------------
    std::cout << "\nPaste remote ICE candidates one per block.\n";
    std::cout << "Format per candidate:\n";
    std::cout << "  mid:<sdp_mid> index:<mline_index> sdp:<candidate_string>\n";
    std::cout << "Enter empty line when done.\n\n";

    while (true) {
        std::string line;
        std::cout << "ICE> ";
        if (!std::getline(std::cin, line) || line.empty()) break;

        // Simple parse: "mid:X index:Y sdp:Z"
        IceCandidateInfo info;
        auto extract = [&](const std::string& key) -> std::string {
            const std::string prefix = key + ":";
            const size_t pos = line.find(prefix);
            if (pos == std::string::npos) return "";
            const size_t start = pos + prefix.size();
            const size_t end   = line.find(' ', start);
            return line.substr(start, end == std::string::npos ? end : end - start);
        };

        info.sdp_mid         = extract("mid");
        info.sdp_mline_index = std::stoi(extract("index").empty() ? "0" : extract("index"));
        info.sdp             = extract("sdp");

        if (!info.sdp.empty()) {
            auto ice_r = host.add_ice_candidate(std::move(info));
            if (!ice_r) {
                spdlog::warn("add_ice_candidate failed: {}", ice_r.error());
            }
        }
    }

    // --- Wait for connection ------------------------------------------------
    spdlog::info("Waiting for PeerConnection state = connected (30s)...");
    observer->wait_for_connected(30);

    if (observer->last_state() != PeerConnectionState::kConnected) {
        spdlog::error("Connection failed, state = {}",
                      to_string(observer->last_state()));
        host.close();
        return 1;
    }

    // --- Wait for DataChannel -----------------------------------------------
    spdlog::info("Waiting for DataChannel open (10s)...");
    if (!observer->wait_for_channel_open(10)) {
        spdlog::warn("DataChannel did not open in time");
    } else {
        // Send a test ping
        const std::string ping = "PING from webrtc_test";
        host.send_data(reinterpret_cast<const uint8_t*>(ping.data()), ping.size());
        spdlog::info("Sent: {}", ping);
    }

    // --- Keep alive until user presses Enter --------------------------------
    std::cout << "\nPress ENTER to close...\n";
    std::cin.get();

    host.close();
    spdlog::info("Done.");
    return 0;
}
