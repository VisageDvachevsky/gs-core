#pragma once

#include "result.h"
#include "capture_types.h"
#include "iframe_capture.h"
#include <d3d11.h>
#include <cstdint>
#include <string>
#include <memory>

namespace gamestream {

// Forward declaration for PIMPL (hides all WinRT types from public header)
class WGCCaptureImpl;

/// Windows.Graphics.Capture API implementation.
/// Captures a specific window at full FPS (bypasses DWM throttling).
/// Requires Windows 10 1803+ and C++/WinRT support.
class WGCCapture : public IFrameCapture {
public:
    WGCCapture();
    ~WGCCapture() override;

    // Non-copyable (holds COM and WinRT resources)
    WGCCapture(const WGCCapture&) = delete;
    WGCCapture& operator=(const WGCCapture&) = delete;

    /// Initialize WGC capture for a specific window.
    /// @param window_handle  HWND of the target window cast to uintptr_t.
    ///                       Pass 0 to capture the current foreground window.
    [[nodiscard]] VoidResult initialize(uintptr_t window_handle = 0) override;

    /// Acquire the next captured frame.
    /// Returns error("timeout") when no new frame arrived — not a failure.
    [[nodiscard]] Result<CaptureFrame> acquire_frame(uint64_t timeout_ms = 16) override;

    /// Release the previously acquired frame, returning its buffer to the pool.
    void release_frame() override;

    /// Get current capture resolution.
    void get_resolution(uint32_t& width, uint32_t& height) const override;

    /// Check if capture is initialized.
    bool is_initialized() const override;

    /// Get capture statistics.
    CaptureStats get_stats() const override;

    /// Get the D3D11 device created internally by WGC.
    /// Returns nullptr if not initialized.
    ID3D11Device* get_device() const override;

private:
    std::unique_ptr<WGCCaptureImpl> impl_;
};

} // namespace gamestream
