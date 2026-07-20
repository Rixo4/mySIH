#include "BatchedSimulationEngine.h"

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
    const std::vector<BatchBlockSpec>& blocks
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

    // ---- Single CudaSimulator sized for the WHOLE batch. This is the crux
    // ---- of the fix: one set of H2D/kernel/D2H calls per timestep covers
    // ---- every dose x repeat at once, instead of one set per run.
    cudaSimulator_ = std::make_unique<cuda::CudaSimulator>(totalNeurons_);
    std::fprintf(stderr, "[SPP-DIAG] totalNeurons=%zu useGpu=%d cudaAvailable=%d\n",
        totalNeurons_, static_cast<int>(config_.useGpu), static_cast<int>(cudaSimulator_->available()));
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

    std::vector<float> iExcPulse(totalNeurons_, 0.0f);
    std::vector<float> iInhPulse(totalNeurons_, 0.0f);
    std::vector<float> iExcState(totalNeurons_, 0.0f);
    std::vector<float> iInhState(totalNeurons_, 0.0f);
    std::vector<float> iSyn(totalNeurons_, 0.0f);
    std::vector<float> iNoise(totalNeurons_, 0.0f);

    std::vector<float> gNaEff(totalNeurons_, 0.0f);
    std::vector<float> gKEff(totalNeurons_, 0.0f);
    std::vector<float> gCaEff(totalNeurons_, 0.0f);
    std::vector<float> extCurrentWithBackground(totalNeurons_, 0.0f);
    std::vector<float> effectiveExternalCurrent(totalNeurons_, 0.0f);
    std::vector<float> adaptationCurrent(totalNeurons_, 0.0f);

    std::vector<std::uint8_t> spikes(totalNeurons_, 0U);
    std::vector<std::uint32_t> blockSpikeCount(blockCount_, 0U);

    // Per-(block, type) Hill-equation block fractions, recomputed once per
    // timestep instead of once per neuron. Index = block * 2 + type
    // (type: 0 = inhibitory, 1 = excitatory). {blockNa, blockK, blockCa}.
    std::vector<std::array<float, 3>> blockTypeBlockFrac(blockCount_ * 2);

    std::normal_distribution<float> unitNormal(0.0f, 1.0f);
    const bool runOnGpu = config_.useGpu && cudaSimulator_ && cudaSimulator_->available();
    const float excDecay = std::exp(-config_.dtMs / config_.synTauExcMs);
    const float inhDecay = std::exp(-config_.dtMs / config_.synTauInhMs);
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

    for (std::size_t step = 0; step < stepCount; ++step) {
        const float timeMs = static_cast<float>(step) * config_.dtMs;
        const float doseScale = (drugOnsetTauMs > 1.0e-3f)
                                    ? (1.0f - std::exp(-timeMs / drugOnsetTauMs))
                                    : 1.0f;

        matrix_.accumulateSynapticCurrents(delayBuffer_, iExcPulse, iInhPulse);

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
        }

        for (std::size_t i = 0; i < totalNeurons_; ++i) {
            adaptationCurrent[i] = std::clamp(adaptationCurrent[i] * adaptationDecay, 0.0f, adaptationMaxCurrent);

            const float excAccum = iExcState[i] * excDecay + iExcPulse[i];
            const float inhAccum = iInhState[i] * inhDecay + iInhPulse[i];

            iExcState[i] = std::clamp(excAccum, 0.0f, config_.maxSynCurrent);
            iInhState[i] = std::clamp(inhAccum, 0.0f, config_.maxSynCurrent);

            const float synCurrent = iExcState[i] - iInhState[i];
            if (!std::isfinite(synCurrent)) {
                iSyn[i] = 0.0f;
            } else {
                iSyn[i] = std::clamp(synCurrent, -config_.maxSynCurrent, config_.maxSynCurrent);
            }

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

            gNaEff[i] = std::max(0.05f * safeGNa, safeGNa * std::max(0.0f, 1.0f - blockNa));
            gKEff[i]  = std::max(0.05f * safeGK,  safeGK  * (1.0f - blockK));
            gCaEff[i] = std::max(0.02f * safeGCa, safeGCa * std::max(0.0f, 1.0f - blockCa));
        }

        if (runOnGpu) {
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

                neuron::rk4Step(state, config_.dtMs, iTotal, gNaEff[i], gKEff[i], gCaEff[i], population_.params);

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
            }
        }

        std::fill(blockSpikeCount.begin(), blockSpikeCount.end(), 0U);

        for (std::size_t i = 0; i < totalNeurons_; ++i) {
            if (spikes[i] != 0U) {
                const std::size_t block = i / neuronsPerBlock_;
                const std::size_t local = i % neuronsPerBlock_;
                ++blockSpikeCount[block];
                results[block].spikeTimes[local].push_back(timeMs);

                const float adaptStep = (population_.neuronType[i] == 1U)
                                            ? adaptationIncrement
                                            : adaptationIncrement * adaptationInhibitoryScale;
                adaptationCurrent[i] = std::clamp(adaptationCurrent[i] + adaptStep, 0.0f, adaptationMaxCurrent);
            }
        }

        for (std::size_t b = 0; b < blockCount_; ++b) {
            results[b].populationSpikesPerStep[step] = blockSpikeCount[b];
        }

        delayBuffer_.pushSpikes(spikes);
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