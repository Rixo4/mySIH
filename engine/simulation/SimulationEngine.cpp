#include "SimulationEngine.h"
#include "../neuron/ReceptorModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace spp::simulation {

SimulationEngine::SimulationEngine(
    std::size_t neuronCount,
    const network::NetworkConfig& networkConfig,
    const SimulationConfig& simulationConfig
) : neuronCount_(neuronCount),
    network_(networkConfig),
    population_(neuronCount),
    delayBuffer_(neuronCount, std::max<std::size_t>(2, networkConfig.maxDelaySteps + 2)),
    config_(simulationConfig),
    drugModel_(),
    rng_(simulationConfig.randomSeed),
    cudaSimulator_(std::make_unique<cuda::CudaSimulator>(neuronCount)) {
    if (neuronCount_ == 0) {
        throw std::invalid_argument("SimulationEngine neuron count must be > 0.");
    }
    if (config_.dtMs <= 0.0f) {
        throw std::invalid_argument("Simulation dtMs must be > 0.");
    }
    if (config_.durationMs <= config_.dtMs) {
        throw std::invalid_argument("Simulation duration must be greater than dtMs.");
    }
    if (config_.synTauExcMs <= 0.0f || config_.synTauInhMs <= 0.0f) {
        throw std::invalid_argument("Synaptic time constants must be > 0.");
    }
    if (config_.maxSynCurrent <= 0.0f || config_.maxTotalCurrent <= 0.0f) {
        throw std::invalid_argument("Current clamps must be > 0.");
    }

    network_.buildRandom();
    initialize();
}

void SimulationEngine::setDrugModel(const drug::DrugModel& drugModel) {
    drugModel_ = drugModel;
}

void SimulationEngine::initialize() {
    population_.initialize(
        config_.baseExternalCurrent,
        config_.externalCurrentStd,
        config_.baseNoiseStd,
        config_.randomSeed
    );

    // Keep leak near canonical HH values while still damping occasional runaway trajectories.
    population_.params.gL = std::clamp(population_.params.gL * 1.02f, 0.22f, 0.40f);

    applyNetworkNeuronTypes();
    delayBuffer_.clear();

    if (config_.useGpu && cudaSimulator_ && cudaSimulator_->available()) {
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

SimulationResult SimulationEngine::run() {
    const std::size_t stepCount = static_cast<std::size_t>(std::ceil(config_.durationMs / config_.dtMs));

    SimulationResult result;
    result.spikeTimes.assign(neuronCount_, {});
    for (auto& train : result.spikeTimes) {
        train.reserve(static_cast<std::size_t>(config_.durationMs * 0.02f));
    }

    result.populationSpikesPerStep.assign(stepCount, 0U);
    result.neuronTypes = population_.neuronType;
    result.dtMs = config_.dtMs;
    result.durationMs = config_.durationMs;

    std::vector<float> iExcPulse(neuronCount_, 0.0f);
    std::vector<float> iInhPulse(neuronCount_, 0.0f);
    std::vector<float> iExcState(neuronCount_, 0.0f);
    std::vector<float> iGabaBState(neuronCount_, 0.0f);
    std::vector<float> iSyn(neuronCount_, 0.0f);
    std::vector<float> iNoise(neuronCount_, 0.0f);

    // GABA-A and NMDA's true conductance path: per-neuron rise/decay
    // accumulator state (owned here, persists across steps) and the
    // per-step output conductances read from it, computed by Synapse.cpp's
    // accumulateReceptorConductances -- previously-dead Step 2 code, now
    // wired up for real. gGABAa and gNMDA are consumed below; the other two
    // fields exist because accumulateReceptorConductances computes all
    // four every step, but synapticEff only forwards gGABAaEff/gNMDAEff
    // into the neuron model until GABA-B/AMPA get their own true-conductance
    // rebuilds.
    std::vector<synapse::ReceptorConductanceState> receptorStates(neuronCount_);
    std::vector<synapse::ReceptorConductances> receptorConductances(neuronCount_);
    std::vector<neuron::SynapticConductances> synapticEff(neuronCount_);

    std::vector<float> gNaEff(neuronCount_, 0.0f);
    std::vector<float> gKEff(neuronCount_, 0.0f);
    std::vector<float> gCaEff(neuronCount_, 0.0f);
    std::vector<float> extCurrentWithBackground(neuronCount_, 0.0f);
    std::vector<float> effectiveExternalCurrent(neuronCount_, 0.0f);
    std::vector<float> adaptationCurrent(neuronCount_, 0.0f);

    std::vector<std::uint8_t> spikes(neuronCount_, 0U);

    std::normal_distribution<float> unitNormal(0.0f, 1.0f);
    const bool runOnGpu = config_.useGpu && cudaSimulator_ && cudaSimulator_->available();
    const float excDecay = std::exp(-config_.dtMs / config_.synTauExcMs);
    const float inhDecay = std::exp(-config_.dtMs / config_.synTauInhMs);
    const float excFastPoolScale = std::clamp(config_.excFastPoolScale, 0.0f, 1.0f);
    const float gabaBDecay = std::exp(-config_.dtMs / std::max(1.0f, config_.synTauGabaBMs));
    const float gabaBFraction = std::clamp(config_.gabaBFraction, 0.0f, 1.0f);
    const float gabaBDrivingForceAtRest = population_.params.restingVoltage - population_.params.eK;
    const float gabaBSafeDenom = (std::fabs(gabaBDrivingForceAtRest) > 1.0e-3f) ? gabaBDrivingForceAtRest : 1.0f;
    const float ampaFraction = std::clamp(config_.ampaFraction, 0.0f, 1.0f);
    const float ampaDrivingForceAtRest = population_.params.restingVoltage - config_.eAMPA;
    const float ampaSafeDenom = (std::fabs(ampaDrivingForceAtRest) > 1.0e-3f) ? ampaDrivingForceAtRest : 1.0f;
    const float adaptTauMs = std::max(1.0f, config_.adaptationTauMs);
    const float adaptationDecay = std::exp(-config_.dtMs / adaptTauMs);
    const float adaptationIncrement = std::max(0.0f, config_.adaptationIncrement);
    const float adaptationMaxCurrent = std::max(0.0f, config_.adaptationMaxCurrent);
    const float adaptationInhibitoryScale = std::clamp(config_.adaptationInhibitoryScale, 0.0f, 1.0f);
    const float drugOnsetTauMs = std::max(0.0f, config_.drugOnsetTauMs);
    constexpr float kBackgroundCurrent = 0.18f;

    for (std::size_t i = 0; i < neuronCount_; ++i) {
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

        network_.matrix().accumulateSynapticCurrents(delayBuffer_, iExcPulse, iInhPulse);
        // GABA-A's true conductance path, fed by the same spike/delay
        // pipeline as the flat currents above but through Synapse.cpp's
        // per-receptor rise/decay kinetics -- see receptorStates comment.
        network_.matrix().accumulateReceptorConductances(
            delayBuffer_, receptorStates, config_.dtMs, receptorConductances
        );

        for (std::size_t i = 0; i < neuronCount_; ++i) {
            adaptationCurrent[i] = std::clamp(adaptationCurrent[i] * adaptationDecay, 0.0f, adaptationMaxCurrent);

            // Fast (AMPA-like flat-current) pool -- scaled by excFastPoolScale
            // to protect the external_current calibration from silently
            // drifting now that NMDA draws its own independent conductance
            // from the same spike input in parallel (see the long comment on
            // excFastPoolScale in SimulationEngine.h for why the undivided
            // full pulse broke baseline health).
            const float excAccum = iExcState[i] * excDecay + iExcPulse[i] * excFastPoolScale;
            // GABA-A no longer diverts from the flat inhibitory pulse -- it
            // moved to the true conductance path below. GABA-B still draws
            // its slow pool directly from the raw inhibitory pulse.
            const float gabaBAccum = iGabaBState[i] * gabaBDecay + iInhPulse[i] * gabaBFraction;

            iExcState[i] = std::clamp(excAccum, 0.0f, config_.maxSynCurrent);
            // Own tight ceiling, not the shared maxSynCurrent -- see the long
            // comment on gabaBMaxCurrent in SimulationEngine.h for why.
            iGabaBState[i] = std::clamp(gabaBAccum, 0.0f, config_.gabaBMaxCurrent);

            // GABA-A and NMDA: real conductance currents, gGABAa*(V-eGABAa)
            // and gNMDA*mgUnblock(V)*(V-eNMDA), applied inside NeuronModel's
            // RK4 stages (see computeDerivatives) so they see the correct
            // per-substage voltage rather than a single value computed here.
            // gMaxGABAa/gMaxNMDA peak-scale the raw 0..1 conductance the
            // same way drug-modulated gNaEff/gKEff/gCaEff are already
            // peak-scaled before reaching the neuron model.
            synapticEff[i].gGABAaEff = config_.gMaxGABAa * receptorConductances[i].gGABAa;
            synapticEff[i].gNMDAEff = config_.gMaxNMDA * receptorConductances[i].gNMDA;

            // GABA-B: separate slow pool, own driving force ratio anchored
            // at 1.0x at resting voltage (reuses eK -- GABA-B is a K+
            // conductance), same 2x ceiling as GABA-A for consistency.
            const float gabaBDrivingForceNow = population_.v[i] - population_.params.eK;
            const float gabaBRatio = std::clamp(gabaBDrivingForceNow / gabaBSafeDenom, 0.0f, 2.0f);
            const float iGabaBEff = iGabaBState[i] * gabaBRatio;

            // AMPA reweighting: narrow [0.7, 1.3] clamp (not [0, 2.0]) --
            // see the long comment on ampaFraction in SimulationEngine.h for
            // why AMPA needs a much tighter range than the inhibitory
            // receptors got.
            const float ampaDrivingForceNow = population_.v[i] - config_.eAMPA;
            const float ampaRatio = std::clamp(ampaDrivingForceNow / ampaSafeDenom, 0.7f, 1.3f);
            const float ampaWeight = (1.0f - ampaFraction) + ampaFraction * ampaRatio;
            const float iExcEffective = iExcState[i] * ampaWeight;

            const float synCurrent = iExcEffective - iGabaBEff;
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

            const drug::ConductanceResult geff = drugModel_.applyWithDoseScale(
                i,
                population_.gNa[i],
                population_.gK[i],
                population_.gCa[i],
                doseScale
            );
            const float kBlock = std::clamp(std::isfinite(geff.blockK) ? geff.blockK : 0.0f, 0.0f, 1.0f);
            gNaEff[i] = (std::isfinite(geff.gNaEff) && geff.gNaEff >= 0.0f)
                            ? geff.gNaEff
                            : 0.05f * std::max(0.0f, population_.gNa[i]);
            gKEff[i] = (std::isfinite(geff.gKEff) && geff.gKEff >= 0.0f)
                           ? geff.gKEff
                           : 0.0f;
            gCaEff[i] = (std::isfinite(geff.gCaEff) && geff.gCaEff >= 0.0f)
                            ? geff.gCaEff
                            : 0.02f * std::max(0.0f, population_.gCa[i]);

            if (kBlock > 0.30f) {
                effectiveExternalCurrent[i] += 2.0f * (kBlock - 0.30f);
            }

            if (iSyn[i] > 0.0f) {
                iSyn[i] *= (1.0f + 1.5f * kBlock);
            } else if (iSyn[i] < 0.0f) {
                iSyn[i] *= std::clamp(1.0f - 0.35f * kBlock, 0.60f, 1.0f);
            }
        }

        if (runOnGpu) {
            // NOTE: GABA-A's true conductance term (synapticEff) is NOT yet
            // reflected here -- the CUDA kernel only knows the flat iSyn
            // scalar. GPU --simulate runs are missing GABA-A until the CUDA
            // mirror is done (see PHASE2_PLAN.md step 6). Use SPP_FORCE_CPU=1
            // for real-hardware verification of this receptor until then.
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
            cpuStep(timeMs, iSyn, synapticEff, effectiveExternalCurrent, iNoise, gNaEff, gKEff, gCaEff, spikes);
        }

        std::uint32_t spikeCount = 0U;
        for (std::size_t i = 0; i < neuronCount_; ++i) {
            if (spikes[i] != 0U) {
                ++spikeCount;
                result.spikeTimes[i].push_back(timeMs);

                const float adaptStep = (population_.neuronType[i] == 1U)
                                            ? adaptationIncrement
                                            : adaptationIncrement * adaptationInhibitoryScale;
                adaptationCurrent[i] = std::clamp(adaptationCurrent[i] + adaptStep, 0.0f, adaptationMaxCurrent);
            }
        }
        result.populationSpikesPerStep[step] = spikeCount;

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

    result.finalVoltages = population_.v;
    return result;
}

void SimulationEngine::applyNetworkNeuronTypes() {
    if (network_.neuronTypes().size() != population_.size()) {
        throw std::runtime_error("Network neuron type vector size mismatch.");
    }
    population_.neuronType = network_.neuronTypes();

    for (std::size_t i = 0; i < neuronCount_; ++i) {
        if (population_.neuronType[i] == 1U) {
            population_.extCurrent[i] += 0.15f;
            population_.threshold[i] -= 0.8f;
        } else {
            population_.extCurrent[i] -= 0.45f;
            population_.threshold[i] += 1.5f;
        }
    }
}

void SimulationEngine::cpuStep(
    float timeMs,
    const std::vector<float>& synapticCurrent,
    const std::vector<neuron::SynapticConductances>& synapticConductances,
    const std::vector<float>& externalCurrent,
    const std::vector<float>& noiseCurrent,
    const std::vector<float>& gNaEff,
    const std::vector<float>& gKEff,
    const std::vector<float>& gCaEff,
    std::vector<std::uint8_t>& spikes
) {
    for (std::size_t i = 0; i < neuronCount_; ++i) {
        const float oldV = population_.v[i];

        neuron::HHState state;
        state.v    = population_.v[i];
        state.m    = population_.m[i];
        state.h    = population_.h[i];
        state.n    = population_.n[i];
        state.s    = population_.s[i];
        state.caCa = population_.caCa[i];  // FIX: carry Ca concentration across timesteps

        float iTotal = externalCurrent[i] + synapticCurrent[i] + noiseCurrent[i];
        if (!std::isfinite(iTotal)) {
            iTotal = 0.0f;
        }
        iTotal = std::clamp(iTotal, -config_.maxTotalCurrent, config_.maxTotalCurrent);

        neuron::rk4Step(state, config_.dtMs, iTotal, gNaEff[i], gKEff[i], gCaEff[i], population_.params, synapticConductances[i]);

        const bool inRefractory = (timeMs - population_.lastSpikeTime[i]) < config_.refractoryMs;
        const bool crossed = (oldV <= population_.threshold[i]) && (state.v > population_.threshold[i]);
        const std::uint8_t didSpike = (!inRefractory && crossed) ? 1U : 0U;

        if (didSpike != 0U) {
            population_.lastSpikeTime[i] = timeMs;
        }

        population_.v[i]    = state.v;
        population_.m[i]    = state.m;
        population_.h[i]    = state.h;
        population_.n[i]    = state.n;
        population_.s[i]    = state.s;
        population_.caCa[i] = state.caCa;  // FIX: write Ca concentration back

        spikes[i] = didSpike;
    }
}

} // namespace spp::simulation