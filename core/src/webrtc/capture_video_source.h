#pragma once

#include "iframe_capture.h"

#include <media/base/adapted_video_track_source.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <thread>
#include <vector>

namespace gamestream {

class CaptureVideoSource : public webrtc::AdaptedVideoTrackSource {
public:
    explicit CaptureVideoSource(IFrameCapture* capture);
    ~CaptureVideoSource() override;

    CaptureVideoSource(const CaptureVideoSource&)            = delete;
    CaptureVideoSource& operator=(const CaptureVideoSource&) = delete;
    CaptureVideoSource(CaptureVideoSource&&)                 = delete;
    CaptureVideoSource& operator=(CaptureVideoSource&&)      = delete;

    void start();
    void stop();

    SourceState state() const override;
    bool remote()       const override { return false; }
    bool is_screencast() const override { return true; }
    std::optional<bool> needs_denoising() const override { return false; }

private:
    void capture_loop();
    bool ensure_staging_texture(uint32_t width, uint32_t height);

    IFrameCapture* capture_;  // not owned

    Microsoft::WRL::ComPtr<ID3D11Device>        d3d_device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>     staging_tex_;
    uint32_t staging_w_ = 0;
    uint32_t staging_h_ = 0;

    std::atomic<bool> running_{false};
    std::thread       thread_;
};

} // namespace gamestream
