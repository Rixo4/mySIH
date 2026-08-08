// Metrics.h
// Defines MetricsAnalyzer — computes PURE MEASURED QUANTITIES only.
// No decisions, no scores, no probabilities, no interpretations.
// All derived scores (NII, seizure probability, suppression %) are
// computed downstream in NetworkAnalyzer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "../simulation/SimulationEngine.h"
#include "RawMetrics.h"

namespace spp::analyzer {

// ─── Per-neuron metrics ───────────────────────────────────────────────────────
struct NeuronMetrics {
    std::size_t spikeCount  = 0;
    float firingRateHz      = 0.0f;
    float isiMeanMs         = 0.0f;
    float isiVarianceMs     = 0.0f;  // sample variance (÷ n-1)

    // Diagnostic addition (2026-08-08, Tier 2.4 beta/alpha-1 investigation):
    // mirrors SimulationResult::neuronTypes (1 = excitatory, 0 = inhibitory,
    // see network::Network.cpp's convention). Added so per-neuron CSV export
    // can be split by cell type -- needed to test whether a population-level
    // paradoxical effect (e.g. beta/alpha-1's excitatory reading from a
    // per-neuron suppressive lever) is driven by inhibitory-neuron dynamics,
    // without which neuron_stats.csv rows are anonymous and this can't be
    // checked from report output alone.
    std::uint8_t neuronType = 1U;
};

// ─── Network-level raw measurements ──────────────────────────────────────────
// This is what Metrics.cpp produces.
// NOTHING here is a decision or score.
struct NetworkMetrics {

    // Firing rate
    float meanFiringRateHz          = 0.0f;
    float firingRateStdHz           = 0.0f;
    float silentNeuronPct           = 0.0f;  // % neurons < 2 Hz, WHOLE-RUN average
                                              // (see Metrics.cpp KNOWN LIMITATION
                                              // comment: undercounts late-stage
                                              // silencing for onset-ramped drugs)
    float lateWindowSilentNeuronPct = 0.0f;  // Gap 1.3 fix: % neurons < 2 Hz computed
                                              // from ONLY the late half of the run --
                                              // this is what feeds the reported
                                              // "Silent Neuron Delta", since it
                                              // doesn't get masked by early-run
                                              // activity the way silentNeuronPct does
    float earlyWindowRateHz         = 0.0f;
    float lateWindowRateHz          = 0.0f;

    // Phase 3b: three-way split (first/middle/last third of the run),
    // added specifically for the [Adaptation Profile] report section
    // (PHASE3_PLAN.md §7's Short/Medium/Long-term tiers). Same fixed-window
    // computation as earlyWindowRateHz/lateWindowRateHz above, just three
    // windows instead of two -- only meaningful on long-duration
    // (desensitization-style) runs, same caveat as the two-way split.
    float firstThirdRateHz          = 0.0f;
    float middleThirdRateHz         = 0.0f;
    float lastThirdRateHz           = 0.0f;

    // Synchronization
    float synchronizationIndex      = 0.0f;
    float peakSynchronizationIndex  = 0.0f;

    // Burst
    float burstIndex                = 0.0f;  // sigmoid-normalised burstRateHz
    float burstRateHz               = 0.0f;  // raw burst events per second
    float burstingNeuronPct         = 0.0f;  // % neurons with >=1 burst
    float meanBurstDurationMs       = 0.0f;

    // Irregularity
    float irregularityIndex         = 0.0f;  // mean ISI-CV

    // Population
    float populationVariance        = 0.0f;

    // Kept for backward compat with CsvWriter / output layer
    // Set to 0 — not computed here anymore
    float stabilityScore            = 0.0f;
};

// ─── Time window metrics ──────────────────────────────────────────────────────
struct TimeWindowMetrics {
    float startMs               = 0.0f;
    float endMs                 = 0.0f;
    float meanFiringRateHz      = 0.0f;
    float synchronizationIndex  = 0.0f;
    float burstIndex            = 0.0f;
    float burstRateHz           = 0.0f;
    float burstingNeuronPct     = 0.0f;
    float irregularityIndex     = 0.0f;
    float meanBurstDurationMs = 0.0f;
    // Note: no seizureProbability, no nii — those are NetworkAnalyzer's job
};

// ─── Analyzer ─────────────────────────────────────────────────────────────────
class MetricsAnalyzer {
public:
    // Compute per-neuron metrics from simulation result
    static std::vector<NeuronMetrics> computeNeuronMetrics(
        const simulation::SimulationResult& result
    );

    // Compute network-level raw measurements
    // No baseline parameter — suppression is NetworkAnalyzer's job
    static NetworkMetrics computeNetworkMetrics(
        const simulation::SimulationResult& result,
        const std::vector<NeuronMetrics>& neuronMetrics
    );

    // Compute sliding window raw metrics
    static std::vector<TimeWindowMetrics> computeTimeWindowMetrics(
        const simulation::SimulationResult& result,
        float windowMs,
        float stepMs
    );
};

} // namespace spp::analyzer