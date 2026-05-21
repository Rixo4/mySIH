#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../synapse/Synapse.h"

namespace spp::network {

struct NetworkConfig {
    std::size_t neuronCount = 1000;
    float excitatoryFraction = 0.80f;
    float connectionProbability = 0.10f;

    std::uint32_t minDelaySteps = 1;
    std::uint32_t maxDelaySteps = 30;

    float excitatoryWeightMean = 1.1f;
    float inhibitoryWeightMean = 1.4f;
    float weightStdFraction = 0.20f;

    float excitatoryWeightMin = 0.5f;
    float excitatoryWeightMax = 2.5f;
    float inhibitoryWeightMin = 0.3f;
    float inhibitoryWeightMax = 1.5f;

    float recurrentExcitatoryBias = 0.70f; // Higher E->E probability to support burst loops.
    float feedbackInhibitoryBias = 0.85f;  // I neurons mostly target E neurons for feedback inhibition.

    std::size_t maxSynapses = 20000000;
    std::uint32_t randomSeed = 12345;
};

class Network {
public:
    explicit Network(const NetworkConfig& config);

    void buildRandom();

    [[nodiscard]] const NetworkConfig& config() const { return config_; }
    [[nodiscard]] const std::vector<std::uint8_t>& neuronTypes() const { return neuronTypes_; }
    [[nodiscard]] const std::vector<synapse::SynapseEdge>& edges() const { return edges_; }
    [[nodiscard]] const synapse::SynapseMatrix& matrix() const { return matrix_; }

private:
    NetworkConfig config_;
    std::vector<std::uint8_t> neuronTypes_; // 1=Excitatory, 0=Inhibitory
    std::vector<synapse::SynapseEdge> edges_;
    synapse::SynapseMatrix matrix_;
};

} // namespace spp::network
