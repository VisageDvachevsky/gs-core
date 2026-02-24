#include "amf_webrtc_encoder.h"

// Suppress warnings produced by WebRTC headers under /W4 /WX.
#pragma warning(push)
#pragma warning(disable: 4100 4245 4267)
#include <rtc_base/time_utils.h>
#include <modules/video_coding/include/video_error_codes.h>
#include <modules/video_coding/include/video_codec_interface.h>
#include <modules/video_coding/codecs/interface/common_constants.h>
#pragma warning(pop)

#include <spdlog/spdlog.h>

#include <chrono>
#include <format>

using namespace std::chrono;

namespace gamestream {

namespace {

webrtc::ColorSpace make_color_space_for_size(uint32_t width, uint32_t height) {
    (void)width;
    (void)height;
    return webrtc::ColorSpace(webrtc::ColorSpace::PrimaryID::kSMPTE170M,
                              webrtc::ColorSpace::TransferID::kSMPTE170M,
                              webrtc::ColorSpace::MatrixID::kSMPTE170M,
                              webrtc::ColorSpace::RangeID::kLimited);
}

} // namespace

// ---------------------------------------------------------------------------
// AMFVideoEncoder
// ---------------------------------------------------------------------------

AMFVideoEncoder::AMFVideoEncoder(IEncoder* encoder, ID3D11Device* device)
    : encoder_(encoder)
    , d3d_device_(device)
{
    if (d3d_device_) {
        d3d_device_->GetImmediateContext(&d3d_context_);
    }
}

AMFVideoEncoder::~AMFVideoEncoder() {
    Release();
}

int32_t AMFVideoEncoder::InitEncode(const webrtc::VideoCodec*                 codec_settings,
                                    const webrtc::VideoEncoder::Settings& /*settings*/) {
    if (!encoder_) {
        spdlog::error("[AMFVideoEncoder] InitEncode: encoder not injected — "
                      "call AMFVideoEncoderFactory::set_encoder() before WebRTC "
                      "creates the encoder");
        return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }
    if (!d3d_context_) {
        spdlog::error("[AMFVideoEncoder] InitEncode: D3D11 device not injected");
        return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }

    const uint32_t w = static_cast<uint32_t>(codec_settings->width);
    const uint32_t h = static_cast<uint32_t>(codec_settings->height);

    if (!ensure_gpu_texture(w, h)) {
        return WEBRTC_VIDEO_CODEC_ERROR;
    }

    // BGRA = 4 bytes per pixel
    bgra_buf_.resize(static_cast<size_t>(w) * h * 4u);
    waiting_for_first_keyframe_ = true;
    encoded_frames_ = 0;
    dropped_stale_frames_ = 0;
    transient_no_output_frames_ = 0;
    transient_input_full_frames_ = 0;

    spdlog::info("[AMFVideoEncoder] InitEncode: {}x{}", w, h);
    return WEBRTC_VIDEO_CODEC_OK;
}

bool AMFVideoEncoder::ensure_gpu_texture(uint32_t width, uint32_t height) {
    if (gpu_tex_ && tex_width_ == width && tex_height_ == height) {
        return true;
    }

    gpu_tex_.Reset();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = width;
    desc.Height           = height;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_DEFAULT;
    // AMF CreateSurfaceFromDX11Native requires SHADER_RESOURCE; RENDER_TARGET
    // is required by the AMD driver for the texture to be used as an encoder
    // input surface.
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.CPUAccessFlags   = 0;

    HRESULT hr = d3d_device_->CreateTexture2D(&desc, nullptr, &gpu_tex_);
    if (FAILED(hr)) {
        spdlog::error("[AMFVideoEncoder] CreateTexture2D({}x{}, BGRA) failed: 0x{:08X}",
                      width, height, static_cast<uint32_t>(hr));
        return false;
    }

    tex_width_  = width;
    tex_height_ = height;
    spdlog::debug("[AMFVideoEncoder] GPU texture (re)created: {}x{}", width, height);
    return true;
}

bool AMFVideoEncoder::should_drop_stale_frame(const webrtc::VideoFrame& frame) {
    if (waiting_for_first_keyframe_) {
        return false;
    }

    const int64_t now_us = static_cast<int64_t>(webrtc::TimeMicros());
    const int64_t frame_age_us = now_us - static_cast<int64_t>(frame.timestamp_us());
    if (frame_age_us <= kMaxFrameAgeUs) {
        return false;
    }

    ++dropped_stale_frames_;
    log_transient_skip("stale frame", dropped_stale_frames_, frame_age_us);
    return true;
}

bool AMFVideoEncoder::should_force_keyframe(
    const std::vector<webrtc::VideoFrameType>* frame_types) {
    bool force_kf = waiting_for_first_keyframe_ || force_keyframe_.exchange(false);
    if (!frame_types) {
        return force_kf;
    }
    for (const auto type : *frame_types) {
        if (type == webrtc::VideoFrameType::kVideoFrameKey) {
            return true;
        }
    }
    return force_kf;
}

void AMFVideoEncoder::log_transient_skip(std::string_view reason,
                                         uint64_t         skip_count,
                                         int64_t          frame_age_us) {
    if ((skip_count % kTransientWarnEveryN) != 0) {
        return;
    }
    if (frame_age_us >= 0) {
        spdlog::warn("[AMFVideoEncoder] Skipped {} {} entries (latest age={} ms)",
                     skip_count, reason, frame_age_us / 1000);
        return;
    }
    spdlog::warn("[AMFVideoEncoder] Skipped {} {} entries", skip_count, reason);
}

int32_t AMFVideoEncoder::handle_encode_failure(const Result<EncodedFrame>& res) {
    std::string err = "Result<EncodedFrame> invalid state";
    if (const std::string* e = res.error_if()) {
        err = *e;
    }

    if (err.find("no output after max poll attempts") != std::string::npos) {
        ++transient_no_output_frames_;
        log_transient_skip("encoder-no-output", transient_no_output_frames_, -1);
        return WEBRTC_VIDEO_CODEC_OK;
    }
    if (err.find("encoder input full") != std::string::npos) {
        ++transient_input_full_frames_;
        log_transient_skip("encoder-input-full", transient_input_full_frames_, -1);
        return WEBRTC_VIDEO_CODEC_OK;
    }

    spdlog::error("[AMFVideoEncoder] encode_frame failed: {}", err);
    return WEBRTC_VIDEO_CODEC_ERROR;
}

webrtc::CodecSpecificInfo AMFVideoEncoder::make_h264_codec_specific_info(bool is_keyframe) {
    webrtc::CodecSpecificInfo codec_specific{};
    codec_specific.codecType = webrtc::kVideoCodecH264;
    codec_specific.codecSpecific.H264.packetization_mode =
        webrtc::H264PacketizationMode::NonInterleaved;
    codec_specific.codecSpecific.H264.temporal_idx = webrtc::kNoTemporalIdx;
    codec_specific.codecSpecific.H264.idr_frame = is_keyframe;
    codec_specific.codecSpecific.H264.base_layer_sync = false;
    return codec_specific;
}

int32_t AMFVideoEncoder::Encode(const webrtc::VideoFrame&                  frame,
                                const std::vector<webrtc::VideoFrameType>* frame_types) {
    if (!encoder_ || !callback_ || !gpu_tex_) {
        return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }
    if (should_drop_stale_frame(frame)) {
        return WEBRTC_VIDEO_CODEC_OK;
    }

    auto res = encode_frame(frame, should_force_keyframe(frame_types));
    const EncodedFrame* encoded = res.value_if();
    if (!encoded) {
        return handle_encode_failure(res);
    }

    ++encoded_frames_;
    if (waiting_for_first_keyframe_ && encoded->is_keyframe) {
        waiting_for_first_keyframe_ = false;
    }

    webrtc::EncodedImage encoded_image;
    encoded_image.SetEncodedData(
        webrtc::EncodedImageBuffer::Create(encoded->data.data(), encoded->data.size()));
    encoded_image.SetRtpTimestamp(frame.rtp_timestamp());
    encoded_image._encodedWidth    = frame.width();
    encoded_image._encodedHeight   = frame.height();
    encoded_image.capture_time_ms_ = static_cast<int64_t>(frame.timestamp_us() / 1000);
    encoded_image._frameType       = encoded->is_keyframe
                                     ? webrtc::VideoFrameType::kVideoFrameKey
                                     : webrtc::VideoFrameType::kVideoFrameDelta;

    const webrtc::ColorSpace color_space =
        make_color_space_for_size(static_cast<uint32_t>(frame.width()),
                                  static_cast<uint32_t>(frame.height()));
    encoded_image.SetColorSpace(color_space);

    const webrtc::CodecSpecificInfo codec_specific =
        make_h264_codec_specific_info(encoded->is_keyframe);

    webrtc::EncodedImageCallback::Result cb_res =
        callback_->OnEncodedImage(encoded_image, &codec_specific);

    if (cb_res.error != webrtc::EncodedImageCallback::Result::OK) {
        spdlog::trace("[AMFVideoEncoder] OnEncodedImage: frame dropped by WebRTC");
    }

    return WEBRTC_VIDEO_CODEC_OK;
}

Result<EncodedFrame> AMFVideoEncoder::encode_frame(const webrtc::VideoFrame& frame,
                                                   bool                      force_keyframe) {
    // --- Step 1: get I420 buffer ---
    webrtc::scoped_refptr<webrtc::I420BufferInterface> i420 =
        frame.video_frame_buffer()->ToI420();

    const uint32_t w = static_cast<uint32_t>(i420->width());
    const uint32_t h = static_cast<uint32_t>(i420->height());

    // Resize texture and scratch buffer if resolution changed mid-session
    if (!ensure_gpu_texture(w, h)) {
        return Result<EncodedFrame>::error(
            std::format("failed to (re)create GPU texture for {}x{}", w, h));
    }
    const size_t bgra_bytes = static_cast<size_t>(w) * h * 4u;
    if (bgra_buf_.size() != bgra_bytes) {
        bgra_buf_.resize(bgra_bytes);
    }

    // --- Step 2: I420 → BGRA ---
    // libyuv::I420ToARGB writes B,G,R,A in memory order = DXGI_FORMAT_B8G8R8A8_UNORM.
    const int stride_bgra = static_cast<int>(w) * 4;
    libyuv::I420ToARGB(
        i420->DataY(), i420->StrideY(),
        i420->DataU(), i420->StrideU(),
        i420->DataV(), i420->StrideV(),
        bgra_buf_.data(), stride_bgra,
        static_cast<int>(w), static_cast<int>(h));

    // --- Step 3: upload BGRA to GPU texture ---
    // UpdateSubresource is the correct path for USAGE_DEFAULT textures:
    // driver copies from CPU-side buffer to GPU memory via the DMA engine.
    d3d_context_->UpdateSubresource(
        gpu_tex_.Get(),
        /*DstSubresource=*/0,
        /*pDstBox=*/nullptr,
        bgra_buf_.data(),
        static_cast<UINT>(stride_bgra),
        /*DepthRowPitch=*/0);

    // --- Step 4: request keyframe on AMF side if needed ---
    if (force_keyframe) {
        encoder_->request_keyframe();
    }

    // --- Step 5: AMF encode (zero-copy inside AMFEncoder::encode) ---
    const uint64_t ts_us = static_cast<uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());

    CaptureFrame capture_frame;
    capture_frame.texture      = gpu_tex_;
    capture_frame.width        = w;
    capture_frame.height       = h;
    capture_frame.timestamp_us = ts_us;

    return encoder_->encode(capture_frame);
}

int32_t AMFVideoEncoder::RegisterEncodeCompleteCallback(
    webrtc::EncodedImageCallback* callback) {
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
}

void AMFVideoEncoder::SetRates(
    const webrtc::VideoEncoder::RateControlParameters& /*parameters*/) {
    // Adaptive bitrate via RTCP REMB/TWCC feedback: Stage 8 (YAGNI).
    // Implementation: call IEncoder::set_bitrate(parameters.bitrate.get_sum_bps()).
}

int32_t AMFVideoEncoder::Release() {
    gpu_tex_.Reset();
    bgra_buf_.clear();
    bgra_buf_.shrink_to_fit();
    callback_   = nullptr;
    tex_width_  = 0;
    tex_height_ = 0;
    waiting_for_first_keyframe_ = true;
    encoded_frames_ = 0;
    dropped_stale_frames_ = 0;
    transient_no_output_frames_ = 0;
    transient_input_full_frames_ = 0;
    return WEBRTC_VIDEO_CODEC_OK;
}

webrtc::VideoEncoder::EncoderInfo AMFVideoEncoder::GetEncoderInfo() const {
    webrtc::VideoEncoder::EncoderInfo info;
    // Stage 8: set supports_native_handle = true for zero-copy D3D11 native path
    info.supports_native_handle  = false;
    info.implementation_name     = "AMF-H264-Stage3";
    info.is_hardware_accelerated = true;
    return info;
}

// ---------------------------------------------------------------------------
// AMFVideoEncoderFactory
// ---------------------------------------------------------------------------

AMFVideoEncoderFactory::AMFVideoEncoderFactory()  = default;
AMFVideoEncoderFactory::~AMFVideoEncoderFactory() = default;

void AMFVideoEncoderFactory::set_encoder(IEncoder* encoder, ID3D11Device* device) {
    encoder_ = encoder;
    device_  = device;
    spdlog::debug("[AMFVideoEncoderFactory] encoder and D3D11 device injected");
}

std::vector<webrtc::SdpVideoFormat> AMFVideoEncoderFactory::GetSupportedFormats() const {
    // H.264 Baseline Level 3.1 — matches AMF_VIDEO_ENCODER_PROFILE_BASELINE.
    // packetization-mode=1: NAL units fragmented into RTP (required for H.264 in WebRTC).
    return {webrtc::SdpVideoFormat("H264", {
        {"profile-level-id",        "42e01f"},
        {"packetization-mode",      "1"},
        {"level-asymmetry-allowed", "1"}
    })};
}

std::unique_ptr<webrtc::VideoEncoder> AMFVideoEncoderFactory::Create(
    const webrtc::Environment&    /*env*/,
    const webrtc::SdpVideoFormat& /*format*/) {
    if (!encoder_ || !device_) {
        spdlog::error("[AMFVideoEncoderFactory] Create() called before set_encoder() — "
                      "encoder and device must be injected in add_video_track()");
        return nullptr;
    }
    return std::make_unique<AMFVideoEncoder>(encoder_, device_);
}

} // namespace gamestream
