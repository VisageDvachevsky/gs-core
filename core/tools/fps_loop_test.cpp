#include "dxgi_capture.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <format>

using namespace gamestream;
using namespace std::chrono;

// Press Ctrl+C to stop
std::atomic<bool> running{true};

BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT) {
        running = false;
        return TRUE;
    }
    return FALSE;
}

int main() {
    // Setup Ctrl+C handler
    if (!SetConsoleCtrlHandler(console_handler, TRUE)) {
        std::cerr << "Failed to set control handler\n";
        return 1;
    }

    // Initialize COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize COM\n";
        return 1;
    }

    std::cout << "===========================================\n";
    std::cout << "  GameStream - 60 FPS Capture Loop Test\n";
    std::cout << "===========================================\n\n";
    std::cout << "Press Ctrl+C to stop\n\n";

    // Initialize DXGI capture
    DXGICapture capture;
    CaptureConfig config;
    config.find_amd_gpu = true;

    if (!capture.initialize_with_config(config)) {
        std::cerr << "Failed to initialize DXGI capture\n";
        CoUninitialize();
        return 1;
    }

    uint32_t width, height;
    capture.get_resolution(width, height);
    std::cout << "Capture resolution: " << width << "x" << height << "\n";
    std::cout << "Starting capture loop...\n\n";

    // Statistics
    uint64_t frame_count = 0;
    uint64_t frames_captured = 0;
    uint64_t frames_skipped = 0;
    uint64_t access_lost_count = 0;

    auto start_time = steady_clock::now();
    auto last_stats_time = start_time;

    // Main capture loop
    while (running) {
        auto frame_start = steady_clock::now();

        // Capture frame
        auto result = capture.acquire_frame(16);  // 16ms timeout (60 FPS)

        if (!result) {
            const auto& error = result.error();
            if (error == "timeout") {
                frames_skipped++;
            } else if (error == "access_lost") {
                access_lost_count++;
                std::cout << "[WARNING] Access lost, duplication recreated\n";
            } else {
                std::cerr << "[ERROR] Capture failed: " << error << "\n";
                break;
            }
        } else {
            frames_captured++;
            capture.release_frame();
        }

        frame_count++;

        // Print statistics every second
        auto now = steady_clock::now();
        auto elapsed_since_stats = duration<double>(now - last_stats_time).count();

        if (elapsed_since_stats >= 1.0) {
            auto total_elapsed = duration<double>(now - start_time).count();
            double fps = frame_count / total_elapsed;

            auto stats = capture.get_stats();

            std::cout << std::format(
                "[{:.1f}s] FPS: {:.1f} | Captured: {} | Skipped: {} | Latency: {:.3f}/{:.3f}/{:.3f} ms (min/avg/max)\n",
                total_elapsed,
                fps,
                frames_captured,
                frames_skipped,
                stats.min_capture_ms,
                stats.avg_capture_ms,
                stats.max_capture_ms
            );

            last_stats_time = now;
        }

        // Small sleep to avoid busy-wait (target 60 FPS = 16.6ms per frame)
        auto frame_time = duration<double, std::milli>(steady_clock::now() - frame_start).count();
        double target_frame_time = 16.6;  // 60 FPS
        if (frame_time < target_frame_time) {
            std::this_thread::sleep_for(milliseconds(static_cast<int>(target_frame_time - frame_time)));
        }
    }

    // Final statistics
    auto total_time = duration<double>(steady_clock::now() - start_time).count();
    auto stats = capture.get_stats();

    std::cout << "\n===========================================\n";
    std::cout << "  Final Statistics\n";
    std::cout << "===========================================\n";
    std::cout << std::format("Total time:          {:.2f} s\n", total_time);
    std::cout << std::format("Total frames:        {}\n", frame_count);
    std::cout << std::format("Frames captured:     {} ({:.1f}%)\n",
                            frames_captured,
                            100.0 * frames_captured / frame_count);
    std::cout << std::format("Frames skipped:      {} ({:.1f}%)\n",
                            frames_skipped,
                            100.0 * frames_skipped / frame_count);
    std::cout << std::format("Access lost events:  {}\n", access_lost_count);
    std::cout << std::format("Average FPS:         {:.1f}\n", frame_count / total_time);
    std::cout << "\nCapture Latency:\n";
    std::cout << std::format("  Min:  {:.3f} ms\n", stats.min_capture_ms);
    std::cout << std::format("  Avg:  {:.3f} ms\n", stats.avg_capture_ms);
    std::cout << std::format("  Max:  {:.3f} ms\n", stats.max_capture_ms);

    // Performance check
    std::cout << "\nPerformance Assessment:\n";
    if (stats.avg_capture_ms < 2.0) {
        std::cout << "  ✓ EXCELLENT: Capture latency well below 2ms target\n";
    } else {
        std::cout << "  ✗ WARNING: Capture latency above 2ms target\n";
    }

    if (frame_count / total_time >= 59.0) {
        std::cout << "  ✓ EXCELLENT: Sustained 60 FPS\n";
    } else if (frame_count / total_time >= 55.0) {
        std::cout << "  ~ OK: Close to 60 FPS\n";
    } else {
        std::cout << "  ✗ WARNING: Below 60 FPS target\n";
    }

    CoUninitialize();
    return 0;
}
