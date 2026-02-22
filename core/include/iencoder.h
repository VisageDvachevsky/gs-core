#pragma once

#include "result.h"
#include "capture_types.h"
#include "encoder_types.h"

#include <d3d11.h>

namespace gamestream {

/// Interface for video encoder implementations.
///
/// Lifecycle:
///   1. Construct the concrete encoder.
///   2. Call initialize() with a shared D3D11 device (from IFrameCapture::get_device())
///      and the desired encoding configuration.
///   3. For each captured frame, call encode() — returns an EncodedFrame with Annex-B data.
///   4. When a key frame is needed (e.g., on DataChannel "PLI" signal), call request_keyframe()
///      before the next encode() call.
///
/// Thread-safety: implementations are NOT thread-safe unless documented otherwise.
/// Single-threaded encode loop assumed (capture thread → encoder → RingBuffer<EncodedFrame>).
class IEncoder {
public:
    virtual ~IEncoder() = default;

    /// Initialize the encoder with a shared D3D11 device.
    ///
    /// @param device  D3D11 device from the capture backend (IFrameCapture::get_device()).
    ///                The encoder does NOT take ownership — device must outlive the encoder.
    ///                Sharing the same device enables zero-copy from capture texture to AMF surface.
    /// @param config  Encoding parameters (resolution, FPS, bitrate).
    ///                Resolution must match the capture resolution.
    [[nodiscard]] virtual VoidResult initialize(ID3D11Device* device,
                                                const EncoderConfig& config) = 0;

    /// Encode a captured frame to H.264 Annex-B.
    ///
    /// Zero-copy path: the texture inside frame is passed directly to the AMF encoder
    /// without any CPU round-trip.  frame must remain valid for the duration of this call.
    ///
    /// Returns error("timeout") when the encoder pipeline has no output yet (rare on first call).
    /// Returns error(<reason>) on actual failure.
    [[nodiscard]] virtual Result<EncodedFrame> encode(const CaptureFrame& frame) = 0;

    /// Request that the next encoded frame be an IDR (key) frame.
    ///
    /// Must be called before encode() — the flag is consumed on the next encode() call.
    /// Thread-safe: may be called from a separate thread (e.g., DataChannel receiver).
    virtual void request_keyframe() = 0;

    /// Check if the encoder has been successfully initialized.
    virtual bool is_initialized() const = 0;

    /// Get cumulative encoding statistics.
    virtual EncoderStats get_stats() const = 0;
};

} // namespace gamestream
