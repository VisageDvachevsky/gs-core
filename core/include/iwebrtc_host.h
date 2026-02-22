#pragma once

#include "result.h"
#include "webrtc_types.h"
#include "iwebrtc_observer.h"
#include "iframe_capture.h"
#include "iencoder.h"

#include <cstddef>
#include <cstdint>

namespace gamestream {

/// Interface for managing a WebRTC PeerConnection (host/sender side).
///
/// Lifecycle:
///   1. Construct the concrete implementation (WebRTCHost).
///   2. initialize(config, observer)
///   3. create_offer() → SDP delivered via observer->on_local_sdp_created()
///   4. set_remote_description(answer)
///   5. add_ice_candidate() bidirectionally
///   6. add_video_track() after kConnected
///   7. close()
///
/// Thread-safety:
///   - initialize/create_offer/set_remote_description/add_ice_candidate/
///     add_video_track/close — call from one thread (signaling thread).
///   - send_data/request_keyframe — thread-safe, callable from any thread.
///   - get_connection_state/is_initialized — thread-safe (atomic read).
class IWebRTCHost {
public:
    virtual ~IWebRTCHost() = default;

    IWebRTCHost(const IWebRTCHost&) = delete;
    IWebRTCHost& operator=(const IWebRTCHost&) = delete;
    IWebRTCHost(IWebRTCHost&&) = delete;
    IWebRTCHost& operator=(IWebRTCHost&&) = delete;

    // -----------------------------------------------------------------------
    // Initialization
    // -----------------------------------------------------------------------

    /// Initialize the WebRTC subsystem (PeerConnectionFactory + PeerConnection).
    ///
    /// @param config    ICE servers and DataChannel settings.
    /// @param observer  Receives all async events. Must outlive close().
    [[nodiscard]] virtual VoidResult initialize(const WebRTCConfig& config,
                                                IWebRTCObserver*    observer) = 0;

    // -----------------------------------------------------------------------
    // SDP negotiation
    // -----------------------------------------------------------------------

    /// Create a local SDP offer and begin ICE gathering.
    /// Result delivered via observer->on_local_sdp_created().
    [[nodiscard]] virtual VoidResult create_offer() = 0;

    /// Apply the remote peer's SDP (offer or answer).
    [[nodiscard]] virtual VoidResult set_remote_description(SessionDescription sdp) = 0;

    /// Apply a remote ICE candidate received via the signaling channel.
    [[nodiscard]] virtual VoidResult add_ice_candidate(IceCandidateInfo candidate) = 0;

    // -----------------------------------------------------------------------
    // Video pipeline
    // -----------------------------------------------------------------------

    /// Attach the capture→encode→RTP video pipeline to this PeerConnection.
    ///
    /// Both pointers must be already initialized and outlive this host instance.
    /// May be called only once per instance.
    [[nodiscard]] virtual VoidResult add_video_track(IFrameCapture* capture,
                                                     IEncoder*      encoder) = 0;

    // -----------------------------------------------------------------------
    // DataChannel
    // -----------------------------------------------------------------------

    /// Send binary data to the peer via the input DataChannel.
    /// Thread-safe. No-op if the channel is not open.
    virtual void send_data(const uint8_t* data, size_t size) = 0;

    // -----------------------------------------------------------------------
    // Encoder control
    // -----------------------------------------------------------------------

    /// Request an IDR (key) frame from the attached encoder on the next encode().
    /// Thread-safe. Use on PLI/FIR from the remote peer.
    virtual void request_keyframe() = 0;

    // -----------------------------------------------------------------------
    // Shutdown
    // -----------------------------------------------------------------------

    /// Gracefully close the PeerConnection and release all WebRTC resources.
    virtual void close() = 0;

    // -----------------------------------------------------------------------
    // State queries (thread-safe)
    // -----------------------------------------------------------------------

    virtual PeerConnectionState get_connection_state() const = 0;
    virtual bool is_initialized() const = 0;

protected:
    IWebRTCHost() = default;
};

} // namespace gamestream
