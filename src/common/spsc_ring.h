#pragma once
// Lock-free single-producer/single-consumer ring of samples. The audio thread
// writes (never blocks; drops what doesn't fit), a worker thread reads.
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

namespace vfx {

template <typename T>
class SpscRing {
public:
    explicit SpscRing(size_t capacityPow2) : buf_(capacityPow2), mask_(capacityPow2 - 1) {}

    size_t capacity() const { return buf_.size(); }

    // Producer side. Returns how many items were written.
    size_t write(const T* data, size_t n) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);
        const size_t room = buf_.size() - (head - tail);
        n = std::min(n, room);
        for (size_t i = 0; i < n; ++i) buf_[(head + i) & mask_] = data[i];
        head_.store(head + n, std::memory_order_release);
        return n;
    }

    // Consumer side. Returns how many items were read.
    size_t read(T* out, size_t n) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        n = std::min(n, head - tail);
        for (size_t i = 0; i < n; ++i) out[i] = buf_[(tail + i) & mask_];
        tail_.store(tail + n, std::memory_order_release);
        return n;
    }

    size_t available() const {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_relaxed);
    }

    // Consumer side: throw away everything currently buffered.
    void discard() { tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release); }

private:
    std::vector<T> buf_;
    const size_t mask_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

} // namespace vfx
