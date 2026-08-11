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

    // ─── Tier 2.4 part 2 follow-up: E->I synaptic weight scaling ──────────
    // Weights here are chosen by the SOURCE neuron's type only (see
    // Network.cpp's `excitatory ? excitatoryWeightMean : ...`), so an E->E
    // and an E->I synapse currently draw from an identical distribution.
    // Real cortex is markedly asymmetric: excitatory synapses onto PV+
    // fast-spiking interneurons are stronger, more reliable, and faster
    // than those onto other pyramidal cells (higher release probability,
    // larger unitary EPSCs) -- a major reason FS interneurons fire faster
    // than pyramidal cells in vivo.
    //
    // This is also the ONLY lever measured to have real authority over this
    // network's excitatory-vs-inhibitory RATE ratio. Real-hardware sweeps
    // (2026-08-09, PRECISION_GAP_CLOSURE_PLAN.md Tier 2.4 part 2) showed
    // four separate per-population parameters -- adaptation scale (even at
    // its 0.0 extreme), gK, gCa, and external drive -- all fail to move
    // that ratio beyond ~1.03, with drive showing clear saturation
    // (+0.030 ratio for the first +0.5 of offset, only +0.008 for the
    // next). That is the signature of balanced-network servo behaviour
    // (van Vreeswijk & Sompolinsky 1996; Brunel 2000): steady-state
    // population rates are set by the synaptic WEIGHT MATRIX and external
    // input, with intrinsic properties largely cancelling out. Changing the
    // weight matrix is therefore the mechanistically correct intervention,
    // not another neuron-level parameter.
    //
    // 1.0 = disabled / today's exact behaviour (E->I identical to E->E), so
    // every existing config stays bit-identical unless deliberately raised.
    // NOT calibrated: the DIRECTION (E->I stronger than E->E) is
    // literature-grounded, the magnitude is a fit parameter and must be
    // documented as such wherever a result depends on it.
    float excitatoryToInhibitoryWeightScale = 1.0f;

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