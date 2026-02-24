#pragma once

#include "iframe_capture.h"

#include <media/base/adapted_video_track_source.h>
#include <api/video/color_space.h>
#include <api/video/i420_buffer.h>
#include <api/video/video_frame_buffer.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
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
    // Treat as real-time video to minimize buffering on the receiver.
    bool is_screencast() const override { return false; }
    std::optional<bool> needs_denoising() const override { return false; }

private:
    void capture_loop();
    void emit_frame(const webrtc::scoped_refptr<webrtc::VideoFrameBuffer>& buffer,
                    uint32_t rtp_timestamp,
                    const webrtc::ColorSpace& color_space);
    bool handle_acquire_failure(const Result<CaptureFrame>& frame_res,
                                const webrtc::scoped_refptr<webrtc::I420Buffer>& last_i420,
                                uint32_t rtp_timestamp,
                                const webrtc::ColorSpace& color_space,
                                bool& invalid_result_logged,
                                bool& emitted_fallback);
    void process_acquired_frame(const Result<CaptureFrame>& frame_res,
                                webrtc::scoped_refptr<webrtc::I420Buffer>& last_i420,
                                uint32_t& rtp_timestamp,
                                std::chrono::steady_clock::time_point& next_emit_deadline,
                                const webrtc::ColorSpace& color_space,
                                bool& invalid_result_logged);
    bool convert_frame_to_i420(const CaptureFrame& frame,
                               webrtc::scoped_refptr<webrtc::I420Buffer>& i420_buffer);
    bool ensure_staging_texture(uint32_t width, uint32_t height);

    IFrameCapture* capture_;  // not owned

    Microsoft::WRL::ComPtr<ID3D11Device>        d3d_device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>     staging_tex_;
    uint32_t staging_w_ = 0;
    uint32_t staging_h_ = 0;

    std::atomic<bool> running_{false};
    std::thread       thread_;
    bool              timer_resolution_enabled_ = false;
};

} // namespace gamestream
