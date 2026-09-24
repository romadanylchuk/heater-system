#pragma once
#include <atomic>
#include <stddef.h>
#include <stdint.h>

// Lock-free set of small indices (0..CAPACITY-1), written from any task via
// set() (e.g. the AsyncTCP task marking a HA entity dirty after an inbound
// MQTT command) and drained once per loop tick via takeAll(). Backed by 4
// atomic 32-bit words (128 bits total); value-initialised to all-zero.
class AtomicIndexSet {
public:
    static constexpr size_t CAPACITY = 128;

    // Out-of-range indices are silently ignored (no-op).
    void set(size_t i) {
        if (i >= CAPACITY) {
            return;
        }
        const size_t word = i / 32;
        const uint32_t bit = 1u << (i % 32);
        _w[word].fetch_or(bit, std::memory_order_relaxed);
    }

    // Atomically takes (exchanges with 0) every word into out[4]. Returns
    // true if any bit was set (i.e. any index had been marked since the last
    // takeAll()).
    bool takeAll(uint32_t out[4]) {
        bool any = false;
        for (size_t w = 0; w < 4; ++w) {
            out[w] = _w[w].exchange(0, std::memory_order_relaxed);
            if (out[w] != 0) {
                any = true;
            }
        }
        return any;
    }

private:
    std::atomic<uint32_t> _w[4]{};
};
