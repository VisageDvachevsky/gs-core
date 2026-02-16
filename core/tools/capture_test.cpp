#include "dxgi_capture.h"
#include <wrl/client.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>

using namespace gamestream;
using namespace std::chrono;
using Microsoft::WRL::ComPtr;

int main() {
    // Initialize COM (required for WIC)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize COM: 0x" << std::hex << hr << "\n";
        return 1;
    }

    std::cout << "===========================================\n";
    std::cout << "  GameStream - DXGI Capture Test\n";
    std::cout << "===========================================\n\n";

    // Initialize DXGI capture
    DXGICapture capture;
    CaptureConfig config;
    config.find_amd_gpu = true;  // Automatically find AMD RX 6700 XT

    if (!capture.initialize_with_config(config)) {
        std::cerr << "\nFailed to initialize DXGI capture!\n";
        std::cerr << "\nPossible issues:\n";
        std::cerr << "  - Game is in Fullscreen Exclusive mode (use Borderless Windowed)\n";
        std::cerr << "  - Too many applications using desktop duplication\n";
        std::cerr << "  - GPU drivers not installed correctly\n";
        CoUninitialize();
        return 1;
    }

    uint32_t width, height;
    capture.get_resolution(width, height);
    std::cout << "\nCapture resolution: " << width << "x" << height << "\n";

    // Capture 10 frames
    const int num_frames = 10;
    std::cout << "\nCapturing " << num_frames << " frames...\n\n";

    int frames_captured = 0;
    int frames_skipped = 0;

    auto start_time = steady_clock::now();

    for (int i = 0; i < num_frames; ) {
        auto frame_start = steady_clock::now();

        // Capture frame using Result<T>
        auto result = capture.acquire_frame(1000);  // 1 second timeout

        if (!result) {
            frames_skipped++;
            std::cout << "  [Frame " << (i + 1) << "] Skipped (" << result.error() << ")\n";
            std::this_thread::sleep_for(milliseconds(16));  // Wait ~1 frame at 60Hz
            continue;
        }

        auto capture_time = steady_clock::now();
        double capture_ms = duration<double, std::milli>(capture_time - frame_start).count();

        // Save to file
        std::wstringstream filename;
        filename << L"frame_" << std::setw(3) << std::setfill(L'0') << (i + 1) << L".bmp";

        CaptureFrame& frame = result.value();

        // Use ComPtr for RAII (no manual Release needed)
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        frame.texture->GetDevice(&device);
        device->GetImmediateContext(&context);

        bool saved = DXGICapture::save_texture_to_bmp(device.Get(), context.Get(), frame.texture.Get(), filename.str());

        capture.release_frame();

        auto save_time = steady_clock::now();
        double save_ms = duration<double, std::milli>(save_time - capture_time).count();
        double total_ms = duration<double, std::milli>(save_time - frame_start).count();

        if (saved) {
            std::wcout << L"  [Frame " << (i + 1) << L"] Saved to " << filename.str()
                      << L" | Capture: " << std::fixed << std::setprecision(2) << capture_ms << L"ms"
                      << L" | Save: " << save_ms << L"ms"
                      << L" | Total: " << total_ms << L"ms\n";
            frames_captured++;
            i++;
        } else {
            std::cerr << "  [Frame " << (i + 1) << "] Failed to save\n";
        }

        // Small delay to avoid spamming captures
        std::this_thread::sleep_for(milliseconds(100));
    }

    auto end_time = steady_clock::now();
    double total_seconds = duration<double>(end_time - start_time).count();

    // Get statistics
    auto stats = capture.get_stats();

    std::cout << "\n===========================================\n";
    std::cout << "  Capture Summary\n";
    std::cout << "===========================================\n";
    std::cout << "Frames captured: " << frames_captured << "\n";
    std::cout << "Frames skipped:  " << frames_skipped << "\n";
    std::cout << "Total time:      " << std::fixed << std::setprecision(2) << total_seconds << "s\n";
    std::cout << "Average FPS:     " << (frames_captured / total_seconds) << "\n";
    std::cout << "\nCapture Statistics:\n";
    std::cout << "  Avg latency:  " << std::setprecision(3) << stats.avg_capture_ms << " ms\n";
    std::cout << "  Min latency:  " << stats.min_capture_ms << " ms\n";
    std::cout << "  Max latency:  " << stats.max_capture_ms << " ms\n";
    std::cout << "\nCheck the current directory for frame_001.bmp ... frame_010.bmp\n";

    CoUninitialize();
    return 0;
}
