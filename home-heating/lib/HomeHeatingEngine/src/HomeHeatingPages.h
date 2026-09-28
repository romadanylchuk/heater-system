#pragma once
#include <CommonState.h>
#include <DisplayFrame.h>
#include <DisplayPages.h>
#include "HomeHeatingStatus.h"

// The two home-heating OLED pages (stage 08, C13): DisplayPageRenderFn with
// ctx = const HomeHeatingStatus*. Pure, ASCII only, Normal font, <= 21 chars
// per body row (HH_PAGE_ROW_CHARS), at most the 5 body rows of the grid. A null
// ctx or a not-ready status renders the title plus "starting...".
//  - "Heating": "H2 41.2 set 40.0", "H3 70.1", K1 ("K1 35% >" / "K1 recal" /
//    "K1 ?"; '>' opening, '<' closing), "P4 ON demand" (ACTUAL relay +
//    controller reason), and the fail-safe text (alarm label) when fail != None.
//  - "DHW": "H3 70.1", "H4 50.0", "K2 TANK charging" / "K2 BYP H4 full"
//    (ACTUAL relay, energised = bypass, + controller reason), "No need: yes|no".
// Temperatures read "--.-" unless the logical sensor is Ok.
constexpr size_t HH_PAGE_ROW_CHARS = 21;   // DISPLAY_CONTENT_WIDTH / Normal advance

void renderHeatingPage(const CommonState& s, DisplayFrame& f, void* ctx);
void renderDhwPage(const CommonState& s, DisplayFrame& f, void* ctx);
