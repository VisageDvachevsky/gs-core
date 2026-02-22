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
    if (!IsWindowVisible(hwnd)) return TRUE;

    // Skip minimized windows
    if (IsIconic(hwnd)) return TRUE;

    wchar_t title[256];
    if (GetWindowTextW(hwnd, title, static_cast<int>(std::size(title))) == 0) return TRUE;
    if (title[0] == L'\0') return TRUE;

    // Only windows with a title bar (skip tool windows, popups, etc.)
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if (!(style & WS_CAPTION)) return TRUE;

    auto* list = reinterpret_cast<std::vector<WindowInfo>*>(lparam);
    list->push_back({ hwnd, title });
    return TRUE;
}

static std::vector<WindowInfo> enumerate_windows() {
    std::vector<WindowInfo> windows;
    EnumWindows(enum_windows_proc, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

static std::string to_utf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                                   nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                        result.data(), size, nullptr, nullptr);
    return result;
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

    // Drain any remaining frames after encode thread signals done
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
// Encode loop — runs on the calling thread for duration_sec seconds
// ---------------------------------------------------------------------------

static int encode_loop(WGCCapture&                  capture,
                       AMFEncoder&                  encoder,
                       RingBuffer<EncodedFrame, 8>& write_queue,
                       double                       duration_sec)
{
    const auto deadline = steady_clock::now() + duration<double>(duration_sec);

    uint32_t frames_ok  = 0;
    uint32_t keyframes  = 0;
    double   sum_cap_ms = 0.0;
    double   sum_enc_ms = 0.0;

    encoder.request_keyframe();  // IDR on first frame

    while (steady_clock::now() < deadline) {
        auto t0 = steady_clock::now();

        auto cap = capture.acquire_frame(16);  // 16 ms timeout (≥ 60 FPS)
        if (!cap) {
            // Timeout is normal — WGC has not produced a new frame yet
            continue;
        }

        auto t1 = steady_clock::now();

        CaptureFrame cf = std::move(cap.value());
        capture.release_frame();

        auto enc = encoder.encode(cf);
        auto t2  = steady_clock::now();

        if (!enc) {
            spdlog::warn("[encode] Frame {}: {}", frames_ok, enc.error());
            continue;
        }

        EncodedFrame ef = std::move(enc.value());

        const bool   is_kf    = ef.is_keyframe;
        const size_t byte_cnt = ef.data.size();
        if (is_kf) keyframes++;

        // Spin-push to writer thread (writer always keeps up in practice)
        while (!write_queue.try_push(std::move(ef))) {
            std::this_thread::yield();
        }

        const double cap_ms = duration<double, std::milli>(t1 - t0).count();
        const double enc_ms = duration<double, std::milli>(t2 - t1).count();

        sum_cap_ms += cap_ms;
        sum_enc_ms += enc_ms;

        spdlog::info("Frame {:4}: {:3s} | {:6} bytes | Capture {:.2f}ms | Encode {:.2f}ms",
                     frames_ok,
                     is_kf ? "IDR" : "  P",
                     byte_cnt,
                     cap_ms, enc_ms);

        ++frames_ok;
    }

    if (frames_ok == 0) {
        spdlog::error("No frames encoded");
        return EXIT_FAILURE;
    }

    const double avg_cap = sum_cap_ms / frames_ok;
    const double avg_enc = sum_enc_ms / frames_ok;
    const double fps     = frames_ok  / duration_sec;

    spdlog::info("=== Stats: {} frames, {} keyframes, {:.1f}s ===",
                 frames_ok, keyframes, duration_sec);
    spdlog::info("    Capture avg: {:.2f} ms", avg_cap);
    spdlog::info("    Encode  avg: {:.2f} ms", avg_enc);
    spdlog::info("    Avg FPS:     {:.1f}", fps);

    if (avg_enc > 6.0) {
        spdlog::warn("Encode latency {:.2f} ms exceeds 6 ms target", avg_enc);
    } else {
        spdlog::info("Latency target MET: encode avg {:.2f} ms < 6 ms", avg_enc);
    }

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

    // -----------------------------------------------------------------------
    // Step 1: List windows and let user choose
    // -----------------------------------------------------------------------
    auto windows = enumerate_windows();

    std::cout << "\nAvailable windows:\n";
    std::cout << "  [0] Foreground window (whichever is active when capture starts)\n";
    for (size_t i = 0; i < windows.size(); ++i) {
        std::cout << "  [" << (i + 1) << "] "
                  << to_utf8(windows[i].title) << "\n";
    }

    std::cout << "\nSelect window number: ";
    int choice = 0;
    std::cin >> choice;

    uintptr_t hwnd_value = 0;  // 0 = foreground window
    if (choice > 0 && choice <= static_cast<int>(windows.size())) {
        hwnd_value = reinterpret_cast<uintptr_t>(windows[choice - 1].hwnd);
        spdlog::info("Selected: {}", to_utf8(windows[choice - 1].title));
    } else {
        spdlog::info("Using foreground window");
    }

    // -----------------------------------------------------------------------
    // Step 2: WGC capture init
    // -----------------------------------------------------------------------
    WGCCapture capture;
    if (auto r = capture.initialize(hwnd_value); !r) {
        spdlog::error("WGC init failed: {}", r.error());
        return EXIT_FAILURE;
    }

    uint32_t width = 0, height = 0;
    capture.get_resolution(width, height);
    spdlog::info("Capture resolution: {}x{}", width, height);

    ID3D11Device* device = capture.get_device();
    if (!device) {
        spdlog::error("WGC returned null D3D11 device");
        return EXIT_FAILURE;
    }

    // -----------------------------------------------------------------------
    // Step 3: AMF encoder init — reuses WGC's D3D11 device (zero-copy path)
    // -----------------------------------------------------------------------
    AMFEncoder encoder;

    EncoderConfig cfg;
    cfg.width       = width;
    cfg.height      = height;
    cfg.fps         = 60;
    cfg.bitrate_bps = 15'000'000;

    if (auto r = encoder.initialize(device, cfg); !r) {
        spdlog::error("AMF init failed: {}", r.error());
        return EXIT_FAILURE;
    }

    spdlog::info("Pipeline ready. Recording for {:.0f}s...", duration_sec);

    // -----------------------------------------------------------------------
    // Step 4: Ring buffer + writer thread
    // -----------------------------------------------------------------------
    RingBuffer<EncodedFrame, 8> write_queue;
    std::atomic<bool>     encode_done{false};
    std::atomic<uint64_t> total_bytes{0};

    const std::string output_path = "window_capture.h264";

    std::thread writer(writer_thread_fn,
                       std::ref(write_queue),
                       std::ref(encode_done),
                       std::cref(output_path),
                       std::ref(total_bytes));

    // -----------------------------------------------------------------------
    // Step 5: Encode loop (calling thread)
    // -----------------------------------------------------------------------
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
