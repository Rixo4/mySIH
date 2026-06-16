// This file defines the SeizureDetector class, which is responsible for analyzing neural network metrics and final voltages to classify the network state into various categories such as Stable, Mild Instability, Hyperexcitable, Seizure Risk, Seizure Active, Depolarization Block, and Neural Suppression. The classification is based on predefined thresholds and patterns observed in the metrics and voltage data. The class also provides a method to convert the classified state into a human-readable string format for reporting purposes.
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
