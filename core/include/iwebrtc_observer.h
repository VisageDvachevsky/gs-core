#pragma once

#include "webrtc_types.h"

#include <cstddef>
#include <cstdint>

namespace gamestream {

/// Observer interface for asynchronous WebRTC events.
///
/// All methods may be called from libwebrtc's internal signaling or worker
/// threads — implementations must be thread-safe.
class IWebRTCObserver {
public:
    virtual ~IWebRTCObserver() = default;

    // -----------------------------------------------------------------------
    // Signaling callbacks — forward to remote peer immediately
    // -----------------------------------------------------------------------

    /// Called when a local SDP (offer or answer) is ready to send via signaling.
    virtual void on_local_sdp_created(SessionDescription sdp) = 0;

    /// Called for each new local ICE candidate to forward to the remote peer.
    virtual void on_ice_candidate(IceCandidateInfo candidate) = 0;

    // -----------------------------------------------------------------------
    // State callbacks
    // -----------------------------------------------------------------------

    /// Called when the overall PeerConnection state changes.
    virtual void on_connection_state_changed(PeerConnectionState state) = 0;

    /// Called when the ICE connection state changes.
    virtual void on_ice_connection_state_changed(IceConnectionState state) = 0;

    // -----------------------------------------------------------------------
    // DataChannel callbacks (input events from the client)
    // -----------------------------------------------------------------------

    /// Called when the input DataChannel is opened and ready.
    virtual void on_data_channel_open() = 0;

    /// Called when binary data arrives on the DataChannel.
    ///
    /// Packet layout (binary input protocol):
    ///   [0]     uint8_t  — type (0x01 mouse_move, 0x02 click, 0x04 key, 0x05 pad)
    ///   [1..8]  uint64_t — timestamp (little-endian, microseconds)
    ///   [9..]   payload  — type-specific fields
    ///
    /// @param data  Valid only for the duration of this call — copy if deferred.
    /// @param size  Number of bytes.
    virtual void on_data_channel_message(const uint8_t* data, size_t size) = 0;

    /// Called when the DataChannel is closed.
    virtual void on_data_channel_closed() = 0;

    // -----------------------------------------------------------------------
    // ICE gathering
    // -----------------------------------------------------------------------

    /// Called when local ICE gathering is complete.
    ///
    /// After this callback, on_local_sdp_created() fires with a SDP that has
    /// all host/srflx candidates embedded — no trickle ICE needed for LAN
    /// testing.  For internet sessions the signaling relay should already
    /// have forwarded individual candidates via on_ice_candidate().
    ///
    /// Default implementation is intentionally empty: callers that do not
    /// need gathering-complete notification simply skip overriding this.
    virtual void on_ice_gathering_complete() {}
};

} // namespace gamestream
