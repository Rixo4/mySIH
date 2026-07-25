#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../neuron/NeuronModel.h"

namespace spp::cuda {

class CudaSimulator {
public:
    explicit CudaSimulator(std::size_t neuronCount);
    ~CudaSimulator();

    CudaSimulator(const CudaSimulator&) = delete;
    CudaSimulator& operator=(const CudaSimulator&) = delete;

    [[nodiscard]] bool available() const;

    void uploadInitialState(
        const std::vector<float>& v,
        const std::vector<float>& m,
        const std::vector<float>& h,
        const std::vector<float>& n,
        const std::vector<float>& s,
        const std::vector<float>& caCa,
        const std::vector<float>& threshold,
        const std::vector<float>& lastSpikeTime
    );

    void step(
        float timeMs,
        float dtMs,
        const neuron::HHParameters& params,
        float refractoryMs,
        const std::vector<float>& iSyn,
        const std::vector<float>& iExt,
        const std::vector<float>& iNoise,
        const std::vector<float>& gNaEff,
        const std::vector<float>& gKEff,
        const std::vector<float>& gCaEff,
        std::vector<float>& v,
        std::vector<float>& m,
        std::vector<float>& h,
        std::vector<float>& n,
        std::vector<float>& s,
        std::vector<float>& caCa,
        std::vector<float>& lastSpikeTime,
        std::vector<std::uint8_t>& spikes
    );

    void downloadState(
        std::vector<float>& v,
        std::vector<float>& m,
        std::vector<float>& h,
        std::vector<float>& n,
        std::vector<float>& s,
        std::vector<float>& caCa,
        std::vector<float>& lastSpikeTime
    );

#ifdef SPP_USE_CUDA
    void initializeBatchedSimulation(
        std::size_t delaySteps,
        const std::vector<std::uint32_t>& incomingOffsets,
        const std::vector<std::uint32_t>& incomingPre,
        const std::vector<std::uint32_t>& incomingDelay,
        const std::vector<float>& incomingWeight,
        const std::vector<std::int8_t>& incomingSign,
        const std::vector<float>& extCurrentWithBackground,
        const std::vector<float>& threshold,
        const std::vector<float>& noiseStd,
        const std::vector<float>& baseGNa,
        const std::vector<float>& baseGK,
        const std::vector<float>& baseGCa,
        const std::vector<std::uint8_t>& neuronType,
        const std::vector<float>& drugParams,
        const neuron::HHParameters& params,
        float refractoryMs,
        float dtMs,
        float adaptationTauMs,
        float adaptationIncrement,
        float adaptationMaxCurrent,
        float adaptationInhibitoryScale,
        float maxTotalCurrent,
        float synTauExcMs,
        float synTauInhMs,
        float maxSynCurrent,
        float drugOnsetTauMs,
        std::uint32_t rngSeed,
        std::size_t batchWindowSteps,
        // PHASE2_PLAN.md step 6: receptor peak-conductance scales and
        // flattened ReceptorDrugProfile -- see NeuronUpdate.h's
        // BatchedStepLaunchInfo comment for the mechanism int encoding
        // (0=None, 1=Block, 2=Potentiate, 3=Agonist, matching
        // spp::drug::ReceptorMechanism exactly).
        float gMaxAMPA,
        float gMaxNMDA,
        float gMaxGABAa,
        float gMaxGABAb,
        float gMaxGABAbAgonist,
        float ampaConductanceCeiling,
        int ampaMechanism,
        float ampaEc50,
        float ampaHill,
        int nmdaMechanism,
        float nmdaEc50,
        float nmdaHill,
        int gabaAMechanism,
        float gabaAEc50,
        float gabaAHill,
        float gabaAMaxPotentiation,
        int gabaBMechanism,
        float gabaBEc50,
        float gabaBHill
    );

    void stepBatched(float timeMs, float doseScale, std::size_t batchStepIndex);

    void downloadBatchedSpikeHistory(
        std::vector<std::uint8_t>& spikeHistory,
        std::size_t stepCountInBatch
    ) const;
#endif

private:
    std::size_t neuronCount_;
    bool available_;

    struct DeviceBuffers;
    DeviceBuffers* buffers_;
};

} // namespace spp::cuda