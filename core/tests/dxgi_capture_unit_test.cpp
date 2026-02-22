/// dxgi_capture_unit_test.cpp — unit tests for DXGICapture (no GPU required)
///
/// Strategy: DXGICapture guards all methods that access GPU resources behind
/// `is_initialized()` (i.e. duplication_ != nullptr).  All tests exercise
/// these guards, which execute before any D3D11/DXGI hardware interaction.

#include <gtest/gtest.h>

#include "dxgi_capture.h"

namespace gamestream {

// ---------------------------------------------------------------------------
// Default state — constructor must not create any D3D11/DXGI objects
// ---------------------------------------------------------------------------

TEST(DXGICaptureTest, DefaultStateNotInitialized) {
    DXGICapture capture;
    EXPECT_FALSE(capture.is_initialized());
}

TEST(DXGICaptureTest, GetResolutionBeforeInitReturnsZero) {
    DXGICapture capture;
    uint32_t w = 0xFFFF, h = 0xFFFF;
    capture.get_resolution(w, h);
    EXPECT_EQ(w, 0u);
    EXPECT_EQ(h, 0u);
}

TEST(DXGICaptureTest, GetDeviceBeforeInitReturnsNull) {
    DXGICapture capture;
    EXPECT_EQ(capture.get_device(), nullptr);
}

TEST(DXGICaptureTest, GetStatsBeforeInitReturnsDefaults) {
    DXGICapture capture;
    const CaptureStats stats = capture.get_stats();
    EXPECT_EQ(stats.frames_captured, 0u);
    EXPECT_EQ(stats.frames_skipped,  0u);
    EXPECT_DOUBLE_EQ(stats.avg_capture_ms, 0.0);
    EXPECT_DOUBLE_EQ(stats.max_capture_ms, 0.0);
    // min_capture_ms is initialized to 999999 (sentinel)
    EXPECT_GT(stats.min_capture_ms, 1000.0);
}

// ---------------------------------------------------------------------------
// acquire_frame() before init — must return an error, not crash
// ---------------------------------------------------------------------------

TEST(DXGICaptureTest, AcquireFrameBeforeInitReturnsError) {
    DXGICapture capture;
    const auto r = capture.acquire_frame(0);
    EXPECT_FALSE(r);
    EXPECT_NE(r.error().find("Duplication not initialized"), std::string::npos);
}

TEST(DXGICaptureTest, AcquireFrameBeforeInitDoesNotChangeStats) {
    DXGICapture capture;
    (void)capture.acquire_frame(0);
    EXPECT_EQ(capture.get_stats().frames_captured, 0u);
}

// ---------------------------------------------------------------------------
// release_frame() before init — must be a no-op, not crash
// ---------------------------------------------------------------------------

TEST(DXGICaptureTest, ReleaseFrameBeforeInitDoesNotCrash) {
    DXGICapture capture;
    EXPECT_NO_FATAL_FAILURE(capture.release_frame());
}

TEST(DXGICaptureTest, ReleaseFrameBeforeInitDoesNotChangeState) {
    DXGICapture capture;
    capture.release_frame();
    EXPECT_FALSE(capture.is_initialized());
}

} // namespace gamestream
