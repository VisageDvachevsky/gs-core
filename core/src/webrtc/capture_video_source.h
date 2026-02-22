#pragma once

// Internal header — included only from webrtc_host.cpp and capture_video_source.cpp.
// Requires libwebrtc headers; do NOT include from core/include/.

#include "iframe_capture.h"

#include <media/base/adapted_video_track_source.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <thread>
#include <vector>

namespace gamestream {

/// Video track source that continuously polls IFrameCapture and pushes
/// frames into the libwebrtc video pipeline.
///
/// Conversion path (Stage 3):
///   DXGI BGRA texture
///     → CopyResource to CPU-readable STAGING texture
///     → Map / libyuv::ARGBToI420
///     → webrtc::I420Buffer → webrtc::VideoFrame
///     → AdaptedVideoTrackSource::OnFrame()
///     → AMFVideoEncoder::Encode()
///
/// Stage 8+ optimisation: replace this with a direct GPU-path source that
/// hands the D3D11 texture straight to AMF without any CPU round-trip.
class CaptureVideoSource final : public webrtc::AdaptedVideoTrackSource {
public:
    /// @param capture  Already-initialized capture backend.
    ///                 Must outlive this object.
    explicit CaptureVideoSource(IFrameCapture* capture);
    ~CaptureVideoSource() override;

    CaptureVideoSource(const CaptureVideoSource&)            = delete;
    CaptureVideoSource& operator=(const CaptureVideoSource&) = delete;
    CaptureVideoSource(CaptureVideoSource&&)                 = delete;
    CaptureVideoSource& operator=(CaptureVideoSource&&)      = delete;

    /// Start the internal capture thread. Called from add_video_track().
    void start();

    /// Stop the internal capture thread. Blocks until the thread exits.
    void stop();

    // webrtc::VideoTrackSourceInterface ----------------------------------------
    SourceState state() const override;
    bool remote()       const override { return false; }
    bool is_screencast() const override { return true; }
    std::optional<bool> needs_denoising() const override { return false; }

private:
    void capture_loop();
    void convert_and_push_frame(const CaptureFrame& frame);
    bool ensure_staging_texture(uint32_t width, uint32_t height);

    IFrameCapture* capture_;  // not owned

    Microsoft::WRL::ComPtr<ID3D11Device>        d3d_device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>  d3d_context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>     staging_tex_;
    uint32_t staging_w_ = 0;
    uint32_t staging_h_ = 0;

    std::atomic<bool> running_{false};
    std::jthread      thread_;
};

} // namespace gamestream
