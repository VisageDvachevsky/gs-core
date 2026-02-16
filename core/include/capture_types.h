#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

using Microsoft::WRL::ComPtr;

namespace gamestream {

/// Captured frame data
struct CaptureFrame {
    ComPtr<ID3D11Texture2D> texture;
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

} // namespace gamestream
