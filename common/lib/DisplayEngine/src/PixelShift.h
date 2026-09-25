#pragma once
#include <stdint.h>

// Bounded burn-in pixel shift (stage 06, D11): an 8-step ring that moves the
// whole picture by 1 px per step and never more than DISPLAY_SHIFT_MAX (2 px)
// in either axis. Header-only, pure.
struct PixelOffset {
    uint8_t dx;
    uint8_t dy;
};

constexpr uint8_t PIXEL_SHIFT_STEPS = 8;

// step % 8 -> {0,0},{1,0},{2,0},{2,1},{2,2},{1,2},{0,2},{0,1}; neighbours differ by 1 px.
inline constexpr PixelOffset PIXEL_SHIFT_TABLE[PIXEL_SHIFT_STEPS] = {
    {0, 0}, {1, 0}, {2, 0}, {2, 1}, {2, 2}, {1, 2}, {0, 2}, {0, 1},
};

inline PixelOffset pixelShiftOffset(uint32_t step) {
    return PIXEL_SHIFT_TABLE[step % PIXEL_SHIFT_STEPS];
}
