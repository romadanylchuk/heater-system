#include "TileScheduler.h"
#include <string.h>

void TileScheduler::invalidateAll() {
    _dirty = 0xFF;
}

uint8_t TileScheduler::scan(const uint8_t* frame) {
    if (frame == nullptr) {
        return _dirty;
    }
    for (uint8_t row = 0; row < DISPLAY_TILE_ROWS; ++row) {
        const size_t offset = static_cast<size_t>(row) * DISPLAY_ROW_BYTES;
        if (memcmp(_shadow + offset, frame + offset, DISPLAY_ROW_BYTES) != 0) {
            _dirty = static_cast<uint8_t>(_dirty | (1u << row));
        }
    }
    return _dirty;
}

bool TileScheduler::hasPending() const {
    return _dirty != 0;
}

uint8_t TileScheduler::pendingMask() const {
    return _dirty;
}

int8_t TileScheduler::nextRow() const {
    for (uint8_t row = 0; row < DISPLAY_TILE_ROWS; ++row) {
        if ((_dirty & (1u << row)) != 0) {
            return static_cast<int8_t>(row);
        }
    }
    return -1;
}

void TileScheduler::markSent(uint8_t row, const uint8_t* frame) {
    if (row >= DISPLAY_TILE_ROWS || frame == nullptr) {
        return;
    }
    const size_t offset = static_cast<size_t>(row) * DISPLAY_ROW_BYTES;
    memcpy(_shadow + offset, frame + offset, DISPLAY_ROW_BYTES);
    _dirty = static_cast<uint8_t>(_dirty & ~(1u << row));
}

uint8_t displayRowsForPass(uint32_t passElapsedMs) {
    if (passElapsedMs < 40) {
        return 2;
    }
    if (passElapsedMs < 80) {
        return 1;
    }
    return 0;
}
