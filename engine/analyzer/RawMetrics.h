#pragma once

namespace spp::analyzer {

struct RawMetrics {

    // Firing rate
    float meanFiringRateHz          = 0.0f;
    float firingRateStdHz           = 0.0f;
    float silentNeuronPct           = 0.0f;
    float lateWindowSilentNeuronPct = 0.0f;  // Gap 1.3 fix -- see Metrics.h comment
    float earlyWindowRateHz         = 0.0f;
    float lateWindowRateHz          = 0.0f;

    // Synchronization
    float synchronizationIndex      = 0.0f;
    float peakSynchronizationIndex  = 0.0f;

    // Burst
    float burstRateHz               = 0.0f;
    float burstingNeuronPct         = 0.0f;
    float meanBurstDurationMs       = 0.0f;

    // Irregularity
    float irregularityIndex         = 0.0f;

    // Population
    float populationVariance        = 0.0f;
};

} // namespace spp::analyzer