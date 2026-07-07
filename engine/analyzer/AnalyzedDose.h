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
    Mixed
};

struct AnalyzedDose {
    float dose    = 0.0f;
    float blockNa = 0.0f;
    float blockK  = 0.0f;
    float blockCa = 0.0f;

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