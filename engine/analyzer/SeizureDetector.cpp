#include "SeizureDetector.h"

#include <algorithm>
#include <cmath>

namespace spp::analyzer {

NetworkState SeizureDetector::classify(
    const NetworkMetrics& metrics,
    const std::vector<float>& finalVoltages
) {
    const float meanRate = std::isfinite(metrics.meanFiringRateHz) ? std::max(0.0f, metrics.meanFiringRateHz) : 0.0f;
    const float sync = std::clamp(
        std::isfinite(metrics.synchronizationIndex) ? metrics.synchronizationIndex : 0.0f,
        0.0f,
        1.0f
    );
    const float burst = std::clamp(
        std::isfinite(metrics.burstIndex) ? metrics.burstIndex : 0.0f,
        0.0f,
        1.0f
    );
    const float seizureProbFrac = std::clamp(
        (std::isfinite(metrics.seizureProbabilityPct) ? metrics.seizureProbabilityPct : 0.0f) / 100.0f,
        0.0f,
        1.0f
    );
    const float suppressionFrac = std::clamp(
        (std::isfinite(metrics.suppressionPct) ? metrics.suppressionPct : 0.0f) / 100.0f,
        0.0f,
        1.0f
    );

    std::size_t depolarizedCount = 0U;
    for (float v : finalVoltages) {
        if (v > -20.0f) {
            ++depolarizedCount;
        }
    }

    const float depolarizedFraction = finalVoltages.empty()
                                          ? 0.0f
                                          : static_cast<float>(depolarizedCount) /
                                                static_cast<float>(finalVoltages.size());

    if (suppressionFrac > 0.80f) {
        return NetworkState::NeuralSuppression;
    }

    if (seizureProbFrac > 0.80f && sync > 0.70f) {
        if (depolarizedFraction > 0.25f && meanRate < 6.0f) {
            return NetworkState::DepolarizationBlock;
        }
        return NetworkState::SeizureActive;
    }

    if (meanRate > 15.0f) {
        return NetworkState::Hyperexcitable;
    }

    if (burst > 0.10f) {
        return NetworkState::MildInstability;
    }

    return NetworkState::Stable;
}

std::string SeizureDetector::toString(NetworkState state) {
    switch (state) {
        case NetworkState::Stable:
            return "Stable";
        case NetworkState::MildInstability:
            return "Mild";
        case NetworkState::Hyperexcitable:
            return "Hyperexcitable";
        case NetworkState::SeizureRisk:
            return "Seizure";
        case NetworkState::SeizureActive:
            return "Seizure";
        case NetworkState::DepolarizationBlock:
            return "Depolarization Block";
        case NetworkState::NeuralSuppression:
            return "Suppression";
        default:
            return "Stable";
    }
}

} // namespace spp::analyzer
