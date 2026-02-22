#pragma once

// Internal header — included ONLY from webrtc_host.cpp and amf_webrtc_encoder.cpp.
// Requires libwebrtc headers; do NOT include from core/include/.

#include "iencoder.h"
#include "encoder_types.h"
#include "capture_types.h"
#include "result.h"

// libwebrtc video encoder API
#include <api/video_codecs/video_encoder.h>
#include <api/video_codecs/video_encoder_factory.h>
#include <api/video_codecs/sdp_video_format.h>
#include <api/environment/environment.h>

// libwebrtc video frame
#include <api/video/video_frame.h>
#include <api/video/i420_buffer.h>

// D3D11 for staging texture (I420 → BGRA → GPU upload)
#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <memory>
#include <vector>

namespace gamestream {

/// WebRTC VideoEncoder adapter wrapping our IEncoder (AMFEncoder).
///
/// WebRTC delivers frames as I420 VideoFrame. Pipeline:
///   1. I420 → BGRA via libyuv::I420ToARGB  (CPU; ~0.5 ms at 1080p)
///   2. BGRA data → D3D11 DEFAULT texture via UpdateSubresource
///   3. Texture → IEncoder::encode()  (zero-copy: AMF CreateSurfaceFromDX11Native)
///   4. Encoded NALUs → callback_->OnEncodedImage()
///
/// Future optimisation (Stage 8+): bypass I420 entirely — feed the DXGI
/// capture texture directly to AMF with zero CPU round-trip.
class AMFVideoEncoder final : public webrtc::VideoEncoder {
public:
    /// @param encoder  Shared AMFEncoder instance (already initialized).
    ///                 Must outlive this object.
    /// @param device   D3D11 device used by the AMFEncoder context.
    ///                 Must outlive this object.
    AMFVideoEncoder(IEncoder* encoder, ID3D11Device* device);
    ~AMFVideoEncoder() override;

    AMFVideoEncoder(const AMFVideoEncoder&)            = delete;
    AMFVideoEncoder& operator=(const AMFVideoEncoder&) = delete;
    AMFVideoEncoder(AMFVideoEncoder&&)                 = delete;
    AMFVideoEncoder& operator=(AMFVideoEncoder&&)      = delete;

    // webrtc::VideoEncoder interface ------------------------------------------

    int32_t InitEncode(const webrtc::VideoCodec*             codec_settings,
                       const webrtc::VideoEncoder::Settings& settings) override;

    /// frame_types[0] == kVideoFrameKey → force IDR (PLI/FIR recovery).
    int32_t Encode(const webrtc::VideoFrame&                  frame,
                   const std::vector<webrtc::VideoFrameType>* frame_types) override;

    int32_t RegisterEncodeCompleteCallback(
        webrtc::EncodedImageCallback* callback) override;

    /// Update bitrate/framerate on RTCP REMB / TWCC feedback.
    void SetRates(const webrtc::VideoEncoder::RateControlParameters& parameters) override;

    int32_t Release() override;

    webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override;

private:
    void convert_and_upload(rtc::scoped_refptr<webrtc::I420BufferInterface> i420);

    IEncoder*     encoder_;     // not owned
    ID3D11Device* d3d_device_;  // not owned

    webrtc::EncodedImageCallback* callback_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>     staging_texture_;
    uint32_t staging_w_ = 0;
    uint32_t staging_h_ = 0;
    std::vector<uint8_t> argb_buf_;

    std::atomic<bool> force_keyframe_{false};
};

/// WebRTC VideoEncoderFactory that produces AMFVideoEncoder instances.
///
/// Registered with PeerConnectionFactory.
/// Advertises H.264 Baseline Profile Level 3.1 (profile-level-id=42e01f).
///
/// Deferred injection pattern: the factory is created in initialize() and
/// encoder/device are injected via set_pipeline() in add_video_track()
/// before the first Create() call.
class AMFVideoEncoderFactory final : public webrtc::VideoEncoderFactory {
public:
    AMFVideoEncoderFactory();
    ~AMFVideoEncoderFactory() override;

    /// Called by WebRTCHostImpl::add_video_track() to inject the live pipeline.
    void set_pipeline(IEncoder* encoder, ID3D11Device* device) noexcept {
        encoder_ = encoder;
        device_  = device;
    }

    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

    std::unique_ptr<webrtc::VideoEncoder> Create(
        const webrtc::Environment&    env,
        const webrtc::SdpVideoFormat& format) override;

private:
    IEncoder*     encoder_ = nullptr;
    ID3D11Device* device_  = nullptr;
};

} // namespace gamestream
