#pragma once

#include "RawMetrics.h"

namespace spp::analyzer {

enum class NetworkState {
    Stable,
    MildInstability,
    Hyperexcitable,
    SeizureRisk,
    SeizureActive,
    DepolarizationBlock,
    NeuralSuppression
};

enum class MechanismSignature {
    Unknown,
    NaBlock,
    KBlock,
    CaBlock,
    Mixed,
    // PHASE2_PLAN.md step 5: receptor mechanism signatures. Kept as their
    // own distinct enum values rather than folded into the existing
    // Na/K/Ca/Mixed set, since a "mixed channel + receptor" combination
    // isn't part of the Phase 2 validation drug set (§5) -- every
    // validation drug targets exactly one receptor and none of the three
    // intrinsic channels, so detectMechanism() below treats all seven as
    // one flat set of candidates rather than two separate hierarchies.
    AmpaBlock,
    NmdaBlock,
    GabaAPotentiate,
    GabaBAgonist,
    // Phase 3a: GAT1 reuptake block (tiagabine) -- a genuinely different
    // axis from the four receptor mechanisms above (it extends a decay
    // time constant, not a conductance/occupancy), but detectMechanism()
    // still treats it as one more flat candidate on the same 0..1 scale,
    // same reasoning as the Phase 2 receptor additions.
    Gat1ReuptakeBlock,
    // Phase 3c: neuromodulator gain receptor occupancy (D1/D2/5-HT1A/
    // 5-HT2A). Same flat-candidate treatment as every mechanism above --
    // detectMechanism() picks whichever occupancy magnitude is largest.
    D1Gain,
    D2Gain,
    Ht1aGain,
    Ht2aGain
};

struct AnalyzedDose {
    float dose    = 0.0f;
    float blockNa = 0.0f;
    float blockK  = 0.0f;
    float blockCa = 0.0f;

    // PHASE2_PLAN.md step 5: receptor drug "strength" fractions, all
    // normalized to the same 0..1 Hill-occupancy scale as blockNa/K/Ca
    // above (see DrugModel::hillBlock) so detectMechanism() can compare
    // all seven candidates on equal footing. For GABA-A this is the
    // underlying occupancy fraction driving the potentiation factor, not
    // the multiplier itself (see main.cpp's buildDoseObservation).
    float blockAmpa      = 0.0f;
    float blockNmda       = 0.0f;
    float potentiateGabaA = 0.0f;
    float activateGabaB   = 0.0f;
    float gat1ReuptakeBlock = 0.0f;

    // Phase 3c: mirrors DoseObservation's d1Gain/d2Gain/ht1aGain/ht2aGain.
    float d1Gain   = 0.0f;
    float d2Gain   = 0.0f;
    float ht1aGain = 0.0f;
    float ht2aGain = 0.0f;

    RawMetrics metrics;

    // Deltas vs baseline
    float rateChangePct        = 0.0f;
    float syncDelta            = 0.0f;
    float syncReductionPct     = 0.0f;
    float burstRateDelta       = 0.0f;
    float burstDurationDelta   = 0.0f;
    float irregularityDelta    = 0.0f;
    float silentNeuronDelta    = 0.0f;
    float peakSyncDelta        = 0.0f;

    // Derived scores
    float nii                  = 0.0f;
    float suppressionScore     = 0.0f;
    float excitabilityScore    = 0.0f;
    float stabilizationScore   = 0.0f;
    float seizureProbability   = 0.0f;

    // Classification
    NetworkState networkState          = NetworkState::Stable;
    MechanismSignature mechanismSignature = MechanismSignature::Unknown;
};

} // namespace spp::analyzer