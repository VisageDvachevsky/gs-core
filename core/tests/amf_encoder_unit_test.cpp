/// amf_encoder_unit_test.cpp — unit tests for AMFEncoder (no GPU required)
///
/// Strategy: AMFEncoder validates preconditions before touching AMF/GPU resources.
/// All tests exercise code paths that execute before any hardware interaction,
/// making them safe to run in any CI environment.

#include <gtest/gtest.h>

#include "amf_encoder.h"
#include "capture_types.h"
#include "encoder_types.h"

namespace gamestream {

// ---------------------------------------------------------------------------
// Default state — constructor must not call AMF
// ---------------------------------------------------------------------------

TEST(AMFEncoderTest, DefaultStateNotInitialized) {
    AMFEncoder encoder;
    EXPECT_FALSE(encoder.is_initialized());
}

TEST(AMFEncoderTest, DefaultStatsAreZero) {
    AMFEncoder encoder;
    const EncoderStats stats = encoder.get_stats();
    EXPECT_EQ(stats.frames_encoded,    0u);
    EXPECT_EQ(stats.keyframes_encoded, 0u);
    EXPECT_EQ(stats.bytes_encoded,     0u);
    EXPECT_DOUBLE_EQ(stats.avg_encode_ms, 0.0);
    EXPECT_DOUBLE_EQ(stats.max_encode_ms, 0.0);
}

TEST(AMFEncoderTest, DefaultStatsMinSentinel) {
    // min_encode_ms is initialised to 999999 so the first real sample always
    // replaces it — verify the sentinel is positive and large.
    AMFEncoder encoder;
    const EncoderStats stats = encoder.get_stats();
    EXPECT_GT(stats.min_encode_ms, 1000.0);
}

// ---------------------------------------------------------------------------
// initialize() precondition checks
// ---------------------------------------------------------------------------

TEST(AMFEncoderTest, InitializeWithNullDeviceReturnsError) {
    AMFEncoder encoder;
    const EncoderConfig config;
    const auto r = encoder.initialize(nullptr, config);
    EXPECT_FALSE(r);
    EXPECT_NE(r.error().find("device must not be nullptr"), std::string::npos);
}

TEST(AMFEncoderTest, InitializeWithNullDeviceDoesNotInitialize) {
    AMFEncoder encoder;
    const EncoderConfig config;
    encoder.initialize(nullptr, config);
    EXPECT_FALSE(encoder.is_initialized());
}

// ---------------------------------------------------------------------------
// encode() precondition check — must not touch the frame before the guard
// ---------------------------------------------------------------------------

TEST(AMFEncoderTest, EncodeWithoutInitializeReturnsError) {
    AMFEncoder encoder;
    const CaptureFrame frame{};  // texture = nullptr; encode() bails out before touching it
    const auto r = encoder.encode(frame);
    EXPECT_FALSE(r);
    EXPECT_NE(r.error().find("not initialized"), std::string::npos);
}

TEST(AMFEncoderTest, EncodeWithoutInitializeDoesNotChangeStats) {
    AMFEncoder encoder;
    const CaptureFrame frame{};
    encoder.encode(frame);
    EXPECT_EQ(encoder.get_stats().frames_encoded, 0u);
}

// ---------------------------------------------------------------------------
// request_keyframe() — sets an atomic flag; must not crash before init
// ---------------------------------------------------------------------------

TEST(AMFEncoderTest, RequestKeyframeBeforeInitDoesNotCrash) {
    AMFEncoder encoder;
    EXPECT_NO_FATAL_FAILURE(encoder.request_keyframe());
}

TEST(AMFEncoderTest, RequestKeyframeDoesNotChangeInitializedState) {
    AMFEncoder encoder;
    encoder.request_keyframe();
    EXPECT_FALSE(encoder.is_initialized());
}

} // namespace gamestream
