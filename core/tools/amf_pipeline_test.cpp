/// amf_pipeline_test — Day 8: multi-threaded capture + encode pipeline
///
/// Architecture:
///   [Encode Thread]  DXGI acquire → AMF encode → RingBuffer<EncodedFrame, 8>
///   [Writer Thread]                              RingBuffer → file write
///
/// Latency breakdown per frame:
///   Capture: Xms | Encode: Yms | Total: Zms
///
/// Checkpoint: stream_pipeline.h264 plays in VLC/ffplay at stable 60 FPS
///
/// Usage: amf_pipeline_test.exe [frame_count]
///   frame_count — frames to encode (default: 300 = ~5 sec at 60 FPS)

#include "dxgi_capture.h"
#include "amf_encoder.h"
#include "util/ring_buffer.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

using namespace gamestream;
using namespace std::chrono;

static void setup_logging() {
    spdlog::set_level(spdlog::level::info);
    spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
}

// ---------------------------------------------------------------------------
// Writer thread
// Drains the ring buffer and appends encoded data to the output file.
// Terminates when `done` is set and the buffer is empty.
// ---------------------------------------------------------------------------

static void writer_thread_fn(RingBuffer<EncodedFrame, 8>& write_queue,
                              std::atomic<bool>& encode_done,
                              const std::string& output_path,
                              std::atomic<uint64_t>& total_bytes_written)
{
    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        spdlog::error("[writer] Cannot open {}", output_path);
        return;
    }

    uint64_t bytes = 0;

    while (true) {
        EncodedFrame frame;
        if (write_queue.try_pop(frame)) {
            out.write(reinterpret_cast<const char*>(frame.data.data()),
                      static_cast<std::streamsize>(frame.data.size()));
            bytes += frame.data.size();
        } else {
            // Queue empty — check if encoder is done
            if (encode_done.load(std::memory_order_acquire)) {
                break;
            }
            std::this_thread::yield();
        }
    }

    // Drain any remaining frames after encode_done
    EncodedFrame frame;
    while (write_queue.try_pop(frame)) {
        out.write(reinterpret_cast<const char*>(frame.data.data()),
                  static_cast<std::streamsize>(frame.data.size()));
        bytes += frame.data.size();
    }

    out.close();
    total_bytes_written.store(bytes, std::memory_order_release);
    spdlog::info("[writer] Wrote {} bytes to {}", bytes, output_path);
}

// ---------------------------------------------------------------------------
// Encode loop (runs on the calling thread)
// ---------------------------------------------------------------------------

struct FrameTiming {
    double capture_ms = 0.0;
    double encode_ms  = 0.0;
    double total_ms   = 0.0;
    bool   is_keyframe = false;
    size_t byte_count  = 0;
};

static int encode_loop(DXGICapture& capture, AMFEncoder& encoder,
                       RingBuffer<EncodedFrame, 8>& write_queue,
                       uint32_t frame_count)
{
    double   sum_capture_ms = 0.0;
    double   sum_encode_ms  = 0.0;
    double   sum_total_ms   = 0.0;
    double   min_encode_ms  = 1e9;
    double   max_encode_ms  = 0.0;
    uint32_t frames_ok      = 0;
    uint32_t keyframes      = 0;

    encoder.request_keyframe();  // IDR on frame 0

    for (uint32_t i = 0; i < frame_count; ++i) {
        // ------------------------------------------------------------------
        // t0: before DXGI acquire
        // ------------------------------------------------------------------
        auto t0 = steady_clock::now();

        auto capture_result = capture.acquire_frame(33);
        if (!capture_result) {
            spdlog::debug("Frame {}: capture timeout, skipping", i);
            continue;
        }

        // ------------------------------------------------------------------
        // t1: frame acquired from DXGI
        // ------------------------------------------------------------------
        auto t1 = steady_clock::now();

        CaptureFrame frame = std::move(capture_result.value());
        capture.release_frame();

        // ------------------------------------------------------------------
        // Encode: SubmitInput + QueryOutput (inside AMFEncoder::encode)
        // ------------------------------------------------------------------
        auto encode_result = encoder.encode(frame);

        // ------------------------------------------------------------------
        // t2: AMF produced an output buffer
        // ------------------------------------------------------------------
        auto t2 = steady_clock::now();

        if (!encode_result) {
            spdlog::warn("Frame {}: encode failed: {}", i, encode_result.error());
            continue;
        }

        EncodedFrame ef = std::move(encode_result.value());

        // ------------------------------------------------------------------
        // Save metadata before moving ef into the write queue
        // ------------------------------------------------------------------
        const bool   is_kf      = ef.is_keyframe;
        const size_t byte_count = ef.data.size();

        if (is_kf) { keyframes++; }

        // Push to writer thread (spin if full — writer should always keep up)
        while (!write_queue.try_push(std::move(ef))) {
            std::this_thread::yield();
        }

        // ------------------------------------------------------------------
        // Timing
        // ------------------------------------------------------------------
        const double cap_ms = duration<double, std::milli>(t1 - t0).count();
        const double enc_ms = duration<double, std::milli>(t2 - t1).count();
        const double tot_ms = duration<double, std::milli>(t2 - t0).count();

        sum_capture_ms += cap_ms;
        sum_encode_ms  += enc_ms;
        sum_total_ms   += tot_ms;

        if (enc_ms < min_encode_ms) min_encode_ms = enc_ms;
        if (enc_ms > max_encode_ms) max_encode_ms = enc_ms;

        spdlog::info("Frame {:4}: {:3s} | {:6} bytes | "
                     "Capture: {:.2f}ms | Encode: {:.2f}ms | Total: {:.2f}ms",
                     i,
                     is_kf ? "IDR" : "  P",
                     byte_count,
                     cap_ms, enc_ms, tot_ms);

        ++frames_ok;
    }

    if (frames_ok == 0) {
        spdlog::error("No frames encoded successfully");
        return EXIT_FAILURE;
    }

    const double avg_cap = sum_capture_ms / frames_ok;
    const double avg_enc = sum_encode_ms  / frames_ok;
    const double avg_tot = sum_total_ms   / frames_ok;

    spdlog::info("=== Pipeline stats ({} frames, {} keyframes) ===", frames_ok, keyframes);
    spdlog::info("    Capture avg:  {:.2f} ms", avg_cap);
    spdlog::info("    Encode  avg:  {:.2f} ms  (min {:.2f} ms, max {:.2f} ms)",
                 avg_enc, min_encode_ms, max_encode_ms);
    spdlog::info("    Total   avg:  {:.2f} ms", avg_tot);

    if (avg_enc > 6.0) {
        spdlog::warn("Encode latency {:.2f} ms exceeds 6 ms target", avg_enc);
    } else {
        spdlog::info("Latency target MET: encode avg {:.2f} ms < 6 ms", avg_enc);
    }

    return EXIT_SUCCESS;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    setup_logging();

    const uint32_t frame_count = (argc > 1) ? static_cast<uint32_t>(std::stoul(argv[1])) : 300;
    spdlog::info("=== AMF Pipeline Test: {} frames ===", frame_count);

    // -----------------------------------------------------------------------
    // Step 1: DXGI capture
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
    // Step 2: AMF encoder
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
    // Step 3: Shared ring buffer + writer thread
    // -----------------------------------------------------------------------
    RingBuffer<EncodedFrame, 8> write_queue;
    std::atomic<bool>     encode_done{false};
    std::atomic<uint64_t> total_bytes{0};

    const std::string output_path = "stream_pipeline.h264";

    std::thread writer(writer_thread_fn,
                       std::ref(write_queue),
                       std::ref(encode_done),
                       std::cref(output_path),
                       std::ref(total_bytes));

    // -----------------------------------------------------------------------
    // Step 4: Encode loop (on the calling thread)
    // -----------------------------------------------------------------------
    const int result = encode_loop(capture, encoder, write_queue, frame_count);

    // Signal writer thread to finish and wait
    encode_done.store(true, std::memory_order_release);
    writer.join();

    spdlog::info("Output: {} ({} bytes)", output_path, total_bytes.load());

    if (result == EXIT_SUCCESS) {
        spdlog::info("=== amf_pipeline_test PASSED ===");
        std::cout << "\namf_pipeline_test PASSED. Verify with: ffplay " << output_path << "\n";
    }

    return result;
}
