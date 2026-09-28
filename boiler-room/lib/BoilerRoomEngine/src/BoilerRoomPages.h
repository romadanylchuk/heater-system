#pragma once
#include <CommonState.h>
#include <DisplayFrame.h>
#include <DisplayPages.h>
#include "BoilerRoomStatus.h"

// The three boiler-room OLED pages (stage 07, C11): DisplayPageRenderFn with
// ctx = const BoilerRoomStatus*. Pure, ASCII only, Normal font, <= 21 chars
// per body row (BR_PAGE_ROW_CHARS), at most the 5 body rows of the grid. A null
// ctx or a not-ready status renders the title plus "starting...".
//  - "Boiler": T1 flow, T2 return, P1, P2, OVERHEAT (when latched).
//  - "Accu":   T3 top, T4 mid, T5 bottom, stored energy.
//  - "Supply": T6 supply, P3, mode (+ offer window / wait minutes), anti-freeze, no-need flag.
// Pump rows show the ACTUAL relay (CommonState.relays.on), the controller
// reason and '!' when the request is on the safety slot.
constexpr size_t BR_PAGE_ROW_CHARS = 21;   // DISPLAY_CONTENT_WIDTH / Normal advance

void renderBoilerPage(const CommonState& s, DisplayFrame& f, void* ctx);
void renderAccuPage(const CommonState& s, DisplayFrame& f, void* ctx);
void renderSupplyPage(const CommonState& s, DisplayFrame& f, void* ctx);
