/// wgc_encode_test — capture + AMF H.264 encode
///
/// Supports two capture backends:
///   WGC  (default)  — per-window Windows.Graphics.Capture.
///   DXGI (--dxgi)   — full-desktop DXGI Desktop Duplication.
///                     Works with exclusive fullscreen and Independent-Flip games.
///
/// Pipeline:
///   [Main Thread]   Capture acquire → AMF encode → RingBuffer<EncodedFrame, 8>
///   [Writer Thread]                                RingBuffer → file write
///
/// Usage: wgc_encode_test.exe [duration_seconds] [--dxgi]
///   duration_seconds — recording duration in seconds (default: 10)
///   --dxgi           — use DXGI Desktop Duplication instead of WGC

#include "dxgi_capture.h"
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
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <windows.h>

using namespace gamestream;
using namespace std::chrono;

static constexpr uint32_t kTargetFps        = 60;
static constexpr uint32_t kDefaultBitrate   = 15'000'000;  // 15 Mbps
static constexpr uint32_t kDefaultDurationS = 10;
static constexpr double   kEncodeTargetMs   = 6.0;         // latency budget target

// ---------------------------------------------------------------------------
// Window enumeration (WGC mode only)
// ---------------------------------------------------------------------------

struct WindowInfo {
    HWND         hwnd;
    std::wstring title;
};

static BOOL CALLBACK enum_windows_proc(HWND hwnd, LPARAM lparam) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;

    wchar_t title[256];
    int title_len = GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    if (title_len == 0) return TRUE;

    // Capture all visible windows including borderless (no WS_CAPTION check)
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
    SetThreadDescription(GetCurrentThread(), L"GameStream Writer");

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

// Non-blocking capture poll. Updates last_frame if a new one arrived.
// Returns true = new frame; false = duplicate (last_frame unchanged).
static bool poll_new_frame(IFrameCapture& capture, CaptureFrame& last_frame) {
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

// Blocks until the first capture frame arrives (up to 5 s).
// Returns the frame on success, empty optional on timeout.
static std::optional<CaptureFrame> wait_first_frame(IFrameCapture& capture) {
    auto cap = capture.acquire_frame(5000);
    if (!cap) {
        spdlog::error("No frame in 5 s: {}", cap.error());
        return std::nullopt;
    }
    CaptureFrame f = cap.value();
    capture.release_frame();
    return f;
}

// ---------------------------------------------------------------------------
// Encode loop — fixed kTargetFps output with frame duplication.
//
// Ticks at a fixed 16.67 ms interval; if no new capture frame arrived,
// re-encodes the previous texture to keep the bitstream at exactly kTargetFps.
// ---------------------------------------------------------------------------

static int encode_loop(IFrameCapture&               capture,
                       AMFEncoder&                  encoder,
                       RingBuffer<EncodedFrame, 8>& write_queue,
                       double                       duration_sec)
{
    const auto frame_interval = nanoseconds(1'000'000'000 / kTargetFps);
    const auto deadline       = steady_clock::now() + duration<double>(duration_sec);
    const auto start_time     = steady_clock::now();

    encoder.request_keyframe();

    auto first = wait_first_frame(capture);
    if (!first) return EXIT_FAILURE;
    CaptureFrame last_frame = std::move(*first);

    auto     next_tick     = steady_clock::now();
    auto     last_log_time = start_time;
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

        // Log once per second — not per-frame (principle 7.3)
        auto now = steady_clock::now();
        if (duration<double>(now - last_log_time).count() >= 1.0) {
            spdlog::info("[{:4.1f}s] Frames: {:4} | Dups: {:4} | Encode avg: {:.2f}ms",
                         duration<double>(now - start_time).count(),
                         frames_out, duplicates,
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
    if (avg_enc > kEncodeTargetMs)
        spdlog::warn("Encode latency exceeds {:.0f} ms target", kEncodeTargetMs);
    else
        spdlog::info("Latency target MET: {:.2f} ms < {:.0f} ms", avg_enc, kEncodeTargetMs);
    return EXIT_SUCCESS;
}

// ---------------------------------------------------------------------------
// remux_to_mp4 — wraps raw H.264 in an MP4 container via ffmpeg.
//
// Sets the correct -framerate so players don't fall back to 25fps heuristics.
// Runs ffmpeg in a hidden window; waits up to 60 s for completion.
// Returns the .mp4 path on success, empty string on failure.
// ---------------------------------------------------------------------------
static std::string remux_to_mp4(const std::string& h264_path, uint32_t fps) {
    const std::string mp4_path =
        h264_path.substr(0, h264_path.rfind('.')) + ".mp4";

    // ffmpeg -y             : overwrite output if exists
    // -framerate <fps>      : set input frame rate (fixes 25 fps heuristic)
    // -i <in>               : input file
    // -c copy               : stream copy — no re-encode
    // -movflags +faststart  : move moov atom to front (better for streaming)
    // -loglevel warning     : suppress noisy info output
    std::string cmd = std::format(
        "ffmpeg -y -framerate {} -i \"{}\" -c copy -movflags +faststart \"{}\" -loglevel warning",
        fps, h264_path, mp4_path);

    STARTUPINFOA si{};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};

    // CreateProcessA requires a writable lpCommandLine buffer.
    std::vector<char> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back('\0');

    if (!CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        spdlog::warn("[remux] ffmpeg not found in PATH (CreateProcess error 0x{:08X})",
                     static_cast<uint32_t>(GetLastError()));
        return {};
    }

    WaitForSingleObject(pi.hProcess, 60'000);  // Wait up to 60 s

    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exit_code != 0) {
        spdlog::warn("[remux] ffmpeg exited with code {}", exit_code);
        return {};
    }
    return mp4_path;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");

    SetThreadDescription(GetCurrentThread(), L"GameStream Encode");

    // Parse arguments: [duration_seconds] [--dxgi]
    double duration_sec = kDefaultDurationS;
    bool   use_dxgi     = false;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--dxgi") {
            use_dxgi = true;
        } else {
            try { duration_sec = std::stod(argv[i]); } catch (...) {}
        }
    }

    const char* mode_name = use_dxgi ? "DXGI" : "WGC";
    spdlog::info("=== Encode Test [{}]: {:.0f}s recording ===", mode_name, duration_sec);

    // Create and initialize capture backend
    std::unique_ptr<IFrameCapture> capture;
    if (use_dxgi) {
        auto cap = std::make_unique<DXGICapture>();
        // initialize(0) = first AMD adapter
        if (auto r = cap->initialize(0); !r) {
            spdlog::error("DXGI init failed: {}", r.error());
            return EXIT_FAILURE;
        }
        capture = std::move(cap);
    } else {
        const uintptr_t hwnd_value = select_window();
        auto cap = std::make_unique<WGCCapture>();
        if (auto r = cap->initialize(hwnd_value); !r) {
            spdlog::error("WGC init failed: {}", r.error());
            return EXIT_FAILURE;
        }
        capture = std::move(cap);
    }

    uint32_t width = 0, height = 0;
    capture->get_resolution(width, height);
    spdlog::info("Capture [{}]: {}x{}", mode_name, width, height);

    ID3D11Device* device = capture->get_device();
    if (!device) { spdlog::error("Capture returned null D3D11 device"); return EXIT_FAILURE; }

    AMFEncoder encoder;
    EncoderConfig cfg;
    cfg.width       = width;
    cfg.height      = height;
    cfg.fps         = kTargetFps;
    cfg.bitrate_bps = kDefaultBitrate;
    if (auto r = encoder.initialize(device, cfg); !r) {
        spdlog::error("AMF init failed: {}", r.error());
        return EXIT_FAILURE;
    }

    spdlog::info("Pipeline ready. Recording for {:.0f}s...", duration_sec);

    const std::string output_path = use_dxgi ? "desktop_capture.h264" : "window_capture.h264";
    RingBuffer<EncodedFrame, 8> write_queue;
    std::atomic<bool>     encode_done{false};
    std::atomic<uint64_t> total_bytes{0};

    int result = EXIT_FAILURE;
    {
        // jthread RAII-joins on destruction, ensuring the writer always finishes
        // cleanly even if an exception propagates — unlike std::thread.
        std::jthread writer([&]() {
            writer_thread_fn(write_queue, encode_done, output_path, total_bytes);
        });

        result = encode_loop(*capture, encoder, write_queue, duration_sec);

        encode_done.store(true, std::memory_order_release);
        // writer joins automatically when the block exits
    }
    // total_bytes is fully written now

    spdlog::info("Raw H.264: {} ({} bytes)", output_path, total_bytes.load());
    if (result == EXIT_SUCCESS) {
        spdlog::info("=== wgc_encode_test PASSED ===");

        spdlog::info("Remuxing to MP4...");
        const std::string mp4_path = remux_to_mp4(output_path, kTargetFps);
        if (!mp4_path.empty()) {
            DeleteFileA(output_path.c_str());
            spdlog::info("Output: {}", mp4_path);
            std::cout << "\nDone! Play with: ffplay " << mp4_path << "\n";
        } else {
            // ffmpeg unavailable or failed — keep H.264, advise explicit fps
            spdlog::warn("Remux failed — raw H.264 preserved");
            std::cout << "\nDone! Play with: ffplay -framerate " << kTargetFps
                      << " " << output_path << "\n";
        }
    }
    return result;
}
