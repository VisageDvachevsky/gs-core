#pragma once

#include "iencoder.h"
#include "result.h"
#include "capture_types.h"
#include "encoder_types.h"

#include <d3d11.h>
#include <memory>

namespace gamestream {

// Forward declaration — hides all AMF headers from consumers of this header.
// AMF types (AMFContextPtr, AMFComponentPtr, amf_handle) live exclusively in amf_encoder.cpp.
class AMFEncoderImpl;

/// H.264 video encoder backed by AMD Advanced Media Framework (AMF).
///
/// Uses the ULTRA_LOW_LATENCY preset with Baseline profile and no B-frames to minimise
/// encode latency (target < 6 ms per frame on RX 6700 XT at 1080p60).
///
/// Zero-copy path: the D3D11 texture from capture (CaptureFrame::texture) is wrapped in
/// an AMF surface via CreateSurfaceFromDX11Native() — no CPU round-trip occurs.
///
/// PIMPL pattern: all AMF headers are confined to amf_encoder.cpp so that including
/// amf_encoder.h does not pull in AMF's macro-heavy headers into unrelated translation units.
class AMFEncoder : public IEncoder {
public:
    AMFEncoder();
    ~AMFEncoder() override;

    // Non-copyable (owns GPU resources)
    AMFEncoder(const AMFEncoder&) = delete;
    AMFEncoder& operator=(const AMFEncoder&) = delete;

    // Non-movable: the PIMPL impl_ stores raw 'this' pointer in AMF callbacks
    AMFEncoder(AMFEncoder&&) = delete;
    AMFEncoder& operator=(AMFEncoder&&) = delete;

    /// Initialize the AMF encoder.
    ///
    /// @param device  Shared D3D11 device from IFrameCapture::get_device().
    ///                Must not be nullptr.  The device must remain alive for the lifetime
    ///                of this encoder.
    /// @param config  Encoding parameters.  width/height must match the capture resolution.
    [[nodiscard]] VoidResult initialize(ID3D11Device* device,
                                        const EncoderConfig& config) override;

    /// Encode a captured frame to H.264 Annex-B via zero-copy AMF path.
    ///
    /// frame.texture must be on the same D3D11 device that was passed to initialize().
    [[nodiscard]] Result<EncodedFrame> encode(const CaptureFrame& frame) override;

    /// Signal that the next encode() call must produce an IDR (key) frame.
    /// Thread-safe: may be called from the DataChannel receiver thread.
    void request_keyframe() override;

    bool is_initialized() const override;
    EncoderStats get_stats() const override;

private:
    std::unique_ptr<AMFEncoderImpl> impl_;
};

} // namespace gamestream
