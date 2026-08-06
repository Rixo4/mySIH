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

    // Phase 3a: GAT1 reuptake block (tiagabine) -- extends GABA-A's and
    // GABA-B's decay time constant instead of touching gMax/occupancy, see
    // engine/synapse/ReuptakeTransporter.h for the design rationale and
    // engine/drug/ReceptorDrugProfile.h's TransporterAction for the CPU-side
    // equivalent this mirrors. mechanism uses
    // spp::synapse::TransporterBlockType's numeric values (0=None,
    // 1=Competitive, 2=NonCompetitive) -- same static_cast-safe pattern as
    // the receptor mechanism ints above. Unlike gMaxAMPA etc., there is no
    // per-neuron override array on the GPU side -- since dose is already
    // per-neuron (via drugParams) and gat1Ki/gat1Hill/gat1MaxExtensionFold
    // are single scalars for the whole batch (one compound under test), the
    // kernel computes each neuron's effective GABA-A/B decay factor and
    // norm directly per-thread, same way it already computes
    // gabaAPotentiation/nmdaResidual per-thread from a per-neuron dose.
    int gat1Mechanism = 0;
    float gat1KiUm = 1.0e9f;
    float gat1Hill = 1.0f;
    float gat1MaxExtensionFold = 1.0f;

    // Phase 3b: GABA-A desensitization ("receptor tiredness") -- mirrors
    // synapse::DesensitizationConfig (Synapse.h) exactly, same equation, same
    // per-thread computation pattern as gat1* above (dose/drive is already
    // per-neuron via the kernel's own state, tau/attenuation are shared
    // scalars for the whole batch). See Synapse.h's DesensitizationConfig
    // comment for the full design rationale and literature citations.
    bool desensitizationEnabled = false;
    float desensitizationTauDesenseMs = 30000.0f;
    float desensitizationTauRecoveryMs = 124000.0f;
    float desensitizationMaxAttenuation = 0.9f;

    // Phase 3c: neuromodulator gain (D1/D2/5-HT1A/5-HT2A) -- mirrors
    // synapse::NeuromodulatorProfile / DrugModel::computeNeuromodulatorGainModifiers
    // (NeuromodulatorSystem.h/.cpp) exactly. Same per-thread computation
    // pattern as gat1* above: dose is already per-neuron (drugParams), and
    // every ec50/hill/ceiling below is a single shared scalar for the whole
    // batch, since receptorProfile_ is one shared compound under test --
    // exactly the same reasoning BatchedSimulationEngine.cpp's CPU path
    // uses (its blockNeuromodMods is computed once per dose block from that
    // same shared receptorProfile_). No persistent per-neuron state needed
    // (unlike desensitization) -- this is a stateless function of dose each
    // step, so no new BatchedStepDevicePointers fields either. See
    // NeuromodulatorSystem.h for the full design rationale and citations.
    float d1Ec50 = 1.0e9f;
    float d1Hill = 1.0f;
    float d1MaxAdaptationReductionFrac = 0.0f;
    float d1MaxNmdaGainFold = 1.0f;
    float d2Ec50 = 1.0e9f;
    float d2Hill = 1.0f;
    float d2MaxReleaseReductionFrac = 0.0f;
    float ht1aEc50 = 1.0e9f;
    float ht1aHill = 1.0f;
    float ht1aMaxKGainFold = 1.0f;
    // Tier 2.1 (PRECISION_GAP_CLOSURE_PLAN.md): presynaptic autoreceptor
    // pathway -- see NeuromodulatorSystem.h's Serotonin5HT1AAction comment.
    // SYNC WARNING: this is a separate copy from the host struct, same
    // manual-sync duplication as every other field in this section --
    // keep in sync with NeuromodulatorSystem.h if either changes.
    float ht1aAutoreceptorEc50 = 1.0e9f;
    float ht1aAutoreceptorHill = 1.0f;
    float ht1aMaxAutoreceptorSuppressionFrac = 0.0f;
    // Tier 2.1 correction: time-dependent desensitization of the
    // autoreceptor above -- uses this struct's EXISTING `timeMs` field
    // (see below), no new time field needed. Huge/inert defaults = never
    // desensitizes, same backward-compat guarantee as every other field.
    float ht1aAutoreceptorTauDesenseMs = 1.0e12f;
    float ht1aAutoreceptorTauRecoveryMs = 1.0e9f;
    float ht2aEc50 = 1.0e9f;
    float ht2aHill = 1.0f;
    float ht2aMaxKReductionFrac = 0.0f;
    float ht2aMaxAdaptationReductionFrac = 0.0f;

    // Phase 3c retrofit: SERT/DAT reuptake block dose-amplification -- see
    // ReuptakeTransporter.h's amplifiedDoseUm and NeuromodulatorSystem.h's
    // two-dose computeNeuromodulatorGainModifiers comment. DAT amplifies
    // the dose D1/D2 see; SERT amplifies the dose 5-HT1A/5-HT2A see.
    // Mechanism ints match spp::synapse::TransporterBlockType (0=None,
    // 1=Competitive, 2=NonCompetitive), same encoding as gat1Mechanism
    // above. No per-neuron override array needed, same reasoning as
    // gat1Ki/gat1Hill (dose is already per-neuron; these are shared scalars
    // for the whole batch).
    int sertMechanism = 0;
    float sertKiUm = 1.0e9f;
    float sertHill = 1.0f;
    float sertMaxExtensionFold = 1.0f;
    int datMechanism = 0;
    float datKiUm = 1.0e9f;
    float datHill = 1.0f;
    float datMaxExtensionFold = 1.0f;

    // Phase 3c: vesicle pool dynamics -- mirrors synapse::VesiclePoolConfig
    // (NeurotransmitterPool.h) exactly. Unlike gat1Ki/d1Ec50/etc above, this
    // DOES need persistent per-neuron state (BatchedStepDevicePointers::
    // vesicleRrp/vesicleReserve below), same reasoning as desensitization's
    // gabaADesensitization -- pool depletion/refill has to persist across
    // steps, it isn't a stateless function of dose. vesiclePoolEnabled is an
    // int (not bool) for the same launch-struct-is-POD-copied-to-device
    // reason gat1Mechanism/sertMechanism etc. are ints, not enums.
    int vesiclePoolEnabled = 0;
    float vesiclePoolRrpSize = 10.0f;
    float vesiclePoolReserveSize = 100.0f;
    float vesiclePoolRrpRefillTauMs = 1500.0f;
    float vesiclePoolReserveRefillTauMs = 20000.0f;
    float vesiclePoolCalciumFactor = 1.0f;
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

    // Phase 3b: persistent per-neuron GABA-A desensitization state (0..1
    // "tiredness"), mirrors ReceptorConductanceState::gabaADesensitization
    // (Synapse.h) exactly -- a single float per neuron, separate from the
    // packed 8-wide receptor state block above since it's one value instead
    // of a decay/rise pair.
    float* gabaADesensitization = nullptr;

    // Phase 3c: vesicle pool release-scale ring buffer, parallel to
    // delayBuffer above (same neuronCount*delaySteps shape, same indexing).
    // Mirrors DelayBuffer::releaseScale_ (Synapse.h) exactly -- defaults to
    // 1.0f everywhere (pure multiplicative no-op) unless vesiclePoolEnabled.
    // Persistent per-neuron pool state (rrp/reserve), mirrors
    // synapse::VesiclePoolState exactly -- one triplet's worth of state per
    // PRESYNAPTIC neuron (see NeurotransmitterPool.h's per-neuron-not-per-
    // synapse scope note), initialized to each pool's own fresh size.
    float* releaseScale = nullptr;
    float* vesicleRrp = nullptr;
    float* vesicleReserve = nullptr;

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