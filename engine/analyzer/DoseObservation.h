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

    // Phase 3a: GAT1 reuptake-block occupancy (0..1), same Hill-occupancy
    // scale as the fields above, so detectMechanism() can compare it
    // directly. See AnalyzedDose.h.
    float gat1ReuptakeBlock = 0.0f;

    // Phase 3c: neuromodulator receptor occupancy (0..1 each), same scale
    // and purpose as gat1ReuptakeBlock above. See AnalyzedDose.h.
    float d1Gain   = 0.0f;
    float d2Gain   = 0.0f;
    float ht1aGain = 0.0f;
    float ht2aGain = 0.0f;
    // Tier 2.2 (PRECISION_GAP_CLOSURE_PLAN.md): alpha-2 presynaptic
    // occupancy, same scale/purpose as d1Gain/d2Gain/ht1aGain/ht2aGain
    // above. See AnalyzedDose.h.
    float alpha2Gain = 0.0f;

    RawMetrics metrics;
};

} // namespace spp::analyzer