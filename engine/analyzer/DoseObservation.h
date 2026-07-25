#pragma once

#include "RawMetrics.h"

namespace spp::analyzer {

struct DoseObservation {
    float dose    = 0.0f;
    float blockNa = 0.0f;
    float blockK  = 0.0f;
    float blockCa = 0.0f;

    // PHASE2_PLAN.md step 5: see AnalyzedDose.h for what these mean --
    // same fields, carried through from collection to analysis.
    float blockAmpa       = 0.0f;
    float blockNmda        = 0.0f;
    float potentiateGabaA  = 0.0f;
    float activateGabaB    = 0.0f;

    RawMetrics metrics;
};

} // namespace spp::analyzer