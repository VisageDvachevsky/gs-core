/// amf_init_test — Day 6 smoke test
///
/// Verifies the full AMF initialization chain:
///   DXGI capture init → get D3D11 device → AMF encoder init → print GPU info → clean shutdown
///
/// Expected output (RX 6700 XT):
///   [info] [capture] Found AMD GPU: AMD Radeon RX 6700 XT (adapter 0)
///   [info] [capture] Initialized successfully: 1920x1080
///   [info] [AMF] DLL loaded and factory created
///   [info] [AMF] Encoder configured: 1920x1080@60 fps, 15 Mbps, Baseline, Ultra-Low-Latency
///   [info] [AMF] Initialized successfully
///   [info] AMF init test PASSED

#include "dxgi_capture.h"
#include "amf_encoder.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>
#include <iostream>

using namespace gamestream;

static void setup_logging() {
    spdlog::set_level(spdlog::level::debug);
    spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
}

int main() {
    setup_logging();
    spdlog::info("=== AMF Init Test ===");

    // -----------------------------------------------------------------------
    // Step 1: Initialize DXGI capture on AMD GPU
    // -----------------------------------------------------------------------
    DXGICapture capture;
    auto init_result = capture.initialize(0);  // 0 = find AMD GPU automatically
    if (!init_result) {
        spdlog::error("DXGI capture init failed: {}", init_result.error());
        return EXIT_FAILURE;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    capture.get_resolution(width, height);
    spdlog::info("Capture resolution: {}x{}", width, height);

    ID3D11Device* device = capture.get_device();
    if (!device) {
        spdlog::error("get_device() returned nullptr after successful capture init");
        return EXIT_FAILURE;
    }
    spdlog::info("D3D11 device pointer: 0x{:016X}",
                 reinterpret_cast<uintptr_t>(device));

    // -----------------------------------------------------------------------
    // Step 2: Initialize AMF encoder with shared D3D11 device
    // -----------------------------------------------------------------------
    AMFEncoder encoder;

    EncoderConfig enc_config;
    enc_config.width       = width;
    enc_config.height      = height;
    enc_config.fps         = 60;
    enc_config.bitrate_bps = 15'000'000;

    auto enc_result = encoder.initialize(device, enc_config);
    if (!enc_result) {
        spdlog::error("AMF encoder init failed: {}", enc_result.error());
        return EXIT_FAILURE;
    }

    if (!encoder.is_initialized()) {
        spdlog::error("is_initialized() returned false after successful init");
        return EXIT_FAILURE;
    }

    // -----------------------------------------------------------------------
    // Step 3: Verify stats are at zero (no frames encoded yet)
    // -----------------------------------------------------------------------
    auto stats = encoder.get_stats();
    if (stats.frames_encoded != 0) {
        spdlog::error("Expected frames_encoded == 0 after init, got {}",
                      stats.frames_encoded);
        return EXIT_FAILURE;
    }

    // -----------------------------------------------------------------------
    // Step 4: Verify request_keyframe() doesn't crash
    // -----------------------------------------------------------------------
    encoder.request_keyframe();
    spdlog::info("request_keyframe() OK (flag set, will apply on next encode)");

    // -----------------------------------------------------------------------
    // Done
    // -----------------------------------------------------------------------
    spdlog::info("=== AMF init test PASSED ===");
    std::cout << "\nAMF init test PASSED. GPU encoder is ready.\n";
    return EXIT_SUCCESS;
}
