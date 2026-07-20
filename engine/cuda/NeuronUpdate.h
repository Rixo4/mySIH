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
