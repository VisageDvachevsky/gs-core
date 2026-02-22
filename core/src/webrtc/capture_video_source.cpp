// Must be first: winsock2.h must precede windows.h (WebRTC rtc_base requirement)
#include <winsock2.h>

#include "capture_video_source.h"

// libwebrtc video frame
#include <api/video/i420_buffer.h>
#include <api/video/video_frame.h>

// libyuv — bundled with libwebrtc
#include <third_party/libyuv/include/libyuv/convert.h>

#include <spdlog/spdlog.h>

namespace gamestream {

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

CaptureVideoSource::CaptureVideoSource(IFrameCapture* capture)
    : capture_(capture)
{
    ID3D11Device* raw = capture_->get_device();
    if (raw) {
        d3d_device_ = raw;   // ComPtr::operator= → AddRef

        ID3D11DeviceContext* ctx = nullptr;
        raw->GetImmediateContext(&ctx);
        d3d_context_.Attach(ctx);  // Attach = take ownership, no extra AddRef
    } else {
        spdlog::error("[CaptureVideoSource] IFrameCapture::get_device() returned nullptr");
    }
}

CaptureVideoSource::~CaptureVideoSource() {
    stop();
}

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------

void CaptureVideoSource::start() {
    if (running_.exchange(true)) return;  // already running
    thread_ = std::jthread(&CaptureVideoSource::capture_loop, this);
    spdlog::info("[CaptureVideoSource] Capture thread started");
}

void CaptureVideoSource::stop() {
    if (!running_.exchange(false)) return;  // was not running
    // jthread automatically joins on destruction or reassignment, but
    // since we use atomic running flag, we could request stop if we used stop_token.
    // For now we just let the loop exit and the thread will join eventually.
    spdlog::info("[CaptureVideoSource] Capture thread stopped");
}

// ---------------------------------------------------------------------------
// VideoTrackSourceInterface
// ---------------------------------------------------------------------------

webrtc::MediaSourceInterface::SourceState CaptureVideoSource::state() const {
    return running_.load() ? SourceState::kLive : SourceState::kEnded;
}

// ---------------------------------------------------------------------------
// Capture loop (runs on private thread)
// ---------------------------------------------------------------------------

void CaptureVideoSource::capture_loop() {
    SetThreadDescription(GetCurrentThread(), L"DXGI WebRTC Src");
    spdlog::debug("[CaptureVideoSource] Loop started");

    while (running_.load(std::memory_order_relaxed)) {
        auto result = capture_->acquire_frame(16);  // 16 ms ≈ 60 fps poll
        if (!result) {
            // "timeout" is normal (screen unchanged); other errors are real failures
            if (result.error() != "timeout") {
                spdlog::warn("[CaptureVideoSource] acquire_frame: {}", result.error());
            }
            continue;
        }

        const CaptureFrame& frame = result.value();

        if (!d3d_device_ || !d3d_context_) {
            capture_->release_frame();
            continue;
        }

        // --- Ensure staging (CPU-readable) texture matches frame size ----------
        if (!ensure_staging_texture(frame.width, frame.height)) {
            spdlog::error("[CaptureVideoSource] Failed to create staging texture {}x{}",
                          frame.width, frame.height);
            capture_->release_frame();
            continue;
        }

        // --- GPU → CPU copy and WebRTC push --------------------------------
        convert_and_push_frame(frame);

        capture_->release_frame();
    }

    spdlog::debug("[CaptureVideoSource] Loop exited");
}

// ---------------------------------------------------------------------------
// Conversion and staging
// ---------------------------------------------------------------------------

void CaptureVideoSource::convert_and_push_frame(const CaptureFrame& frame) {
    d3d_context_->CopyResource(staging_tex_.Get(), frame.texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = d3d_context_->Map(staging_tex_.Get(), 0,
                                    D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        spdlog::warn("[CaptureVideoSource] Map failed: 0x{:08X}", static_cast<uint32_t>(hr));
        return;
    }

    const int w = static_cast<int>(frame.width);
    const int h = static_cast<int>(frame.height);

    // --- BGRA → I420 (libyuv) -----------------------------------------
    // libyuv "ARGB" = BGRA in memory on little-endian = DXGI_B8G8R8A8
    auto i420 = webrtc::I420Buffer::Create(w, h);
    libyuv::ARGBToI420(
        static_cast<const uint8_t*>(mapped.pData),
        static_cast<int>(mapped.RowPitch),
        i420->MutableDataY(), i420->StrideY(),
        i420->MutableDataU(), i420->StrideU(),
        i420->MutableDataV(), i420->StrideV(),
        w, h);

    d3d_context_->Unmap(staging_tex_.Get(), 0);

    // --- Push frame into WebRTC pipeline ------------------------------
    webrtc::VideoFrame video_frame =
        webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(i420)
            .set_timestamp_us(static_cast<int64_t>(frame.timestamp_us))
            .build();

    OnFrame(video_frame);
}

bool CaptureVideoSource::ensure_staging_texture(uint32_t width, uint32_t height) {
    if (staging_w_ == width && staging_h_ == height && staging_tex_) {
        return true;
    }

    staging_tex_.Reset();
    staging_w_ = 0;
    staging_h_ = 0;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width            = width;
    desc.Height           = height;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;

    HRESULT hr = d3d_device_->CreateTexture2D(&desc, nullptr, &staging_tex_);
    if (FAILED(hr)) {
        spdlog::error("[CaptureVideoSource] CreateTexture2D(STAGING) failed: 0x{:08X}",
                      static_cast<uint32_t>(hr));
        return false;
    }

    staging_w_ = width;
    staging_h_ = height;
    spdlog::debug("[CaptureVideoSource] Staging texture created {}x{}", width, height);
    return true;
}

} // namespace gamestream
