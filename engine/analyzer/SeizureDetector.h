#pragma once

#include <string>
#include <vector>

#include "Metrics.h"

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

class SeizureDetector {
public:
    static NetworkState classify(
        const NetworkMetrics& metrics,
        const std::vector<float>& finalVoltages
    );

    static std::string toString(NetworkState state);
};

} // namespace spp::analyzer
