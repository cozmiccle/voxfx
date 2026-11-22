#pragma once
// Bounded lock-free multi-producer/multi-consumer queue (Dmitry Vyukov's design).
// Used to send commands from the UI, input threads and the ctl socket into the
// real-time audio thread. Never allocates after construction; push/pop never block.
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>

namespace vfx {

template <typename T>
class MpmcQueue {
    static_assert(std::is_trivially_copyable_v<T>, "commands must be trivially copyable");
public:
    explicit MpmcQueue(size_t capacityPow2 = 1024)
        : mask_(capacityPow2 - 1), cells_(new Cell[capacityPow2]) {
        // capacity must be a power of two
        for (size_t i = 0; i < capacityPow2; ++i) cells_[i].seq.store(i, std::memory_order_relaxed);
    }

    bool push(const T& v) {
        size_t pos = enq_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& c = cells_[pos & mask_];
            size_t seq = c.seq.load(std::memory_order_acquire);
            intptr_t diff = intptr_t(seq) - intptr_t(pos);
            if (diff == 0) {
                if (enq_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    c.value = v;
                    c.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; // full
            } else {
                pos = enq_.load(std::memory_order_relaxed);
            }
        }
    }

    bool pop(T& out) {
        size_t pos = deq_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& c = cells_[pos & mask_];
            size_t seq = c.seq.load(std::memory_order_acquire);
            intptr_t diff = intptr_t(seq) - intptr_t(pos + 1);
            if (diff == 0) {
                if (deq_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    out = c.value;
                    c.seq.store(pos + mask_ + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; // empty
            } else {
                pos = deq_.load(std::memory_order_relaxed);
            }
        }
    }

private:
    struct Cell { std::atomic<size_t> seq; T value; };
    const size_t mask_;
    std::unique_ptr<Cell[]> cells_;
    alignas(64) std::atomic<size_t> enq_{0};
    alignas(64) std::atomic<size_t> deq_{0};
};

} // namespace vfx
