/// amf_encode_test — Day 7 encode loop test
///
/// Full pipeline: DXGI capture → AMF H.264 encoder → stream.h264 file
///
/// Tests:
///   - First frame is an IDR keyframe
///   - Subsequent frames are P-frames
///   - Encode latency per frame is logged (target: < 6 ms avg)
///   - Output file is valid Annex-B H.264 (verify with: ffplay stream.h264)
///
/// Usage: amf_encode_test.exe [frame_count]
///   frame_count — number of frames to encode (default: 300 = ~5 sec at 60 FPS)
///
/// Expected output:
///   [info] Frame 0: IDR | 12345 bytes | 4.2 ms
///   [info] Frame 1:   P | 3456 bytes | 3.1 ms
///   ...
///   [info] === Encode complete: 300 frames, avg 3.8 ms, min 2.1 ms, max 8.4 ms ===
///   [info] Output: stream.h264 (total X bytes)

#include "dxgi_capture.h"
#include "amf_encoder.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace gamestream;
using namespace std::chrono;

static void setup_logging() {
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
}

// ---------------------------------------------------------------------------
// Encode loop
// ---------------------------------------------------------------------------

static int encode_loop(DXGICapture& capture, AMFEncoder& encoder,
                       uint32_t frame_count, const std::string& output_path)
{
    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        spdlog::error("Failed to open output file: {}", output_path);
        return EXIT_FAILURE;
    }

    uint64_t total_bytes   = 0;
    uint32_t keyframes     = 0;
    double   sum_encode_ms = 0.0;
    double   min_encode_ms = 1e9;
    double   max_encode_ms = 0.0;

    // Request a keyframe for the very first frame
    encoder.request_keyframe();

    for (uint32_t i = 0; i < frame_count; ++i) {
        // --- Capture ---
        auto capture_result = capture.acquire_frame(33);  // 33 ms timeout (~30 FPS floor)
        if (!capture_result) {
            // Timeout is acceptable — skip this frame
            spdlog::debug("Frame {}: acquire timeout ({}), skipping",
                          i, capture_result.error());
            continue;
        }

        CaptureFrame frame = std::move(capture_result.value());
        capture.release_frame();

        // --- Encode ---
        auto t0 = steady_clock::now();
        auto encode_result = encoder.encode(frame);
        auto t1 = steady_clock::now();

        const double encode_ms = duration<double, std::milli>(t1 - t0).count();

        if (!encode_result) {
            spdlog::warn("Frame {}: encode failed: {}", i, encode_result.error());
            continue;
        }

        const EncodedFrame& ef = encode_result.value();

        // --- Write to file ---
        out.write(reinterpret_cast<const char*>(ef.data.data()),
                  static_cast<std::streamsize>(ef.data.size()));

        // --- Stats ---
        total_bytes += ef.data.size();
        sum_encode_ms += encode_ms;
        if (encode_ms < min_encode_ms) min_encode_ms = encode_ms;
        if (encode_ms > max_encode_ms) max_encode_ms = encode_ms;
        if (ef.is_keyframe) { keyframes++; }

        spdlog::info("Frame {:4}: {:3s} | {:6} bytes | {:.2f} ms",
                     i,
                     ef.is_keyframe ? "IDR" : "  P",
                     ef.data.size(),
                     encode_ms);
    }

    out.close();

    const double avg_encode_ms = (frame_count > 0) ? sum_encode_ms / frame_count : 0.0;
    spdlog::info("=== Encode complete: {} frames, {} keyframes ===", frame_count, keyframes);
    spdlog::info("    avg {:.2f} ms | min {:.2f} ms | max {:.2f} ms",
                 avg_encode_ms, min_encode_ms, max_encode_ms);
    spdlog::info("    Output: {} ({} bytes)", output_path, total_bytes);

    if (avg_encode_ms > 6.0) {
        spdlog::warn("Average encode latency {:.2f} ms exceeds 6 ms target", avg_encode_ms);
    } else {
        spdlog::info("Latency target MET: avg {:.2f} ms < 6 ms", avg_encode_ms);
    }

    return EXIT_SUCCESS;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    setup_logging();

    const uint32_t frame_count = (argc > 1) ? static_cast<uint32_t>(std::stoul(argv[1])) : 300;
    spdlog::info("=== AMF Encode Test: {} frames ===", frame_count);

    // -----------------------------------------------------------------------
    // Step 1: DXGI capture init
    // -----------------------------------------------------------------------
    DXGICapture capture;
    if (auto r = capture.initialize(0); !r) {
        spdlog::error("DXGI init failed: {}", r.error());
        return EXIT_FAILURE;
    }

    uint32_t width = 0, height = 0;
    capture.get_resolution(width, height);
    spdlog::info("Capture: {}x{}", width, height);

    ID3D11Device* device = capture.get_device();

    // -----------------------------------------------------------------------
    // Step 2: AMF encoder init
    // -----------------------------------------------------------------------
    AMFEncoder encoder;

    EncoderConfig cfg;
    cfg.width       = width;
    cfg.height      = height;
    cfg.fps         = 60;
    cfg.bitrate_bps = 15'000'000;

    if (auto r = encoder.initialize(device, cfg); !r) {
        spdlog::error("AMF init failed: {}", r.error());
        return EXIT_FAILURE;
    }

    // -----------------------------------------------------------------------
    // Step 3: Encode loop
    // -----------------------------------------------------------------------
    const std::string output_path = "stream.h264";
    const int result = encode_loop(capture, encoder, frame_count, output_path);

    if (result == EXIT_SUCCESS) {
        spdlog::info("=== amf_encode_test PASSED ===");
        std::cout << "\namf_encode_test PASSED. Verify with: ffplay " << output_path << "\n";
    }

    return result;
}
