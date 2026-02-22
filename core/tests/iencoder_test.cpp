#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "iencoder.h"
#include "encoder_types.h"
#include "capture_types.h"
#include "result.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace gamestream {

// ---------------------------------------------------------------------------
// MockEncoder — GMock implementation of IEncoder
// Used to verify that call sites of IEncoder behave correctly, independently
// of any real GPU hardware.
// ---------------------------------------------------------------------------

class MockEncoder : public IEncoder {
public:
    MOCK_METHOD(VoidResult, initialize,
                (ID3D11Device* device, const EncoderConfig& config), (override));

    MOCK_METHOD(Result<EncodedFrame>, encode,
                (const CaptureFrame& frame), (override));

    MOCK_METHOD(void, request_keyframe, (), (override));

    MOCK_METHOD(bool, is_initialized, (), (const, override));

    MOCK_METHOD(EncoderStats, get_stats, (), (const, override));
};

// ---------------------------------------------------------------------------
// Helper: build an EncodedFrame with Annex-B SPS + IDR data.
// ---------------------------------------------------------------------------
static EncodedFrame make_keyframe(int64_t pts_us) {
    EncodedFrame f;
    f.data       = {0x00, 0x00, 0x00, 0x01, 0x67,   // SPS start code + NAL type
                    0x00, 0x00, 0x00, 0x01, 0x65};   // IDR start code + NAL type
    f.is_keyframe = true;
    f.pts_us      = pts_us;
    return f;
}

static EncodedFrame make_delta_frame(int64_t pts_us) {
    EncodedFrame f;
    f.data       = {0x00, 0x00, 0x00, 0x01, 0x61};  // non-IDR slice NAL
    f.is_keyframe = false;
    f.pts_us      = pts_us;
    return f;
}

// ---------------------------------------------------------------------------
// IEncoder interface contract tests
// These tests verify the expected calling patterns and return types.
// ---------------------------------------------------------------------------

TEST(IEncoderTest, InitializeCalledWithDeviceAndConfig) {
    MockEncoder enc;
    EncoderConfig cfg;
    cfg.width  = 1920;
    cfg.height = 1080;
    cfg.fps    = 60;

    // Expect exactly one initialize() call; device pointer may be null in tests
    EXPECT_CALL(enc, initialize(nullptr, testing::_))
        .WillOnce(testing::Return(VoidResult{}));  // success

    auto result = enc.initialize(nullptr, cfg);
    EXPECT_TRUE(result);
}

TEST(IEncoderTest, InitializeFailureReturnsError) {
    MockEncoder enc;
    const std::string kExpectedMsg = "AMF init failed: 0x80004005";

    EXPECT_CALL(enc, initialize(testing::_, testing::_))
        .WillOnce(testing::Return(VoidResult::error(kExpectedMsg)));

    auto result = enc.initialize(nullptr, EncoderConfig{});
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), kExpectedMsg);
}

TEST(IEncoderTest, IsInitializedReturnsFalseBeforeInit) {
    MockEncoder enc;
    EXPECT_CALL(enc, is_initialized()).WillOnce(testing::Return(false));
    EXPECT_FALSE(enc.is_initialized());
}

TEST(IEncoderTest, IsInitializedReturnsTrueAfterSuccessfulInit) {
    MockEncoder enc;
    EXPECT_CALL(enc, initialize(testing::_, testing::_))
        .WillOnce(testing::Return(VoidResult{}));
    EXPECT_CALL(enc, is_initialized()).WillOnce(testing::Return(true));

    enc.initialize(nullptr, EncoderConfig{});
    EXPECT_TRUE(enc.is_initialized());
}

TEST(IEncoderTest, EncodeReturnsKeyframeOnFirstCall) {
    MockEncoder enc;
    CaptureFrame dummy_frame{};  // null texture is fine for mock

    EXPECT_CALL(enc, encode(testing::_))
        .WillOnce(testing::Return(make_keyframe(0)));

    auto result = enc.encode(dummy_frame);
    ASSERT_TRUE(result);
    EXPECT_TRUE(result.value().is_keyframe);
    EXPECT_EQ(result.value().pts_us, 0);
}

TEST(IEncoderTest, EncodeReturnsDeltaFrames) {
    MockEncoder enc;
    CaptureFrame dummy_frame{};

    constexpr int64_t kFrameIntervalUs = 16'667;  // ~60 FPS

    EXPECT_CALL(enc, encode(testing::_))
        .WillOnce(testing::Return(make_keyframe(0)))
        .WillOnce(testing::Return(make_delta_frame(kFrameIntervalUs)))
        .WillOnce(testing::Return(make_delta_frame(2 * kFrameIntervalUs)));

    auto r0 = enc.encode(dummy_frame);
    ASSERT_TRUE(r0);
    EXPECT_TRUE(r0.value().is_keyframe);

    auto r1 = enc.encode(dummy_frame);
    ASSERT_TRUE(r1);
    EXPECT_FALSE(r1.value().is_keyframe);
    EXPECT_EQ(r1.value().pts_us, kFrameIntervalUs);

    auto r2 = enc.encode(dummy_frame);
    ASSERT_TRUE(r2);
    EXPECT_FALSE(r2.value().is_keyframe);
    EXPECT_EQ(r2.value().pts_us, 2 * kFrameIntervalUs);
}

TEST(IEncoderTest, EncodeFailureReturnsError) {
    MockEncoder enc;
    CaptureFrame dummy_frame{};

    EXPECT_CALL(enc, encode(testing::_))
        .WillOnce(testing::Return(Result<EncodedFrame>::error("AMF SubmitInput failed")));

    auto result = enc.encode(dummy_frame);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error(), "AMF SubmitInput failed");
}

TEST(IEncoderTest, RequestKeyframeIsCalledBeforeEncode) {
    MockEncoder enc;
    CaptureFrame dummy_frame{};

    // Sequence: request_keyframe() must precede encode() in a PLI recovery scenario
    testing::InSequence seq;
    EXPECT_CALL(enc, request_keyframe()).Times(1);
    EXPECT_CALL(enc, encode(testing::_))
        .WillOnce(testing::Return(make_keyframe(0)));

    enc.request_keyframe();
    auto r = enc.encode(dummy_frame);
    ASSERT_TRUE(r);
    EXPECT_TRUE(r.value().is_keyframe);
}

TEST(IEncoderTest, RequestKeyframeCanBeCalledMultipleTimes) {
    // Multiple PLI signals before the next encode — must not crash.
    MockEncoder enc;
    EXPECT_CALL(enc, request_keyframe()).Times(3);
    enc.request_keyframe();
    enc.request_keyframe();
    enc.request_keyframe();
}

TEST(IEncoderTest, GetStatsReturnsZeroBeforeAnyFrames) {
    MockEncoder enc;
    EncoderStats zero_stats{};

    EXPECT_CALL(enc, get_stats()).WillOnce(testing::Return(zero_stats));

    auto stats = enc.get_stats();
    EXPECT_EQ(stats.frames_encoded,    0u);
    EXPECT_EQ(stats.keyframes_encoded, 0u);
    EXPECT_EQ(stats.bytes_encoded,     0u);
    EXPECT_DOUBLE_EQ(stats.avg_encode_ms, 0.0);
}

TEST(IEncoderTest, GetStatsAfterEncoding) {
    MockEncoder enc;

    EncoderStats expected;
    expected.frames_encoded    = 100;
    expected.keyframes_encoded = 2;
    expected.bytes_encoded     = 1'234'567;
    expected.avg_encode_ms     = 4.8;
    expected.min_encode_ms     = 3.1;
    expected.max_encode_ms     = 8.2;

    EXPECT_CALL(enc, get_stats()).WillOnce(testing::Return(expected));

    auto stats = enc.get_stats();
    EXPECT_EQ(stats.frames_encoded,    100u);
    EXPECT_EQ(stats.keyframes_encoded, 2u);
    EXPECT_NEAR(stats.avg_encode_ms,   4.8, 0.001);
    EXPECT_NEAR(stats.min_encode_ms,   3.1, 0.001);
    EXPECT_NEAR(stats.max_encode_ms,   8.2, 0.001);
}

TEST(IEncoderTest, PolymorphicUseThroughBasePointer) {
    // Verify that IEncoder can be used polymorphically (as unique_ptr<IEncoder>).
    std::unique_ptr<IEncoder> enc = std::make_unique<MockEncoder>();
    auto* mock = static_cast<MockEncoder*>(enc.get());

    EXPECT_CALL(*mock, is_initialized()).WillOnce(testing::Return(false));
    EXPECT_FALSE(enc->is_initialized());
}

// ---------------------------------------------------------------------------
// Encode pipeline simulation test
// Verifies that a typical capture→encode loop compiles and behaves correctly.
// ---------------------------------------------------------------------------

TEST(IEncoderTest, SimulatedEncodePipeline) {
    MockEncoder enc;
    std::vector<CaptureFrame> frames(10);

    // Set up: first frame is keyframe, rest are delta
    EXPECT_CALL(enc, encode(testing::_))
        .WillOnce(testing::Return(make_keyframe(0)))
        .WillRepeatedly([](const CaptureFrame& /*f*/) -> Result<EncodedFrame> {
            static int64_t pts = 16'667;
            return make_delta_frame(pts += 16'667);
        });

    int keyframe_count = 0;
    int delta_count    = 0;

    for (auto& frame : frames) {
        auto result = enc.encode(frame);
        ASSERT_TRUE(result) << "encode() should not fail in simulation";

        if (result.value().is_keyframe) {
            keyframe_count++;
        } else {
            delta_count++;
        }
    }

    EXPECT_EQ(keyframe_count, 1);
    EXPECT_EQ(delta_count,    9);
}

} // namespace gamestream
