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
        float gabaBHill,
        // Phase 3a: GAT1 reuptake block -- see NeuronUpdate.h's
        // BatchedStepLaunchInfo comment. Mechanism ints match
        // spp::synapse::TransporterBlockType (0=None, 1=Competitive,
        // 2=NonCompetitive).
        int gat1Mechanism,
        float gat1KiUm,
        float gat1Hill,
        float gat1MaxExtensionFold,
        // Phase 3b: GABA-A desensitization -- see NeuronUpdate.h's
        // BatchedStepLaunchInfo comment / Synapse.h's DesensitizationConfig
        // for the design.
        bool desensitizationEnabled,
        float desensitizationTauDesenseMs,
        float desensitizationTauRecoveryMs,
        float desensitizationMaxAttenuation,
        // Phase 3c: neuromodulator gain (D1/D2/5-HT1A/5-HT2A) -- see
        // NeuronUpdate.h's BatchedStepLaunchInfo comment / NeuromodulatorSystem.h
        // for the design and citations. Stateless (no persistent per-neuron
        // buffer, unlike desensitization above).
        float d1Ec50,
        float d1Hill,
        float d1MaxAdaptationReductionFrac,
        float d1MaxNmdaGainFold,
        float d2Ec50,
        float d2Hill,
        float d2MaxReleaseReductionFrac,
        float ht1aEc50,
        float ht1aHill,
        float ht1aMaxKGainFold,
        float ht1aAutoreceptorEc50,
        float ht1aAutoreceptorHill,
        float ht1aMaxAutoreceptorSuppressionFrac,
        float ht2aEc50,
        float ht2aHill,
        float ht2aMaxKReductionFrac,
        float ht2aMaxAdaptationReductionFrac,
        // Phase 3c retrofit: SERT/DAT reuptake block dose-amplification --
        // see NeuronUpdate.h's BatchedStepLaunchInfo comment /
        // ReuptakeTransporter.h's amplifiedDoseUm for the design. Mechanism
        // ints match spp::synapse::TransporterBlockType (0=None,
        // 1=Competitive, 2=NonCompetitive).
        int sertMechanism,
        float sertKiUm,
        float sertHill,
        float sertMaxExtensionFold,
        int datMechanism,
        float datKiUm,
        float datHill,
        float datMaxExtensionFold,
        // Phase 3c: vesicle pool dynamics -- see NeuronUpdate.h's
        // BatchedStepLaunchInfo comment / NeurotransmitterPool.h for the
        // design. No GPU kernel existed for this until now; main.cpp's
        // validateConfig() used to hard-reject vesiclePoolEnabled &&
        // use_cuda for exactly that reason -- that guard should be revisited
        // once this path is verified on real hardware.
        int vesiclePoolEnabled,
        float vesiclePoolRrpSize,
        float vesiclePoolReserveSize,
        float vesiclePoolRrpRefillTauMs,
        float vesiclePoolReserveRefillTauMs,
        float vesiclePoolCalciumFactor
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