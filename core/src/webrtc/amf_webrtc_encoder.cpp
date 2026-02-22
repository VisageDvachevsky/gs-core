// Must be first: winsock2.h must precede windows.h (WebRTC rtc_base requirement)
#include <winsock2.h>

#include "amf_webrtc_encoder.h"

// WEBRTC_VIDEO_CODEC_* error code constants
#include <modules/video_coding/include/video_error_codes.h>

// codec_specific_info for H264
#include <modules/video_coding/include/video_codec_interface.h>

// H264 profile/level string helpers
#include <api/video_codecs/h264_profile_level_id.h>

// libyuv — bundled with libwebrtc
#include <third_party/libyuv/include/libyuv/convert_argb.h>

#include <spdlog/spdlog.h>

namespace gamestream {

// ---------------------------------------------------------------------------
// AMFVideoEncoder
// ---------------------------------------------------------------------------

AMFVideoEncoder::AMFVideoEncoder(IEncoder* encoder, ID3D11Device* device)
    : encoder_(encoder)
    , d3d_device_(device)
{
}

AMFVideoEncoder::~AMFVideoEncoder() {
    Release();
}

int32_t AMFVideoEncoder::InitEncode(
    const webrtc::VideoCodec*             codec_settings,
    const webrtc::VideoEncoder::Settings& /*settings*/)
{
    if (!encoder_ || !d3d_device_) {
        return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }
    if (!codec_settings) {
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }

    const uint32_t w = codec_settings->width;
    const uint32_t h = codec_settings->height;

    // D3D11 DEFAULT texture — AMF wraps it via CreateSurfaceFromDX11Native.
    // AMFEncoder was initialized with AMF_SURFACE_BGRA, so format = B8G8R8A8.
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width            = w;
    desc.Height           = h;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_DEFAULT;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    staging_texture_.Reset();
    HRESULT hr = d3d_device_->CreateTexture2D(&desc, nullptr, &staging_texture_);
    if (FAILED(hr)) {
        spdlog::error("[AMFVideoEncoder] CreateTexture2D failed: 0x{:08X}",
                      static_cast<uint32_t>(hr));
        return WEBRTC_VIDEO_CODEC_MEMORY;
    }

    ID3D11DeviceContext* ctx = nullptr;
    d3d_device_->GetImmediateContext(&ctx);
    d3d_context_.Attach(ctx);

    argb_buf_.resize(static_cast<size_t>(w) * h * 4);
    staging_w_ = w;
    staging_h_ = h;

    spdlog::info("[AMFVideoEncoder] InitEncode {}x{}", w, h);
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t AMFVideoEncoder::RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback* callback)
{
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t AMFVideoEncoder::Release() {
    staging_texture_.Reset();
    d3d_context_.Reset();
    staging_w_ = staging_h_ = 0;
    argb_buf_.clear();
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t AMFVideoEncoder::Encode(
    const webrtc::VideoFrame&                   frame,
    const std::vector<webrtc::VideoFrameType>*  frame_types)
{
    if (!callback_ || !staging_texture_ || !d3d_context_) {
        return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }

    // --- Keyframe request ---------------------------------------------------
    bool force_kf = force_keyframe_.exchange(false, std::memory_order_relaxed);
    if (frame_types) {
        for (const auto ft : *frame_types) {
            if (ft == webrtc::VideoFrameType::kVideoFrameKey) {
                force_kf = true;
                break;
            }
        }
    }
    if (force_kf) {
        encoder_->request_keyframe();
    }

    // --- Get I420 buffer ----------------------------------------------------
    webrtc::scoped_refptr<webrtc::I420BufferInterface> i420 =
        frame.video_frame_buffer()->GetI420();
    if (!i420) {
        return WEBRTC_VIDEO_CODEC_ERROR;
    }

    const int w = i420->width();
    const int h = i420->height();

    if (w <= 0 || h <= 0) {
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }

    convert_and_upload(i420);

    // --- Call IEncoder (AMF) ------------------------------------------------
    CaptureFrame cap_frame{};
    cap_frame.texture      = staging_texture_;
    cap_frame.width        = static_cast<uint32_t>(w);
    cap_frame.height       = static_cast<uint32_t>(h);
    cap_frame.timestamp_us = static_cast<uint64_t>(frame.timestamp_us());

    auto enc_result = encoder_->encode(cap_frame);
    if (!enc_result) {
        if (enc_result.error() == "timeout") {
            return WEBRTC_VIDEO_CODEC_OK;  // normal — first frame may be late
        }
        spdlog::warn("[AMFVideoEncoder] encode error: {}", enc_result.error());
        return WEBRTC_VIDEO_CODEC_ERROR;
    }

    const EncodedFrame& ef = enc_result.value();

    // --- Build webrtc::EncodedImage -----------------------------------------
    auto encoded_buf = webrtc::EncodedImageBuffer::Create(
        ef.data.data(), ef.data.size());

    webrtc::EncodedImage encoded;
    encoded.SetEncodedData(encoded_buf);
    encoded.SetRtpTimestamp(frame.rtp_timestamp());
    encoded.SetFrameType(ef.is_keyframe
        ? webrtc::VideoFrameType::kVideoFrameKey
        : webrtc::VideoFrameType::kVideoFrameDelta);
    encoded._encodedWidth  = static_cast<uint32_t>(w);
    encoded._encodedHeight = static_cast<uint32_t>(h);

    // --- H.264 codec-specific info ------------------------------------------
    webrtc::CodecSpecificInfo codec_info;
    codec_info.codecType = webrtc::kVideoCodecH264;
    codec_info.codecSpecific.H264.packetization_mode =
        webrtc::H264PacketizationMode::NonInterleaved;
    codec_info.codecSpecific.H264.idr_frame       = ef.is_keyframe;
    codec_info.codecSpecific.H264.base_layer_sync = false;
    codec_info.codecSpecific.H264.temporal_idx    = 0xFF;  // kNoTemporalIdx

    callback_->OnEncodedImage(encoded, &codec_info);
    return WEBRTC_VIDEO_CODEC_OK;
}

void AMFVideoEncoder::SetRates(
    const webrtc::VideoEncoder::RateControlParameters& params)
{
    // Stage 5+: pass params.bitrate.get_sum_bps() to AMFEncoder for dynamic CBR.
    spdlog::debug("[AMFVideoEncoder] SetRates: {:.0f} kbps @ {:.1f} fps",
                  static_cast<double>(params.bitrate.get_sum_kbps()),
                  params.framerate_fps);
}

void AMFVideoEncoder::convert_and_upload(rtc::scoped_refptr<webrtc::I420BufferInterface> i420) {
    const int w = i420->width();
    const int h = i420->height();

    // --- I420 → BGRA  -------------------------------------------------------
    // libyuv "ARGB" on little-endian x64 = byte order B G R A = DXGI B8G8R8A8
    const size_t needed = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    if (argb_buf_.size() < needed) {
        argb_buf_.resize(needed);
    }

    libyuv::I420ToARGB(
        i420->DataY(), i420->StrideY(),
        i420->DataU(), i420->StrideU(),
        i420->DataV(), i420->StrideV(),
        argb_buf_.data(), w * 4,
        w, h);

    // --- Upload BGRA to D3D11 DEFAULT texture --------------------------------
    d3d_context_->UpdateSubresource(
        staging_texture_.Get(), 0, nullptr,
        argb_buf_.data(),
        static_cast<UINT>(w * 4), 0u);
}

webrtc::VideoEncoder::EncoderInfo AMFVideoEncoder::GetEncoderInfo() const {
    webrtc::VideoEncoder::EncoderInfo info;
    info.supports_native_handle  = false;
    info.implementation_name     = "AMF_H264";
    info.is_hardware_accelerated = true;
    return info;
}

// ---------------------------------------------------------------------------
// AMFVideoEncoderFactory
// ---------------------------------------------------------------------------

AMFVideoEncoderFactory::AMFVideoEncoderFactory()  = default;
AMFVideoEncoderFactory::~AMFVideoEncoderFactory() = default;

std::vector<webrtc::SdpVideoFormat>
AMFVideoEncoderFactory::GetSupportedFormats() const
{
    // H.264 Baseline Profile Level 3.1 — profile-level-id = 42e01f
    auto pli_str = webrtc::H264ProfileLevelIdToString(
        webrtc::H264ProfileLevelId{
            webrtc::H264Profile::kProfileBaseline,
            webrtc::H264Level::kLevel3_1});

    const std::string pli = pli_str.value_or("42e01f");

    return {
        webrtc::SdpVideoFormat("H264",
            webrtc::CodecParameterMap{
                {"profile-level-id",        pli},
                {"level-asymmetry-allowed", "1"},
                {"packetization-mode",      "1"},
            })
    };
}

std::unique_ptr<webrtc::VideoEncoder> AMFVideoEncoderFactory::Create(
    const webrtc::Environment&    /*env*/,
    const webrtc::SdpVideoFormat& /*format*/)
{
    if (!encoder_ || !device_) {
        spdlog::error("[AMFVideoEncoderFactory] Create() called before set_pipeline()");
        return nullptr;
    }
    return std::make_unique<AMFVideoEncoder>(encoder_, device_);
}

} // namespace gamestream
