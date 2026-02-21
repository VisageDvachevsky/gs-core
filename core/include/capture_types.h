#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

namespace gamestream {

/// Captured frame data
struct CaptureFrame {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    uint32_t width;
    uint32_t height;
    uint64_t timestamp_us;  // Microseconds since capture start
};

/// Capture statistics for monitoring
struct CaptureStats {
    uint64_t frames_captured = 0;
    uint64_t frames_skipped = 0;
    double avg_capture_ms = 0.0;
    double min_capture_ms = 999999.0;
    double max_capture_ms = 0.0;
};

/// Configuration for DXGI capture backend initialization
struct CaptureConfig {
    uint32_t adapter_index = 0;  // 0 = first adapter, will search for AMD
    uint32_t output_index = 0;   // 0 = primary monitor
    bool find_amd_gpu = true;    // Automatically find AMD GPU
};

} // namespace gamestream
