#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "iwebrtc_host.h"
#include "iwebrtc_observer.h"
#include "webrtc_types.h"
#include "result.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace gamestream {

// ===========================================================================
// MockWebRTCObserver
// ===========================================================================

class MockWebRTCObserver : public IWebRTCObserver {
public:
    MOCK_METHOD(void, on_local_sdp_created,
                (SessionDescription sdp), (override));

    MOCK_METHOD(void, on_ice_candidate,
                (IceCandidateInfo candidate), (override));

    MOCK_METHOD(void, on_connection_state_changed,
                (PeerConnectionState state), (override));

    MOCK_METHOD(void, on_ice_connection_state_changed,
                (IceConnectionState state), (override));

    MOCK_METHOD(void, on_data_channel_open, (), (override));

    MOCK_METHOD(void, on_data_channel_message,
                (const uint8_t* data, size_t size), (override));

    MOCK_METHOD(void, on_data_channel_closed, (), (override));
};

// ===========================================================================
// MockWebRTCHost — verifies that consumers call the interface correctly
// ===========================================================================

class MockWebRTCHost : public IWebRTCHost {
public:
    MOCK_METHOD(VoidResult, initialize,
                (const WebRTCConfig& config, IWebRTCObserver* observer), (override));

    MOCK_METHOD(VoidResult, create_offer, (), (override));

    MOCK_METHOD(VoidResult, set_remote_description,
                (SessionDescription sdp), (override));

    MOCK_METHOD(VoidResult, add_ice_candidate,
                (IceCandidateInfo candidate), (override));

    MOCK_METHOD(VoidResult, add_video_track,
                (IFrameCapture* capture, IEncoder* encoder), (override));

    MOCK_METHOD(void, send_data,
                (const uint8_t* data, size_t size), (override));

    MOCK_METHOD(void, request_keyframe, (), (override));

    MOCK_METHOD(void, close, (), (override));

    MOCK_METHOD(PeerConnectionState, get_connection_state, (), (const, override));

    MOCK_METHOD(bool, is_initialized, (), (const, override));
};

// ===========================================================================
// IWebRTCHost — interface contract tests
// ===========================================================================

TEST(IWebRTCHostTest, InitializeSucceeds) {
    MockWebRTCHost host;
    MockWebRTCObserver observer;

    WebRTCConfig cfg;
    IceServer stun;
    stun.uri = "stun:stun.l.google.com:19302";
    cfg.ice_servers.push_back(stun);

    EXPECT_CALL(host, initialize(testing::_, &observer))
        .WillOnce(testing::Return(VoidResult{}));

    EXPECT_TRUE(host.initialize(cfg, &observer));
}

TEST(IWebRTCHostTest, InitializeFailureReturnsError) {
    MockWebRTCHost host;
    MockWebRTCObserver observer;
    const std::string kErr = "libwebrtc thread start failed";

    EXPECT_CALL(host, initialize(testing::_, testing::_))
        .WillOnce(testing::Return(VoidResult::error(kErr)));

    auto result = host.initialize(WebRTCConfig{}, &observer);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), kErr);
}

TEST(IWebRTCHostTest, IsInitializedFalseBeforeInit) {
    MockWebRTCHost host;
    EXPECT_CALL(host, is_initialized()).WillOnce(testing::Return(false));
    EXPECT_FALSE(host.is_initialized());
}

TEST(IWebRTCHostTest, IsInitializedTrueAfterSuccessfulInit) {
    MockWebRTCHost host;
    MockWebRTCObserver observer;

    EXPECT_CALL(host, initialize(testing::_, testing::_))
        .WillOnce(testing::Return(VoidResult{}));
    EXPECT_CALL(host, is_initialized()).WillOnce(testing::Return(true));

    host.initialize(WebRTCConfig{}, &observer);
    EXPECT_TRUE(host.is_initialized());
}

TEST(IWebRTCHostTest, ConnectionStateIsNewBeforeInit) {
    MockWebRTCHost host;
    EXPECT_CALL(host, get_connection_state())
        .WillOnce(testing::Return(PeerConnectionState::kNew));
    EXPECT_EQ(host.get_connection_state(), PeerConnectionState::kNew);
}

TEST(IWebRTCHostTest, CreateOfferSucceeds) {
    MockWebRTCHost host;
    EXPECT_CALL(host, create_offer()).WillOnce(testing::Return(VoidResult{}));
    EXPECT_TRUE(host.create_offer());
}

TEST(IWebRTCHostTest, CreateOfferFailsWithError) {
    MockWebRTCHost host;
    EXPECT_CALL(host, create_offer())
        .WillOnce(testing::Return(VoidResult::error("not initialized")));

    auto result = host.create_offer();
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), "not initialized");
}

TEST(IWebRTCHostTest, SetRemoteDescriptionWithAnswer) {
    MockWebRTCHost host;

    SessionDescription answer;
    answer.type = SessionDescriptionType::kAnswer;
    answer.sdp  = "v=0\r\ns=answer\r\n";

    EXPECT_CALL(host, set_remote_description(answer))
        .WillOnce(testing::Return(VoidResult{}));

    EXPECT_TRUE(host.set_remote_description(answer));
}

TEST(IWebRTCHostTest, SetRemoteDescriptionWithOffer) {
    MockWebRTCHost host;

    SessionDescription offer;
    offer.type = SessionDescriptionType::kOffer;
    offer.sdp  = "v=0\r\ns=offer\r\n";

    EXPECT_CALL(host, set_remote_description(offer))
        .WillOnce(testing::Return(VoidResult{}));

    EXPECT_TRUE(host.set_remote_description(offer));
}

TEST(IWebRTCHostTest, AddIceCandidateSucceeds) {
    MockWebRTCHost host;

    IceCandidateInfo candidate;
    candidate.sdp_mid         = "0";
    candidate.sdp_mline_index = 0;
    candidate.sdp             = "candidate:1 1 UDP 2122252543 192.168.1.1 51235 typ host";

    EXPECT_CALL(host, add_ice_candidate(candidate))
        .WillOnce(testing::Return(VoidResult{}));

    EXPECT_TRUE(host.add_ice_candidate(candidate));
}

TEST(IWebRTCHostTest, AddIceCandidateCanBeCalledMultipleTimes) {
    MockWebRTCHost host;

    EXPECT_CALL(host, add_ice_candidate(testing::_))
        .Times(2)
        .WillRepeatedly(testing::Return(VoidResult{}));

    IceCandidateInfo c1;
    c1.sdp_mid = "0"; c1.sdp_mline_index = 0; c1.sdp = "candidate:a";

    IceCandidateInfo c2;
    c2.sdp_mid = "0"; c2.sdp_mline_index = 0; c2.sdp = "candidate:b";

    EXPECT_TRUE(host.add_ice_candidate(c1));
    EXPECT_TRUE(host.add_ice_candidate(c2));
}

TEST(IWebRTCHostTest, AddVideoTrackSucceeds) {
    MockWebRTCHost host;
    EXPECT_CALL(host, add_video_track(nullptr, nullptr))
        .WillOnce(testing::Return(VoidResult{}));
    EXPECT_TRUE(host.add_video_track(nullptr, nullptr));
}

TEST(IWebRTCHostTest, AddVideoTrackCanOnlyBeCalledOnce) {
    MockWebRTCHost host;
    EXPECT_CALL(host, add_video_track(testing::_, testing::_))
        .WillOnce(testing::Return(VoidResult{}))
        .WillOnce(testing::Return(VoidResult::error("video track already added")));

    EXPECT_TRUE(host.add_video_track(nullptr, nullptr));

    auto second = host.add_video_track(nullptr, nullptr);
    EXPECT_FALSE(second);
    EXPECT_EQ(second.error(), "video track already added");
}

TEST(IWebRTCHostTest, SendDataIsCalledWithCorrectBytes) {
    MockWebRTCHost host;

    const uint8_t kPacket[] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                0x00, 0x0A, 0x00, 0xFB, 0xFF};
    EXPECT_CALL(host, send_data(kPacket, sizeof(kPacket))).Times(1);
    host.send_data(kPacket, sizeof(kPacket));
}

TEST(IWebRTCHostTest, RequestKeyframeCanBeCalledMultipleTimes) {
    MockWebRTCHost host;
    EXPECT_CALL(host, request_keyframe()).Times(3);
    host.request_keyframe();
    host.request_keyframe();
    host.request_keyframe();
}

TEST(IWebRTCHostTest, CloseTransitionsStateToKClosed) {
    MockWebRTCHost host;

    testing::InSequence seq;
    EXPECT_CALL(host, close());
    EXPECT_CALL(host, get_connection_state())
        .WillOnce(testing::Return(PeerConnectionState::kClosed));
    EXPECT_CALL(host, is_initialized()).WillOnce(testing::Return(false));

    host.close();
    EXPECT_EQ(host.get_connection_state(), PeerConnectionState::kClosed);
    EXPECT_FALSE(host.is_initialized());
}

TEST(IWebRTCHostTest, PolymorphicUseThroughBasePointer) {
    std::unique_ptr<IWebRTCHost> host = std::make_unique<MockWebRTCHost>();
    auto* mock = static_cast<MockWebRTCHost*>(host.get());

    EXPECT_CALL(*mock, is_initialized()).WillOnce(testing::Return(false));
    EXPECT_FALSE(host->is_initialized());
}

// ===========================================================================
// SimulatedWebRTCHost — drives observer callbacks for protocol tests
// ===========================================================================

class SimulatedWebRTCHost : public IWebRTCHost {
public:
    VoidResult initialize(const WebRTCConfig& /*config*/,
                          IWebRTCObserver* observer) override {
        observer_    = observer;
        initialized_ = true;
        state_       = PeerConnectionState::kNew;
        return VoidResult{};
    }

    VoidResult create_offer() override {
        if (!observer_) { return VoidResult::error("not initialized"); }

        SessionDescription offer;
        offer.type = SessionDescriptionType::kOffer;
        offer.sdp  = "v=0\r\no=- 12345 2 IN IP4 127.0.0.1\r\n";
        observer_->on_local_sdp_created(offer);

        IceCandidateInfo c;
        c.sdp_mid = "0"; c.sdp_mline_index = 0;
        c.sdp = "candidate:1 1 UDP 2122252543 192.168.1.10 54321 typ host";
        observer_->on_ice_candidate(c);

        state_ = PeerConnectionState::kConnecting;
        observer_->on_connection_state_changed(state_);
        observer_->on_ice_connection_state_changed(IceConnectionState::kChecking);
        return VoidResult{};
    }

    VoidResult set_remote_description(SessionDescription /*sdp*/) override {
        if (!observer_) { return VoidResult::error("not initialized"); }
        state_ = PeerConnectionState::kConnected;
        observer_->on_connection_state_changed(state_);
        observer_->on_ice_connection_state_changed(IceConnectionState::kConnected);
        observer_->on_data_channel_open();
        return VoidResult{};
    }

    VoidResult add_ice_candidate(IceCandidateInfo /*candidate*/) override {
        return VoidResult{};
    }

    VoidResult add_video_track(IFrameCapture* /*capture*/,
                               IEncoder*      /*encoder*/) override {
        return VoidResult{};
    }

    void send_data(const uint8_t* /*data*/, size_t /*size*/) override {}
    void request_keyframe() override {}

    void close() override {
        if (observer_) {
            state_ = PeerConnectionState::kClosed;
            observer_->on_connection_state_changed(state_);
            observer_->on_data_channel_closed();
        }
        initialized_ = false;
    }

    PeerConnectionState get_connection_state() const override { return state_; }
    bool is_initialized() const override { return initialized_; }

    void simulate_input_packet(const uint8_t* data, size_t size) {
        if (observer_) { observer_->on_data_channel_message(data, size); }
    }

private:
    IWebRTCObserver*    observer_    = nullptr;
    bool                initialized_ = false;
    PeerConnectionState state_       = PeerConnectionState::kNew;
};

// ===========================================================================
// IWebRTCObserver — callback protocol tests
// ===========================================================================

TEST(IWebRTCObserverTest, OfferIsDeliveredOnCreateOffer) {
    SimulatedWebRTCHost host;
    MockWebRTCObserver observer;

    EXPECT_CALL(observer, on_local_sdp_created(
        testing::Field(&SessionDescription::type, SessionDescriptionType::kOffer)))
        .Times(1);
    EXPECT_CALL(observer, on_ice_candidate(testing::_)).Times(testing::AtLeast(1));
    EXPECT_CALL(observer, on_connection_state_changed(testing::_))
        .Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_ice_connection_state_changed(testing::_))
        .Times(testing::AnyNumber());

    host.initialize(WebRTCConfig{}, &observer);
    host.create_offer();
}

TEST(IWebRTCObserverTest, ConnectionBecomesConnectedOnRemoteDescription) {
    SimulatedWebRTCHost host;
    MockWebRTCObserver observer;

    bool connected = false;
    EXPECT_CALL(observer, on_connection_state_changed(testing::_))
        .WillRepeatedly([&connected](PeerConnectionState s) {
            if (s == PeerConnectionState::kConnected) { connected = true; }
        });
    EXPECT_CALL(observer, on_ice_connection_state_changed(testing::_))
        .Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_local_sdp_created(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_ice_candidate(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_data_channel_open()).Times(1);

    host.initialize(WebRTCConfig{}, &observer);
    host.create_offer();

    SessionDescription answer;
    answer.type = SessionDescriptionType::kAnswer;
    answer.sdp  = "v=0\r\n";
    host.set_remote_description(answer);

    EXPECT_TRUE(connected);
}

TEST(IWebRTCObserverTest, DataChannelOpenFiresAfterConnection) {
    SimulatedWebRTCHost host;
    MockWebRTCObserver observer;

    bool dc_open = false;
    EXPECT_CALL(observer, on_data_channel_open())
        .WillOnce([&dc_open]() { dc_open = true; });
    EXPECT_CALL(observer, on_connection_state_changed(testing::_))
        .Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_ice_connection_state_changed(testing::_))
        .Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_local_sdp_created(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_ice_candidate(testing::_)).Times(testing::AnyNumber());

    host.initialize(WebRTCConfig{}, &observer);
    host.create_offer();

    SessionDescription answer;
    answer.type = SessionDescriptionType::kAnswer;
    answer.sdp  = "v=0\r\n";
    host.set_remote_description(answer);

    EXPECT_TRUE(dc_open);
}

TEST(IWebRTCObserverTest, DataChannelMessageIsDeliveredCorrectly) {
    SimulatedWebRTCHost host;
    MockWebRTCObserver observer;

    // Mouse move packet: type=0x01, timestamp(8 bytes), dx=10, dy=-5
    const uint8_t kPacket[] = {
        0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0A, 0x00,  // dx = 10 (little-endian i16)
        0xFB, 0xFF   // dy = -5 (little-endian i16)
    };

    EXPECT_CALL(observer, on_data_channel_message(testing::_, sizeof(kPacket)))
        .WillOnce([](const uint8_t* data, size_t size) {
            ASSERT_EQ(size, 13u);
            EXPECT_EQ(data[0],  0x01u);
            EXPECT_EQ(data[9],  0x0Au);
            EXPECT_EQ(data[11], 0xFBu);
            EXPECT_EQ(data[12], 0xFFu);
        });
    EXPECT_CALL(observer, on_connection_state_changed(testing::_))
        .Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_ice_connection_state_changed(testing::_))
        .Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_local_sdp_created(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_ice_candidate(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_data_channel_open()).Times(testing::AnyNumber());

    host.initialize(WebRTCConfig{}, &observer);
    host.create_offer();

    SessionDescription answer;
    answer.type = SessionDescriptionType::kAnswer;
    answer.sdp  = "v=0\r\n";
    host.set_remote_description(answer);

    host.simulate_input_packet(kPacket, sizeof(kPacket));
}

TEST(IWebRTCObserverTest, DataChannelClosedFiresOnClose) {
    SimulatedWebRTCHost host;
    MockWebRTCObserver observer;

    bool dc_closed = false;
    EXPECT_CALL(observer, on_data_channel_closed())
        .WillOnce([&dc_closed]() { dc_closed = true; });
    EXPECT_CALL(observer, on_connection_state_changed(testing::_))
        .Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_ice_connection_state_changed(testing::_))
        .Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_local_sdp_created(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_ice_candidate(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_data_channel_open()).Times(testing::AnyNumber());

    host.initialize(WebRTCConfig{}, &observer);
    host.create_offer();

    SessionDescription answer;
    answer.type = SessionDescriptionType::kAnswer;
    answer.sdp  = "v=0\r\n";
    host.set_remote_description(answer);

    host.close();

    EXPECT_TRUE(dc_closed);
    EXPECT_FALSE(host.is_initialized());
    EXPECT_EQ(host.get_connection_state(), PeerConnectionState::kClosed);
}

TEST(IWebRTCObserverTest, StateTransitionSequenceIsCorrect) {
    SimulatedWebRTCHost host;
    MockWebRTCObserver observer;

    std::vector<PeerConnectionState> log;
    EXPECT_CALL(observer, on_connection_state_changed(testing::_))
        .WillRepeatedly([&log](PeerConnectionState s) { log.push_back(s); });
    EXPECT_CALL(observer, on_ice_connection_state_changed(testing::_))
        .Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_local_sdp_created(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_ice_candidate(testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_data_channel_open()).Times(testing::AnyNumber());
    EXPECT_CALL(observer, on_data_channel_closed()).Times(testing::AnyNumber());

    host.initialize(WebRTCConfig{}, &observer);
    host.create_offer();  // → kConnecting

    SessionDescription answer;
    answer.type = SessionDescriptionType::kAnswer;
    answer.sdp  = "v=0\r\n";
    host.set_remote_description(answer);  // → kConnected

    host.close();  // → kClosed

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], PeerConnectionState::kConnecting);
    EXPECT_EQ(log[1], PeerConnectionState::kConnected);
    EXPECT_EQ(log[2], PeerConnectionState::kClosed);
}

} // namespace gamestream
