#pragma once

#include <vector>
#include "AnalyzedDose.h"
#include "DoseObservation.h"

namespace spp::analyzer {

class NetworkAnalyzer {
public:
    // Main entry point
    // Receives full dose series, dose[0] is baseline
    static std::vector<AnalyzedDose> analyze(
        const std::vector<DoseObservation>& observations
    );

private:
    // Delta computation
    static void computeDeltas(
        AnalyzedDose& dose,
        const RawMetrics& baseline
    );

    // Derived scores
    static float computeNII(const RawMetrics& metrics);

    static float computeSuppressionScore(
        float rateChangePct,
        float silentNeuronDelta
    );

    static float computeExcitabilityScore(
        float rateChangePct,
        float burstRateDelta,
        float irregularityDelta
    );

    static float computeStabilizationScore(
        float syncReductionPct,
        float burstDurationDelta,
        float niiDelta,
        float rateChangePct
    );

    static float computeSeizureProbability(
        const RawMetrics& metrics,
        float nii
    );

    // Mechanism detection
    static MechanismSignature detectMechanism(
        const AnalyzedDose& dose
    );

    // Network state classification
    static NetworkState classifyState(
        const AnalyzedDose& dose
    );
};

} // namespace spp::analyzer