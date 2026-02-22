#pragma once

#include "result.h"
#include "capture_types.h"
#include <cstdint>
#include <d3d11.h>

namespace gamestream {

/// Interface for frame capture implementations.
/// Allows mocking in unit tests (principle 2.2).
class IFrameCapture {
public:
    virtual ~IFrameCapture() = default;

    /// Initialize capture.
    /// For DXGI: param = adapter index (0 = first/AMD).
    /// For WGC:  param = HWND cast to uintptr_t (0 = foreground window).
    /// Returns error string on failure.
    [[nodiscard]] virtual VoidResult initialize(uintptr_t param = 0) = 0;

    /// Acquire next frame from desktop.
    /// Returns error("timeout") when screen hasn't changed — not a failure.
    /// Returns error(<reason>) on actual failure.
    [[nodiscard]] virtual Result<CaptureFrame> acquire_frame(uint64_t timeout_ms = 16) = 0;

    /// Release previously acquired frame.
    /// Must be called after each successful acquire_frame().
    virtual void release_frame() = 0;

    /// Get current capture resolution.
    virtual void get_resolution(uint32_t& width, uint32_t& height) const = 0;

    /// Check if capture is initialized.
    virtual bool is_initialized() const = 0;

    /// Get capture statistics.
    virtual CaptureStats get_stats() const = 0;

    /// Get the D3D11 device used internally by this capture backend.
    /// Required for zero-copy encoder initialization: pass this device to
    /// IEncoder::initialize() so the encoder can share the same GPU context.
    /// Returns nullptr if not initialized.
    virtual ID3D11Device* get_device() const = 0;
};

} // namespace gamestream
