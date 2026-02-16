#pragma once

#include "result.h"
#include "capture_types.h"
#include "iframe_capture.h"
#include <d3d11.h>
#include <cstdint>
#include <string>
#include <memory>

namespace gamestream {

// Forward declaration for implementation
class WGCCaptureImpl;

/// Windows.Graphics.Capture API implementation
/// Captures specific window frames at full FPS (bypasses DWM throttling)
/// Requires Windows 10 1803+ and C++/WinRT support
class WGCCapture : public IFrameCapture {
public:
    WGCCapture();
    ~WGCCapture() override;

    // Non-copyable
    WGCCapture(const WGCCapture&) = delete;
    WGCCapture& operator=(const WGCCapture&) = delete;

    /// Initialize WGC capture for a specific window
    /// @param window_handle HWND of the target window (0 = foreground window)
    /// @return Success or error message
    VoidResult initialize(uint32_t window_handle = 0) override;

    /// Acquire the next frame from the capture session
    /// @param timeout_ms Maximum wait time in milliseconds
    /// @return CaptureFrame or error message
    Result<CaptureFrame> acquire_frame(uint64_t timeout_ms = 16) override;

    /// Release the previously acquired frame
    void release_frame() override;

    /// Get current capture resolution
    void get_resolution(uint32_t& width, uint32_t& height) const override;

    /// Check if capture is initialized
    bool is_initialized() const override;

    /// Get capture statistics
    CaptureStats get_stats() const override;

private:
    // PIMPL to hide WinRT types from public header
    std::unique_ptr<WGCCaptureImpl> impl_;
};

} // namespace gamestream
