/// wgc_encode_test — per-window WGC capture + AMF H.264 encode
///
/// Pipeline:
///   [Main Thread]   WGC acquire → AMF encode → RingBuffer<EncodedFrame, 8>
///   [Writer Thread]                             RingBuffer → file write
///
/// Usage: wgc_encode_test.exe [duration_seconds]
///   duration_seconds — recording duration in seconds (default: 10)
///
/// Output: window_capture.h264 (verify with: ffplay window_capture.h264)

#include "wgc_capture.h"
#include "amf_encoder.h"
#include "util/ring_buffer.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

using namespace gamestream;
using namespace std::chrono;

// ---------------------------------------------------------------------------
// Window enumeration
// ---------------------------------------------------------------------------

struct WindowInfo {
    HWND         hwnd;
    std::wstring title;
};

static BOOL CALLBACK enum_windows_proc(HWND hwnd, LPARAM lparam) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;

    wchar_t title[256];
    if (GetWindowTextW(hwnd, title, static_cast<int>(std::size(title))) == 0) return TRUE;
    if (title[0] == L'\0') return TRUE;

    // Only windows with a title bar (skip tool windows, popups, etc.)
    if (!(GetWindowLongW(hwnd, GWL_STYLE) & WS_CAPTION)) return TRUE;

    reinterpret_cast<std::vector<WindowInfo>*>(lparam)->push_back({ hwnd, title });
    return TRUE;
}

static std::vector<WindowInfo> enumerate_windows() {
    std::vector<WindowInfo> windows;
    EnumWindows(enum_windows_proc, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

static std::string to_utf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

// Shows window list, reads user choice, returns selected HWND (0 = foreground).
static uintptr_t select_window() {
    auto windows = enumerate_windows();
    std::cout << "\nAvailable windows:\n";
    std::cout << "  [0] Foreground window\n";
    for (size_t i = 0; i < windows.size(); ++i) {
        std::cout << "  [" << (i + 1) << "] " << to_utf8(windows[i].title) << "\n";
    }
    std::cout << "\nSelect window number: ";
    int choice = 0;
    std::cin >> choice;
    if (choice > 0 && choice <= static_cast<int>(windows.size())) {
        spdlog::info("Selected: {}", to_utf8(windows[choice - 1].title));
        return reinterpret_cast<uintptr_t>(windows[choice - 1].hwnd);
    }
    spdlog::info("Using foreground window");
    return 0;
}

// ---------------------------------------------------------------------------
// Writer thread — drains the ring buffer to disk
// ---------------------------------------------------------------------------

static void writer_thread_fn(RingBuffer<EncodedFrame, 8>& write_queue,
                              std::atomic<bool>&           encode_done,
                              const std::string&           output_path,
                              std::atomic<uint64_t>&       total_bytes_written)
{
    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        spdlog::error("[writer] Cannot open {}", output_path);
        return;
    }

    uint64_t bytes = 0;
    while (true) {
        EncodedFrame frame;
        if (write_queue.try_pop(frame)) {
            out.write(reinterpret_cast<const char*>(frame.data.data()),
                      static_cast<std::streamsize>(frame.data.size()));
            bytes += frame.data.size();
        } else {
            if (encode_done.load(std::memory_order_acquire)) break;
            std::this_thread::yield();
        }
    }

    // Drain remaining frames after encode thread signals done
    EncodedFrame frame;
    while (write_queue.try_pop(frame)) {
        out.write(reinterpret_cast<const char*>(frame.data.data()),
                  static_cast<std::streamsize>(frame.data.size()));
        bytes += frame.data.size();
    }

    out.close();
    total_bytes_written.store(bytes, std::memory_order_release);
    spdlog::info("[writer] Wrote {} bytes to {}", bytes, output_path);
}

// ---------------------------------------------------------------------------
// Encode loop helpers
// ---------------------------------------------------------------------------

// Non-blocking WGC poll. Updates last_frame if a new one arrived.
// Returns true = new frame; false = duplicate (last_frame unchanged).
static bool poll_new_frame(WGCCapture& capture, CaptureFrame& last_frame) {
    auto cap = capture.acquire_frame(0);  // 0 ms = do not wait
    if (!cap) return false;
    last_frame = cap.value();
    capture.release_frame();
    return true;
}

struct FrameMetrics { bool ok; bool is_kf; double enc_ms; size_t bytes; };

// Encodes frame and pushes to the write queue.
static FrameMetrics encode_and_push(AMFEncoder&                  encoder,
                                    const CaptureFrame&          frame,
                                    RingBuffer<EncodedFrame, 8>& queue,
                                    uint32_t                     frame_idx)
{
    auto t0  = steady_clock::now();
    auto enc = encoder.encode(frame);
    auto t1  = steady_clock::now();

    if (!enc) {
        spdlog::warn("[encode] Frame {}: {}", frame_idx, enc.error());
        return {false, false, 0.0, 0};
    }

    EncodedFrame ef = std::move(enc.value());
    const FrameMetrics m{true, ef.is_keyframe,
                         duration<double, std::milli>(t1 - t0).count(),
                         ef.data.size()};

    spdlog::trace("Frame {:4}: {:3s} | {:6} B | {:.2f}ms",
                  frame_idx, m.is_kf ? "IDR" : "  P", m.bytes, m.enc_ms);

    while (!queue.try_push(std::move(ef))) {
        std::this_thread::yield();
    }
    return m;
}

// ---------------------------------------------------------------------------
// Encode loop — fixed 60 fps output with frame duplication.
//
// WGC delivers frames whenever screen content changes.  We tick at a fixed
// 16.67 ms (60 fps) interval; if no new WGC frame arrived, we re-encode the
// previous texture to keep the bitstream at exactly 60 fps.
// ---------------------------------------------------------------------------

static int encode_loop(WGCCapture&                  capture,
                       AMFEncoder&                  encoder,
                       RingBuffer<EncodedFrame, 8>& write_queue,
                       double                       duration_sec)
{
    constexpr int kTargetFps    = 60;
    const auto frame_interval   = std::chrono::nanoseconds(1'000'000'000 / kTargetFps);
    const auto deadline         = steady_clock::now() + duration<double>(duration_sec);

    encoder.request_keyframe();
    CaptureFrame last_frame{};
    {   // Block until the first WGC frame arrives
        auto cap = capture.acquire_frame(5000);
        if (!cap) { spdlog::error("No frame in 5 s: {}", cap.error()); return EXIT_FAILURE; }
        last_frame = cap.value();
        capture.release_frame();
    }

    auto     next_tick     = steady_clock::now();
    auto     last_log_time = steady_clock::now();
    uint32_t frames_out = 0, keyframes = 0, duplicates = 0;
    double   sum_enc_ms = 0.0;

    while (steady_clock::now() < deadline) {
        next_tick += frame_interval;
        std::this_thread::sleep_until(next_tick);

        if (!poll_new_frame(capture, last_frame)) ++duplicates;

        auto m = encode_and_push(encoder, last_frame, write_queue, frames_out);
        if (!m.ok) continue;
        if (m.is_kf) keyframes++;
        sum_enc_ms += m.enc_ms;
        ++frames_out;

        // Log once per second (not per-frame — principle 7.3)
        auto now = steady_clock::now();
        if (duration<double>(now - last_log_time).count() >= 1.0) {
            const double elapsed = duration<double>(now - (deadline - duration<double>(duration_sec))).count();
            spdlog::info("[{:4.1f}s] Frames: {:4} | Dups: {:4} | Encode avg: {:.2f}ms",
                         elapsed, frames_out, duplicates,
                         frames_out > 0 ? sum_enc_ms / frames_out : 0.0);
            last_log_time = now;
        }
    }

    if (frames_out == 0) { spdlog::error("No frames encoded"); return EXIT_FAILURE; }

    const double avg_enc = sum_enc_ms / frames_out;
    spdlog::info("=== Stats: {} frames ({} unique + {} dups), {:.1f}s, FPS: {:.1f} ===",
                 frames_out, frames_out - duplicates, duplicates,
                 duration_sec, frames_out / duration_sec);
    spdlog::info("    Encode avg: {:.2f} ms", avg_enc);
    if (avg_enc > 6.0) spdlog::warn("Encode latency exceeds 6 ms target");
    else               spdlog::info("Latency target MET: {:.2f} ms < 6 ms", avg_enc);
    return EXIT_SUCCESS;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");

    const double duration_sec = (argc > 1) ? std::stod(argv[1]) : 10.0;
    spdlog::info("=== WGC Encode Test: {:.0f}s recording ===", duration_sec);

    const uintptr_t hwnd_value = select_window();

    WGCCapture capture;
    if (auto r = capture.initialize(hwnd_value); !r) {
        spdlog::error("WGC init failed: {}", r.error());
        return EXIT_FAILURE;
    }

    uint32_t width = 0, height = 0;
    capture.get_resolution(width, height);
    spdlog::info("Capture: {}x{}", width, height);

    ID3D11Device* device = capture.get_device();
    if (!device) { spdlog::error("WGC returned null D3D11 device"); return EXIT_FAILURE; }

    AMFEncoder encoder;
    EncoderConfig cfg;
    cfg.width = width; cfg.height = height; cfg.fps = 60; cfg.bitrate_bps = 15'000'000;
    if (auto r = encoder.initialize(device, cfg); !r) {
        spdlog::error("AMF init failed: {}", r.error());
        return EXIT_FAILURE;
    }

    spdlog::info("Pipeline ready. Recording for {:.0f}s...", duration_sec);

    const std::string output_path = "window_capture.h264";
    RingBuffer<EncodedFrame, 8> write_queue;
    std::atomic<bool>     encode_done{false};
    std::atomic<uint64_t> total_bytes{0};

    std::thread writer(writer_thread_fn, std::ref(write_queue),
                       std::ref(encode_done), std::cref(output_path), std::ref(total_bytes));

    const int result = encode_loop(capture, encoder, write_queue, duration_sec);

    encode_done.store(true, std::memory_order_release);
    writer.join();

    spdlog::info("Output: {} ({} bytes)", output_path, total_bytes.load());
    if (result == EXIT_SUCCESS) {
        spdlog::info("=== wgc_encode_test PASSED ===");
        std::cout << "\nDone! Play with: ffplay " << output_path << "\n";
    }
    return result;
}
