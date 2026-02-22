/// wgc_capture_unit_test.cpp — unit tests for WGCCapture (no GPU required)
///
/// Strategy: WGCCapture guards all methods that access WinRT/D3D11 resources
/// behind `impl_->is_initialized_`.  The default constructor only allocates a
/// WGCCaptureImpl with null-initialized WinRT members — no WinRT apartment
/// or GPU calls occur, making these tests safe to run without graphics hardware.

#include <gtest/gtest.h>

#include "wgc_capture.h"

namespace gamestream {

// ---------------------------------------------------------------------------
// Default state — constructor must not call any WinRT / D3D11 APIs
// ---------------------------------------------------------------------------

TEST(WGCCaptureTest, DefaultStateNotInitialized) {
    WGCCapture capture;
    EXPECT_FALSE(capture.is_initialized());
}

TEST(WGCCaptureTest, GetResolutionBeforeInitReturnsZero) {
    WGCCapture capture;
    uint32_t w = 0xFFFF, h = 0xFFFF;
    capture.get_resolution(w, h);
    EXPECT_EQ(w, 0u);
    EXPECT_EQ(h, 0u);
}

TEST(WGCCaptureTest, GetDeviceBeforeInitReturnsNull) {
    WGCCapture capture;
    EXPECT_EQ(capture.get_device(), nullptr);
}

TEST(WGCCaptureTest, GetStatsBeforeInitReturnsDefaults) {
    WGCCapture capture;
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

TEST(WGCCaptureTest, AcquireFrameBeforeInitReturnsError) {
    WGCCapture capture;
    const auto r = capture.acquire_frame(0);
    EXPECT_FALSE(r);
    EXPECT_NE(r.error().find("Not initialized"), std::string::npos);
}

TEST(WGCCaptureTest, AcquireFrameBeforeInitDoesNotChangeStats) {
    WGCCapture capture;
    (void)capture.acquire_frame(0);
    EXPECT_EQ(capture.get_stats().frames_captured, 0u);
}

// ---------------------------------------------------------------------------
// release_frame() before init — must be a no-op, not crash
// The implementation sets held_frame_ = nullptr; held_frame_ is already
// null-initialized by the WinRT projection, so this is always safe.
// ---------------------------------------------------------------------------

TEST(WGCCaptureTest, ReleaseFrameBeforeInitDoesNotCrash) {
    WGCCapture capture;
    EXPECT_NO_FATAL_FAILURE(capture.release_frame());
}

TEST(WGCCaptureTest, ReleaseFrameBeforeInitDoesNotChangeState) {
    WGCCapture capture;
    capture.release_frame();
    EXPECT_FALSE(capture.is_initialized());
}

} // namespace gamestream
