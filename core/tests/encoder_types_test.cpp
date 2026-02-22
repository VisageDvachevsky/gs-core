#include <gtest/gtest.h>

#include "encoder_types.h"

#include <cstdint>
#include <vector>

namespace gamestream {

// ---------------------------------------------------------------------------
// EncodedFrame tests
// ---------------------------------------------------------------------------

TEST(EncodedFrameTest, DefaultConstruction) {
    EncodedFrame frame;
    EXPECT_TRUE(frame.data.empty());
    EXPECT_FALSE(frame.is_keyframe);
    EXPECT_EQ(frame.pts_us, 0);
}

TEST(EncodedFrameTest, NonKeyframeByDefault) {
    // Regression: encoder must set is_keyframe explicitly for IDR frames.
    // The default must be false so non-keyframes don't accidentally get treated as IDR.
    EncodedFrame frame;
    EXPECT_FALSE(frame.is_keyframe);
}

TEST(EncodedFrameTest, SetKeyframe) {
    EncodedFrame frame;
    frame.is_keyframe = true;
    frame.pts_us = 1'000'000;  // 1 second
    EXPECT_TRUE(frame.is_keyframe);
    EXPECT_EQ(frame.pts_us, 1'000'000);
}

TEST(EncodedFrameTest, DataStorageRoundtrip) {
    // Verify data vector is a regular std::vector<uint8_t>.
    EncodedFrame frame;
    frame.data = {0x00, 0x00, 0x00, 0x01, 0x67};  // Annex-B SPS NAL start code + type byte
    ASSERT_EQ(frame.data.size(), 5u);
    EXPECT_EQ(frame.data[0], 0x00);
    EXPECT_EQ(frame.data[3], 0x01);
    EXPECT_EQ(frame.data[4], 0x67);  // NAL type 0x67 = SPS
}

TEST(EncodedFrameTest, MoveSemantics) {
    EncodedFrame src;
    src.data.assign(1024, 0xAB);
    src.is_keyframe = true;
    src.pts_us = 500;

    EncodedFrame dst = std::move(src);

    EXPECT_EQ(dst.data.size(), 1024u);
    EXPECT_EQ(dst.data[0], 0xAB);
    EXPECT_TRUE(dst.is_keyframe);
    EXPECT_EQ(dst.pts_us, 500);

    // src is in a valid but unspecified state after move
    EXPECT_TRUE(src.data.empty());
}

TEST(EncodedFrameTest, MoveAssignment) {
    EncodedFrame src;
    src.data = {0x01, 0x02, 0x03};
    src.is_keyframe = false;
    src.pts_us = 33'333;

    EncodedFrame dst;
    dst = std::move(src);

    ASSERT_EQ(dst.data.size(), 3u);
    EXPECT_EQ(dst.data[1], 0x02);
    EXPECT_FALSE(dst.is_keyframe);
    EXPECT_EQ(dst.pts_us, 33'333);
}

TEST(EncodedFrameTest, NegativePtsIsValid) {
    // pts_us is int64_t — negative values should not cause issues (e.g. pre-roll frames).
    EncodedFrame frame;
    frame.pts_us = -1;
    EXPECT_EQ(frame.pts_us, -1);
}

TEST(EncodedFrameTest, LargePtsValue) {
    // Streams running for hours must not overflow pts_us.
    // 24 hours = 86400 seconds = 86'400'000'000 µs — well within int64_t.
    EncodedFrame frame;
    frame.pts_us = 86'400'000'000LL;
    EXPECT_EQ(frame.pts_us, 86'400'000'000LL);
}

TEST(EncodedFrameTest, EmptyDataIsValidForPipelineTests) {
    // Some unit tests use empty frames as placeholders — must not crash.
    EncodedFrame frame;
    EXPECT_TRUE(frame.data.empty());
    EXPECT_EQ(frame.data.size(), 0u);
}

// ---------------------------------------------------------------------------
// EncoderConfig tests
// ---------------------------------------------------------------------------

TEST(EncoderConfigTest, DefaultValues) {
    EncoderConfig config;
    EXPECT_EQ(config.width,       1920u);
    EXPECT_EQ(config.height,      1080u);
    EXPECT_EQ(config.fps,         60u);
    EXPECT_EQ(config.bitrate_bps, 15'000'000u);
}

TEST(EncoderConfigTest, CustomValues) {
    EncoderConfig config;
    config.width       = 2560;
    config.height      = 1440;
    config.fps         = 144;
    config.bitrate_bps = 30'000'000;

    EXPECT_EQ(config.width,       2560u);
    EXPECT_EQ(config.height,      1440u);
    EXPECT_EQ(config.fps,         144u);
    EXPECT_EQ(config.bitrate_bps, 30'000'000u);
}

TEST(EncoderConfigTest, BitrateUnitConsistency) {
    // Ensure bitrate_bps is in bits-per-second, not Mbps.
    // 15 Mbps = 15,000,000 bps.
    EncoderConfig config;
    EXPECT_EQ(config.bitrate_bps, 15'000'000u);
    EXPECT_GT(config.bitrate_bps, 1'000'000u);  // > 1 Mbps
    EXPECT_LT(config.bitrate_bps, 100'000'000u);  // < 100 Mbps (sanity cap)
}

TEST(EncoderConfigTest, Aggregate) {
    // EncoderConfig is an aggregate struct — aggregate initialisation must work.
    EncoderConfig config{3840, 2160, 30, 50'000'000};
    EXPECT_EQ(config.width,       3840u);
    EXPECT_EQ(config.height,      2160u);
    EXPECT_EQ(config.fps,         30u);
    EXPECT_EQ(config.bitrate_bps, 50'000'000u);
}

// ---------------------------------------------------------------------------
// EncoderStats tests
// ---------------------------------------------------------------------------

TEST(EncoderStatsTest, DefaultValues) {
    EncoderStats stats;
    EXPECT_EQ(stats.frames_encoded,    0u);
    EXPECT_EQ(stats.keyframes_encoded, 0u);
    EXPECT_EQ(stats.bytes_encoded,     0u);
    EXPECT_DOUBLE_EQ(stats.avg_encode_ms, 0.0);
    EXPECT_DOUBLE_EQ(stats.max_encode_ms, 0.0);
    // min_encode_ms is initialised to a sentinel (999999) so the first real sample
    // always replaces it — verify this sentinel is positive and large.
    EXPECT_GT(stats.min_encode_ms, 1000.0);
}

TEST(EncoderStatsTest, IncrementalUpdate) {
    // Simulate the accumulation pattern used inside the encoder.
    EncoderStats stats;
    stats.frames_encoded = 1;
    stats.avg_encode_ms  = 4.2;
    stats.min_encode_ms  = 4.2;
    stats.max_encode_ms  = 4.2;
    stats.bytes_encoded  = 12345;

    stats.frames_encoded++;
    const double second_sample = 5.8;
    stats.avg_encode_ms = (stats.avg_encode_ms * (stats.frames_encoded - 1) + second_sample)
                        / stats.frames_encoded;
    if (second_sample < stats.min_encode_ms) stats.min_encode_ms = second_sample;
    if (second_sample > stats.max_encode_ms) stats.max_encode_ms = second_sample;

    EXPECT_EQ(stats.frames_encoded, 2u);
    EXPECT_NEAR(stats.avg_encode_ms, 5.0, 0.01);
    EXPECT_NEAR(stats.min_encode_ms, 4.2, 0.01);
    EXPECT_NEAR(stats.max_encode_ms, 5.8, 0.01);
}

TEST(EncoderStatsTest, KeyframeCount) {
    EncoderStats stats;
    stats.keyframes_encoded = 5;
    EXPECT_EQ(stats.keyframes_encoded, 5u);
}

TEST(EncoderStatsTest, BytesEncodedLargeStream) {
    // A 2-hour stream at 15 Mbps ≈ 13.5 GB — uint64_t must not overflow.
    constexpr uint64_t kTwoHoursAt15Mbps = 2ULL * 3600ULL * 15'000'000ULL / 8ULL;  // ~13.5 GB
    EncoderStats stats;
    stats.bytes_encoded = kTwoHoursAt15Mbps;
    EXPECT_EQ(stats.bytes_encoded, kTwoHoursAt15Mbps);
    EXPECT_LT(stats.bytes_encoded, std::numeric_limits<uint64_t>::max());
}

} // namespace gamestream
