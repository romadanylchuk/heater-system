#pragma once
#include <stddef.h>
#include <stdint.h>
#include <CommonState.h>
#include "DisplayFormat.h"
#include "DisplayFrame.h"

// Page registry and the shared page / special-screen renderers (stage 06,
// D12/D15/D16). Controller pages (stages 07/08) are registered first, then
// appendSharedPages() adds Network and Sensors; the Alarms page is dynamic
// (rendered by the scheduler's Alarms screen, not registered). Every renderer
// clears the frame and fills it from state only: pure, no heap, no Arduino.
using DisplayPageRenderFn = void (*)(const CommonState& state, DisplayFrame& frame, void* ctx);

struct DisplayPageDesc {
    const char* name;
    DisplayPageRenderFn render;
    void* ctx;
};

constexpr size_t DISPLAY_MAX_PAGES = 8;  // controller pages + Network + Sensors

// DisplayScheduler counts pages in uint8_t and adds 1 for the dynamic Alarms
// page (pageCount + 1, then % n). That math is only safe while the registry
// cap stays well below 255; keep this bound if the cap is ever raised.
static_assert(DISPLAY_MAX_PAGES < 255, "DisplayScheduler uint8_t page math needs DISPLAY_MAX_PAGES + 1 <= 255");

class DisplayPageList {
public:
    bool add(const DisplayPageDesc& page);       // false if full or render == nullptr
    size_t count() const;
    const DisplayPageDesc* at(size_t i) const;   // nullptr if out of range

private:
    DisplayPageDesc _pages[DISPLAY_MAX_PAGES] = {};
    size_t _count = 0;
};

constexpr uint8_t DISPLAY_ALARMS_PER_SUBPAGE = 5;

uint8_t alarmSubPageCount(uint32_t mask);  // ceil(popcount / 5); 0 when mask == 0

void renderNetworkPage(const CommonState& s, DisplayFrame& f, void* ctx);  // ctx unused
void renderSensorsPage(const CommonState& s, DisplayFrame& f, void* ctx);  // ctx = const DisplayLabels*
// subPage beyond the last sub-page wraps modulo the sub-page count.
void renderAlarmsPage(const CommonState& s, uint8_t subPage, const DisplayLabels& labels, DisplayFrame& f);
void renderSetupScreen(const NetworkStatus& n, DisplayFrame& f);
void renderResetScreen(ResetGatePhase phase, uint8_t secondsLeft, DisplayFrame& f);
void renderOtaScreen(const OtaStatus& o, DisplayFrame& f);
void renderBootSplash(const char* projectName, const char* fwVersion, DisplayFrame& f);

// Appends Network then Sensors (ctx = labels). false if the list has no room.
bool appendSharedPages(DisplayPageList& list, const DisplayLabels* labels);
