#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace gamestream {

/// Lock-free single-producer / single-consumer (SPSC) ring buffer.
///
/// Capacity: N-1 elements (one slot is always kept empty to distinguish full from empty).
/// Example: RingBuffer<int, 4> holds at most 3 elements.
///
/// Thread-safety: ONE thread may call try_push(), ONE thread may call try_pop().
/// Any other usage pattern (MPSC, MPMC) is undefined behaviour.
///
/// Memory ordering:
///   try_push(): writes buffer[head] with relaxed, then publishes head with release.
///   try_pop():  reads tail with relaxed, loads head with acquire, then advances tail with release.
/// This ensures that data written by the producer is visible to the consumer after the
/// release/acquire pair on head_.
///
/// Non-copyable and non-movable: std::atomic members cannot be moved.
template<typename T, size_t N>
class RingBuffer {
public:
    static_assert(N >= 2, "RingBuffer capacity must be at least 2 (holds N-1 elements)");

    RingBuffer() = default;

    // Non-copyable, non-movable (atomics are not movable)
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    RingBuffer(RingBuffer&&) = delete;
    RingBuffer& operator=(RingBuffer&&) = delete;

    /// Push an element by move.  Returns false if the buffer is full.
    bool try_push(T value) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) % N;

        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // Full
        }

        buffer_[head] = std::move(value);
        head_.store(next, std::memory_order_release);
        return true;
    }

    /// Pop an element into value by move.  Returns false if the buffer is empty.
    bool try_pop(T& value) {
        const size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // Empty
        }

        value = std::move(buffer_[tail]);
        tail_.store((tail + 1) % N, std::memory_order_release);
        return true;
    }

    /// Returns true if the buffer currently has no elements.
    /// Note: in concurrent usage this is a snapshot; the state may change immediately after.
    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    /// Returns true if the buffer is currently full (N-1 elements present).
    /// Note: in concurrent usage this is a snapshot; the state may change immediately after.
    bool full() const {
        const size_t next = (head_.load(std::memory_order_acquire) + 1) % N;
        return next == tail_.load(std::memory_order_acquire);
    }

    /// Approximate element count.  Not exact under concurrent push/pop.
    size_t size() const {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return (h >= t) ? (h - t) : (N - t + h);
    }

    /// Maximum number of elements this buffer can hold (N-1).
    static constexpr size_t capacity() { return N - 1; }

private:
    std::array<T, N> buffer_{};
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};

} // namespace gamestream
