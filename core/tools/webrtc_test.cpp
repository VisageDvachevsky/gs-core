/// webrtc_test.cpp — Stage 3 end-to-end WebRTC streaming test.
///
/// Usage:  webrtc_test.exe [server_host [server_port [session_id]]]
/// Default: webrtc_test localhost 8765 test-session
///
/// Sequence:
///   1. DXGI capture + AMF encoder (share the same D3D11 device, zero-copy)
///   2. WebRTCHost::initialize  →  PeerConnectionFactory + PeerConnection
///   3. add_video_track         →  CaptureVideoSource thread starts
///   4. create_offer + wait for on_ice_gathering_complete (non-trickle)
///   5. POST /api/session/{id}/offer  to signaling server (server/main.py)
///   6. Poll GET /api/session/{id}/answer until browser submits its SDP
///   7. set_remote_description(answer)
///   8. Stream until Ctrl+C

#include <windows.h>
#include <winhttp.h>

#include "webrtc_host.h"
#include "dxgi_capture.h"
#include "amf_encoder.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

using namespace gamestream;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Ctrl+C / Ctrl+Break shutdown
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
// Minimal JSON helpers — sufficient for SDP signaling
// ---------------------------------------------------------------------------

/// Escape a raw string for embedding in a JSON string literal.
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

/// Unescape a JSON string literal value (reverse of json_escape).
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

/// Extract the string value of @p key from a flat JSON object.
/// Returns empty string on parse failure.
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

// ---------------------------------------------------------------------------
// Minimal synchronous WinHTTP client (HTTP, LAN use)
// ---------------------------------------------------------------------------

class HttpClient {
public:
    explicit HttpClient(std::wstring_view host, INTERNET_PORT port) {
        session_ = WinHttpOpen(
            L"GameStream/1.0",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);
        if (!session_) {
            spdlog::error("[Http] WinHttpOpen failed: {:#010x}", GetLastError());
            return;
        }
        connect_ = WinHttpConnect(session_, host.data(), port, 0);
        if (!connect_) {
            spdlog::error("[Http] WinHttpConnect failed: {:#010x}", GetLastError());
        }
    }

    ~HttpClient() {
        if (connect_) WinHttpCloseHandle(connect_);
        if (session_) WinHttpCloseHandle(session_);
    }

    HttpClient(const HttpClient&)            = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    [[nodiscard]] bool ok() const noexcept { return connect_ != nullptr; }

    /// POST a JSON body to @p path. Returns response body or empty on error.
    [[nodiscard]] std::string post_json(std::string_view path, std::string_view body) {
        return request(L"POST", path, body, L"Content-Type: application/json\r\n");
    }

    /// GET @p path. Returns response body or empty on error / 404.
    [[nodiscard]] std::string get(std::string_view path) {
        return request(L"GET", path, {}, nullptr);
    }

private:
    HINTERNET session_ = nullptr;
    HINTERNET connect_ = nullptr;

    std::string request(const wchar_t*  verb,
                        std::string_view path,
                        std::string_view body,
                        const wchar_t*   extra_headers) {
        if (!connect_) return {};

        const std::wstring wpath(path.begin(), path.end());
        HINTERNET req = WinHttpOpenRequest(
            connect_, verb, wpath.c_str(),
            nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!req) {
            spdlog::error("[Http] WinHttpOpenRequest failed: {:#010x}", GetLastError());
            return {};
        }
        struct ReqGuard { HINTERNET h; ~ReqGuard() { WinHttpCloseHandle(h); } } guard{req};

        const auto  body_len = static_cast<DWORD>(body.size());
        // LPVOID cast: WinHTTP won't modify the buffer.
        LPVOID body_ptr = body.empty()
                          ? nullptr
                          : const_cast<char*>(body.data());
        const BOOL sent = WinHttpSendRequest(
            req,
            extra_headers,
            extra_headers ? static_cast<DWORD>(-1L) : 0u,
            body_ptr, body_len, body_len, 0);
        if (!sent) {
            spdlog::error("[Http] WinHttpSendRequest failed: {:#010x}", GetLastError());
            return {};
        }
        if (!WinHttpReceiveResponse(req, nullptr)) {
            spdlog::error("[Http] WinHttpReceiveResponse failed: {:#010x}", GetLastError());
            return {};
        }

        DWORD status     = 0;
        DWORD status_len = sizeof(status);
        WinHttpQueryHeaders(req,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr,
            &status, &status_len, nullptr);
        if (status >= 400) {
            spdlog::debug("[Http] HTTP {} from {}", status, path);
            return {};
        }

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
// Observer: stores offer SDP, signals when ICE gathering is complete
// ---------------------------------------------------------------------------

class StreamingObserver final : public IWebRTCObserver {
public:
    /// Block until ICE gathering completes or @p timeout elapses.
    /// Returns the complete offer SDP (with all candidates embedded) on success.
    [[nodiscard]] std::optional<SessionDescription>
    wait_for_offer(std::chrono::seconds timeout) {
        std::unique_lock lk(mtx_);
        if (!cv_.wait_for(lk, timeout, [this] { return gathering_done_; })) {
            return std::nullopt;
        }
        return offer_sdp_;
    }

    // ---- IWebRTCObserver ----

    void on_local_sdp_created(SessionDescription sdp) override {
        // Store SDP first; signal deferred to on_ice_gathering_complete so
        // the SDP already contains all embedded ICE candidates.
        std::lock_guard lk(mtx_);
        offer_sdp_ = std::move(sdp);
    }

    void on_ice_candidate(IceCandidateInfo /*candidate*/) override {
        // Non-trickle mode: candidates are embedded in the offer SDP at
        // gathering-complete time. Individual candidates are not forwarded.
    }

    void on_connection_state_changed(PeerConnectionState state) override {
        spdlog::info("[observer] PeerConnection: {}", to_string(state));
    }

    void on_ice_connection_state_changed(IceConnectionState state) override {
        spdlog::info("[observer] ICE connection: {}", to_string(state));
    }

    void on_data_channel_open() override {
        spdlog::info("[observer] DataChannel open");
    }

    void on_data_channel_message(const uint8_t* data, size_t size) override {
        spdlog::debug("[observer] DataChannel: {} bytes", size);
        (void)data;  // Stage 4: dispatch to OS input layer
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
    logger->set_level(spdlog::level::debug);
    logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
    spdlog::set_default_logger(std::move(logger));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    setup_logging();
    spdlog::info("=== GameStream WebRTC Stage 3 ===");

    const char*   server_host = (argc > 1) ? argv[1] : "localhost";
    const auto    server_port = static_cast<INTERNET_PORT>(
                                    (argc > 2) ? std::stoul(argv[2]) : 8765u);
    const char*   session_id  = (argc > 3) ? argv[3] : "test-session";

    spdlog::info("[main] server={}:{}  session={}", server_host, server_port, session_id);
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    // ---- 1. DXGI capture ----
    DXGICapture capture;
    if (auto r = capture.initialize(0); !r) {
        spdlog::error("[main] DXGI capture init: {}", r.error());
        return 1;
    }
    uint32_t width = 0, height = 0;
    capture.get_resolution(width, height);
    spdlog::info("[main] Capture: {}x{}", width, height);

    // ---- 2. AMF encoder (shares the DXGI D3D11 device — zero-copy) ----
    AMFEncoder   encoder;
    EncoderConfig enc_cfg;
    enc_cfg.width       = width;
    enc_cfg.height      = height;
    enc_cfg.fps         = 60;
    enc_cfg.bitrate_bps = 15'000'000;
    if (auto r = encoder.initialize(capture.get_device(), enc_cfg); !r) {
        spdlog::error("[main] AMF encoder init: {}", r.error());
        return 1;
    }

    // ---- 3. WebRTCHost ----
    StreamingObserver observer;
    WebRTCHost        host;
    WebRTCConfig      cfg;
    {
        IceServer stun;
        stun.uri = "stun:stun.l.google.com:19302";
        cfg.ice_servers.push_back(std::move(stun));
    }
    if (auto r = host.initialize(cfg, &observer); !r) {
        spdlog::error("[main] WebRTCHost init: {}", r.error());
        return 1;
    }
    spdlog::info("[main] WebRTCHost initialized");

    // ---- 4. Add video track (starts CaptureVideoSource) ----
    if (auto r = host.add_video_track(&capture, &encoder); !r) {
        spdlog::error("[main] add_video_track: {}", r.error());
        return 1;
    }
    spdlog::info("[main] Video track added");

    // ---- 5. Create offer + wait for ICE gathering ----
    if (auto r = host.create_offer(); !r) {
        spdlog::error("[main] create_offer: {}", r.error());
        return 1;
    }
    spdlog::info("[main] Waiting for ICE gathering (30s timeout)...");
    const auto offer_opt = observer.wait_for_offer(30s);
    if (!offer_opt) {
        spdlog::error("[main] ICE gathering timed out");
        host.close();
        return 1;
    }
    const SessionDescription& offer = *offer_opt;
    spdlog::info("[main] Offer ready ({} bytes SDP)", offer.sdp.size());

    // ---- 6. POST offer to signaling server ----
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
    spdlog::info("[main] POST {} ...", offer_path);
    if (http.post_json(offer_path, offer_json).empty()) {
        spdlog::error("[main] POST offer failed — is server/main.py running on {}:{}?",
                      server_host, server_port);
        host.close();
        return 1;
    }
    spdlog::info("[main] Offer accepted — open http://{}:{}/test.html in a browser",
                 server_host, server_port);

    // ---- 7. Poll for browser answer ----
    const std::string answer_path = std::string("/api/session/") + session_id + "/answer";
    spdlog::info("[main] Polling {} ...", answer_path);
    std::string answer_json;
    constexpr int kPollMaxAttempts = 120;  // 120 × 500 ms = 60 s
    for (int i = 0; i < kPollMaxAttempts && !g_stop; ++i) {
        answer_json = http.get(answer_path);
        if (!answer_json.empty()) break;
        std::this_thread::sleep_for(500ms);
        if ((i + 1) % 10 == 0) {
            spdlog::info("[main] Waiting for browser answer... {}/{}s",
                         (i + 1) / 2, kPollMaxAttempts / 2);
        }
    }
    if (answer_json.empty()) {
        spdlog::error("[main] Browser answer timed out ({}s)", kPollMaxAttempts / 2);
        host.close();
        return 1;
    }

    // ---- 8. Apply remote description ----
    const std::string answer_sdp = json_get_string(answer_json, "sdp");
    if (answer_sdp.empty()) {
        spdlog::error("[main] Could not parse 'sdp' from answer JSON: {:.80}", answer_json);
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
    spdlog::info("[main] Remote description set — waiting for kConnected...");

    // ---- 9. Stream until Ctrl+C ----
    spdlog::info("[main] Streaming. Press Ctrl+C to stop.");
    while (!g_stop) {
        std::this_thread::sleep_for(1s);
    }

    spdlog::info("[main] Shutting down...");
    host.close();
    spdlog::info("=== GameStream WebRTC Stage 3 — Done ===");
    return 0;
}
