/// @file capture_types_test.cpp
/// Unit tests for capture_types.h — CaptureStats, CaptureConfig, CaptureFrame.
/// Target: 100% line coverage of every executable line in capture_types.h.
///
/// Note: CaptureFrame contains Microsoft::WRL::ComPtr<ID3D11Texture2D>.
/// Tests verify default construction and field assignment without touching
/// any real D3D11 resources.

#include "capture_types.h"

#include <gtest/gtest.h>

using namespace gamestream;

// ===========================================================================
// CaptureStats
// ===========================================================================

// Verify all in-class initializers fire correctly (covers lines with = 0, = 0.0, etc.)
TEST(CaptureStatsTest, DefaultValues) {
    CaptureStats stats;
    EXPECT_EQ(stats.frames_captured, 0u);
    EXPECT_EQ(stats.frames_skipped, 0u);
    EXPECT_DOUBLE_EQ(stats.avg_capture_ms, 0.0);
    EXPECT_DOUBLE_EQ(stats.min_capture_ms, 999999.0);
    EXPECT_DOUBLE_EQ(stats.max_capture_ms, 0.0);
}

// Verify every field is independently writable (struct layout is correct)
TEST(CaptureStatsTest, FieldsAreAssignable) {
    CaptureStats stats;
    stats.frames_captured = 100;
    stats.frames_skipped  = 5;
    stats.avg_capture_ms  = 8.5;
    stats.min_capture_ms  = 2.1;
    stats.max_capture_ms  = 15.3;

    EXPECT_EQ(stats.frames_captured, 100u);
    EXPECT_EQ(stats.frames_skipped, 5u);
    EXPECT_DOUBLE_EQ(stats.avg_capture_ms, 8.5);
    EXPECT_DOUBLE_EQ(stats.min_capture_ms, 2.1);
    EXPECT_DOUBLE_EQ(stats.max_capture_ms, 15.3);
}

// ===========================================================================
// CaptureConfig
// ===========================================================================

// Verify all in-class initializers (adapter_index=0, output_index=0, find_amd_gpu=true)
TEST(CaptureConfigTest, DefaultValues) {
    CaptureConfig config;
    EXPECT_EQ(config.adapter_index, 0u);
    EXPECT_EQ(config.output_index, 0u);
    EXPECT_TRUE(config.find_amd_gpu);
}

// Verify every field is independently writable
TEST(CaptureConfigTest, FieldsAreAssignable) {
    CaptureConfig config;
    config.adapter_index = 2;
    config.output_index  = 1;
    config.find_amd_gpu  = false;

    EXPECT_EQ(config.adapter_index, 2u);
    EXPECT_EQ(config.output_index, 1u);
    EXPECT_FALSE(config.find_amd_gpu);
}

// ===========================================================================
// CaptureFrame
// ===========================================================================

// Value-initialization zeroes integral members and default-constructs ComPtr (null)
TEST(CaptureFrameTest, DefaultConstruction) {
    CaptureFrame frame{};
    EXPECT_EQ(frame.texture.Get(), nullptr);
    EXPECT_EQ(frame.width, 0u);
    EXPECT_EQ(frame.height, 0u);
    EXPECT_EQ(frame.timestamp_us, 0u);
}

// Verify integral fields are independently writable (texture stays null)
TEST(CaptureFrameTest, IntegralFieldsAreAssignable) {
    CaptureFrame frame{};
    frame.width        = 1920;
    frame.height       = 1080;
    frame.timestamp_us = 12345678;

    EXPECT_EQ(frame.width, 1920u);
    EXPECT_EQ(frame.height, 1080u);
    EXPECT_EQ(frame.timestamp_us, 12345678u);
    EXPECT_EQ(frame.texture.Get(), nullptr);  // ComPtr untouched
}
