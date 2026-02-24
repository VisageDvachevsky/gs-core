#include "capture_video_source.h"

#include <libyuv.h>
#include <api/video/i420_buffer.h>
#include <api/video/color_space.h>
#include <api/video/video_frame.h>
#include <rtc_base/time_utils.h>
#include <spdlog/spdlog.h>

#include <chrono>

namespace gamestream {

namespace {

constexpr int kTargetFps = 60;
constexpr uint32_t kRtpTicksPerFrame = 90000u / kTargetFps;
const auto kFrameInterval = std::chrono::microseconds(1'000'000 / kTargetFps);

void advance_pacer(uint32_t& rtp_timestamp,
                   std::chrono::steady_clock::time_point& next_emit_deadline) {
    rtp_timestamp += kRtpTicksPerFrame;
    next_emit_deadline += kFrameInterval;
    const auto now = std::chrono::steady_clock::now();
    if (next_emit_deadline < now) {
        next_emit_deadline = now;
    }
}

webrtc::ColorSpace make_desktop_color_space() {
    // Desktop content is sRGB (BT.709 primaries/transfer).  AMF H.264 encoder
    // uses BT.709 limited-range matrix for HD (>=720p) input by default.
    // Declaring the same here ensures the browser applies the correct YUV→RGB
    // matrix via the RTP color-space header extension.
    //
    // NOTE: the intermediate I420 buffer (ARGBToI420 / I420ToARGB) uses
    // libyuv BT.601, but that round-trip is cancelled before reaching AMF —
    // it only affects the CPU-side scratch buffer, not the final H.264 stream.
    return webrtc::ColorSpace(
        webrtc::ColorSpace::PrimaryID::kBT709,
        webrtc::ColorSpace::TransferID::kBT709,
        webrtc::ColorSpace::MatrixID::kBT709,
        webrtc::ColorSpace::RangeID::kLimited);
}

}  // namespace

CaptureVideoSource::CaptureVideoSource(IFrameCapture* capture)
    : capture_(capture)
{
    // Need D3D device to create staging texture
    // Do not call get_texture here; defer to capture_loop.
}

CaptureVideoSource::~CaptureVideoSource() {
    stop();
}

void CaptureVideoSource::start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread(&CaptureVideoSource::capture_loop, this);
}

void CaptureVideoSource::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

webrtc::MediaSourceInterface::SourceState CaptureVideoSource::state() const {
    return webrtc::MediaSourceInterface::kLive;
}

bool CaptureVideoSource::ensure_staging_texture(uint32_t width, uint32_t height) {
    if (staging_tex_ && staging_w_ == width && staging_h_ == height) {
        return true;
    }
    staging_tex_.Reset();
    
    // We get the device from the capture source's first texture.
    if (!d3d_device_) return false;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    HRESULT hr = d3d_device_->CreateTexture2D(&desc, nullptr, &staging_tex_);
    if (FAILED(hr)) {
        spdlog::error("[CaptureVideoSource] Failed to create staging texture: 0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    staging_w_ = width;
    staging_h_ = height;
    return true;
}

void CaptureVideoSource::emit_frame(
    const webrtc::scoped_refptr<webrtc::VideoFrameBuffer>& buffer,
    uint32_t                                               rtp_timestamp,
    const webrtc::ColorSpace&                              color_space) {
    webrtc::VideoFrame video_frame = webrtc::VideoFrame::Builder()
        .set_video_frame_buffer(buffer)
        .set_timestamp_rtp(rtp_timestamp)
        .set_timestamp_us(webrtc::TimeMicros())
        .set_color_space(color_space)
        .set_rotation(webrtc::kVideoRotation_0)
        .build();
    OnFrame(video_frame);
}

bool CaptureVideoSource::handle_acquire_failure(
    const Result<CaptureFrame>&                       frame_res,
    const webrtc::scoped_refptr<webrtc::I420Buffer>& last_i420,
    uint32_t                                          rtp_timestamp,
    const webrtc::ColorSpace&                         color_space,
    bool&                                             invalid_result_logged,
    bool&                                             emitted_fallback) {
    emitted_fallback = false;
    if (frame_res) {
        return false;
    }

    std::string err = "Result<CaptureFrame> invalid state";
    if (const std::string* e = frame_res.error_if()) {
        err = *e;
    }
    if (err == "timeout") {
        if (last_i420) {
            emit_frame(last_i420, rtp_timestamp, color_space);
            emitted_fallback = true;
        }
        return true;
    }

    if (!invalid_result_logged) {
        spdlog::error("[CaptureVideoSource] acquire_frame() returned invalid result state: {}", err);
        invalid_result_logged = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return true;
}

bool CaptureVideoSource::convert_frame_to_i420(
    const CaptureFrame&                       frame,
    webrtc::scoped_refptr<webrtc::I420Buffer>& i420_buffer) {
    if (!frame.texture) {
        return false;
    }
    if (!d3d_device_) {
        frame.texture->GetDevice(&d3d_device_);
        d3d_device_->GetImmediateContext(&d3d_context_);
    }
    if (!ensure_staging_texture(frame.width, frame.height)) {
        return false;
    }

    d3d_context_->CopyResource(staging_tex_.Get(), frame.texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(d3d_context_->Map(staging_tex_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return false;
    }

    i420_buffer = webrtc::I420Buffer::Create(frame.width, frame.height);
    libyuv::ARGBToI420(
        static_cast<const uint8_t*>(mapped.pData), mapped.RowPitch,
        i420_buffer->MutableDataY(), i420_buffer->StrideY(),
        i420_buffer->MutableDataU(), i420_buffer->StrideU(),
        i420_buffer->MutableDataV(), i420_buffer->StrideV(),
        frame.width, frame.height);
    d3d_context_->Unmap(staging_tex_.Get(), 0);
    return true;
}

void CaptureVideoSource::process_acquired_frame(
    const Result<CaptureFrame>&               frame_res,
    webrtc::scoped_refptr<webrtc::I420Buffer>& last_i420,
    uint32_t&                                  rtp_timestamp,
    std::chrono::steady_clock::time_point&     next_emit_deadline,
    const webrtc::ColorSpace&                  color_space,
    bool&                                      invalid_result_logged) {
    const CaptureFrame* frame_ptr = frame_res.value_if();
    if (!frame_ptr) {
        if (!invalid_result_logged) {
            spdlog::error("[CaptureVideoSource] acquire_frame() success state has no value");
            invalid_result_logged = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return;
    }

    webrtc::scoped_refptr<webrtc::I420Buffer> i420_buffer;
    if (convert_frame_to_i420(*frame_ptr, i420_buffer)) {
        last_i420 = i420_buffer;
        emit_frame(i420_buffer, rtp_timestamp, color_space);
        advance_pacer(rtp_timestamp, next_emit_deadline);
    }
    capture_->release_frame();
}

void CaptureVideoSource::capture_loop() {
    spdlog::info("[CaptureVideoSource] Capture thread started");

    webrtc::scoped_refptr<webrtc::I420Buffer> last_i420;
    auto next_emit_deadline = std::chrono::steady_clock::now();
    uint32_t rtp_timestamp =
        static_cast<uint32_t>((webrtc::TimeMicros() * 90) / 1000);
    const webrtc::ColorSpace desktop_color_space = make_desktop_color_space();

    bool invalid_result_logged = false;
    while (running_) {
        const auto now = std::chrono::steady_clock::now();
        if (now < next_emit_deadline) {
            std::this_thread::sleep_for(next_emit_deadline - now);
        }

        // 8 ms = ~half a frame at 60 fps.  Keeping the timeout well below the
        // 16.67 ms frame interval prevents acquire_frame from eating into the
        // next slot: if the capture backend is slow, we emit the previous frame
        // as fallback and stay on pace rather than slipping to ~30 fps.
        auto frame_res = capture_->acquire_frame(8);
        bool emitted_fallback = false;
        if (handle_acquire_failure(frame_res, last_i420, rtp_timestamp, desktop_color_space,
                                   invalid_result_logged, emitted_fallback)) {
            if (emitted_fallback) {
                advance_pacer(rtp_timestamp, next_emit_deadline);
            }
            continue;
        }
        process_acquired_frame(frame_res, last_i420, rtp_timestamp, next_emit_deadline,
                               desktop_color_space, invalid_result_logged);
    }

    spdlog::info("[CaptureVideoSource] Capture thread stopped");
}

} // namespace gamestream
