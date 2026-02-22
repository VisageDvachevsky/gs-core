#pragma once

// Internal header — included ONLY from webrtc_host.cpp.
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

// libyuv — color space conversion (bundled with libwebrtc)
#include <third_party/libyuv/include/libyuv.h>

#include <atomic>
#include <memory>
#include <vector>

namespace gamestream {

/// WebRTC VideoEncoder adapter wrapping our IEncoder (AMFEncoder).
///
/// WebRTC delivers frames as I420 VideoFrame. Pipeline:
///   1. I420 → NV12 via libyuv (CPU step; AMF requires NV12).
///   2. NV12 data → D3D11 staging texture upload.
///   3. Texture → IEncoder::encode() (zero-copy: AMF creates surface from D3D11Native).
///   4. Encoded NALUs → callback_->OnEncodedImage().
///
/// Future optimization (Stage 8+): bypass I420 entirely by feeding the DXGI
/// capture texture directly to AMF without any CPU involvement.
class AMFVideoEncoder : public webrtc::VideoEncoder {
public:
    /// @param encoder  Shared AMFEncoder instance (already initialized).
    ///                 Must outlive this object.
    explicit AMFVideoEncoder(IEncoder* encoder);
    ~AMFVideoEncoder() override;

    AMFVideoEncoder(const AMFVideoEncoder&) = delete;
    AMFVideoEncoder& operator=(const AMFVideoEncoder&) = delete;
    AMFVideoEncoder(AMFVideoEncoder&&) = delete;
    AMFVideoEncoder& operator=(AMFVideoEncoder&&) = delete;

    // webrtc::VideoEncoder interface
    int32_t InitEncode(const webrtc::VideoCodec*                  codec_settings,
                       const webrtc::VideoEncoder::Settings&       settings) override;

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
    Result<EncodedFrame> encode_frame(const webrtc::VideoFrame& frame,
                                      bool                      force_keyframe);

    IEncoder*                     encoder_;
    webrtc::EncodedImageCallback* callback_ = nullptr;
    std::atomic<bool>             force_keyframe_{false};
};

/// WebRTC VideoEncoderFactory that produces AMFVideoEncoder instances.
///
/// Registered with PeerConnectionFactory via CreatePeerConnectionFactory().
/// Advertises H.264 Baseline (profile-level-id=42e01f) — matches
/// AMF_VIDEO_ENCODER_PROFILE_BASELINE for minimal decode latency.
class AMFVideoEncoderFactory : public webrtc::VideoEncoderFactory {
public:
    /// @param encoder  Shared IEncoder passed to each AMFVideoEncoder.
    ///                 Must outlive this factory and all encoders it creates.
    explicit AMFVideoEncoderFactory(IEncoder* encoder);
    ~AMFVideoEncoderFactory() override;

    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

    std::unique_ptr<webrtc::VideoEncoder> Create(
        const webrtc::Environment&    env,
        const webrtc::SdpVideoFormat& format) override;

private:
    IEncoder* encoder_;
};

} // namespace gamestream
