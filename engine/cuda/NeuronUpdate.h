#pragma once

#include <cstddef>

#include "../neuron/NeuronModel.h"

namespace spp::cuda {

struct NeuronUpdateLaunchInfo {
    int blockSize = 256;
    int gridSize = 1;
};

NeuronUpdateLaunchInfo computeNeuronUpdateLaunchInfo(
    std::size_t neuronCount,
    int preferredBlockSize = 256
);

#ifdef SPP_USE_CUDA

struct BatchedStepLaunchInfo {
    std::size_t neuronCount = 0;
    std::size_t delaySteps = 0;
    std::size_t oldHead = 0;
    std::size_t batchStepIndex = 0;
    float timeMs = 0.0f;
    float dtMs = 0.0f;
    float doseScale = 1.0f;
    float refractoryMs = 0.0f;
    float synTauExcMs = 0.0f;
    float synTauInhMs = 0.0f;
    float maxSynCurrent = 0.0f;
    float maxTotalCurrent = 0.0f;
    float adaptationTauMs = 0.0f;
    float adaptationIncrement = 0.0f;
    float adaptationMaxCurrent = 0.0f;
    float adaptationInhibitoryScale = 0.0f;
    neuron::HHParameters params;

    // PHASE2_PLAN.md step 6: CUDA mirror of the four receptor conductances
    // (SimulationConfig::gMax* in SimulationEngine.h). Peak-conductance
    // scales for AMPA/NMDA/GABA-A/GABA-B's true-conductance currents, same
    // values the CPU path already uses -- see BatchedSimulationEngine.cpp's
    // CPU fallback loop for the reference math this mirrors exactly.
    float gMaxAMPA = 0.0f;
    float gMaxNMDA = 0.0f;
    float gMaxGABAa = 0.0f;
    float gMaxGABAb = 0.0f;
    float gMaxGABAbAgonist = 0.0f;
    float ampaConductanceCeiling = 1.0f;

    // Flattened ReceptorDrugProfile (ReceptorDrugProfile.h) -- one shared
    // profile for the whole batch (single compound under test), same as
    // BatchedSimulationEngine's receptorProfile_ member. Mechanism ints
    // match spp::drug::ReceptorMechanism's numeric values exactly
    // (0=None, 1=Block, 2=Potentiate, 3=Agonist), so the host side can just
    // static_cast the enum when populating these.
    int ampaMechanism = 0;
    float ampaEc50 = 1.0e9f;
    float ampaHill = 1.0f;
    int nmdaMechanism = 0;
    float nmdaEc50 = 1.0e9f;
    float nmdaHill = 1.0f;
    int gabaAMechanism = 0;
    float gabaAEc50 = 1.0e9f;
    float gabaAHill = 1.0f;
    float gabaAMaxPotentiation = 1.0f;
    int gabaBMechanism = 0;
    float gabaBEc50 = 1.0e9f;
    float gabaBHill = 1.0f;
};

struct BatchedStepDevicePointers {
    const std::uint32_t* incomingOffsets = nullptr;
    const std::uint32_t* incomingPre = nullptr;
    const std::uint32_t* incomingDelay = nullptr;
    const float* incomingWeight = nullptr;
    const std::int8_t* incomingSign = nullptr;

    std::uint8_t* delayBuffer = nullptr;
    float* iExcState = nullptr;
    float* iInhState = nullptr;
    float* adaptationCurrent = nullptr;
    const float* extCurrentWithBackground = nullptr;
    const float* noiseStd = nullptr;
    const float* threshold = nullptr;
    const std::uint8_t* neuronType = nullptr;
    const float* baseGNa = nullptr;
    const float* baseGK = nullptr;
    const float* baseGCa = nullptr;
    const float* drugParams = nullptr;

    // PHASE2_PLAN.md step 6: persistent per-neuron receptor conductance
    // state (decay/rise accumulator pair per receptor), mirrors
    // synapse::ReceptorConductanceState exactly -- see Synapse.h's comment
    // for why this decay-accumulator form is used instead of replaying
    // spike history. Packed into one device allocation on the host side
    // (CudaSimulator::DeviceBuffers::batchedReceptorState), these are the
    // eight sub-pointers into it.
    float* receptorAmpaDecay = nullptr;
    float* receptorAmpaRise = nullptr;
    float* receptorNmdaDecay = nullptr;
    float* receptorNmdaRise = nullptr;
    float* receptorGabaADecay = nullptr;
    float* receptorGabaARise = nullptr;
    float* receptorGabaBDecay = nullptr;
    float* receptorGabaBRise = nullptr;

    float* v = nullptr;
    float* m = nullptr;
    float* h = nullptr;
    float* n = nullptr;
    float* s = nullptr;
    float* caCa = nullptr;
    float* lastSpikeTime = nullptr;

    void* rngStates = nullptr;
    std::uint8_t* spikeHistory = nullptr;
};

void launchBatchedStepKernel(
    const BatchedStepLaunchInfo& launchInfo,
    const BatchedStepDevicePointers& devicePointers
);

void initializeBatchedRandomStates(
    std::size_t neuronCount,
    std::uint32_t seed,
    void* rngStates
);

#endif

} // namespace spp::cuda