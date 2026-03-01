/// input_host_test.cpp — Stage 4 isolated input test tool.
///
/// Receives input packets over WebRTC DataChannel and prints a live overlay
/// to the console. Does NOT call SendInput — purely diagnostic.
///
/// Usage:
///   input_host_test.exe [server_host [server_port [session_id]]]
///   input_host_test.exe localhost 8765 a1b2c3d4-e5f6-4a7b-8c9d-e0f1a2b3c4d5
///
/// Workflow:
///   1. Connect to signaling server (same as webrtc_test.exe)
///   2. Create offer with DataChannels only (no video track)
///   3. Browser opens web/input-lab.html → connects → sends input
///   4. Tool prints live overlay: last dx/dy, active buttons, active keys,
///      p50/p95 relative transport delay (baseline-normalized by first sample)

#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <dbghelp.h>

#include "webrtc_host.h"
#include "iwebrtc_observer.h"
#include "input_handler.h"
#include "input_types.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace gamestream;
using namespace std::chrono_literals;

#pragma comment(lib, "Dbghelp.lib")

// ---------------------------------------------------------------------------
// Ctrl+C shutdown
// ---------------------------------------------------------------------------

static std::atomic<bool> g_stop{false};

static BOOL WINAPI ctrl_handler(DWORD ctrl_type) noexcept {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        g_stop.store(true, std::memory_order_relaxed);
        return TRUE;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Minimal WinHTTP client (copy from webrtc_test.cpp)
// ---------------------------------------------------------------------------

static std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 32);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            default:   out += static_cast<char>(c); break;
        }
    }
    return out;
}

static std::string json_unescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && (i + 1) < s.size()) {
            ++i;
            switch (s[i]) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                default:   out += s[i]; break;
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

static std::string json_get_string(const std::string& json, std::string_view key) {
    std::string needle;
    needle.reserve(key.size() + 2);
    needle += '"';
    needle += key;
    needle += '"';

    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    ++pos;

    std::string raw;
    for (; pos < json.size(); ++pos) {
        if (json[pos] == '\\' && (pos + 1) < json.size()) {
            raw += json[pos];
            raw += json[++pos];
        } else if (json[pos] == '"') {
            break;
        } else {
            raw += json[pos];
        }
    }
    return json_unescape(raw);
}

class HttpClient {
public:
    explicit HttpClient(std::wstring_view host, INTERNET_PORT port) {
        session_ = WinHttpOpen(L"GameStream-InputTest/1.0",
                               WINHTTP_ACCESS_TYPE_NO_PROXY,
                               WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session_) return;
        connect_ = WinHttpConnect(session_, host.data(), port, 0);
    }
    ~HttpClient() {
        if (connect_) WinHttpCloseHandle(connect_);
        if (session_) WinHttpCloseHandle(session_);
    }
    HttpClient(const HttpClient&)            = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    [[nodiscard]] bool ok() const noexcept { return connect_ != nullptr; }

    [[nodiscard]] std::string post_json(std::string_view path, std::string_view body) {
        return request(L"POST", path, body, L"Content-Type: application/json\r\n");
    }
    [[nodiscard]] std::string get(std::string_view path) {
        return request(L"GET", path, {}, nullptr);
    }

private:
    HINTERNET session_ = nullptr;
    HINTERNET connect_ = nullptr;

    std::string request(const wchar_t* verb, std::string_view path,
                        std::string_view body, const wchar_t* extra_headers) {
        if (!connect_) return {};
        const std::wstring wpath(path.begin(), path.end());
        HINTERNET req = WinHttpOpenRequest(connect_, verb, wpath.c_str(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!req) return {};
        struct Guard { HINTERNET h; ~Guard() { WinHttpCloseHandle(h); } } g{req};

        const auto body_len = static_cast<DWORD>(body.size());
        LPVOID body_ptr = body.empty() ? nullptr : const_cast<char*>(body.data());
        if (!WinHttpSendRequest(req, extra_headers,
                                extra_headers ? static_cast<DWORD>(-1L) : 0u,
                                body_ptr, body_len, body_len, 0)) return {};
        if (!WinHttpReceiveResponse(req, nullptr)) return {};

        DWORD status = 0, status_len = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            nullptr, &status, &status_len, nullptr);
        if (status >= 400) return {};

        std::string response;
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(req, &available) && available > 0) {
            const size_t off = response.size();
            response.resize(off + available);
            DWORD read = 0;
            if (!WinHttpReadData(req, response.data() + off, available, &read)) break;
            response.resize(off + read);
        }
        return response;
    }
};

// ---------------------------------------------------------------------------
// InputStats — tracks received events for the live overlay
// ---------------------------------------------------------------------------

class InputStats {
public:
    void record_packet(const InputEvent& ev) {
        // Relative transport delay estimate:
        //   baseline_offset_us = (host_now - client_ts) on first sample
        //   sample_delay_ms    = (host_now - client_ts - baseline_offset_us)
        //
        // This avoids mixing absolute epochs between clocks and tracks
        // delay variation over the initial baseline.
        const uint64_t now_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());

        std::lock_guard<std::mutex> lock(mutex_);
        total_packets_++;

        switch (ev.type) {
            case InputPacketType::kMouseMove:
                last_dx_ = ev.mouse_move.dx;
                last_dy_ = ev.mouse_move.dy;
                mouse_move_packets_++;
                break;
            case InputPacketType::kMouseButton:
                if (ev.mouse_button.is_down) {
                    held_buttons_.insert(static_cast<uint8_t>(ev.mouse_button.button));
                } else {
                    held_buttons_.erase(static_cast<uint8_t>(ev.mouse_button.button));
                }
                break;
            case InputPacketType::kKey:
                if (ev.key.is_down && !ev.key.repeat) {
                    held_keys_.insert(static_cast<uint16_t>(ev.key.code));
                } else if (!ev.key.is_down) {
                    held_keys_.erase(static_cast<uint16_t>(ev.key.code));
                }
                break;
            case InputPacketType::kReleaseAll:
                held_buttons_.clear();
                held_keys_.clear();
                break;
            default:
                break;
        }

        // Delay ring buffer (last 500 samples).
        if (ev.timestamp_us > 0 && now_us > ev.timestamp_us) {
            const uint64_t raw_delta_us = now_us - ev.timestamp_us;
            if (!has_baseline_offset_) {
                baseline_offset_us_ = raw_delta_us;
                has_baseline_offset_ = true;
            }
            if (raw_delta_us >= baseline_offset_us_) {
                const double delay_ms =
                    static_cast<double>(raw_delta_us - baseline_offset_us_) / 1000.0;
                if (delay_ms < 5000.0) {
                    latency_samples_.push_back(delay_ms);
                    if (latency_samples_.size() > 500) {
                        latency_samples_.pop_front();
                    }
                }
            }
        }
    }

    struct Snapshot {
        int16_t  last_dx;
        int16_t  last_dy;
        uint64_t total_packets;
        uint64_t mouse_move_packets;
        double   p50_ms;
        double   p95_ms;
        std::set<uint8_t>  held_buttons;
        std::set<uint16_t> held_keys;
    };

    [[nodiscard]] Snapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        Snapshot s;
        s.last_dx            = last_dx_;
        s.last_dy            = last_dy_;
        s.total_packets      = total_packets_;
        s.mouse_move_packets = mouse_move_packets_;
        s.held_buttons       = held_buttons_;
        s.held_keys          = held_keys_;
        s.p50_ms             = 0.0;
        s.p95_ms             = 0.0;

        if (!latency_samples_.empty()) {
            std::vector<double> sorted(latency_samples_.begin(), latency_samples_.end());
            std::sort(sorted.begin(), sorted.end());
            s.p50_ms = sorted[sorted.size() / 2];
            s.p95_ms = sorted[static_cast<size_t>(sorted.size() * 0.95)];
        }
        return s;
    }

private:
    mutable std::mutex     mutex_;
    int16_t                last_dx_ = 0;
    int16_t                last_dy_ = 0;
    uint64_t               total_packets_ = 0;
    uint64_t               mouse_move_packets_ = 0;
    std::set<uint8_t>      held_buttons_;
    std::set<uint16_t>     held_keys_;
    std::deque<double>     latency_samples_;
    bool                   has_baseline_offset_ = false;
    uint64_t               baseline_offset_us_  = 0;
};

// ---------------------------------------------------------------------------
// Overlay printer — refreshes console every 250 ms
// ---------------------------------------------------------------------------

static void print_overlay(const InputStats::Snapshot& s) {
    // Clear line + print stats (no ANSI escape needed — just print).
    std::printf("\r[INPUT OVERLAY]  total=%5llu  move=%5llu | dx=%+5d dy=%+5d | "
                "buttons=%zu keys=%zu | lat p50=%.1fms p95=%.1fms    ",
                static_cast<unsigned long long>(s.total_packets),
                static_cast<unsigned long long>(s.mouse_move_packets),
                static_cast<int>(s.last_dx),
                static_cast<int>(s.last_dy),
                s.held_buttons.size(),
                s.held_keys.size(),
                s.p50_ms,
                s.p95_ms);
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Observer — receives WebRTC events, dispatches input to InputStats
// ---------------------------------------------------------------------------

class InputTestObserver final : public IWebRTCObserver {
public:
    explicit InputTestObserver(InputStats* stats) : stats_(stats) {}

    [[nodiscard]] std::optional<SessionDescription>
    wait_for_offer(std::chrono::seconds timeout) {
        std::unique_lock lk(mtx_);
        if (!cv_.wait_for(lk, timeout, [this] { return gathering_done_; })) {
            return std::nullopt;
        }
        return offer_sdp_;
    }

    // IWebRTCObserver
    void on_local_sdp_created(SessionDescription sdp) override {
        std::lock_guard lk(mtx_);
        offer_sdp_ = std::move(sdp);
    }

    void on_ice_candidate(IceCandidateInfo /*c*/) override {}

    void on_connection_state_changed(PeerConnectionState state) override {
        spdlog::info("[observer] PeerConnection: {}", to_string(state));
    }

    void on_ice_connection_state_changed(IceConnectionState state) override {
        spdlog::info("[observer] ICE: {}", to_string(state));
    }

    void on_data_channel_open() override {
        spdlog::info("[observer] DataChannels open — send input from input-lab.html");
    }

    void on_data_channel_message(const uint8_t* data, size_t size) override {
        auto result = InputHandler::parse_packet(data, size);
        if (!result) {
            spdlog::warn("[observer] parse_packet: {}", result.error());
            return;
        }
        stats_->record_packet(result.value());
    }

    void on_data_channel_closed() override {
        spdlog::info("[observer] DataChannel closed");
    }

    void on_ice_gathering_complete() override {
        spdlog::info("[observer] ICE gathering complete");
        {
            std::lock_guard lk(mtx_);
            gathering_done_ = true;
        }
        cv_.notify_all();
    }

private:
    InputStats*             stats_;
    std::mutex              mtx_;
    std::condition_variable cv_;
    bool                    gathering_done_ = false;
    SessionDescription      offer_sdp_;
};

// ---------------------------------------------------------------------------
// Logging setup
// ---------------------------------------------------------------------------

static void setup_logging() {
    auto sink   = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("", sink);
    logger->set_level(spdlog::level::info);
    logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_default_logger(std::move(logger));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    setup_logging();
    spdlog::info("=== GameStream Input Test (Stage 4) ===");
    spdlog::info("Opens DataChannels only. No video. Connect with web/input-lab.html.");

    const char*        server_host = (argc > 1) ? argv[1] : "localhost";
    const auto         server_port = static_cast<INTERNET_PORT>(
                                         (argc > 2) ? std::stoul(argv[2]) : 8765u);
    const char*        session_id  = (argc > 3) ? argv[3] : "a1b2c3d4-e5f6-4a7b-8c9d-e0f1a2b3c4d5";

    spdlog::info("[main] server={}:{}  session={}", server_host, server_port, session_id);
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    InputStats        stats;
    InputTestObserver observer(&stats);
    WebRTCHost        host;
    WebRTCConfig      cfg;
    // Default: STUN only. Optional TURN can be provided via env vars:
    //   GAMESTREAM_TURN_URI, GAMESTREAM_TURN_USERNAME, GAMESTREAM_TURN_PASSWORD
    // Secrets must not be hardcoded in source.
    const auto read_env = [](const char* name) -> std::string {
        char* value = nullptr;
        size_t len = 0;
        if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) {
            return {};
        }
        std::string out(value);
        std::free(value);
        return out;
    };

    const std::string turn_uri  = read_env("GAMESTREAM_TURN_URI");
    const std::string turn_user = read_env("GAMESTREAM_TURN_USERNAME");
    const std::string turn_pass = read_env("GAMESTREAM_TURN_PASSWORD");
    cfg.ice_servers.push_back({"stun:stun.l.google.com:19302",                    "", ""});
    cfg.ice_servers.push_back({"stun:stun1.l.google.com:19302",                   "", ""});
    if (!turn_uri.empty() && !turn_user.empty() && !turn_pass.empty()) {
        cfg.ice_servers.push_back({turn_uri, turn_user, turn_pass});
        spdlog::info("[main] TURN enabled from environment: {}", turn_uri);
    } else {
        spdlog::info("[main] TURN not configured (using STUN only)");
    }
    // Lock ICE to the two forwarded UDP ports so STUN srflx candidates
    // are reachable from the internet (only 8080 and 8081 are port-forwarded).
    cfg.min_ice_port = 8080;
    cfg.max_ice_port = 8081;

    if (auto r = host.initialize(cfg, &observer); !r) {
        spdlog::error("[main] WebRTCHost init: {}", r.error());
        return 1;
    }
    spdlog::info("[main] WebRTCHost initialized (DataChannels only — no video)");

    // Create offer WITHOUT a video track (DataChannel-only SDP).
    if (auto r = host.create_offer(); !r) {
        spdlog::error("[main] create_offer: {}", r.error());
        return 1;
    }
    spdlog::info("[main] Waiting for ICE gathering...");
    const auto offer_opt = observer.wait_for_offer(60s);
    if (!offer_opt) {
        spdlog::error("[main] ICE gathering timed out");
        host.close();
        return 1;
    }
    const SessionDescription& offer = *offer_opt;

    // ---- POST offer to signaling server ----
    const std::wstring whost(server_host, server_host + std::strlen(server_host));
    HttpClient http(whost, server_port);
    if (!http.ok()) {
        spdlog::error("[main] Cannot connect to {}:{}", server_host, server_port);
        host.close();
        return 1;
    }

    const std::string offer_path = std::string("/api/session/") + session_id + "/offer";
    const std::string offer_json = "{\"type\":\""
                                 + std::string(to_string(offer.type))
                                 + "\",\"sdp\":\""
                                 + json_escape(offer.sdp)
                                 + "\"}";
    if (http.post_json(offer_path, offer_json).empty()) {
        spdlog::error("[main] POST offer failed — is server/main.py running on {}:{}?",
                      server_host, server_port);
        host.close();
        return 1;
    }
    spdlog::info("[main] Offer accepted. Open web/input-lab.html and connect to session '{}'",
                 session_id);

    // ---- Poll for browser answer ----
    const std::string answer_path = std::string("/api/session/") + session_id + "/answer";
    std::string answer_json;
    constexpr int kPollMax = 120;
    for (int i = 0; i < kPollMax && !g_stop; ++i) {
        answer_json = http.get(answer_path);
        if (!answer_json.empty()) break;
        std::this_thread::sleep_for(500ms);
        if ((i + 1) % 10 == 0) {
            spdlog::info("[main] Waiting for browser answer... {}/{}s",
                         (i + 1) / 2, kPollMax / 2);
        }
    }
    if (answer_json.empty()) {
        spdlog::error("[main] Browser answer timed out");
        host.close();
        return 1;
    }

    const std::string answer_sdp = json_get_string(answer_json, "sdp");
    if (answer_sdp.empty()) {
        spdlog::error("[main] Could not parse 'sdp' from answer");
        host.close();
        return 1;
    }
    SessionDescription answer;
    answer.type = SessionDescriptionType::kAnswer;
    answer.sdp  = answer_sdp;
    if (auto r = host.set_remote_description(std::move(answer)); !r) {
        spdlog::error("[main] set_remote_description: {}", r.error());
        host.close();
        return 1;
    }
    spdlog::info("[main] Connected. Waiting for input packets...");
    std::printf("\n");

    // ---- Live overlay loop ----
    while (!g_stop) {
        std::this_thread::sleep_for(250ms);
        print_overlay(stats.snapshot());
    }

    std::printf("\n");
    spdlog::info("[main] Shutting down...");
    host.close();
    spdlog::info("=== GameStream Input Test — Done ===");
    return 0;
}
