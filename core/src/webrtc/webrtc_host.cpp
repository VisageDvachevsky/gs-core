// Must be first: winsock2.h must precede windows.h (WebRTC rtc_base requirement)
#include <winsock2.h>

#include "webrtc_host.h"
#include "iwebrtc_observer.h"
#include "webrtc_types.h"

// Internal pipeline
#include "amf_webrtc_encoder.h"
#include "capture_video_source.h"

// libwebrtc: factory & threads
#include <api/create_peerconnection_factory.h>
#include <api/peer_connection_interface.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/video_codecs/builtin_video_decoder_factory.h>
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

#include <spdlog/spdlog.h>

#include <atomic>
#include <format>
#include <memory>
#include <string>

namespace gamestream {

// ===========================================================================
// One-shot observers — defined BEFORE WebRTCHostImpl so their full types are
// visible when webrtc::make_ref_counted<T> is instantiated inside methods.
// ===========================================================================

// ---------------------------------------------------------------------------
// SetLocalDescObserver — logs SetLocalDescription errors
// ---------------------------------------------------------------------------
class SetLocalDescObserver final
    : public webrtc::SetSessionDescriptionObserver
{
public:
    void OnSuccess() override {
        spdlog::debug("[WebRTCHost] SetLocalDescription OK");
    }
    void OnFailure(webrtc::RTCError error) override {
        spdlog::error("[WebRTCHost] SetLocalDescription failed: {}", error.message());
    }
};

// ---------------------------------------------------------------------------
// SetRemoteDescObserver — logs SetRemoteDescription errors
// ---------------------------------------------------------------------------
class SetRemoteDescObserver final
    : public webrtc::SetSessionDescriptionObserver
{
public:
    void OnSuccess() override {
        spdlog::debug("[WebRTCHost] SetRemoteDescription OK");
    }
    void OnFailure(webrtc::RTCError error) override {
        spdlog::error("[WebRTCHost] SetRemoteDescription failed: {}", error.message());
    }
};

// ---------------------------------------------------------------------------
// CreateOfferObserver — on success: SetLocalDescription + fire app callback
// (SetLocalDescObserver must be fully defined above)
// ---------------------------------------------------------------------------
class CreateOfferObserver final
    : public webrtc::CreateSessionDescriptionObserver
{
public:
    CreateOfferObserver(webrtc::PeerConnectionInterface* pc,
                        IWebRTCObserver*                 observer)
        : pc_(pc), observer_(observer) {}

    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override {
        // Serialize before SetLocalDescription takes ownership
        std::string sdp_str;
        desc->ToString(&sdp_str);

        const webrtc::SdpType wtype = desc->GetType();

        auto set_obs = webrtc::make_ref_counted<SetLocalDescObserver>();
        pc_->SetLocalDescription(set_obs.get(), desc);

        SessionDescriptionType our_type;
        switch (wtype) {
            case webrtc::SdpType::kOffer:    our_type = SessionDescriptionType::kOffer;    break;
            case webrtc::SdpType::kPrAnswer: our_type = SessionDescriptionType::kPrAnswer; break;
            case webrtc::SdpType::kAnswer:   our_type = SessionDescriptionType::kAnswer;   break;
            default:                         our_type = SessionDescriptionType::kOffer;    break;
        }

        spdlog::info("[WebRTCHost] Local SDP ({})", to_string(our_type));
        observer_->on_local_sdp_created(SessionDescription{our_type, std::move(sdp_str)});
    }

    void OnFailure(webrtc::RTCError error) override {
        spdlog::error("[WebRTCHost] CreateOffer failed: {}", error.message());
    }

private:
    webrtc::PeerConnectionInterface* pc_;
    IWebRTCObserver*                 observer_;
};

// ===========================================================================
// WebRTCHostImpl
// ===========================================================================

class WebRTCHostImpl final
    : public webrtc::PeerConnectionObserver
    , public webrtc::DataChannelObserver
{
public:
    WebRTCHostImpl() = default;
    ~WebRTCHostImpl() override { do_close(); }

    WebRTCHostImpl(const WebRTCHostImpl&) = delete;
    WebRTCHostImpl& operator=(const WebRTCHostImpl&) = delete;

    // IWebRTCHost API -------------------------------------------------------
    VoidResult initialize(const WebRTCConfig& config, IWebRTCObserver* observer);
    VoidResult create_offer();
    VoidResult set_remote_description(SessionDescription sdp);
    VoidResult add_ice_candidate(IceCandidateInfo candidate);
    VoidResult add_video_track(IFrameCapture* capture, IEncoder* encoder);
    void       send_data(const uint8_t* data, size_t size);
    void       request_keyframe();
    void       close();

    PeerConnectionState get_connection_state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    bool is_initialized() const noexcept { return initialized_; }

    // Raw pointer to encoder factory for deferred set_pipeline() injection
    AMFVideoEncoderFactory* encoder_factory_ = nullptr;

private:
    // PeerConnectionObserver -----------------------------------------------
    void OnSignalingChange(
        webrtc::PeerConnectionInterface::SignalingState) override {}

    void OnDataChannel(
        webrtc::scoped_refptr<webrtc::DataChannelInterface> dc) override;

    void OnIceConnectionChange(
        webrtc::PeerConnectionInterface::IceConnectionState s) override;

    void OnConnectionChange(
        webrtc::PeerConnectionInterface::PeerConnectionState s) override;

    void OnIceCandidate(const webrtc::IceCandidate* candidate) override;

    void OnIceGatheringChange(
        webrtc::PeerConnectionInterface::IceGatheringState) override {}
    void OnAddTrack(
        webrtc::scoped_refptr<webrtc::RtpReceiverInterface>,
        const std::vector<webrtc::scoped_refptr<
            webrtc::MediaStreamInterface>>&) override {}
    void OnTrack(
        webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>) override {}
    void OnRemoveTrack(
        webrtc::scoped_refptr<webrtc::RtpReceiverInterface>) override {}
    void OnRenegotiationNeeded() override {}

    // DataChannelObserver --------------------------------------------------
    void OnStateChange() override;
    void OnMessage(const webrtc::DataBuffer& buffer) override;
    void OnBufferedAmountChange(uint64_t) override {}

    // Helpers --------------------------------------------------------------
    void do_close();

    static PeerConnectionState map_pc_state(
        webrtc::PeerConnectionInterface::PeerConnectionState) noexcept;
    static IceConnectionState map_ice_state(
        webrtc::PeerConnectionInterface::IceConnectionState) noexcept;

    // State ----------------------------------------------------------------
    bool             initialized_ = false;
    IWebRTCObserver* observer_    = nullptr;

    std::unique_ptr<webrtc::Thread> network_thread_;
    std::unique_ptr<webrtc::Thread> worker_thread_;
    std::unique_ptr<webrtc::Thread> signaling_thread_;

    webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface>        pc_;
    webrtc::scoped_refptr<webrtc::DataChannelInterface>           data_channel_;
    webrtc::scoped_refptr<CaptureVideoSource>                     video_source_;

    std::atomic<PeerConnectionState> state_{PeerConnectionState::kNew};
};

// ===========================================================================
// WebRTCHostImpl — implementations
// ===========================================================================

VoidResult WebRTCHostImpl::initialize(const WebRTCConfig& config,
                                       IWebRTCObserver*    observer)
{
    if (initialized_) {
        return VoidResult::error("WebRTCHostImpl::initialize: already initialized");
    }
    if (!observer) {
        return VoidResult::error("WebRTCHostImpl::initialize: observer is null");
    }

    observer_ = observer;
    rtc::InitializeSSL();

    // Create and start libwebrtc threads
    network_thread_   = rtc::Thread::CreateWithSocketServer();
    worker_thread_    = rtc::Thread::Create();
    signaling_thread_ = rtc::Thread::Create();

    network_thread_->SetName("gs_net",  nullptr);
    worker_thread_->SetName("gs_work",  nullptr);
    signaling_thread_->SetName("gs_sig", nullptr);

    network_thread_->Start();
    worker_thread_->Start();
    signaling_thread_->Start();

    // AMFVideoEncoderFactory — deferred: encoder/device injected later
    auto enc_factory = std::make_unique<AMFVideoEncoderFactory>();
    encoder_factory_ = enc_factory.get();

    factory_ = webrtc::CreatePeerConnectionFactory(
        network_thread_.get(),
        worker_thread_.get(),
        signaling_thread_.get(),
        /*adm*/              nullptr,
        webrtc::CreateBuiltinAudioEncoderFactory(),
        webrtc::CreateBuiltinAudioDecoderFactory(),
        std::move(enc_factory),
        webrtc::CreateBuiltinVideoDecoderFactory(),
        /*audio_mixer*/      nullptr,
        /*audio_processing*/ nullptr);

    if (!factory_) {
        return VoidResult::error("CreatePeerConnectionFactory returned null");
    }

    // ICE configuration
    webrtc::PeerConnectionInterface::RTCConfiguration rtc_cfg;
    rtc_cfg.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;

    for (const IceServer& srv : config.ice_servers) {
        webrtc::PeerConnectionInterface::IceServer ice;
        ice.uri      = srv.uri;
        ice.username = srv.username;
        ice.password = srv.password;
        rtc_cfg.servers.push_back(std::move(ice));
    }

    webrtc::PeerConnectionDependencies deps(this);
    auto pc_or_err = factory_->CreatePeerConnectionOrError(rtc_cfg, std::move(deps));
    if (!pc_or_err.ok()) {
        return VoidResult::error(std::format(
            "CreatePeerConnection failed: {}", pc_or_err.error().message()));
    }

    pc_ = std::move(pc_or_err.value());
    initialized_ = true;

    spdlog::info("[WebRTCHost] Initialized. ICE servers: {}", config.ice_servers.size());
    return {};
}

VoidResult WebRTCHostImpl::create_offer() {
    if (!initialized_ || !pc_) {
        return VoidResult::error("create_offer: not initialized");
    }

    // Input DataChannel (unreliable, unordered — ideal for game input)
    webrtc::DataChannelInit dc_init;
    dc_init.ordered        = false;
    dc_init.maxRetransmits = 0;

    auto dc_or_err = pc_->CreateDataChannelOrError("input", &dc_init);
    if (dc_or_err.ok()) {
        data_channel_ = std::move(dc_or_err.value());
        data_channel_->RegisterObserver(this);
        spdlog::info("[WebRTCHost] DataChannel 'input' created (unreliable, unordered)");
    } else {
        spdlog::warn("[WebRTCHost] CreateDataChannel failed: {}",
                     dc_or_err.error().message());
    }

    auto offer_obs = webrtc::make_ref_counted<CreateOfferObserver>(
        pc_.get(), observer_);

    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions opts;
    opts.offer_to_receive_audio = 0;
    opts.offer_to_receive_video = 0;

    pc_->CreateOffer(offer_obs.get(), opts);
    spdlog::debug("[WebRTCHost] CreateOffer enqueued");
    return {};
}

VoidResult WebRTCHostImpl::set_remote_description(SessionDescription sdp) {
    if (!initialized_ || !pc_) {
        return VoidResult::error("set_remote_description: not initialized");
    }

    webrtc::SdpType wtype;
    switch (sdp.type) {
        case SessionDescriptionType::kOffer:    wtype = webrtc::SdpType::kOffer;    break;
        case SessionDescriptionType::kPrAnswer: wtype = webrtc::SdpType::kPrAnswer; break;
        case SessionDescriptionType::kAnswer:   wtype = webrtc::SdpType::kAnswer;   break;
        case SessionDescriptionType::kRollback: wtype = webrtc::SdpType::kRollback; break;
        default:                                wtype = webrtc::SdpType::kAnswer;   break;
    }

    webrtc::SdpParseError parse_err;
    auto desc = webrtc::CreateSessionDescription(wtype, sdp.sdp, &parse_err);
    if (!desc) {
        return VoidResult::error(std::format(
            "SDP parse error at '{}': {}", parse_err.line, parse_err.description));
    }

    auto set_obs = webrtc::make_ref_counted<SetRemoteDescObserver>();
    pc_->SetRemoteDescription(set_obs.get(), desc.release());

    spdlog::info("[WebRTCHost] SetRemoteDescription({})", to_string(sdp.type));
    return {};
}

VoidResult WebRTCHostImpl::add_ice_candidate(IceCandidateInfo candidate) {
    if (!initialized_ || !pc_) {
        return VoidResult::error("add_ice_candidate: not initialized");
    }

    webrtc::SdpParseError parse_err;
    webrtc::IceCandidate* raw = webrtc::CreateIceCandidate(
        candidate.sdp_mid,
        candidate.sdp_mline_index,
        candidate.sdp,
        &parse_err);

    if (!raw) {
        return VoidResult::error(std::format(
            "ICE candidate parse error: {}", parse_err.description));
    }

    pc_->AddIceCandidate(
        std::unique_ptr<webrtc::IceCandidate>(raw),
        [](webrtc::RTCError err) {
            if (!err.ok()) {
                spdlog::warn("[WebRTCHost] AddIceCandidate error: {}", err.message());
            }
        });

    spdlog::debug("[WebRTCHost] AddIceCandidate mid={} idx={}",
                  candidate.sdp_mid, candidate.sdp_mline_index);
    return {};
}

VoidResult WebRTCHostImpl::add_video_track(IFrameCapture* capture,
                                            IEncoder*      encoder)
{
    if (!initialized_ || !pc_ || !factory_) {
        return VoidResult::error("add_video_track: not initialized");
    }
    if (!capture || !encoder) {
        return VoidResult::error("add_video_track: null capture or encoder");
    }

    ID3D11Device* device = capture->get_device();
    if (!device) {
        return VoidResult::error("add_video_track: capture device is null");
    }

    // Inject pipeline into factory BEFORE any encoder is created
    encoder_factory_->set_pipeline(encoder, device);

    // Create and start the capture → I420 → WebRTC source
    video_source_ = webrtc::make_ref_counted<CaptureVideoSource>(capture);
    video_source_->start();

    auto video_track = factory_->CreateVideoTrack(video_source_, "video");
    if (!video_track) {
        video_source_->stop();
        video_source_ = nullptr;
        return VoidResult::error("add_video_track: CreateVideoTrack returned null");
    }

    auto result = pc_->AddTrack(video_track, {"stream"});
    if (!result.ok()) {
        video_source_->stop();
        video_source_ = nullptr;
        return VoidResult::error(std::format(
            "add_video_track: AddTrack failed: {}", result.error().message()));
    }

    spdlog::info("[WebRTCHost] Video track added");
    return {};
}

void WebRTCHostImpl::send_data(const uint8_t* data, size_t size) {
    if (!data_channel_ ||
        data_channel_->state() != webrtc::DataChannelInterface::kOpen)
    {
        return;
    }
    webrtc::CopyOnWriteBuffer buf(data, size);
    data_channel_->Send(webrtc::DataBuffer(buf, /*binary=*/true));
}

void WebRTCHostImpl::request_keyframe() {
    spdlog::debug("[WebRTCHost] request_keyframe — caller should invoke IEncoder::request_keyframe()");
}

void WebRTCHostImpl::close() { do_close(); }

void WebRTCHostImpl::do_close() {
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

    factory_         = nullptr;
    encoder_factory_ = nullptr;

    if (signaling_thread_) { signaling_thread_->Stop(); signaling_thread_.reset(); }
    if (worker_thread_)    { worker_thread_->Stop();    worker_thread_.reset();    }
    if (network_thread_)   { network_thread_->Stop();   network_thread_.reset();   }

    rtc::CleanupSSL();

    state_.store(PeerConnectionState::kClosed, std::memory_order_release);
    initialized_ = false;
    spdlog::info("[WebRTCHost] Closed");
}

// ---------------------------------------------------------------------------
// PeerConnectionObserver
// ---------------------------------------------------------------------------

void WebRTCHostImpl::OnDataChannel(
    webrtc::scoped_refptr<webrtc::DataChannelInterface> dc)
{
    spdlog::info("[WebRTCHost] OnDataChannel: label={}", dc->label());
    if (!data_channel_) {
        data_channel_ = std::move(dc);
        data_channel_->RegisterObserver(this);
    }
}

void WebRTCHostImpl::OnIceConnectionChange(
    webrtc::PeerConnectionInterface::IceConnectionState s)
{
    if (observer_) {
        observer_->on_ice_connection_state_changed(map_ice_state(s));
    }
}

void WebRTCHostImpl::OnConnectionChange(
    webrtc::PeerConnectionInterface::PeerConnectionState s)
{
    const PeerConnectionState our = map_pc_state(s);
    state_.store(our, std::memory_order_release);
    spdlog::info("[WebRTCHost] ConnectionState → {}", to_string(our));
    if (observer_) {
        observer_->on_connection_state_changed(our);
    }
}

void WebRTCHostImpl::OnIceCandidate(const webrtc::IceCandidate* candidate) {
    if (!candidate || !observer_) return;
    std::string sdp_str;
    candidate->ToString(&sdp_str);
    observer_->on_ice_candidate(IceCandidateInfo{
        candidate->sdp_mid(),
        candidate->sdp_mline_index(),
        std::move(sdp_str)
    });
}

// ---------------------------------------------------------------------------
// DataChannelObserver
// ---------------------------------------------------------------------------

void WebRTCHostImpl::OnStateChange() {
    if (!data_channel_ || !observer_) return;
    const auto s = data_channel_->state();
    if (s == webrtc::DataChannelInterface::kOpen) {
        observer_->on_data_channel_open();
    } else if (s == webrtc::DataChannelInterface::kClosed) {
        observer_->on_data_channel_closed();
    }
}

void WebRTCHostImpl::OnMessage(const webrtc::DataBuffer& buffer) {
    if (observer_) {
        observer_->on_data_channel_message(buffer.data.data(), buffer.data.size());
    }
}

// ---------------------------------------------------------------------------
// State mappers
// ---------------------------------------------------------------------------

PeerConnectionState WebRTCHostImpl::map_pc_state(
    webrtc::PeerConnectionInterface::PeerConnectionState s) noexcept
{
    using W = webrtc::PeerConnectionInterface::PeerConnectionState;
    switch (s) {
        case W::kNew:          return PeerConnectionState::kNew;
        case W::kConnecting:   return PeerConnectionState::kConnecting;
        case W::kConnected:    return PeerConnectionState::kConnected;
        case W::kDisconnected: return PeerConnectionState::kDisconnected;
        case W::kFailed:       return PeerConnectionState::kFailed;
        case W::kClosed:       return PeerConnectionState::kClosed;
    }
    return PeerConnectionState::kFailed;
}

IceConnectionState WebRTCHostImpl::map_ice_state(
    webrtc::PeerConnectionInterface::IceConnectionState s) noexcept
{
    using W = webrtc::PeerConnectionInterface::IceConnectionState;
    switch (s) {
        case W::kIceConnectionNew:          return IceConnectionState::kNew;
        case W::kIceConnectionChecking:     return IceConnectionState::kChecking;
        case W::kIceConnectionConnected:    return IceConnectionState::kConnected;
        case W::kIceConnectionCompleted:    return IceConnectionState::kCompleted;
        case W::kIceConnectionFailed:       return IceConnectionState::kFailed;
        case W::kIceConnectionDisconnected: return IceConnectionState::kDisconnected;
        case W::kIceConnectionClosed:       return IceConnectionState::kClosed;
        default:                            return IceConnectionState::kFailed;
    }
}

// ===========================================================================
// WebRTCHost — PIMPL facade
// ===========================================================================

WebRTCHost::WebRTCHost()
    : impl_(std::make_unique<WebRTCHostImpl>()) {}

WebRTCHost::~WebRTCHost() = default;

VoidResult WebRTCHost::initialize(const WebRTCConfig& cfg, IWebRTCObserver* obs) {
    return impl_->initialize(cfg, obs);
}
VoidResult WebRTCHost::create_offer() {
    return impl_->create_offer();
}
VoidResult WebRTCHost::set_remote_description(SessionDescription sdp) {
    return impl_->set_remote_description(std::move(sdp));
}
VoidResult WebRTCHost::add_ice_candidate(IceCandidateInfo c) {
    return impl_->add_ice_candidate(std::move(c));
}
VoidResult WebRTCHost::add_video_track(IFrameCapture* cap, IEncoder* enc) {
    return impl_->add_video_track(cap, enc);
}
void WebRTCHost::send_data(const uint8_t* data, size_t size) {
    impl_->send_data(data, size);
}
void WebRTCHost::request_keyframe() {
    impl_->request_keyframe();
}
void WebRTCHost::close() {
    impl_->close();
}
PeerConnectionState WebRTCHost::get_connection_state() const {
    return impl_->get_connection_state();
}
bool WebRTCHost::is_initialized() const {
    return impl_->is_initialized();
}

} // namespace gamestream
