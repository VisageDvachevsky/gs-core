// Must be first: winsock2.h must precede windows.h (WebRTC rtc_base requirement)
#include <winsock2.h>
#include <windows.h>

#include "webrtc_host.h"
#include "iwebrtc_observer.h"
#include "webrtc_types.h"
#include "amf_webrtc_encoder.h"
#include "capture_video_source.h"

// Suppress warnings produced by WebRTC headers under /W4 /WX.
//   C4100 — unreferenced formal parameter (widespread in WebRTC observer callbacks)
//   C4245 — signed/unsigned mismatch in WebRTC template internals
//   C4267 — size_t narrowing inside WebRTC
#pragma warning(push)
#pragma warning(disable: 4100 4245 4267)
#include <api/create_peerconnection_factory.h>
#include <api/peer_connection_interface.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/data_channel_interface.h>
#include <api/jsep.h>
#include <api/rtc_error.h>
#include <api/media_stream_interface.h>
#include <api/rtp_receiver_interface.h>
#include <api/rtp_transceiver_interface.h>
#include <api/scoped_refptr.h>
#include <api/make_ref_counted.h>
#include <rtc_base/thread.h>
#include <rtc_base/ssl_adapter.h>
#include <rtc_base/copy_on_write_buffer.h>
#pragma warning(pop)

#include <d3d11.h>
#include <d3d11_4.h>  // ID3D11Multithread
#include <wrl/client.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <string>

namespace gamestream {

namespace {

// ---------------------------------------------------------------------------
// State mapping helpers
// ---------------------------------------------------------------------------

PeerConnectionState map_pc_state(webrtc::PeerConnectionInterface::PeerConnectionState s) {
    switch (s) {
        case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected:
            return PeerConnectionState::kConnected;
        case webrtc::PeerConnectionInterface::PeerConnectionState::kFailed:
            return PeerConnectionState::kFailed;
        case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed:
            return PeerConnectionState::kClosed;
        default:
            return PeerConnectionState::kConnecting;
    }
}

PeerConnectionState map_pc_state(webrtc::PeerConnectionInterface::IceConnectionState s) {
    switch (s) {
        case webrtc::PeerConnectionInterface::kIceConnectionConnected:
        case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
            return PeerConnectionState::kConnected;
        case webrtc::PeerConnectionInterface::kIceConnectionFailed:
            return PeerConnectionState::kFailed;
        case webrtc::PeerConnectionInterface::kIceConnectionClosed:
            return PeerConnectionState::kClosed;
        default:
            return PeerConnectionState::kConnecting;
    }
}

IceConnectionState map_ice_state(webrtc::PeerConnectionInterface::IceConnectionState s) {
    switch (s) {
        case webrtc::PeerConnectionInterface::kIceConnectionNew:          return IceConnectionState::kNew;
        case webrtc::PeerConnectionInterface::kIceConnectionChecking:     return IceConnectionState::kChecking;
        case webrtc::PeerConnectionInterface::kIceConnectionConnected:    return IceConnectionState::kConnected;
        case webrtc::PeerConnectionInterface::kIceConnectionCompleted:    return IceConnectionState::kCompleted;
        case webrtc::PeerConnectionInterface::kIceConnectionFailed:       return IceConnectionState::kFailed;
        case webrtc::PeerConnectionInterface::kIceConnectionDisconnected: return IceConnectionState::kDisconnected;
        case webrtc::PeerConnectionInterface::kIceConnectionClosed:       return IceConnectionState::kClosed;
        default:                                                           return IceConnectionState::kNew;
    }
}

// ---------------------------------------------------------------------------
// SetDescriptionObserver
// ---------------------------------------------------------------------------

class SetDescriptionObserver : public webrtc::SetSessionDescriptionObserver {
public:
    static webrtc::scoped_refptr<SetDescriptionObserver> Create() {
        return webrtc::make_ref_counted<SetDescriptionObserver>();
    }
    void OnSuccess() override {}
    void OnFailure(webrtc::RTCError error) override {
        spdlog::error("[WebRTCHost] SetSessionDescription failed: {}", error.message());
    }
};

// ---------------------------------------------------------------------------
// CreateOfferObserver
// Sets local description and waits for ICE gathering to complete.
// The app observer is notified in OnIceGatheringChange(kComplete).
// ---------------------------------------------------------------------------

class CreateOfferObserver : public webrtc::CreateSessionDescriptionObserver {
public:
    static webrtc::scoped_refptr<CreateOfferObserver> Create(
        webrtc::PeerConnectionInterface* pc) {
        return webrtc::make_ref_counted<CreateOfferObserver>(pc);
    }
    explicit CreateOfferObserver(webrtc::PeerConnectionInterface* pc) : pc_(pc) {}

    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override {
        // SetLocalDescription transfers ownership of desc.
        // on_local_sdp_created() fires later in OnIceGatheringChange(kComplete)
        // with the fully-populated SDP (all ICE candidates embedded).
        pc_->SetLocalDescription(SetDescriptionObserver::Create().get(), desc);
    }
    void OnFailure(webrtc::RTCError error) override {
        spdlog::error("[WebRTCHost] CreateOffer failed: {}", error.message());
    }
private:
    webrtc::PeerConnectionInterface* pc_;  // not owned
};

} // namespace

// ---------------------------------------------------------------------------
// WebRTCHostImpl
// ---------------------------------------------------------------------------

class WebRTCHostImpl : public webrtc::PeerConnectionObserver,
                       public webrtc::DataChannelObserver {
public:
    WebRTCHostImpl();
    ~WebRTCHostImpl() override;

    [[nodiscard]] VoidResult initialize(const WebRTCConfig& config, IWebRTCObserver* observer);
    [[nodiscard]] VoidResult create_offer();
    [[nodiscard]] VoidResult set_remote_description(SessionDescription sdp);
    [[nodiscard]] VoidResult add_ice_candidate(IceCandidateInfo candidate);
    [[nodiscard]] VoidResult add_video_track(IFrameCapture* capture, IEncoder* encoder);
    void send_data(const uint8_t* data, size_t size);
    void request_keyframe();
    void close();

    PeerConnectionState get_connection_state() const { return state_.load(); }
    bool is_initialized() const { return initialized_.load(); }

    // webrtc::PeerConnectionObserver
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface> dc) override;
    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState s) override;
    void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState s) override;
    void OnIceCandidate(const webrtc::IceCandidate* candidate) override;
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState state) override;
    void OnAddTrack(webrtc::scoped_refptr<webrtc::RtpReceiverInterface>,
                    const std::vector<webrtc::scoped_refptr<webrtc::MediaStreamInterface>>&) override {}
    void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>) override {}
    void OnRemoveTrack(webrtc::scoped_refptr<webrtc::RtpReceiverInterface>) override {}
    void OnRenegotiationNeeded() override {}

    // webrtc::DataChannelObserver
    void OnStateChange() override;
    void OnMessage(const webrtc::DataBuffer& buffer) override;

private:
    std::unique_ptr<webrtc::Thread> network_thread_;
    std::unique_ptr<webrtc::Thread> worker_thread_;
    std::unique_ptr<webrtc::Thread> signaling_thread_;

    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface>        pc_;
    webrtc::scoped_refptr<webrtc::DataChannelInterface>           data_channel_;
    webrtc::scoped_refptr<CaptureVideoSource>                     video_source_;

    // Non-owning pointer to the factory (owned by factory_ via CreatePeerConnectionFactory).
    // Retained so set_encoder() can be called in add_video_track() before WebRTC's
    // first Create() during SDP negotiation.
    AMFVideoEncoderFactory* amf_encoder_factory_ = nullptr;

    IWebRTCObserver*                 observer_ = nullptr;
    std::atomic<bool>                initialized_{false};
    std::atomic<PeerConnectionState> state_{PeerConnectionState::kNew};
    std::mutex                       encoder_mutex_;
    IEncoder*                        encoder_ = nullptr;
};

// ---------------------------------------------------------------------------
// WebRTCHostImpl — implementations
// ---------------------------------------------------------------------------

WebRTCHostImpl::WebRTCHostImpl()  = default;
WebRTCHostImpl::~WebRTCHostImpl() { close(); }

VoidResult WebRTCHostImpl::initialize(const WebRTCConfig& config,
                                       IWebRTCObserver*    observer) {
    if (initialized_) return VoidResult::error("Already initialized");
    observer_ = observer;

    webrtc::InitializeSSL();

    network_thread_ = webrtc::Thread::CreateWithSocketServer();
    network_thread_->SetName("network_thread", nullptr);
    network_thread_->Start();

    worker_thread_ = webrtc::Thread::Create();
    worker_thread_->SetName("worker_thread", nullptr);
    worker_thread_->Start();

    signaling_thread_ = webrtc::Thread::Create();
    signaling_thread_->SetName("signaling_thread", nullptr);
    signaling_thread_->Start();

    // Create encoder factory with deferred injection.
    // Save raw pointer BEFORE std::move — factory_ takes ownership, but we
    // keep amf_encoder_factory_ as a non-owning observation point.
    auto factory_owned = std::make_unique<AMFVideoEncoderFactory>();
    amf_encoder_factory_ = factory_owned.get();

    factory_ = webrtc::CreatePeerConnectionFactory(
        network_thread_.get(),
        worker_thread_.get(),
        signaling_thread_.get(),
        /*default_adm=*/nullptr,
        webrtc::CreateBuiltinAudioEncoderFactory(),
        webrtc::CreateBuiltinAudioDecoderFactory(),
        std::move(factory_owned),
        /*video_decoder_factory=*/nullptr,   // sender-only host; no inbound video to decode
        /*audio_mixer=*/nullptr,
        /*audio_processing=*/nullptr);

    if (!factory_) return VoidResult::error("Failed to create PeerConnectionFactory");

    webrtc::PeerConnectionInterface::RTCConfiguration rtc_config;
    rtc_config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;

    if (!config.ice_servers.empty()) {
        for (const auto& s : config.ice_servers) {
            webrtc::PeerConnectionInterface::IceServer ice;
            ice.uri      = s.uri;
            ice.username = s.username;
            ice.password = s.password;
            rtc_config.servers.push_back(ice);
        }
    } else {
        webrtc::PeerConnectionInterface::IceServer stun;
        stun.uri = "stun:stun.l.google.com:19302";
        rtc_config.servers.push_back(stun);
    }

    webrtc::PeerConnectionDependencies deps(this);
    auto pc_res = factory_->CreatePeerConnectionOrError(rtc_config, std::move(deps));
    if (!pc_res.ok()) {
        return VoidResult::error("Failed to create PeerConnection");
    }
    pc_ = pc_res.MoveValue();

    // Create input DataChannel now (we are the offerer).
    // Stage 4 will switch to ordered=false, maxRetransmits=0 for input events.
    webrtc::DataChannelInit dc_init;
    dc_init.ordered = true;

    auto dc_res = pc_->CreateDataChannelOrError("gamestream_input", &dc_init);
    if (dc_res.ok()) {
        data_channel_ = dc_res.MoveValue();
        data_channel_->RegisterObserver(this);
    } else {
        spdlog::error("[WebRTCHost] CreateDataChannel failed");
    }

    initialized_ = true;
    spdlog::info("[WebRTCHost] Initialized successfully");
    return VoidResult();
}

VoidResult WebRTCHostImpl::create_offer() {
    if (!pc_) return VoidResult::error("Not initialized");
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
    options.offer_to_receive_video =
        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions::kOfferToReceiveMediaTrue;
    pc_->CreateOffer(CreateOfferObserver::Create(pc_.get()).get(), options);
    return VoidResult();
}

VoidResult WebRTCHostImpl::set_remote_description(SessionDescription sdp) {
    if (!pc_) return VoidResult::error("Not initialized");

    webrtc::SdpType type = (sdp.type == SessionDescriptionType::kOffer)
                           ? webrtc::SdpType::kOffer
                           : webrtc::SdpType::kAnswer;
    webrtc::SdpParseError parse_error;
    std::unique_ptr<webrtc::SessionDescriptionInterface> desc =
        webrtc::CreateSessionDescription(type, sdp.sdp, &parse_error);

    if (!desc) {
        return VoidResult::error(
            std::string("Failed to parse SDP: ") + parse_error.description);
    }

    pc_->SetRemoteDescription(SetDescriptionObserver::Create().get(), desc.release());
    return VoidResult();
}

VoidResult WebRTCHostImpl::add_ice_candidate(IceCandidateInfo candidate) {
    if (!pc_) return VoidResult::error("Not initialized");

    webrtc::SdpParseError parse_error;
    std::unique_ptr<webrtc::IceCandidateInterface> ice_candidate(
        webrtc::CreateIceCandidate(
            candidate.sdp_mid,
            candidate.sdp_mline_index,
            candidate.sdp,
            &parse_error));

    if (!ice_candidate) {
        return VoidResult::error(
            std::string("Failed to parse ICE candidate: ") + parse_error.description);
    }

    pc_->AddIceCandidate(ice_candidate.get());
    return VoidResult();
}

VoidResult WebRTCHostImpl::add_video_track(IFrameCapture* capture, IEncoder* encoder) {
    if (!pc_ || !factory_) return VoidResult::error("Not initialized");

    // Enable D3D11 multithread protection.
    // CaptureVideoSource (capture thread) and AMFVideoEncoder (WebRTC encode thread)
    // both use the device's immediate context.  SetMultithreadProtected serializes all
    // D3D11 API calls via a per-device critical section — standard D3D11 mechanism.
    ID3D11Device* device = capture->get_device();
    if (device) {
        Microsoft::WRL::ComPtr<ID3D11Multithread> mt;
        if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D11Multithread),
                                             reinterpret_cast<void**>(mt.GetAddressOf())))) {
            mt->SetMultithreadProtected(TRUE);
            spdlog::debug("[WebRTCHost] D3D11 multithread protection enabled");
        }
    }

    // Inject encoder + device into the factory before video track creation,
    // ensuring Create() has valid pointers when WebRTC calls it during negotiation.
    if (amf_encoder_factory_) {
        amf_encoder_factory_->set_encoder(encoder, device);
    } else {
        spdlog::warn("[WebRTCHost] amf_encoder_factory_ is null — encoder not injected");
    }

    {
        std::lock_guard<std::mutex> lock(encoder_mutex_);
        encoder_ = encoder;
    }

    video_source_ = webrtc::make_ref_counted<CaptureVideoSource>(capture);

    webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track(
        factory_->CreateVideoTrack(video_source_, "gamestream_video"));

    auto res = pc_->AddTrack(video_track, {"gamestream_stream"});
    if (!res.ok()) {
        return VoidResult::error("Failed to add video track to PeerConnection");
    }

    video_source_->start();
    spdlog::info("[WebRTCHost] Video track added, capture started");
    return VoidResult();
}

void WebRTCHostImpl::send_data(const uint8_t* data, size_t size) {
    if (!data_channel_ ||
        data_channel_->state() != webrtc::DataChannelInterface::kOpen) {
        return;
    }
    webrtc::CopyOnWriteBuffer buf(data, size);
    data_channel_->Send(webrtc::DataBuffer(buf, /*binary=*/true));
}

void WebRTCHostImpl::request_keyframe() {
    std::lock_guard<std::mutex> lock(encoder_mutex_);
    if (encoder_) {
        encoder_->request_keyframe();
    }
}

void WebRTCHostImpl::close() {
    initialized_ = false;

    if (video_source_) {
        video_source_->stop();
        video_source_ = nullptr;
    }
    if (data_channel_) {
        data_channel_->UnregisterObserver();
        data_channel_->Close();
        data_channel_ = nullptr;
    }
    if (pc_) {
        pc_->Close();
        pc_ = nullptr;
    }
    amf_encoder_factory_ = nullptr;
    factory_              = nullptr;

    if (network_thread_)   network_thread_->Stop();
    if (worker_thread_)    worker_thread_->Stop();
    if (signaling_thread_) signaling_thread_->Stop();

    network_thread_.reset();
    worker_thread_.reset();
    signaling_thread_.reset();

    webrtc::CleanupSSL();

    state_ = PeerConnectionState::kClosed;
    spdlog::debug("[WebRTCHost] Closed");
}

// ---------------------------------------------------------------------------
// PeerConnectionObserver callbacks
// ---------------------------------------------------------------------------

void WebRTCHostImpl::OnDataChannel(
    webrtc::scoped_refptr<webrtc::DataChannelInterface> dc) {
    if (!data_channel_) {
        data_channel_ = dc;
        data_channel_->RegisterObserver(this);
    }
}

void WebRTCHostImpl::OnIceConnectionChange(
    webrtc::PeerConnectionInterface::IceConnectionState s) {
    state_ = map_pc_state(s);
    if (observer_) observer_->on_ice_connection_state_changed(map_ice_state(s));
}

void WebRTCHostImpl::OnConnectionChange(
    webrtc::PeerConnectionInterface::PeerConnectionState s) {
    state_ = map_pc_state(s);
    if (observer_) observer_->on_connection_state_changed(state_.load());
}

void WebRTCHostImpl::OnIceCandidate(const webrtc::IceCandidate* candidate) {
    if (!observer_) return;
    std::string sdp;
    candidate->ToString(&sdp);
    IceCandidateInfo info;
    info.sdp             = sdp;
    info.sdp_mid         = candidate->sdp_mid();
    info.sdp_mline_index = candidate->sdp_mline_index();
    observer_->on_ice_candidate(info);
}

void WebRTCHostImpl::OnIceGatheringChange(
    webrtc::PeerConnectionInterface::IceGatheringState state) {
    if (state != webrtc::PeerConnectionInterface::kIceGatheringComplete) return;
    if (!observer_ || !pc_) return;

    // All ICE candidates are now embedded in the local description SDP.
    // Notify observer with the complete SDP — no ICE trickling needed for LAN/loopback.
    // Trickle ICE is Stage 5.
    const webrtc::SessionDescriptionInterface* local_desc = pc_->local_description();
    if (!local_desc) {
        spdlog::warn("[WebRTCHost] ICE gathering complete but local description is null");
        return;
    }

    std::string sdp;
    local_desc->ToString(&sdp);

    SessionDescription s;
    s.type = (local_desc->GetType() == webrtc::SdpType::kOffer)
             ? SessionDescriptionType::kOffer
             : SessionDescriptionType::kAnswer;
    s.sdp = sdp;

    spdlog::info("[WebRTCHost] ICE gathering complete — SDP ready ({} bytes)", sdp.size());
    observer_->on_local_sdp_created(s);
    observer_->on_ice_gathering_complete();
}

// ---------------------------------------------------------------------------
// DataChannelObserver callbacks
// ---------------------------------------------------------------------------

void WebRTCHostImpl::OnStateChange() {
    if (!data_channel_) return;
    if (data_channel_->state() == webrtc::DataChannelInterface::kOpen) {
        if (observer_) observer_->on_data_channel_open();
    } else if (data_channel_->state() == webrtc::DataChannelInterface::kClosed) {
        if (observer_) observer_->on_data_channel_closed();
    }
}

void WebRTCHostImpl::OnMessage(const webrtc::DataBuffer& buffer) {
    if (observer_) {
        observer_->on_data_channel_message(buffer.data.data(), buffer.data.size());
    }
}

// ---------------------------------------------------------------------------
// WebRTCHost — PIMPL forwarding
// ---------------------------------------------------------------------------

WebRTCHost::WebRTCHost()  : impl_(std::make_unique<WebRTCHostImpl>()) {}
WebRTCHost::~WebRTCHost() = default;

VoidResult WebRTCHost::initialize(const WebRTCConfig& config, IWebRTCObserver* observer) {
    return impl_->initialize(config, observer);
}
VoidResult WebRTCHost::create_offer() { return impl_->create_offer(); }
VoidResult WebRTCHost::set_remote_description(SessionDescription sdp) {
    return impl_->set_remote_description(sdp);
}
VoidResult WebRTCHost::add_ice_candidate(IceCandidateInfo candidate) {
    return impl_->add_ice_candidate(candidate);
}
VoidResult WebRTCHost::add_video_track(IFrameCapture* capture, IEncoder* encoder) {
    return impl_->add_video_track(capture, encoder);
}
void WebRTCHost::send_data(const uint8_t* data, size_t size) {
    impl_->send_data(data, size);
}
void WebRTCHost::request_keyframe() { impl_->request_keyframe(); }
void WebRTCHost::close()            { impl_->close(); }
PeerConnectionState WebRTCHost::get_connection_state() const {
    return impl_->get_connection_state();
}
bool WebRTCHost::is_initialized() const { return impl_->is_initialized(); }

} // namespace gamestream
