#pragma once

#include "iwebrtc_host.h"
#include "result.h"
#include "webrtc_types.h"

#include <memory>

namespace gamestream {

// Forward declaration — hides all libwebrtc headers from consumers.
// libwebrtc types live exclusively in webrtc_host.cpp.
class WebRTCHostImpl;

/// Concrete WebRTC host implementation backed by libwebrtc.
///
/// PIMPL pattern: all libwebrtc types (PeerConnectionFactory, PeerConnection,
/// rtc::Thread, etc.) are confined to webrtc_host.cpp.
///
/// Threading model:
///   - Public signaling methods must be called from the application thread.
///   - Observer callbacks are delivered on libwebrtc's signaling thread.
///   - send_data() and request_keyframe() are thread-safe.
class WebRTCHost : public IWebRTCHost {
public:
    WebRTCHost();
    ~WebRTCHost() override;

    WebRTCHost(const WebRTCHost&) = delete;
    WebRTCHost& operator=(const WebRTCHost&) = delete;
    WebRTCHost(WebRTCHost&&) = delete;
    WebRTCHost& operator=(WebRTCHost&&) = delete;

    /// Initialize libwebrtc PeerConnectionFactory and create the PeerConnection.
    ///
    /// Creates network/worker/signaling threads, registers AMFVideoEncoderFactory,
    /// and configures ICE servers.
    [[nodiscard]] VoidResult initialize(const WebRTCConfig& config,
                                        IWebRTCObserver*    observer) override;

    /// Create a local SDP offer.
    /// Delivers result via observer->on_local_sdp_created() on signaling thread.
    [[nodiscard]] VoidResult create_offer() override;

    /// Apply the remote SDP (offer or answer).
    [[nodiscard]] VoidResult set_remote_description(SessionDescription sdp) override;

    /// Apply a remote ICE candidate. May be called before set_remote_description().
    [[nodiscard]] VoidResult add_ice_candidate(IceCandidateInfo candidate) override;

    /// Attach the video pipeline (capture → AMF encode → RTP).
    ///
    /// Creates AMFVideoEncoderFactory, DXGIVideoSource, and VideoTrack.
    [[nodiscard]] VoidResult add_video_track(IFrameCapture* capture,
                                             IEncoder*      encoder) override;

    /// Send binary data on the input DataChannel. Thread-safe.
    void send_data(const uint8_t* data, size_t size) override;

    /// Request an IDR frame from the encoder. Thread-safe.
    void request_keyframe() override;

    /// Gracefully close the PeerConnection and join libwebrtc threads.
    void close() override;

    PeerConnectionState get_connection_state() const override;
    bool is_initialized() const override;

private:
    std::unique_ptr<WebRTCHostImpl> impl_;
};

} // namespace gamestream
