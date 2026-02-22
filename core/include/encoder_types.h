#pragma once

#include <cstdint>
#include <vector>

namespace gamestream {

/// A single encoded video frame produced by IEncoder.
///
/// Data format: H.264 Annex-B (start codes 0x00 0x00 0x00 0x01 before each NAL unit).
/// This is the format expected by WebRTC's RTP packetizer (stage 3).
///
/// Lifetime: the caller owns the data vector; encoder retains no reference after encode() returns.
struct EncodedFrame {
    std::vector<uint8_t> data;  ///< Annex-B bitstream (SPS/PPS prepended on keyframes)
    bool     is_keyframe = false;  ///< True for IDR frames (safe to start decoding here)
    int64_t  pts_us      = 0;      ///< Presentation timestamp, microseconds (monotonic clock)
};

/// Configuration supplied once at IEncoder::initialize() time.
/// All fields have production-ready defaults for 1080p60 game streaming.
struct EncoderConfig {
    uint32_t width       = 1920;
    uint32_t height      = 1080;
    uint32_t fps         = 60;
    uint32_t bitrate_bps = 15'000'000;  ///< 15 Mbps — adequate for 1080p60 Baseline H.264

    // H.264 Baseline / ultra-low-latency tuning (per development plan, stage 2).
    // These are intentionally not runtime-configurable at this stage (YAGNI).
    // When HEVC or AV1 support is added (stage 8+), extend this struct.
};

/// Cumulative encoder performance statistics.
/// Exposed via IEncoder::get_stats() for monitoring and latency budgeting.
struct EncoderStats {
    uint64_t frames_encoded   = 0;
    uint64_t keyframes_encoded = 0;
    uint64_t bytes_encoded    = 0;

    double avg_encode_ms = 0.0;
    double min_encode_ms = 999999.0;
    double max_encode_ms = 0.0;
};

} // namespace gamestream
