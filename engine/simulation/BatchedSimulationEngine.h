#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

#include "../cuda/CudaSimulator.h"
#include "../drug/DrugModel.h"
#include "../network/Network.h"
#include "../neuron/NeuronModel.h"
#include "../synapse/Synapse.h"
#include "SimulationEngine.h" // reuses SimulationConfig / SimulationResult

namespace spp::simulation {

// One independent (dose x repeat) block. Each block gets its own randomly
// generated network topology (biological-replicate style) and its own dose;
// all blocks share the same drug channel profile (IC50/Hill), the same
// timestep loop, and are stepped together on the GPU in a single kernel
// launch per timestep. Blocks never synapse with each other.
struct BatchBlockSpec {
    float dose = 0.0f;
    std::uint32_t networkSeed = 0; // seeds this block's own Network::buildRandom()
};

class BatchedSimulationEngine {
public:
    BatchedSimulationEngine(
        std::size_t neuronsPerBlock,
        const network::NetworkConfig& networkConfigTemplate,
        const SimulationConfig& simulationConfig,
        const drug::ChannelDrugProfile& drugProfile,
        const std::vector<BatchBlockSpec>& blocks,
        const drug::ReceptorDrugProfile& receptorProfile = drug::ReceptorDrugProfile{}
    );

    // Runs every block simultaneously through the shared timestep loop and
    // returns one SimulationResult per block, in the same order as `blocks`
    // was given to the constructor. Each SimulationResult has exactly the
    // shape a single-run SimulationEngine::run() would have produced, so
    // existing downstream code (Metrics, NetworkAnalyzer, etc.) needs no
    // changes.
    std::vector<SimulationResult> run();

    [[nodiscard]] std::size_t neuronsPerBlock() const { return neuronsPerBlock_; }
    [[nodiscard]] std::size_t blockCount() const { return blockCount_; }

private:
    std::size_t neuronsPerBlock_;
    std::size_t blockCount_;
    std::size_t totalNeurons_;

    SimulationConfig config_;
    drug::DrugModel drugModel_;
    std::vector<float> blockDoses_;
    drug::ChannelDrugProfile excProfile_;
    drug::ChannelDrugProfile inhProfile_;
    // PHASE2_PLAN.md step 4: single shared receptor profile across the whole
    // batch (the compound being tested), same as excProfile_/inhProfile_ are
    // one shared channel profile per neuron type. No per-block variant --
    // every block is the same drug at a different dose, not a different
    // drug. Not split by excitatory/inhibitory neuron type either, unlike
    // the channel profiles: receptor identity depends on the incoming
    // synapse type (which receptor a spike drives), not which type the
    // postsynaptic neuron is.
    drug::ReceptorDrugProfile receptorProfile_;

    neuron::NeuronPopulation population_;
    synapse::DelayBuffer delayBuffer_;
    synapse::SynapseMatrix matrix_;
    std::vector<std::uint8_t> combinedNeuronTypes_;

    std::mt19937 rng_;

    std::unique_ptr<cuda::CudaSimulator> cudaSimulator_;
};

} // namespace spp::simulation