#include "capture_video_source.h"

#include <libyuv.h>
#include <api/video/i420_buffer.h>
#include <api/video/video_frame.h>
#include <rtc_base/time_utils.h>
#include <spdlog/spdlog.h>

namespace gamestream {

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

void CaptureVideoSource::capture_loop() {
    spdlog::info("[CaptureVideoSource] Capture thread started");
    
    while (running_) {
        // Assume 60fps pacing done by capture_ -> it wait for vblank or similar.
        auto frame_res = capture_->acquire_frame(16);
        if (!frame_res) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        const auto& frame = frame_res.value();
        if (!frame.texture) continue;

        if (!d3d_device_) {
            frame.texture->GetDevice(&d3d_device_);
            d3d_device_->GetImmediateContext(&d3d_context_);
        }

        if (!ensure_staging_texture(frame.width, frame.height)) {
            continue;
        }

        // Copy frame to STAGING texture
        d3d_context_->CopyResource(staging_tex_.Get(), frame.texture.Get());

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(d3d_context_->Map(staging_tex_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            
            // Convert to I420
            webrtc::scoped_refptr<webrtc::I420Buffer> i420_buffer = 
                webrtc::I420Buffer::Create(frame.width, frame.height);
                
            libyuv::ARGBToI420(
                static_cast<const uint8_t*>(mapped.pData), mapped.RowPitch,
                i420_buffer->MutableDataY(), i420_buffer->StrideY(),
                i420_buffer->MutableDataU(), i420_buffer->StrideU(),
                i420_buffer->MutableDataV(), i420_buffer->StrideV(),
                frame.width, frame.height
            );

            d3d_context_->Unmap(staging_tex_.Get(), 0);

            webrtc::VideoFrame video_frame = webrtc::VideoFrame::Builder()
                .set_video_frame_buffer(i420_buffer)
                .set_timestamp_rtp(static_cast<uint32_t>(webrtc::TimeMillis() * 90))
                .set_timestamp_us(webrtc::TimeMicros())
                .set_rotation(webrtc::kVideoRotation_0)
                .build();

            // Broadcaster / OnFrame from AdaptedVideoTrackSource
            OnFrame(video_frame);
        }
        capture_->release_frame();
    }
    
    spdlog::info("[CaptureVideoSource] Capture thread stopped");
}

} // namespace gamestream
