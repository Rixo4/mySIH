#include "BatchedSimulationEngine.h"
#include "../neuron/ReceptorModel.h"
#include "../synapse/NeurotransmitterPool.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace spp::simulation {

BatchedSimulationEngine::BatchedSimulationEngine(
    std::size_t neuronsPerBlock,
    const network::NetworkConfig& networkConfigTemplate,
    const SimulationConfig& simulationConfig,
    const drug::ChannelDrugProfile& drugProfile,
    const std::vector<BatchBlockSpec>& blocks,
    const drug::ReceptorDrugProfile& receptorProfile
) : neuronsPerBlock_(neuronsPerBlock),
    blockCount_(blocks.size()),
    totalNeurons_(neuronsPerBlock * blocks.size()),
    config_(simulationConfig),
    drugModel_(),
    population_(neuronsPerBlock * blocks.size()),
    rng_(simulationConfig.randomSeed)
{
    if (neuronsPerBlock_ == 0) {
        throw std::invalid_argument("BatchedSimulationEngine neuronsPerBlock must be > 0.");
    }
    if (blockCount_ == 0) {
        throw std::invalid_argument("BatchedSimulationEngine requires at least one block.");
    }
    if (config_.dtMs <= 0.0f) {
        throw std::invalid_argument("Simulation dtMs must be > 0.");
    }
    if (config_.durationMs <= config_.dtMs) {
        throw std::invalid_argument("Simulation duration must be greater than dtMs.");
    }

    // ---- Build one independent network per block, then merge into a single
    // ---- block-diagonal SynapseMatrix. Blocks never share edges, so
    // ---- spiking in one dose/repeat can never leak into another.
    std::vector<synapse::SynapseEdge> combinedEdges;
    combinedNeuronTypes_.resize(totalNeurons_);

    std::uint32_t maxDelaySteps = networkConfigTemplate.maxDelaySteps;

    for (std::size_t b = 0; b < blockCount_; ++b) {
        network::NetworkConfig blockConfig = networkConfigTemplate;
        blockConfig.neuronCount = neuronsPerBlock_;
        blockConfig.randomSeed = blocks[b].networkSeed;

        network::Network blockNet(blockConfig);
        blockNet.buildRandom();

        const std::size_t offset = b * neuronsPerBlock_;
        for (const synapse::SynapseEdge& edge : blockNet.edges()) {
            synapse::SynapseEdge shifted = edge;
            shifted.preNeuron = static_cast<std::uint32_t>(edge.preNeuron + offset);
            shifted.postNeuron = static_cast<std::uint32_t>(edge.postNeuron + offset);
            combinedEdges.push_back(shifted);
        }

        const auto& types = blockNet.neuronTypes();
        if (types.size() != neuronsPerBlock_) {
            throw std::runtime_error("Block network neuron type vector size mismatch.");
        }
        std::copy(types.begin(), types.end(), combinedNeuronTypes_.begin() + static_cast<std::ptrdiff_t>(offset));

        maxDelaySteps = std::max(maxDelaySteps, blockConfig.maxDelaySteps);
    }

    matrix_.build(totalNeurons_, combinedEdges);
    delayBuffer_.resize(totalNeurons_, std::max<std::size_t>(2, static_cast<std::size_t>(maxDelaySteps) + 2));

    // ---- Population init (same statistical distributions as a single run,
    // ---- just drawn once across the whole batch's neuron count).
    population_.initialize(
        config_.baseExternalCurrent,
        config_.externalCurrentStd,
        config_.baseNoiseStd,
        config_.randomSeed
    );
    population_.params.gL = std::clamp(population_.params.gL * 1.02f, 0.22f, 0.40f);

    population_.neuronType = combinedNeuronTypes_;
    for (std::size_t i = 0; i < totalNeurons_; ++i) {
        if (population_.neuronType[i] == 1U) {
            population_.extCurrent[i] += 0.15f;
            population_.threshold[i] -= 0.8f;
        } else {
            population_.extCurrent[i] -= 0.45f;
            population_.threshold[i] += 1.5f;
        }
    }

    delayBuffer_.clear();

    // ---- Per-neuron drug dosing: one DrugModel, one shared channel profile
    // ---- (the compound being tested), but each block's neurons are pinned
    // ---- to that block's dose via the existing per-neuron dose mechanism.
    drugModel_.setGlobalProfile(drugProfile);
    drugModel_.enablePerNeuronProfiles(totalNeurons_);
    for (std::size_t b = 0; b < blockCount_; ++b) {
            const std::size_t offset = b * neuronsPerBlock_;
            for (std::size_t j = 0; j < neuronsPerBlock_; ++j) {
                const std::size_t idx = offset + j;
                drug::ChannelDrugProfile p = drugProfile;
                if (population_.neuronType[idx] == 0U) { // inhibitory
                    p.ic50K *= 1.75f;
                }
                drugModel_.setNeuronProfile(idx, p);
                drugModel_.setNeuronDose(idx, blocks[b].dose);
            }
    }

    // ---- Cache per-block dose and the two possible drug profiles
    // ---- (excitatory/inhibitory), so the hot loop can compute Hill-equation
    // ---- block fractions once per (block, type) pair per timestep instead
    // ---- of once per neuron (all neurons of the same type in the same
    // ---- block share an identical dose + profile, so this is exact, not
    // ---- an approximation).
    blockDoses_.resize(blockCount_);
    for (std::size_t b = 0; b < blockCount_; ++b) {
        blockDoses_[b] = blocks[b].dose;
    }
    excProfile_ = drugProfile;
    inhProfile_ = drugProfile;
    inhProfile_.ic50K *= 1.75f;
    receptorProfile_ = receptorProfile;

    // ---- Single CudaSimulator sized for the WHOLE batch. This is the crux
    // ---- of the fix: one set of H2D/kernel/D2H calls per timestep covers
    // ---- every dose x repeat at once, instead of one set per run.
    cudaSimulator_ = std::make_unique<cuda::CudaSimulator>(totalNeurons_);
    if (config_.useGpu && cudaSimulator_->available()) {
        cudaSimulator_->uploadInitialState(
            population_.v,
            population_.m,
            population_.h,
            population_.n,
            population_.s,
            population_.caCa,
            population_.threshold,
            population_.lastSpikeTime
        );
#ifdef SPP_USE_CUDA
        std::vector<float> extCurrentWithBackground(totalNeurons_);
        std::vector<float> baseGNa = population_.gNa;
        std::vector<float> baseGK = population_.gK;
        std::vector<float> baseGCa = population_.gCa;
        std::vector<float> noiseStd = population_.noiseStd;
        std::vector<std::uint8_t> neuronType = population_.neuronType;
        std::vector<float> drugParams(totalNeurons_ * 7U);

        constexpr float kBackgroundCurrent = 0.18f;
        for (std::size_t i = 0; i < totalNeurons_; ++i) {
            float baseCurrent = population_.extCurrent[i];
            if (!std::isfinite(baseCurrent)) {
                baseCurrent = config_.baseExternalCurrent;
            }
            extCurrentWithBackground[i] = baseCurrent + kBackgroundCurrent;

            const std::size_t block = i / neuronsPerBlock_;
            const bool inhibitory = (population_.neuronType[i] == 0U);
            const drug::ChannelDrugProfile& prof = inhibitory ? inhProfile_ : excProfile_;
            const std::size_t offset = i * 7U;
            drugParams[offset + 0] = prof.ic50Na;
            drugParams[offset + 1] = prof.ic50K;
            drugParams[offset + 2] = prof.ic50Ca;
            drugParams[offset + 3] = prof.hillNa;
            drugParams[offset + 4] = prof.hillK;
            drugParams[offset + 5] = prof.hillCa;
            drugParams[offset + 6] = blockDoses_[block];
        }

        cudaSimulator_->initializeBatchedSimulation(
            delayBuffer_.delaySteps(),
            matrix_.incomingOffsets(),
            matrix_.incomingPre(),
            matrix_.incomingDelay(),
            matrix_.incomingWeight(),
            matrix_.incomingSign(),
            extCurrentWithBackground,
            population_.threshold,
            noiseStd,
            baseGNa,
            baseGK,
            baseGCa,
            neuronType,
            drugParams,
            population_.params,
            config_.refractoryMs,
            config_.dtMs,
            config_.adaptationTauMs,
            config_.adaptationIncrement,
            config_.adaptationMaxCurrent,
            config_.adaptationInhibitoryScale,
            config_.maxTotalCurrent,
            config_.synTauExcMs,
            config_.synTauInhMs,
            config_.maxSynCurrent,
            config_.drugOnsetTauMs,
            config_.randomSeed,
            50U,
            // PHASE2_PLAN.md step 6: receptor peak-conductance scales +
            // flattened receptorProfile_, same values the CPU fallback
            // loop below reads from config_/receptorProfile_ directly.
            // static_cast<int> on the mechanism enums works because
            // ReceptorMechanism's numeric values (None=0/Block=1/
            // Potentiate=2/Agonist=3) are exactly what the CUDA kernel's
            // launchInfo.*Mechanism ints expect -- see NeuronUpdate.h.
            config_.gMaxAMPA,
            config_.gMaxNMDA,
            config_.gMaxGABAa,
            config_.gMaxGABAb,
            config_.gMaxGABAbAgonist,
            config_.ampaConductanceCeiling,
            static_cast<int>(receptorProfile_.ampa.mechanism),
            receptorProfile_.ampa.ec50,
            receptorProfile_.ampa.hill,
            static_cast<int>(receptorProfile_.nmda.mechanism),
            receptorProfile_.nmda.ec50,
            receptorProfile_.nmda.hill,
            static_cast<int>(receptorProfile_.gabaA.mechanism),
            receptorProfile_.gabaA.ec50,
            receptorProfile_.gabaA.hill,
            receptorProfile_.gabaA.maxPotentiationFactor,
            static_cast<int>(receptorProfile_.gabaB.mechanism),
            receptorProfile_.gabaB.ec50,
            receptorProfile_.gabaB.hill,
            // Phase 3a: GAT1 reuptake block -- static_cast<int> works the
            // same way, spp::synapse::TransporterBlockType's numeric values
            // (None=0/Competitive=1/NonCompetitive=2) match the CUDA
            // kernel's launchInfo.gat1Mechanism int expectations exactly.
            static_cast<int>(receptorProfile_.gat1.mechanism),
            receptorProfile_.gat1.kiUm,
            receptorProfile_.gat1.hill,
            receptorProfile_.gat1.maxExtensionFold,
            // Phase 3b: GABA-A desensitization -- see CudaSimulator.h /
            // NeuronUpdate.h's BatchedStepLaunchInfo comment.
            config_.desensitizationEnabled,
            config_.desensitizationTauDesenseMs,
            config_.desensitizationTauRecoveryMs,
            config_.desensitizationMaxAttenuation,
            // Phase 3c: neuromodulator gain -- same shared-profile values
            // the CPU fallback loop reads from receptorProfile_.neuromod
            // directly (see blockNeuromodMods above).
            receptorProfile_.neuromod.d1.ec50,
            receptorProfile_.neuromod.d1.hill,
            receptorProfile_.neuromod.d1.maxAdaptationReductionFrac,
            receptorProfile_.neuromod.d1.maxNmdaGainFold,
            receptorProfile_.neuromod.d2.ec50,
            receptorProfile_.neuromod.d2.hill,
            receptorProfile_.neuromod.d2.maxReleaseReductionFrac,
            receptorProfile_.neuromod.ht1a.ec50,
            receptorProfile_.neuromod.ht1a.hill,
            receptorProfile_.neuromod.ht1a.maxKGainFold,
            // Tier 2.1: presynaptic autoreceptor pathway -- same
            // shared-profile passthrough as the postsynaptic fields above.
            receptorProfile_.neuromod.ht1a.autoreceptorEc50,
            receptorProfile_.neuromod.ht1a.autoreceptorHill,
            receptorProfile_.neuromod.ht1a.maxAutoreceptorSuppressionFrac,
            receptorProfile_.neuromod.ht2a.ec50,
            receptorProfile_.neuromod.ht2a.hill,
            receptorProfile_.neuromod.ht2a.maxKReductionFrac,
            receptorProfile_.neuromod.ht2a.maxAdaptationReductionFrac,
            // Phase 3c retrofit: SERT/DAT reuptake block dose-amplification
            // -- same shared-profile values the CPU fallback loop reads
            // from receptorProfile_.sert/dat directly (see
            // DrugModel::amplifiedDoseForDopamine/amplifiedDoseForSerotonin).
            static_cast<int>(receptorProfile_.sert.mechanism),
            receptorProfile_.sert.kiUm,
            receptorProfile_.sert.hill,
            receptorProfile_.sert.maxExtensionFold,
            static_cast<int>(receptorProfile_.dat.mechanism),
            receptorProfile_.dat.kiUm,
            receptorProfile_.dat.hill,
            receptorProfile_.dat.maxExtensionFold,
            // Phase 3c: vesicle pool dynamics -- same config_.vesiclePool*
            // fields the CPU fallback loop below reads directly (see
            // vesiclePoolConfig construction further down in this
            // function). static_cast<int> for the bool, same POD-copied-
            // to-device reasoning as every other mechanism int above.
            static_cast<int>(config_.vesiclePoolEnabled),
            config_.vesiclePoolRrpSize,
            config_.vesiclePoolReserveSize,
            config_.vesiclePoolRrpRefillTauMs,
            config_.vesiclePoolReserveRefillTauMs,
            config_.vesiclePoolCalciumFactor
        );
#endif
    }
}

std::vector<SimulationResult> BatchedSimulationEngine::run() {
    const std::size_t stepCount = static_cast<std::size_t>(std::ceil(config_.durationMs / config_.dtMs));

    std::vector<SimulationResult> results(blockCount_);
    for (std::size_t b = 0; b < blockCount_; ++b) {
        SimulationResult& r = results[b];
        r.spikeTimes.assign(neuronsPerBlock_, {});
        for (auto& train : r.spikeTimes) {
            train.reserve(static_cast<std::size_t>(config_.durationMs * 0.02f));
        }
        r.populationSpikesPerStep.assign(stepCount, 0U);
        r.neuronTypes.assign(
            combinedNeuronTypes_.begin() + static_cast<std::ptrdiff_t>(b * neuronsPerBlock_),
            combinedNeuronTypes_.begin() + static_cast<std::ptrdiff_t>((b + 1) * neuronsPerBlock_)
        );
        r.dtMs = config_.dtMs;
        r.durationMs = config_.durationMs;
    }

    // iSyn is the old flat scalar synaptic current -- now permanently 0.0f
    // for the entire run. All four receptors have moved to the true-
    // conductance path (synapticEff below) -- see SimulationEngine::run()'s
    // comment for the full explanation and what this means for the GPU path.
    std::vector<float> iSyn(totalNeurons_, 0.0f);
    std::vector<float> iNoise(totalNeurons_, 0.0f);

    // All four receptors' true conductance path -- same pattern as
    // SimulationEngine::run(). GABA-A -> NMDA -> GABA-B -> AMPA build order
    // is complete; all four fields in synapticEff are consumed below.
    std::vector<synapse::ReceptorConductanceState> receptorStates(totalNeurons_);
    std::vector<synapse::ReceptorConductances> receptorConductances(totalNeurons_);
    std::vector<neuron::SynapticConductances> synapticEff(totalNeurons_);

    std::vector<float> gNaEff(totalNeurons_, 0.0f);
    std::vector<float> gKEff(totalNeurons_, 0.0f);
    std::vector<float> gCaEff(totalNeurons_, 0.0f);
    std::vector<float> extCurrentWithBackground(totalNeurons_, 0.0f);
    std::vector<float> effectiveExternalCurrent(totalNeurons_, 0.0f);
    std::vector<float> adaptationCurrent(totalNeurons_, 0.0f);

    std::vector<std::uint8_t> spikes(totalNeurons_, 0U);
    std::vector<std::uint32_t> blockSpikeCount(blockCount_, 0U);
    std::vector<std::uint8_t> batchSpikeHistory;

    // Phase 3c: vesicle pool dynamics, CPU path only for now (see
    // NeurotransmitterPool.h's design notes). One pool triplet per
    // PRESYNAPTIC neuron -- see the header's per-neuron-not-per-synapse
    // scope note -- initialized to each pool's own configured fresh size
    // (not VesiclePoolState's own defaults, which may not match a custom
    // config). releaseScales mirrors spikes but carries the vesicle-pool
    // release-scale multiplier instead of a spike bit; defaults to 1.0
    // (no-op) and is only ever set away from 1.0 for a neuron that spiked
    // this step.
    synapse::VesiclePoolConfig vesiclePoolConfig;
    vesiclePoolConfig.enabled = config_.vesiclePoolEnabled;
    vesiclePoolConfig.rrpSize = config_.vesiclePoolRrpSize;
    vesiclePoolConfig.reserveSize = config_.vesiclePoolReserveSize;
    vesiclePoolConfig.rrpRefillTauMs = config_.vesiclePoolRrpRefillTauMs;
    vesiclePoolConfig.reserveRefillTauMs = config_.vesiclePoolReserveRefillTauMs;
    vesiclePoolConfig.calciumFactor = config_.vesiclePoolCalciumFactor;

    std::vector<synapse::VesiclePoolState> vesiclePoolStates(totalNeurons_);
    for (auto& poolState : vesiclePoolStates) {
        poolState.rrp = vesiclePoolConfig.rrpSize;
        poolState.reserve = vesiclePoolConfig.reserveSize;
    }
    std::vector<float> releaseScales(totalNeurons_, 1.0f);

    // Per-(block, type) Hill-equation block fractions, recomputed once per
    // timestep instead of once per neuron. Index = block * 2 + type
    // (type: 0 = inhibitory, 1 = excitatory). {blockNa, blockK, blockCa}.
    std::vector<std::array<float, 3>> blockTypeBlockFrac(blockCount_ * 2);

    // PHASE2_PLAN.md step 4: per-block receptor drug modifiers, recomputed
    // once per timestep -- no per-type split needed (see receptorProfile_
    // comment in the header), just per-block since each block has its own
    // dose.
    std::vector<drug::ReceptorConductanceModifiers> blockReceptorMods(blockCount_);

    // Phase 3c: per-block neuromodulator gain modifiers (D1/D2/5-HT1A/
    // 5-HT2A), recomputed once per timestep alongside blockReceptorMods --
    // same "per-block since each block has its own dose" reasoning. All
    // fields default to 1.0 (inert) via NeuromodulatorGainModifiers{}, so an
    // unconfigured profile leaves gKEff/gMaxNMDA/adaptation/excitatory-weight
    // exactly as they were before Phase 3c existed.
    std::vector<synapse::NeuromodulatorGainModifiers> blockNeuromodMods(blockCount_);

    // Phase 3a: precompute per-neuron drug-modified decay kinetics ONCE
    // (not per timestep -- dose is fixed per block for the whole run). Only
    // built when a transporter-blocking mechanism is actually configured;
    // otherwise nullptr is passed, so accumulateReceptorConductances falls
    // back to the exact original fixed-constant behavior with zero
    // overhead. NOTE simplification: uses each block's FINAL dose (not the
    // onset-ramped effDose used by blockReceptorMods below) -- the decay
    // time constant switches to its fully-blocked-transporter value from
    // step 0, while the conductance modifiers still ramp in over
    // drugOnsetTauMs. Documented rather than silently approximated; the
    // difference only matters during the onset transient.
    const bool needsKineticsOverride =
        receptorProfile_.eaat.mechanism != synapse::TransporterBlockType::None ||
        receptorProfile_.gat1.mechanism != synapse::TransporterBlockType::None;
    synapse::ReceptorKineticsOverride kineticsOverride;
    if (needsKineticsOverride) {
        kineticsOverride.ampaDecayF.resize(totalNeurons_);
        kineticsOverride.ampaNorm.resize(totalNeurons_);
        kineticsOverride.nmdaDecayF.resize(totalNeurons_);
        kineticsOverride.nmdaNorm.resize(totalNeurons_);
        kineticsOverride.gabaADecayF.resize(totalNeurons_);
        kineticsOverride.gabaANorm.resize(totalNeurons_);
        kineticsOverride.gabaBDecayF.resize(totalNeurons_);
        kineticsOverride.gabaBNorm.resize(totalNeurons_);

        const auto expDecay = [](float dt, float tau) {
            return (tau > 0.0f) ? std::exp(-dt / tau) : 0.0f;
        };

        for (std::size_t b = 0; b < blockCount_; ++b) {
            const drug::ReceptorKineticsModifiers km =
                drug::DrugModel::computeReceptorKineticsModifiers(receptorProfile_, blockDoses_[b]);

            const float blockAmpaDecayF  = expDecay(config_.dtMs, km.ampaTauDecayMs);
            const float blockNmdaDecayF  = expDecay(config_.dtMs, km.nmdaTauDecayMs);
            const float blockGabaADecayF = expDecay(config_.dtMs, km.gabaATauDecayMs);
            const float blockGabaBDecayF = expDecay(config_.dtMs, km.gabaBTauDecayMs);

            const neuron::DualExpKernel ampaKernel{neuron::ReceptorKinetics::kAmpaTauRiseMs, km.ampaTauDecayMs, 0.0f};
            const neuron::DualExpKernel nmdaKernel{neuron::ReceptorKinetics::kNmdaTauRiseMs, km.nmdaTauDecayMs, 0.0f};
            const neuron::DualExpKernel gabaAKernel{neuron::ReceptorKinetics::kGabaATauRiseMs, km.gabaATauDecayMs, 0.0f};
            const neuron::DualExpKernel gabaBKernel{neuron::ReceptorKinetics::kGabaBTauRiseMs, km.gabaBTauDecayMs, 0.0f};

            const float blockAmpaNorm  = ampaKernel.normFactor();
            const float blockNmdaNorm  = nmdaKernel.normFactor();
            const float blockGabaANorm = gabaAKernel.normFactor();
            const float blockGabaBNorm = gabaBKernel.normFactor();

            const std::size_t offset = b * neuronsPerBlock_;
            for (std::size_t j = 0; j < neuronsPerBlock_; ++j) {
                const std::size_t idx = offset + j;
                kineticsOverride.ampaDecayF[idx]  = blockAmpaDecayF;
                kineticsOverride.ampaNorm[idx]    = blockAmpaNorm;
                kineticsOverride.nmdaDecayF[idx]  = blockNmdaDecayF;
                kineticsOverride.nmdaNorm[idx]    = blockNmdaNorm;
                kineticsOverride.gabaADecayF[idx] = blockGabaADecayF;
                kineticsOverride.gabaANorm[idx]   = blockGabaANorm;
                kineticsOverride.gabaBDecayF[idx] = blockGabaBDecayF;
                kineticsOverride.gabaBNorm[idx]   = blockGabaBNorm;
            }
        }
    }

    // Phase 3b: built once, reused across the whole run (config is fixed
    // per run). nullptr-equivalent when disabled -- accumulateReceptorConductances
    // treats desensitization->enabled == false exactly like a nullptr.
    synapse::DesensitizationConfig desensitizationConfig;
    desensitizationConfig.enabled = config_.desensitizationEnabled;
    desensitizationConfig.tauDesenseMs = config_.desensitizationTauDesenseMs;
    desensitizationConfig.tauRecoveryMs = config_.desensitizationTauRecoveryMs;
    desensitizationConfig.maxAttenuation = config_.desensitizationMaxAttenuation;

    std::normal_distribution<float> unitNormal(0.0f, 1.0f);
    const bool runOnGpu = config_.useGpu && cudaSimulator_ && cudaSimulator_->available();
    lastRunUsedGpu_ = runOnGpu;
    const float adaptTauMs = std::max(1.0f, config_.adaptationTauMs);
    const float adaptationDecay = std::exp(-config_.dtMs / adaptTauMs);
    const float adaptationIncrement = std::max(0.0f, config_.adaptationIncrement);
    const float adaptationMaxCurrent = std::max(0.0f, config_.adaptationMaxCurrent);
    const float adaptationInhibitoryScale = std::clamp(config_.adaptationInhibitoryScale, 0.0f, 1.0f);
    const float drugOnsetTauMs = std::max(0.0f, config_.drugOnsetTauMs);
    constexpr float kBackgroundCurrent = 0.18f;

    for (std::size_t i = 0; i < totalNeurons_; ++i) {
        float baseCurrent = population_.extCurrent[i];
        if (!std::isfinite(baseCurrent)) {
            baseCurrent = config_.baseExternalCurrent;
        }
        extCurrentWithBackground[i] = baseCurrent + kBackgroundCurrent;
    }

#ifdef SPP_USE_CUDA
    // PHASE2_PLAN.md step 6 (done): this GPU-resident batched path
    // (stepBatched -> fusedBatchedStepKernel in NeuronUpdate.cu) now
    // mirrors all four receptor conductances and the block/potentiate/
    // agonist drug math, same formulas as the CPU loop below. NOT yet
    // verified on real GPU hardware by this session (SPP_FORCE_CPU=1 was
    // used throughout) -- treat as implemented-but-unverified until a real
    // CUDA run confirms it matches the CPU path's dose-response curves.
    // The non-batched single-run GPU step() path (used only by plain
    // --simulate GPU mode, not --dose-eval) is intentionally NOT mirrored
    // -- see NeuronUpdate.cu's hhStepKernel comment.
    if (runOnGpu) {
        constexpr std::size_t kGpuBatchSteps = 50U;
        for (std::size_t batchStart = 0; batchStart < stepCount; batchStart += kGpuBatchSteps) {
            const std::size_t stepsInBatch = std::min<std::size_t>(kGpuBatchSteps, stepCount - batchStart);

            for (std::size_t localStep = 0; localStep < stepsInBatch; ++localStep) {
                const std::size_t globalStep = batchStart + localStep;
                const float timeMs = static_cast<float>(globalStep) * config_.dtMs;
                const float doseScale = (drugOnsetTauMs > 1.0e-3f)
                                            ? (1.0f - std::exp(-timeMs / drugOnsetTauMs))
                                            : 1.0f;
                cudaSimulator_->stepBatched(timeMs, doseScale, localStep);
            }

            cudaSimulator_->downloadBatchedSpikeHistory(batchSpikeHistory, stepsInBatch);

            for (std::size_t localStep = 0; localStep < stepsInBatch; ++localStep) {
                const std::size_t globalStep = batchStart + localStep;
                const float timeMs = static_cast<float>(globalStep) * config_.dtMs;
                const std::size_t rowOffset = localStep * totalNeurons_;

                std::fill(blockSpikeCount.begin(), blockSpikeCount.end(), 0U);

                for (std::size_t i = 0; i < totalNeurons_; ++i) {
                    if (batchSpikeHistory[rowOffset + i] == 0U) {
                        continue;
                    }

                    const std::size_t block = i / neuronsPerBlock_;
                    const std::size_t local = i % neuronsPerBlock_;
                    ++blockSpikeCount[block];
                    results[block].spikeTimes[local].push_back(timeMs);
                }

                for (std::size_t b = 0; b < blockCount_; ++b) {
                    results[b].populationSpikesPerStep[globalStep] = blockSpikeCount[b];
                }
            }
        }

        cudaSimulator_->downloadState(
            population_.v,
            population_.m,
            population_.h,
            population_.n,
            population_.s,
            population_.caCa,
            population_.lastSpikeTime
        );

        for (std::size_t b = 0; b < blockCount_; ++b) {
            const std::size_t offset = b * neuronsPerBlock_;
            results[b].finalVoltages.assign(
                population_.v.begin() + static_cast<std::ptrdiff_t>(offset),
                population_.v.begin() + static_cast<std::ptrdiff_t>(offset + neuronsPerBlock_)
            );
        }

        return results;
    }
#endif

    for (std::size_t step = 0; step < stepCount; ++step) {
        const float timeMs = static_cast<float>(step) * config_.dtMs;
        const float doseScale = (drugOnsetTauMs > 1.0e-3f)
                                    ? (1.0f - std::exp(-timeMs / drugOnsetTauMs))
                                    : 1.0f;

        // All four receptor conductances come from here now -- CPU fallback
        // only (see the #ifdef SPP_USE_CUDA early-return above for the GPU
        // path, which does not reflect any receptor -- same gap noted in
        // SimulationEngine::run()). The old flat accumulateSynapticCurrents
        // path has been fully retired now that AMPA has moved to the true
        // conductance model too.
        matrix_.accumulateReceptorConductances(
            delayBuffer_, receptorStates, config_.dtMs, receptorConductances,
            needsKineticsOverride ? &kineticsOverride : nullptr,
            &desensitizationConfig);

        for (std::size_t b = 0; b < blockCount_; ++b) {
            const float effDose = blockDoses_[b] * doseScale;
            for (int t = 0; t < 2; ++t) {
                const drug::ChannelDrugProfile& prof = (t == 1) ? excProfile_ : inhProfile_;
                blockTypeBlockFrac[b * 2 + static_cast<std::size_t>(t)] = {
                    drug::DrugModel::hillBlock(effDose, prof.ic50Na, prof.hillNa),
                    drug::DrugModel::hillBlock(effDose, prof.ic50K,  prof.hillK),
                    drug::DrugModel::hillBlock(effDose, prof.ic50Ca, prof.hillCa)
                };
            }
            blockReceptorMods[b] = drug::DrugModel::computeReceptorModifiers(receptorProfile_, effDose);
            blockNeuromodMods[b] = drug::DrugModel::computeNeuromodulatorGainModifiers(receptorProfile_, effDose);
        }

        for (std::size_t i = 0; i < totalNeurons_; ++i) {
            adaptationCurrent[i] = std::clamp(adaptationCurrent[i] * adaptationDecay, 0.0f, adaptationMaxCurrent);

            // Phase 3c: continuous pool refill, every neuron, every step,
            // regardless of whether it spikes -- refill is a background
            // process independent of firing (see NeurotransmitterPool.h).
            // No-op when vesiclePoolConfig.enabled is false.
            synapse::refillVesiclePools(vesiclePoolStates[i], vesiclePoolConfig, config_.dtMs);

            // All four receptors: real conductance currents, same as
            // SimulationEngine::run() -- gMaxAMPA/gMaxGABAa/gMaxNMDA/
            // gMaxGABAb peak-scale the raw 0..1 conductance, applied inside
            // NeuronModel's RK4 stages rather than here.
            // Ceiling prevents a synchronized-burst conductance spike from
            // causing depolarization block -- see the long comment on
            // ampaConductanceCeiling in SimulationEngine.h.
            // Drug modifiers (block/potentiate/agonist) looked up per-block
            // from blockReceptorMods, same pattern as blockTypeBlockFrac
            // above -- see the long comment in SimulationEngine::run() for
            // what each field means.
            const drug::ReceptorConductanceModifiers& rMods = blockReceptorMods[i / neuronsPerBlock_];
            // Phase 3c: D1 boosts gMaxNMDA, D2 shrinks excitatory drive
            // (release-probability proxy applied to both AMPA and NMDA --
            // same "one glutamatergic release site drives both" reasoning
            // already used for the receptor-conductance accumulator
            // pattern, see Synapse.h). Inert (1.0) when unconfigured.
            const synapse::NeuromodulatorGainModifiers& nMods = blockNeuromodMods[i / neuronsPerBlock_];
            synapticEff[i].gAMPAEff = std::clamp(
                config_.gMaxAMPA * receptorConductances[i].gAMPA * rMods.ampaResidual * nMods.excitatoryWeightScale,
                0.0f,
                config_.ampaConductanceCeiling
            );
            synapticEff[i].gGABAaEff =
                config_.gMaxGABAa * receptorConductances[i].gGABAa * rMods.gabaAPotentiation;
            synapticEff[i].gNMDAEff =
                config_.gMaxNMDA * nMods.gMaxNmdaScale * receptorConductances[i].gNMDA * rMods.nmdaResidual * nMods.excitatoryWeightScale;
            synapticEff[i].gGABAbEff =
                config_.gMaxGABAb * receptorConductances[i].gGABAb +
                config_.gMaxGABAbAgonist * rMods.gabaBAgonistActivation;

            iNoise[i] = unitNormal(rng_) * population_.noiseStd[i];
            if (!std::isfinite(iNoise[i])) {
                iNoise[i] = 0.0f;
            }

            const float extWithAdapt = extCurrentWithBackground[i] - adaptationCurrent[i];
            effectiveExternalCurrent[i] = std::isfinite(extWithAdapt) ? extWithAdapt : 0.0f;

            // Cached per-(block, type) Hill block fractions instead of a
            // per-neuron DrugModel::applyWithDoseScale call — mathematically
            // identical since every neuron of a given type in a given block
            // shares the same dose and profile, just far fewer pow() calls.
            const std::size_t block = i / neuronsPerBlock_;
            const std::size_t type  = population_.neuronType[i];
            const std::array<float, 3>& frac = blockTypeBlockFrac[block * 2 + type];
            const float blockNa = frac[0];
            const float blockK  = frac[1];
            const float blockCa = frac[2];

            const float safeGNa = (std::isfinite(population_.gNa[i]) && population_.gNa[i] > 0.0f) ? population_.gNa[i] : 0.0f;
            const float safeGK  = (std::isfinite(population_.gK[i])  && population_.gK[i]  > 0.0f) ? population_.gK[i]  : 0.0f;
            const float safeGCa = (std::isfinite(population_.gCa[i]) && population_.gCa[i] > 0.0f) ? population_.gCa[i] : 0.0f;

            // Gap: this used to be its own hand-typed copy of the floor
            // formula (0.05f/0.02f), a second host-side copy independent of
            // DrugModel::apply()'s. Now routed through the same
            // DrugModel::conductanceFloor() both places use, so the two
            // host paths can no longer drift apart -- see the long comment
            // on DrugModel.h's kNaConductanceFloor for the one duplication
            // (the CUDA kernel) that still can't be closed this way.
            gNaEff[i] = drug::DrugModel::conductanceFloor(safeGNa, blockNa, drug::DrugModel::kNaConductanceFloor);
            // Phase 3c: 5-HT1A increases / 5-HT2A decreases intrinsic K+
            // conductance -- applied AFTER the existing Phase 1 channel-
            // block reduction, as an independent multiplicative factor
            // (nMods.gKEffScale == 1.0 when unconfigured, exact no-op).
            gKEff[i]  = drug::DrugModel::conductanceFloor(safeGK, blockK, drug::DrugModel::kKConductanceFloor) * nMods.gKEffScale;
            gCaEff[i] = drug::DrugModel::conductanceFloor(safeGCa, blockCa, drug::DrugModel::kCaConductanceFloor);
        }

        if (runOnGpu) {
            // Same gap as SimulationEngine::run(): iSyn is permanently
            // 0.0f now that all four receptors have moved to the
            // synapticEff/true-conductance path, which this CUDA call has
            // no knowledge of. This non-batched GPU step (as opposed to the
            // stepBatched path above) currently runs with NO synaptic
            // connectivity at all -- use SPP_FORCE_CPU=1 for verification.
            cudaSimulator_->step(
                timeMs,
                config_.dtMs,
                population_.params,
                config_.refractoryMs,
                iSyn,
                effectiveExternalCurrent,
                iNoise,
                gNaEff,
                gKEff,
                gCaEff,
                population_.v,
                population_.m,
                population_.h,
                population_.n,
                population_.s,
                population_.caCa,
                population_.lastSpikeTime,
                spikes
            );
        } else {
            // CPU fallback: identical per-neuron math to SimulationEngine::cpuStep,
            // inlined here since BatchedSimulationEngine has no Network member
            // to route through.
            for (std::size_t i = 0; i < totalNeurons_; ++i) {
                const float oldV = population_.v[i];

                neuron::HHState state;
                state.v = population_.v[i];
                state.m = population_.m[i];
                state.h = population_.h[i];
                state.n = population_.n[i];
                state.s = population_.s[i];
                state.caCa = population_.caCa[i];  // FIX: carry Ca concentration across timesteps

                float iTotal = effectiveExternalCurrent[i] + iSyn[i] + iNoise[i];
                if (!std::isfinite(iTotal)) {
                    iTotal = 0.0f;
                }
                iTotal = std::clamp(iTotal, -config_.maxTotalCurrent, config_.maxTotalCurrent);

                neuron::rk4Step(state, config_.dtMs, iTotal, gNaEff[i], gKEff[i], gCaEff[i], population_.params, synapticEff[i]);

                const bool inRefractory = (timeMs - population_.lastSpikeTime[i]) < config_.refractoryMs;
                const bool crossed = (oldV <= population_.threshold[i]) && (state.v > population_.threshold[i]);
                const std::uint8_t didSpike = (!inRefractory && crossed) ? 1U : 0U;

                if (didSpike != 0U) {
                    population_.lastSpikeTime[i] = timeMs;
                }

                population_.v[i] = state.v;
                population_.m[i] = state.m;
                population_.h[i] = state.h;
                population_.n[i] = state.n;
                population_.s[i] = state.s;
                population_.caCa[i] = state.caCa;  // FIX: write Ca concentration back

                spikes[i] = didSpike;

                // Phase 3c: spike-triggered vesicle release, using this
                // neuron's own gCaEff (already computed above this step, see
                // the synapticEff loop) as the Ca-dependent release-
                // probability drive proxy, per PHASE3_PLAN.md section 4's
                // releaseProb = 1 - exp(-gCaEff * calciumFactor). Returns
                // 1.0 (no-op) whenever vesiclePoolConfig.enabled is false or
                // this neuron didn't spike -- see NeurotransmitterPool.h's
                // baseline-preservation note.
                releaseScales[i] = (didSpike != 0U)
                    ? synapse::triggerVesicleRelease(vesiclePoolStates[i], vesiclePoolConfig, gCaEff[i], rng_)
                    : 1.0f;
            }
        }

        std::fill(blockSpikeCount.begin(), blockSpikeCount.end(), 0U);

        for (std::size_t i = 0; i < totalNeurons_; ++i) {
            if (spikes[i] != 0U) {
                const std::size_t block = i / neuronsPerBlock_;
                const std::size_t local = i % neuronsPerBlock_;
                ++blockSpikeCount[block];
                results[block].spikeTimes[local].push_back(timeMs);

                // Phase 3c: D1 and 5-HT2A both shrink spike-frequency
                // adaptation (see NeuromodulatorSystem.h header note) --
                // applied as a per-block multiplicative scale on the
                // increment step. nMods.adaptationScale == 1.0 when
                // unconfigured, exact no-op.
                const synapse::NeuromodulatorGainModifiers& nMods = blockNeuromodMods[i / neuronsPerBlock_];
                const float adaptStep = ((population_.neuronType[i] == 1U)
                                            ? adaptationIncrement
                                            : adaptationIncrement * adaptationInhibitoryScale)
                                        * nMods.adaptationScale;
                adaptationCurrent[i] = std::clamp(adaptationCurrent[i] + adaptStep, 0.0f, adaptationMaxCurrent);
            }
        }

        for (std::size_t b = 0; b < blockCount_; ++b) {
            results[b].populationSpikesPerStep[step] = blockSpikeCount[b];
        }

        delayBuffer_.pushSpikes(spikes);
        // Must follow pushSpikes() in the same step -- see DelayBuffer's
        // header comment on why this writes to the ring slot pushSpikes()
        // just advanced to rather than advancing it again itself.
        delayBuffer_.pushReleaseScales(releaseScales);
    }

    if (runOnGpu) {
        cudaSimulator_->downloadState(
            population_.v,
            population_.m,
            population_.h,
            population_.n,
            population_.s,
            population_.caCa,
            population_.lastSpikeTime
        );
    }

    for (std::size_t b = 0; b < blockCount_; ++b) {
        const std::size_t offset = b * neuronsPerBlock_;
        results[b].finalVoltages.assign(
            population_.v.begin() + static_cast<std::ptrdiff_t>(offset),
            population_.v.begin() + static_cast<std::ptrdiff_t>(offset + neuronsPerBlock_)
        );
    }

    return results;
}

} // namespace spp::simulation