#include "NoNeed.h"

bool computeNoNeed(const NoNeedInputs& in) {
    const bool k2PhysBypass = in.k2ExerciseRunning ? in.k2BypassRequested : in.k2RelayActual;
    const bool p4PhysOff = in.p4ExerciseRunning ? !in.p4Requested : !in.p4RelayActual;
    return !in.anyPending && !in.h3FailedHeating && in.k2BypassRequested && k2PhysBypass &&
           !in.p4Requested && p4PhysOff && in.p4OffDelayElapsed;
}
