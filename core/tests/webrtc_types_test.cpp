#include <gtest/gtest.h>

#include "webrtc_types.h"

namespace gamestream {

// ===========================================================================
// SessionDescriptionType — to_string()
// ===========================================================================

TEST(SessionDescriptionTypeTest, ToStringOffer) {
    EXPECT_STREQ(to_string(SessionDescriptionType::kOffer), "offer");
}

TEST(SessionDescriptionTypeTest, ToStringPrAnswer) {
    EXPECT_STREQ(to_string(SessionDescriptionType::kPrAnswer), "pranswer");
}

TEST(SessionDescriptionTypeTest, ToStringAnswer) {
    EXPECT_STREQ(to_string(SessionDescriptionType::kAnswer), "answer");
}

TEST(SessionDescriptionTypeTest, ToStringRollback) {
    EXPECT_STREQ(to_string(SessionDescriptionType::kRollback), "rollback");
}

// ===========================================================================
// SessionDescription
// ===========================================================================

TEST(SessionDescriptionTest, ConstructWithOfferType) {
    SessionDescription sdp;
    sdp.type = SessionDescriptionType::kOffer;
    sdp.sdp  = "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\n";

    EXPECT_EQ(sdp.type, SessionDescriptionType::kOffer);
    EXPECT_EQ(sdp.sdp, "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\n");
}

TEST(SessionDescriptionTest, EqualityWhenSameTypeAndSdp) {
    SessionDescription a;
    a.type = SessionDescriptionType::kAnswer;
    a.sdp  = "sdp-text";

    SessionDescription b;
    b.type = SessionDescriptionType::kAnswer;
    b.sdp  = "sdp-text";

    EXPECT_EQ(a, b);
}

TEST(SessionDescriptionTest, InequalityWhenDifferentType) {
    SessionDescription a;
    a.type = SessionDescriptionType::kOffer;
    a.sdp  = "sdp-text";

    SessionDescription b;
    b.type = SessionDescriptionType::kAnswer;
    b.sdp  = "sdp-text";

    EXPECT_NE(a, b);
}

TEST(SessionDescriptionTest, InequalityWhenDifferentSdp) {
    SessionDescription a;
    a.type = SessionDescriptionType::kOffer;
    a.sdp  = "sdp-a";

    SessionDescription b;
    b.type = SessionDescriptionType::kOffer;
    b.sdp  = "sdp-b";

    EXPECT_NE(a, b);
}

TEST(SessionDescriptionTest, EmptySdpIsValid) {
    SessionDescription sdp;
    sdp.type = SessionDescriptionType::kRollback;
    sdp.sdp  = "";

    EXPECT_EQ(sdp.type, SessionDescriptionType::kRollback);
    EXPECT_TRUE(sdp.sdp.empty());
}

// ===========================================================================
// IceCandidateInfo
// ===========================================================================

TEST(IceCandidateInfoTest, ConstructWithAllFields) {
    IceCandidateInfo c;
    c.sdp_mid         = "video";
    c.sdp_mline_index = 0;
    c.sdp             = "candidate:1 1 UDP 2122252543 192.168.1.1 51235 typ host";

    EXPECT_EQ(c.sdp_mid, "video");
    EXPECT_EQ(c.sdp_mline_index, 0);
    EXPECT_EQ(c.sdp, "candidate:1 1 UDP 2122252543 192.168.1.1 51235 typ host");
}

TEST(IceCandidateInfoTest, EqualityWhenAllFieldsMatch) {
    IceCandidateInfo a;
    a.sdp_mid = "0"; a.sdp_mline_index = 0; a.sdp = "candidate:abc";

    IceCandidateInfo b = a;
    EXPECT_EQ(a, b);
}

TEST(IceCandidateInfoTest, InequalityWhenSdpMidDiffers) {
    IceCandidateInfo a;
    a.sdp_mid = "audio"; a.sdp_mline_index = 0; a.sdp = "candidate:abc";

    IceCandidateInfo b;
    b.sdp_mid = "video"; b.sdp_mline_index = 0; b.sdp = "candidate:abc";

    EXPECT_NE(a, b);
}

TEST(IceCandidateInfoTest, InequalityWhenMlineIndexDiffers) {
    IceCandidateInfo a;
    a.sdp_mid = "0"; a.sdp_mline_index = 0; a.sdp = "candidate:abc";

    IceCandidateInfo b;
    b.sdp_mid = "0"; b.sdp_mline_index = 1; b.sdp = "candidate:abc";

    EXPECT_NE(a, b);
}

TEST(IceCandidateInfoTest, InequalityWhenSdpDiffers) {
    IceCandidateInfo a;
    a.sdp_mid = "0"; a.sdp_mline_index = 0; a.sdp = "candidate:aaa";

    IceCandidateInfo b;
    b.sdp_mid = "0"; b.sdp_mline_index = 0; b.sdp = "candidate:bbb";

    EXPECT_NE(a, b);
}

// ===========================================================================
// PeerConnectionState
// ===========================================================================

TEST(PeerConnectionStateTest, ToStringCoversAllStates) {
    EXPECT_STREQ(to_string(PeerConnectionState::kNew),          "new");
    EXPECT_STREQ(to_string(PeerConnectionState::kConnecting),   "connecting");
    EXPECT_STREQ(to_string(PeerConnectionState::kConnected),    "connected");
    EXPECT_STREQ(to_string(PeerConnectionState::kDisconnected), "disconnected");
    EXPECT_STREQ(to_string(PeerConnectionState::kFailed),       "failed");
    EXPECT_STREQ(to_string(PeerConnectionState::kClosed),       "closed");
}

TEST(PeerConnectionStateTest, AllEnumValuesAreDistinct) {
    EXPECT_NE(static_cast<int>(PeerConnectionState::kNew),
              static_cast<int>(PeerConnectionState::kConnecting));
    EXPECT_NE(static_cast<int>(PeerConnectionState::kConnecting),
              static_cast<int>(PeerConnectionState::kConnected));
    EXPECT_NE(static_cast<int>(PeerConnectionState::kConnected),
              static_cast<int>(PeerConnectionState::kDisconnected));
    EXPECT_NE(static_cast<int>(PeerConnectionState::kDisconnected),
              static_cast<int>(PeerConnectionState::kFailed));
    EXPECT_NE(static_cast<int>(PeerConnectionState::kFailed),
              static_cast<int>(PeerConnectionState::kClosed));
}

// ===========================================================================
// IceConnectionState
// ===========================================================================

TEST(IceConnectionStateTest, ToStringCoversAllStates) {
    EXPECT_STREQ(to_string(IceConnectionState::kNew),          "new");
    EXPECT_STREQ(to_string(IceConnectionState::kChecking),     "checking");
    EXPECT_STREQ(to_string(IceConnectionState::kConnected),    "connected");
    EXPECT_STREQ(to_string(IceConnectionState::kCompleted),    "completed");
    EXPECT_STREQ(to_string(IceConnectionState::kFailed),       "failed");
    EXPECT_STREQ(to_string(IceConnectionState::kDisconnected), "disconnected");
    EXPECT_STREQ(to_string(IceConnectionState::kClosed),       "closed");
}

// ===========================================================================
// SignalingState
// ===========================================================================

TEST(SignalingStateTest, ToStringCoversAllStates) {
    EXPECT_STREQ(to_string(SignalingState::kStable),             "stable");
    EXPECT_STREQ(to_string(SignalingState::kHaveLocalOffer),     "have-local-offer");
    EXPECT_STREQ(to_string(SignalingState::kHaveLocalPrAnswer),  "have-local-pranswer");
    EXPECT_STREQ(to_string(SignalingState::kHaveRemoteOffer),    "have-remote-offer");
    EXPECT_STREQ(to_string(SignalingState::kHaveRemotePrAnswer), "have-remote-pranswer");
    EXPECT_STREQ(to_string(SignalingState::kClosed),             "closed");
}

// ===========================================================================
// IceServer
// ===========================================================================

TEST(IceServerTest, StunServerHasNoCredentials) {
    IceServer stun;
    stun.uri = "stun:stun.l.google.com:19302";

    EXPECT_EQ(stun.uri, "stun:stun.l.google.com:19302");
    EXPECT_TRUE(stun.username.empty());
    EXPECT_TRUE(stun.password.empty());
}

TEST(IceServerTest, TurnServerHasCredentials) {
    IceServer turn;
    turn.uri      = "turn:relay.example.com:3478";
    turn.username = "gamestream";
    turn.password = "secret";

    EXPECT_EQ(turn.uri,      "turn:relay.example.com:3478");
    EXPECT_EQ(turn.username, "gamestream");
    EXPECT_EQ(turn.password, "secret");
}

// ===========================================================================
// WebRTCConfig
// ===========================================================================

TEST(WebRTCConfigTest, DefaultInputChannelLabelIsInput) {
    WebRTCConfig cfg;
    EXPECT_EQ(cfg.input_channel_label, "input");
}

TEST(WebRTCConfigTest, DefaultIceServersListIsEmpty) {
    WebRTCConfig cfg;
    EXPECT_TRUE(cfg.ice_servers.empty());
}

TEST(WebRTCConfigTest, DefaultIceBackupCandidatePairPingIntervalIs1000ms) {
    WebRTCConfig cfg;
    EXPECT_EQ(cfg.ice_backup_candidate_pair_ping_interval_ms, 1000);
}

TEST(WebRTCConfigTest, DefaultIceConnectionReceivingTimeoutIs10000ms) {
    WebRTCConfig cfg;
    EXPECT_EQ(cfg.ice_connection_receiving_timeout_ms, 10000);
}

TEST(WebRTCConfigTest, DefaultVideoSenderLimitsAreReasonable) {
    WebRTCConfig cfg;
    EXPECT_EQ(cfg.video_max_framerate, 60);
    EXPECT_EQ(cfg.video_min_bitrate_bps, 2'000'000);
    EXPECT_EQ(cfg.video_max_bitrate_bps, 15'000'000);
    EXPECT_FALSE(cfg.disable_video_adaptation);
}

TEST(WebRTCConfigTest, AddingIceServersWorks) {
    WebRTCConfig cfg;
    IceServer stun;
    stun.uri = "stun:stun.l.google.com:19302";
    cfg.ice_servers.push_back(stun);

    ASSERT_EQ(cfg.ice_servers.size(), 1u);
    EXPECT_EQ(cfg.ice_servers[0].uri, "stun:stun.l.google.com:19302");
}

TEST(WebRTCConfigTest, IceTimeoutSettingsAreWritable) {
    WebRTCConfig cfg;
    cfg.ice_backup_candidate_pair_ping_interval_ms = 1500;
    cfg.ice_connection_receiving_timeout_ms = 12000;

    EXPECT_EQ(cfg.ice_backup_candidate_pair_ping_interval_ms, 1500);
    EXPECT_EQ(cfg.ice_connection_receiving_timeout_ms, 12000);
}

TEST(WebRTCConfigTest, VideoSenderLimitsAreWritable) {
    WebRTCConfig cfg;
    cfg.video_max_framerate = 30;
    cfg.video_min_bitrate_bps = 1'500'000;
    cfg.video_max_bitrate_bps = 8'000'000;
    cfg.disable_video_adaptation = true;

    EXPECT_EQ(cfg.video_max_framerate, 30);
    EXPECT_EQ(cfg.video_min_bitrate_bps, 1'500'000);
    EXPECT_EQ(cfg.video_max_bitrate_bps, 8'000'000);
    EXPECT_TRUE(cfg.disable_video_adaptation);
}

// ===========================================================================
// SignalingMessage
// ===========================================================================

TEST(SignalingMessageTest, SdpMessageHasCorrectType) {
    SignalingMessage msg;
    msg.type     = SignalingMessageType::kSdp;
    msg.sdp.type = SessionDescriptionType::kOffer;
    msg.sdp.sdp  = "v=0\r\n";

    EXPECT_EQ(msg.type, SignalingMessageType::kSdp);
    EXPECT_EQ(msg.sdp.type, SessionDescriptionType::kOffer);
}

TEST(SignalingMessageTest, IceCandidateMessageHasCorrectType) {
    SignalingMessage msg;
    msg.type                      = SignalingMessageType::kIceCandidate;
    msg.candidate.sdp_mid         = "0";
    msg.candidate.sdp_mline_index = 0;
    msg.candidate.sdp             = "candidate:x";

    EXPECT_EQ(msg.type, SignalingMessageType::kIceCandidate);
    EXPECT_EQ(msg.candidate.sdp_mid, "0");
}

} // namespace gamestream
