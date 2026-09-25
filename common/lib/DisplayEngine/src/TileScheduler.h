#pragma once
#include <stddef.h>
#include <stdint.h>

// Dirty tile-row scheduler (stage 06, D3/D4): diffs the U8g2 full-buffer
// frame (8 tile rows x 128 bytes, _F layout) against a shadow of what the
// panel holds, so each fast pass sends only changed rows within a budget.
// A row stays dirty until markSent() (a NACKed row is simply not marked).
constexpr uint8_t DISPLAY_TILE_ROWS = 8;
constexpr size_t DISPLAY_ROW_BYTES = 128;
constexpr size_t DISPLAY_BUFFER_BYTES = DISPLAY_TILE_ROWS * DISPLAY_ROW_BYTES;  // 1024 (U8g2 _F layout)

class TileScheduler {
public:
    void invalidateAll();                               // all 8 rows dirty (panel content unknown)
    uint8_t scan(const uint8_t* frame);                 // dirty |= rows whose bytes != shadow; returns dirty mask
    bool hasPending() const;
    uint8_t pendingMask() const;
    int8_t nextRow() const;                             // lowest dirty row, -1 if none
    void markSent(uint8_t row, const uint8_t* frame);   // shadow[row] = frame[row]; clear bit (row<8)

private:
    uint8_t _shadow[DISPLAY_BUFFER_BYTES] = {};
    uint8_t _dirty = 0xFF;  // unknown panel content at construction
};

// Row budget for one fast pass given ms already spent in the pass before the display:
// < 40 -> 2, < 80 -> 1, else 0.
uint8_t displayRowsForPass(uint32_t passElapsedMs);
