#pragma once
#include "DisplayFrameSuite.h"
#include "DisplayPagesSuite.h"
#include "DisplaySchedulerSuite.h"

// Aggregates every DisplayEngine native suite (stage 06). Called once from
// CommonSuite.h::runCommonSuite() so both projects run these tests. Grows one
// runXxxSuite() call per phase.
inline void runDisplaySuite() {
    runDisplayFrameSuite();
    runDisplaySchedulerSuite();
    runDisplayPagesSuite();
}
