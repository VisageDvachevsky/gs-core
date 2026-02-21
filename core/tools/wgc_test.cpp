#include "wgc_capture.h"
#include <iostream>
#include <format>
#include <chrono>
#include <csignal>
#include <atomic>
#include <thread>

using namespace gamestream;
using namespace std::chrono;

std::atomic<bool> running{true};

void signal_handler(int signal) {
    running = false;
}

int main(int argc, char** argv) {
    std::cout << "===========================================\n";
    std::cout << "  WGC Capture Test (from core)\n";
    std::cout << "===========================================\n\n";

    // Parse window handle from command line (hex HWND value, e.g. 0x1A04)
    uintptr_t window_handle = 0;  // 0 = foreground window
    if (argc > 1) {
        window_handle = static_cast<uintptr_t>(std::stoull(argv[1], nullptr, 16));
    }

    std::cout << "Starting in 5 seconds... Switch to target window!\n";
    for (int i = 5; i > 0; i--) {
        std::cout << i << "...\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "\n";

    // Initialize capture
    WGCCapture capture;
    auto init_result = capture.initialize(window_handle);
    if (!init_result) {
        std::cerr << "Failed to initialize WGC capture: " << init_result.error() << "\n";
        return 1;
    }

    // Setup signal handler for Ctrl+C
    std::signal(SIGINT, signal_handler);

    std::cout << "Capture started. Press Ctrl+C to stop.\n\n";

    int frames_captured = 0;
    int frames_skipped = 0;
    auto start_time = steady_clock::now();
    auto last_fps_time = steady_clock::now();
    int last_frame_count = 0;

    while (running) {
        auto result = capture.acquire_frame(16);  // 16ms timeout (60 FPS)

        if (!result) {
            auto error = result.error();
            if (error == "timeout") {
                frames_skipped++;
            } else {
                std::cerr << "Acquire frame error: " << error << "\n";
                break;
            }
        } else {
            frames_captured++;
            capture.release_frame();
        }

        // Print FPS every second
        auto now = steady_clock::now();
        auto elapsed = duration<float>(now - last_fps_time).count();
        if (elapsed >= 1.0f) {
            int frames_done = frames_captured - last_frame_count;
            float fps = frames_done / elapsed;

            auto stats = capture.get_stats();
            auto total_time = duration<float>(now - start_time).count();

            std::cout << std::format(
                "[{:.1f}s] FPS: {:.1f} | Captured: {} | Skipped: {} | Latency: {:.3f}/{:.3f}/{:.3f} ms (min/avg/max)\n",
                total_time, fps, frames_captured, frames_skipped,
                stats.min_capture_ms, stats.avg_capture_ms, stats.max_capture_ms);

            last_fps_time = now;
            last_frame_count = frames_captured;
        }
    }

    // Final statistics
    auto total_time = duration<float>(steady_clock::now() - start_time).count();
    auto stats = capture.get_stats();

    std::cout << "\n===========================================\n";
    std::cout << "  Final Statistics\n";
    std::cout << "===========================================\n";
    std::cout << std::format("Total time: {:.1f} seconds\n", total_time);
    std::cout << std::format("Frames captured: {} ({:.1f} FPS average)\n",
                             frames_captured, frames_captured / total_time);
    std::cout << std::format("Frames skipped: {}\n", frames_skipped);
    std::cout << std::format("Capture latency: {:.3f}/{:.3f}/{:.3f} ms (min/avg/max)\n",
                             stats.min_capture_ms, stats.avg_capture_ms, stats.max_capture_ms);

    if (frames_captured > 0) {
        float avg_fps = frames_captured / total_time;
        if (avg_fps >= 50) {
            std::cout << "\n[OK] SUCCESS: Achieved 50+ FPS!\n";
        } else {
            std::cout << "\n[WARNING] FPS below 50. Check if target window is active.\n";
        }
    }

    return 0;
}
