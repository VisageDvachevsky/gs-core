#include <gtest/gtest.h>

#include "util/ring_buffer.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace gamestream {

// ---------------------------------------------------------------------------
// Basic push / pop contract
// ---------------------------------------------------------------------------

TEST(RingBufferTest, EmptyBufferPopFails) {
    RingBuffer<int, 4> buf;
    int value = -1;
    EXPECT_FALSE(buf.try_pop(value));
    EXPECT_EQ(value, -1);  // must not be modified on failure
}

TEST(RingBufferTest, PushAndPopSingleElement) {
    RingBuffer<int, 4> buf;
    EXPECT_TRUE(buf.try_push(42));
    int value = 0;
    EXPECT_TRUE(buf.try_pop(value));
    EXPECT_EQ(value, 42);
}

TEST(RingBufferTest, PopOnEmptyAfterDrainFails) {
    RingBuffer<int, 4> buf;
    EXPECT_TRUE(buf.try_push(1));
    int v;
    EXPECT_TRUE(buf.try_pop(v));
    EXPECT_FALSE(buf.try_pop(v));  // already drained
}

// ---------------------------------------------------------------------------
// Capacity: RingBuffer<T, N> holds N-1 elements
// ---------------------------------------------------------------------------

TEST(RingBufferTest, CapacityIsNMinusOne) {
    // RingBuffer<int, 4> must hold exactly 3 elements (N-1 = 3).
    EXPECT_EQ((RingBuffer<int, 4>::capacity()), 3u);
    EXPECT_EQ((RingBuffer<int, 8>::capacity()), 7u);
    EXPECT_EQ((RingBuffer<int, 2>::capacity()), 1u);
}

TEST(RingBufferTest, FullBufferRejectsPush) {
    RingBuffer<int, 4> buf;  // capacity = 3
    EXPECT_TRUE(buf.try_push(1));
    EXPECT_TRUE(buf.try_push(2));
    EXPECT_TRUE(buf.try_push(3));
    EXPECT_FALSE(buf.try_push(4));  // 4th push must fail
}

TEST(RingBufferTest, PushAfterPopOpensSlot) {
    RingBuffer<int, 4> buf;  // capacity = 3
    // Fill to capacity
    ASSERT_TRUE(buf.try_push(1));
    ASSERT_TRUE(buf.try_push(2));
    ASSERT_TRUE(buf.try_push(3));

    // Pop one to free a slot
    int v;
    ASSERT_TRUE(buf.try_pop(v));
    EXPECT_EQ(v, 1);

    // Now one push must succeed
    EXPECT_TRUE(buf.try_push(99));
    // Buffer is full again (3 → 2 → 3)
    EXPECT_FALSE(buf.try_push(100));
}

// ---------------------------------------------------------------------------
// FIFO ordering
// ---------------------------------------------------------------------------

TEST(RingBufferTest, FIFOOrder) {
    RingBuffer<int, 5> buf;  // capacity = 4
    ASSERT_TRUE(buf.try_push(10));
    ASSERT_TRUE(buf.try_push(20));
    ASSERT_TRUE(buf.try_push(30));

    int v;
    ASSERT_TRUE(buf.try_pop(v)); EXPECT_EQ(v, 10);
    ASSERT_TRUE(buf.try_pop(v)); EXPECT_EQ(v, 20);
    ASSERT_TRUE(buf.try_pop(v)); EXPECT_EQ(v, 30);
    EXPECT_FALSE(buf.try_pop(v));
}

TEST(RingBufferTest, FIFOOrderAfterWrap) {
    // Push elements until wrap-around to verify FIFO is preserved past the ring boundary.
    RingBuffer<int, 4> buf;  // capacity = 3
    int v;

    // First batch
    ASSERT_TRUE(buf.try_push(1));
    ASSERT_TRUE(buf.try_push(2));
    ASSERT_TRUE(buf.try_pop(v)); EXPECT_EQ(v, 1);

    // Second batch — head wraps around
    ASSERT_TRUE(buf.try_push(3));
    ASSERT_TRUE(buf.try_push(4));

    ASSERT_TRUE(buf.try_pop(v)); EXPECT_EQ(v, 2);
    ASSERT_TRUE(buf.try_pop(v)); EXPECT_EQ(v, 3);
    ASSERT_TRUE(buf.try_pop(v)); EXPECT_EQ(v, 4);
    EXPECT_FALSE(buf.try_pop(v));
}

// ---------------------------------------------------------------------------
// State predicates: empty() / full() / size()
// ---------------------------------------------------------------------------

TEST(RingBufferTest, EmptyPredicateOnNewBuffer) {
    RingBuffer<int, 4> buf;
    EXPECT_TRUE(buf.empty());
    EXPECT_FALSE(buf.full());
    EXPECT_EQ(buf.size(), 0u);
}

TEST(RingBufferTest, EmptyPredicateAfterPushAndPop) {
    RingBuffer<int, 4> buf;
    buf.try_push(1);
    EXPECT_FALSE(buf.empty());
    int v;
    buf.try_pop(v);
    EXPECT_TRUE(buf.empty());
}

TEST(RingBufferTest, FullPredicate) {
    RingBuffer<int, 4> buf;  // capacity = 3
    EXPECT_FALSE(buf.full());
    buf.try_push(1);
    buf.try_push(2);
    EXPECT_FALSE(buf.full());
    buf.try_push(3);
    EXPECT_TRUE(buf.full());
}

TEST(RingBufferTest, SizeCountsElements) {
    RingBuffer<int, 5> buf;  // capacity = 4
    EXPECT_EQ(buf.size(), 0u);
    buf.try_push(1); EXPECT_EQ(buf.size(), 1u);
    buf.try_push(2); EXPECT_EQ(buf.size(), 2u);
    buf.try_push(3); EXPECT_EQ(buf.size(), 3u);
    int v;
    buf.try_pop(v);  EXPECT_EQ(buf.size(), 2u);
    buf.try_pop(v);  EXPECT_EQ(buf.size(), 1u);
    buf.try_pop(v);  EXPECT_EQ(buf.size(), 0u);
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

TEST(RingBufferTest, MoveOnlyTypeWorks) {
    // std::unique_ptr is move-only — verify the buffer works with such types.
    RingBuffer<std::unique_ptr<int>, 4> buf;

    EXPECT_TRUE(buf.try_push(std::make_unique<int>(42)));
    EXPECT_TRUE(buf.try_push(std::make_unique<int>(100)));

    std::unique_ptr<int> v;
    EXPECT_TRUE(buf.try_pop(v));
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(*v, 42);

    EXPECT_TRUE(buf.try_pop(v));
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(*v, 100);
}

TEST(RingBufferTest, MovedFromSlotIsOverwritten) {
    // After try_pop() moves out an element, a subsequent try_push() to the same
    // slot (after the ring wraps) must overwrite it cleanly.
    RingBuffer<std::string, 3> buf;  // capacity = 2

    ASSERT_TRUE(buf.try_push(std::string("hello")));
    std::string v;
    ASSERT_TRUE(buf.try_pop(v));
    EXPECT_EQ(v, "hello");

    // Slot 0 is now moved-from.  Push again to the same slot after wrap.
    ASSERT_TRUE(buf.try_push(std::string("world")));
    ASSERT_TRUE(buf.try_pop(v));
    EXPECT_EQ(v, "world");
}

// ---------------------------------------------------------------------------
// Minimum capacity (N=2, holds 1 element)
// ---------------------------------------------------------------------------

TEST(RingBufferTest, MinimumCapacityBuffer) {
    RingBuffer<int, 2> buf;  // capacity = 1
    EXPECT_EQ(buf.capacity(), 1u);
    EXPECT_TRUE(buf.empty());

    EXPECT_TRUE(buf.try_push(7));
    EXPECT_TRUE(buf.full());
    EXPECT_FALSE(buf.try_push(8));  // Full

    int v;
    EXPECT_TRUE(buf.try_pop(v));
    EXPECT_EQ(v, 7);
    EXPECT_TRUE(buf.empty());
}

// ---------------------------------------------------------------------------
// Concurrent SPSC stress test
// Producer and consumer run in separate threads.
// ---------------------------------------------------------------------------

TEST(RingBufferTest, SPSCStressTest) {
    constexpr size_t kItems = 100'000;
    RingBuffer<uint64_t, 16> buf;

    std::atomic<bool> producer_done{false};

    std::thread producer([&] {
        for (uint64_t i = 0; i < kItems; ++i) {
            while (!buf.try_push(i)) {
                // Spin until a slot is free
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    uint64_t expected = 0;
    while (expected < kItems) {
        uint64_t value;
        if (buf.try_pop(value)) {
            // Verify FIFO ordering
            EXPECT_EQ(value, expected) << "FIFO order violated at item " << expected;
            ++expected;
        } else {
            std::this_thread::yield();
        }
    }

    producer.join();
    EXPECT_EQ(expected, kItems);
    EXPECT_TRUE(buf.empty());
}

// ---------------------------------------------------------------------------
// Large element (simulating CaptureFrame / EncodedFrame)
// ---------------------------------------------------------------------------

TEST(RingBufferTest, LargeElementType) {
    struct BigStruct {
        std::vector<uint8_t> data;
        int64_t pts_us = 0;
    };

    RingBuffer<BigStruct, 4> buf;  // capacity = 3

    BigStruct src;
    src.data.assign(1024, 0xFF);
    src.pts_us = 123'456;

    EXPECT_TRUE(buf.try_push(std::move(src)));

    BigStruct dst;
    EXPECT_TRUE(buf.try_pop(dst));
    EXPECT_EQ(dst.data.size(), 1024u);
    EXPECT_EQ(dst.data[0], 0xFF);
    EXPECT_EQ(dst.pts_us, 123'456);
}

} // namespace gamestream
