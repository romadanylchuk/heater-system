#pragma once
#include "WebApiSuite.h"
#include "WebAuthSuite.h"
#include "WebCoreHookSuite.h"
#include "WebCredSuite.h"
#include "WebLangSuite.h"

// Aggregates every WebEngine-related native suite (stage 05). Called once
// from CommonSuite.h::runCommonSuite() so both projects run these tests.
// Grows one runXxxSuite() call per phase.
inline void runWebSuite() {
    runWebAuthSuite();
    runWebApiSuite();
    runWebCoreHookSuite();
    runWebCredSuite();
    runWebLangSuite();
}
