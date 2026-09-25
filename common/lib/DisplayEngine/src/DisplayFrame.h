#pragma once
#include <stddef.h>
#include <stdint.h>

// Pure frame model for the 128x64 SSD1306 OLED (stage 06, D14): monospace
// font metrics, the title/body layout grid and text fitting. A DisplayFrame
// is a list of text lines plus an optional title rule and progress bar; the
// ESP32 adapter draws it with U8g2. Every glyph box is kept inside the
// 126x62 content area so the burn-in pixel shift (<= 2 px) never clips.
constexpr uint8_t DISPLAY_WIDTH = 128, DISPLAY_HEIGHT = 64;
constexpr uint8_t DISPLAY_SHIFT_MAX = 2;                                        // burn-in shift bound (px)
constexpr uint8_t DISPLAY_CONTENT_WIDTH = DISPLAY_WIDTH - DISPLAY_SHIFT_MAX;    // 126
constexpr uint8_t DISPLAY_CONTENT_HEIGHT = DISPLAY_HEIGHT - DISPLAY_SHIFT_MAX;  // 62

// u8g2 5x8_tr / 6x10_tr / 10x20_tr (all monospace).
enum class DisplayFont : uint8_t { Small, Normal, Large };

struct DisplayFontMetrics {
    uint8_t advance;
    uint8_t ascent;
    uint8_t descent;
};

// Small{5,7,1} Normal{6,8,2} Large{10,16,4}.
DisplayFontMetrics displayFontMetrics(DisplayFont f);

constexpr uint8_t DISPLAY_TITLE_BASELINE = 8;
constexpr uint8_t DISPLAY_TITLE_RULE_Y = 10;
constexpr uint8_t DISPLAY_BODY_ROWS = 5;
constexpr uint8_t DISPLAY_BODY_BASELINE[DISPLAY_BODY_ROWS] = {19, 29, 39, 49, 59};
constexpr size_t DISPLAY_TEXT_MAX = 25;  // 126 / 5
constexpr size_t DISPLAY_MAX_LINES = 10;

struct DisplayTextLine {
    DisplayFont font;
    uint8_t x;
    uint8_t baseline;
    char text[DISPLAY_TEXT_MAX + 1];
};

struct DisplayBar {  // outline + fill
    bool used;
    uint8_t x, y, w, h;
    uint8_t percent;
};

struct DisplayFrame {
    uint8_t lineCount;
    DisplayTextLine line[DISPLAY_MAX_LINES];
    bool titleRule;
    DisplayBar bar;

    void clear();
    // Fits text (displayFitText) and appends. false if frame full, text null, or the
    // glyph box would leave the content area: baseline < ascent, baseline + descent
    // > DISPLAY_CONTENT_HEIGHT - 1, or x + advance > DISPLAY_CONTENT_WIDTH.
    bool addText(DisplayFont font, uint8_t x, uint8_t baseline, const char* text);
    bool setTitle(const char* text);                              // Normal @ (0, TITLE_BASELINE), titleRule = true
    bool addBody(uint8_t row, const char* text, uint8_t x = 0);   // Normal @ BODY_BASELINE[row]; false if row >= 5
    bool addCentered(DisplayFont font, uint8_t baseline, const char* text);
    void setBar(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t percent);  // clamps into content area, percent<=100
};

// (CONTENT_WIDTH - x) / advance, 0 if x >= width.
size_t displayFitChars(DisplayFont font, uint8_t x);

// Copies in -> out (cap incl. NUL) keeping at most displayFitChars(font,x) chars; bytes
// outside 0x20..0x7E become '?'; if truncated the last kept char is '~'. null in -> "".
void displayFitText(const char* in, DisplayFont font, uint8_t x, char* out, size_t cap);
