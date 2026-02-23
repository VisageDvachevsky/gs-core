#pragma once

// Internal header — included ONLY from webrtc_host.cpp and amf_webrtc_encoder.cpp.
// Requires libwebrtc and D3D11 headers; do NOT include from core/include/.

#include "iencoder.h"
#include "encoder_types.h"
#include "capture_types.h"
#include "result.h"

// Suppress warnings produced by WebRTC headers under /W4 /WX.
// These originate in third-party code that we cannot modify:
//   C4100 — unreferenced formal parameter (common in WebRTC callbacks/overrides)
//   C4245 — signed/unsigned mismatch in conversions inside WebRTC templates
//   C4267 — size_t → smaller type conversion in WebRTC internals
#pragma warning(push)
#pragma warning(disable: 4100 4245 4267)
#include <api/video_codecs/video_encoder.h>
#include <api/video_codecs/video_encoder_factory.h>
#include <api/video_codecs/sdp_video_format.h>
#include <api/environment/environment.h>
#include <api/video/video_frame.h>
#include <api/video/i420_buffer.h>
#include <third_party/libyuv/include/libyuv.h>
#pragma warning(pop)

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <memory>
#include <vector>

namespace gamestream {

/// WebRTC VideoEncoder adapter wrapping our IEncoder (AMFEncoder).
///
/// WebRTC delivers I420 VideoFrames. Pipeline (Stage 3):
///   1. I420 → BGRA via libyuv::I420ToARGB (CPU step;
///      BGRA = DXGI_FORMAT_B8G8R8A8_UNORM — matches AMF_SURFACE_BGRA).
///   2. BGRA CPU buffer → D3D11 BGRA GPU texture via UpdateSubresource.
///   3. GPU texture wrapped in CaptureFrame → IEncoder::encode() (AMF zero-copy
///      via CreateSurfaceFromDX11Native).
///   4. Encoded Annex-B NALUs → callback_->OnEncodedImage().
///
/// Stage 8 optimization: bypass I420/BGRA conversion entirely by feeding the
/// DXGI capture texture directly to AMF (GPU-only pipeline).
///
/// Thread safety: Encode() is called from WebRTC's encode thread while
/// CaptureVideoSource runs its own capture thread — both share the D3D11
/// immediate context.  Caller MUST enable ID3D11Multithread protection on the
/// device before calling Encode() (done in WebRTCHostImpl::add_video_track).
class AMFVideoEncoder : public webrtc::VideoEncoder {
public:
    /// @param encoder  Initialized IEncoder (AMFEncoder). Not owned; must outlive this object.
    /// @param device   D3D11 device shared with IEncoder. Not owned; must outlive this object.
    AMFVideoEncoder(IEncoder* encoder, ID3D11Device* device);
    ~AMFVideoEncoder() override;

    AMFVideoEncoder(const AMFVideoEncoder&)            = delete;
    AMFVideoEncoder& operator=(const AMFVideoEncoder&) = delete;
    AMFVideoEncoder(AMFVideoEncoder&&)                 = delete;
    AMFVideoEncoder& operator=(AMFVideoEncoder&&)      = delete;

    // webrtc::VideoEncoder interface

    /// Creates the D3D11 BGRA texture sized to codec_settings dimensions.
    int32_t InitEncode(const webrtc::VideoCodec*                  codec_settings,
                       const webrtc::VideoEncoder::Settings&       settings) override;

    /// Encodes one frame.  frame_types[0] == kVideoFrameKey → forces IDR.
    int32_t Encode(const webrtc::VideoFrame&                  frame,
                   const std::vector<webrtc::VideoFrameType>* frame_types) override;

    int32_t RegisterEncodeCompleteCallback(
        webrtc::EncodedImageCallback* callback) override;

    /// Passes bitrate/framerate updates from RTCP feedback to the encoder.
    /// Full adaptive bitrate is Stage 8; currently a no-op placeholder.
    void SetRates(const webrtc::VideoEncoder::RateControlParameters& parameters) override;

    int32_t Release() override;

    webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override;

private:
    /// Convert I420 → BGRA, upload to GPU texture, call IEncoder::encode().
    [[nodiscard]] Result<EncodedFrame> encode_frame(const webrtc::VideoFrame& frame,
                                                    bool                      force_keyframe);

    /// Create or resize the BGRA GPU texture.  Returns false and logs on failure.
    bool ensure_gpu_texture(uint32_t width, uint32_t height);

    IEncoder*                     encoder_;     // not owned
    ID3D11Device*                 d3d_device_;  // not owned
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>     gpu_tex_;
    uint32_t                      tex_width_  = 0;
    uint32_t                      tex_height_ = 0;
    std::vector<uint8_t>          bgra_buf_;   // CPU scratch: I420 → BGRA

    webrtc::EncodedImageCallback* callback_       = nullptr;
    std::atomic<bool>             force_keyframe_{false};
};

/// WebRTC VideoEncoderFactory that produces AMFVideoEncoder instances.
///
/// Designed with deferred injection:
///   1. Constructed in WebRTCHostImpl::initialize() (before CreatePeerConnectionFactory).
///   2. A non-owning raw pointer is retained by WebRTCHostImpl.
///   3. In WebRTCHostImpl::add_video_track(), set_encoder() is called to inject
///      the real IEncoder and ID3D11Device before WebRTC's first Create() call.
///
/// Advertises H.264 Baseline (profile-level-id=42e01f) — matches
/// AMF_VIDEO_ENCODER_PROFILE_BASELINE for minimum decode latency.
class AMFVideoEncoderFactory : public webrtc::VideoEncoderFactory {
public:
    AMFVideoEncoderFactory();
    ~AMFVideoEncoderFactory() override;

    /// Inject the real encoder and D3D11 device.
    /// Must be called before WebRTC invokes Create().
    void set_encoder(IEncoder* encoder, ID3D11Device* device);

    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

    std::unique_ptr<webrtc::VideoEncoder> Create(
        const webrtc::Environment&    env,
        const webrtc::SdpVideoFormat& format) override;

private:
    IEncoder*     encoder_ = nullptr;
    ID3D11Device* device_  = nullptr;
};

} // namespace gamestream
