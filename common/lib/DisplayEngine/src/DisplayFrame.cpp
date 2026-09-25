#include "DisplayFrame.h"
#include <string.h>

DisplayFontMetrics displayFontMetrics(DisplayFont f) {
    switch (f) {
        case DisplayFont::Small:
            return DisplayFontMetrics{5, 7, 1};
        case DisplayFont::Large:
            return DisplayFontMetrics{10, 16, 4};
        case DisplayFont::Normal:
        default:
            return DisplayFontMetrics{6, 8, 2};
    }
}

static bool isKnownFont(DisplayFont f) {
    return f == DisplayFont::Small || f == DisplayFont::Normal || f == DisplayFont::Large;
}

size_t displayFitChars(DisplayFont font, uint8_t x) {
    if (x >= DISPLAY_CONTENT_WIDTH) {
        return 0;
    }
    const DisplayFontMetrics m = displayFontMetrics(font);
    return static_cast<size_t>(DISPLAY_CONTENT_WIDTH - x) / m.advance;
}

void displayFitText(const char* in, DisplayFont font, uint8_t x, char* out, size_t cap) {
    if (out == nullptr || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (in == nullptr) {
        return;
    }
    size_t limit = displayFitChars(font, x);
    if (limit > cap - 1) {
        limit = cap - 1;
    }
    size_t n = 0;
    while (n < limit && in[n] != '\0') {
        const unsigned char c = static_cast<unsigned char>(in[n]);
        out[n] = (c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : '?';
        ++n;
    }
    out[n] = '\0';
    const bool truncated = in[n] != '\0';
    if (truncated && n > 0) {
        out[n - 1] = '~';
    }
}

void DisplayFrame::clear() {
    memset(this, 0, sizeof(*this));
}

bool DisplayFrame::addText(DisplayFont font, uint8_t x, uint8_t baseline, const char* text) {
    if (text == nullptr || lineCount >= DISPLAY_MAX_LINES || !isKnownFont(font)) {
        return false;
    }
    const DisplayFontMetrics m = displayFontMetrics(font);
    if (baseline < m.ascent) {
        return false;
    }
    if (static_cast<uint16_t>(baseline) + m.descent > DISPLAY_CONTENT_HEIGHT - 1) {
        return false;
    }
    if (static_cast<uint16_t>(x) + m.advance > DISPLAY_CONTENT_WIDTH) {
        return false;
    }
    DisplayTextLine& l = line[lineCount];
    l.font = font;
    l.x = x;
    l.baseline = baseline;
    displayFitText(text, font, x, l.text, sizeof(l.text));
    ++lineCount;
    return true;
}

bool DisplayFrame::setTitle(const char* text) {
    if (!addText(DisplayFont::Normal, 0, DISPLAY_TITLE_BASELINE, text)) {
        return false;
    }
    titleRule = true;
    return true;
}

bool DisplayFrame::addBody(uint8_t row, const char* text, uint8_t x) {
    if (row >= DISPLAY_BODY_ROWS) {
        return false;
    }
    return addText(DisplayFont::Normal, x, DISPLAY_BODY_BASELINE[row], text);
}

bool DisplayFrame::addCentered(DisplayFont font, uint8_t baseline, const char* text) {
    if (text == nullptr || !isKnownFont(font)) {
        return false;
    }
    char fitted[DISPLAY_TEXT_MAX + 1];
    displayFitText(text, font, 0, fitted, sizeof(fitted));
    const DisplayFontMetrics m = displayFontMetrics(font);
    const size_t width = strlen(fitted) * m.advance;
    const uint8_t x = static_cast<uint8_t>((DISPLAY_CONTENT_WIDTH - width) / 2);
    return addText(font, x, baseline, fitted);
}

void DisplayFrame::setBar(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t percent) {
    if (x > DISPLAY_CONTENT_WIDTH) {
        x = DISPLAY_CONTENT_WIDTH;
    }
    if (y > DISPLAY_CONTENT_HEIGHT) {
        y = DISPLAY_CONTENT_HEIGHT;
    }
    if (w > DISPLAY_CONTENT_WIDTH - x) {
        w = static_cast<uint8_t>(DISPLAY_CONTENT_WIDTH - x);
    }
    if (h > DISPLAY_CONTENT_HEIGHT - y) {
        h = static_cast<uint8_t>(DISPLAY_CONTENT_HEIGHT - y);
    }
    bar.used = true;
    bar.x = x;
    bar.y = y;
    bar.w = w;
    bar.h = h;
    bar.percent = percent > 100 ? 100 : percent;
}
