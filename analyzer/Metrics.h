#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../simulation/SimulationEngine.h"

namespace spp::analyzer {

struct NeuronMetrics {
    std::size_t spikeCount = 0;
    float firingRateHz = 0.0f;
    float isiMeanMs = 0.0f;
    float isiVarianceMs = 0.0f;
};

struct NetworkMetrics {
    float meanFiringRateHz = 0.0f;
    float synchronizationIndex = 0.0f;
    float burstIndex = 0.0f;
    float populationVariance = 0.0f;
    float voltageVariance = 0.0f;
    float irregularityIndex = 0.0f;

    float earlyWindowRateHz = 0.0f;
    float lateWindowRateHz = 0.0f;

    float suppressionPct = 0.0f;
    float seizureProbabilityPct = 0.0f;
    float stabilityScore = 1.0f;

    float nii = 0.0f;
};

struct TimeWindowMetrics {
    float startMs = 0.0f;
    float endMs = 0.0f;
    float meanFiringRateHz = 0.0f;
    float synchronizationIndex = 0.0f;
    float burstIndex = 0.0f;
    float seizureProbability = 0.0f;
};

class MetricsAnalyzer {
public:
    static std::vector<NeuronMetrics> computeNeuronMetrics(const simulation::SimulationResult& result);
    static NetworkMetrics computeNetworkMetrics(
        const simulation::SimulationResult& result,
        const std::vector<NeuronMetrics>& neuronMetrics
    );

    static std::vector<TimeWindowMetrics> computeTimeWindowMetrics(
        const simulation::SimulationResult& result,
        float windowMs,
        float stepMs
    );

private:
    static float computeNII(
        float populationVariance,
        float voltageVariance,
        float irregularityIndex,
        float synchronization,
        float burstIndex
    );
};

} // namespace spp::analyzer
