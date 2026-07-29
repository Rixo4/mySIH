#include "Synapse.h"
#include "../neuron/ReceptorModel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace spp::synapse {

namespace {

// exp(-dt/tau), guarding against tau <= 0 (shouldn't happen given the
// literature-sourced constants in ReceptorKinetics, but cheap to guard).
inline float decayFactor(float dtMs, float tauMs) {
    if (tauMs <= 0.0f) return 0.0f;
    return std::exp(-dtMs / tauMs);
}

} // namespace

DelayBuffer::DelayBuffer() : neuronCount_(0), delaySteps_(1), head_(0), buffer_() {}

DelayBuffer::DelayBuffer(std::size_t neuronCount, std::size_t delaySteps)
    : neuronCount_(0), delaySteps_(1), head_(0), buffer_() {
    resize(neuronCount, delaySteps);
}

void DelayBuffer::resize(std::size_t neuronCount, std::size_t delaySteps) {
    neuronCount_ = neuronCount;
    delaySteps_ = std::max<std::size_t>(1, delaySteps);
    head_ = 0;
    buffer_.assign(neuronCount_ * delaySteps_, 0U);
    releaseScale_.assign(neuronCount_ * delaySteps_, 1.0f);
}

void DelayBuffer::clear() {
    std::fill(buffer_.begin(), buffer_.end(), 0U);
    std::fill(releaseScale_.begin(), releaseScale_.end(), 1.0f);
    head_ = 0;
}

std::size_t DelayBuffer::rowIndexForDelay(std::size_t delaySteps) const {
    const std::size_t clampedDelay = std::max<std::size_t>(1, std::min(delaySteps, delaySteps_));
    const std::size_t back = clampedDelay - 1;
    return (head_ + delaySteps_ - back) % delaySteps_;
}

void DelayBuffer::pushSpikes(const std::vector<std::uint8_t>& spikes) {
    if (spikes.size() != neuronCount_) {
        throw std::invalid_argument("Spike vector size must match delay buffer neuron count.");
    }

    head_ = (head_ + 1) % delaySteps_;
    const std::size_t offset = head_ * neuronCount_;
    std::copy(spikes.begin(), spikes.end(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::uint8_t DelayBuffer::getDelayedSpike(std::size_t neuronId, std::size_t delaySteps) const {
    if (neuronId >= neuronCount_) {
        throw std::out_of_range("Neuron index out of range in delay buffer.");
    }

    const std::size_t row = rowIndexForDelay(delaySteps);
    const std::size_t index = row * neuronCount_ + neuronId;
    return buffer_[index];
}

void DelayBuffer::pushReleaseScales(const std::vector<float>& releaseScales) {
    if (releaseScales.size() != neuronCount_) {
        throw std::invalid_argument("Release-scale vector size must match delay buffer neuron count.");
    }

    // Writes to the CURRENT head_ -- the slot pushSpikes() already advanced
    // to for this same step. Does not advance head_ itself; see header
    // comment on call-order requirement.
    const std::size_t offset = head_ * neuronCount_;
    std::copy(releaseScales.begin(), releaseScales.end(), releaseScale_.begin() + static_cast<std::ptrdiff_t>(offset));
}

float DelayBuffer::getDelayedReleaseScale(std::size_t neuronId, std::size_t delaySteps) const {
    if (neuronId >= neuronCount_) {
        throw std::out_of_range("Neuron index out of range in delay buffer.");
    }

    const std::size_t row = rowIndexForDelay(delaySteps);
    const std::size_t index = row * neuronCount_ + neuronId;
    return releaseScale_[index];
}

void SynapseMatrix::build(std::size_t neuronCount, const std::vector<SynapseEdge>& edges) {
    neuronCount_ = neuronCount;
    incomingOffsets_.assign(neuronCount_ + 1, 0U);

    for (const SynapseEdge& edge : edges) {
        if (edge.postNeuron >= neuronCount_) {
            throw std::out_of_range("Synapse post-synaptic index out of range.");
        }
        incomingOffsets_[edge.postNeuron + 1] += 1U;
    }

    for (std::size_t i = 1; i < incomingOffsets_.size(); ++i) {
        incomingOffsets_[i] += incomingOffsets_[i - 1];
    }

    incomingPre_.assign(edges.size(), 0U);
    incomingDelay_.assign(edges.size(), 1U);
    incomingWeight_.assign(edges.size(), 0.0f);
    incomingSign_.assign(edges.size(), 1);

    std::vector<std::uint32_t> cursor = incomingOffsets_;

    for (const SynapseEdge& edge : edges) {
        const std::uint32_t insertAt = cursor[edge.postNeuron]++;
        incomingPre_[insertAt] = edge.preNeuron;
        incomingDelay_[insertAt] = std::max<std::uint32_t>(1U, edge.delaySteps);
        incomingWeight_[insertAt] = edge.weight;
        incomingSign_[insertAt] = (edge.type == SynapseType::Excitatory) ? 1 : -1;
    }
}

void SynapseMatrix::accumulateSynapticCurrents(
    const DelayBuffer& delayBuffer,
    std::vector<float>& excitatoryCurrent,
    std::vector<float>& inhibitoryCurrent
) const {
    if (delayBuffer.neuronCount() != neuronCount_) {
        throw std::invalid_argument("Delay buffer size does not match synapse matrix neuron count.");
    }

    excitatoryCurrent.assign(neuronCount_, 0.0f);
    inhibitoryCurrent.assign(neuronCount_, 0.0f);

    for (std::size_t post = 0; post < neuronCount_; ++post) {
        const std::uint32_t begin = incomingOffsets_[post];
        const std::uint32_t end = incomingOffsets_[post + 1];

        float iExc = 0.0f;
        float iInh = 0.0f;

        for (std::uint32_t idx = begin; idx < end; ++idx) {
            const std::uint8_t spike = delayBuffer.getDelayedSpike(incomingPre_[idx], incomingDelay_[idx]);
            if (spike == 0U) {
                continue;
            }

            if (incomingSign_[idx] > 0) {
                iExc += incomingWeight_[idx];
            } else {
                iInh += incomingWeight_[idx];
            }
        }

        excitatoryCurrent[post] = iExc;
        inhibitoryCurrent[post] = iInh;
    }
}

void SynapseMatrix::accumulateReceptorConductances(
    const DelayBuffer& delayBuffer,
    std::vector<ReceptorConductanceState>& states,
    float dtMs,
    std::vector<ReceptorConductances>& outConductances,
    const ReceptorKineticsOverride* kineticsOverride,
    const DesensitizationConfig* desensitization
) const {
    if (delayBuffer.neuronCount() != neuronCount_) {
        throw std::invalid_argument("Delay buffer size does not match synapse matrix neuron count.");
    }
    if (states.size() != neuronCount_) {
        throw std::invalid_argument("Receptor conductance state size does not match neuron count.");
    }
    if (kineticsOverride != nullptr) {
        if (kineticsOverride->ampaDecayF.size() != neuronCount_ ||
            kineticsOverride->nmdaDecayF.size() != neuronCount_ ||
            kineticsOverride->gabaADecayF.size() != neuronCount_ ||
            kineticsOverride->gabaBDecayF.size() != neuronCount_) {
            throw std::invalid_argument("Receptor kinetics override size does not match neuron count.");
        }
    }

    outConductances.assign(neuronCount_, ReceptorConductances{});

    using namespace spp::neuron::ReceptorKinetics;

    // Precomputed once per call (dt is fixed within a run) rather than per
    // neuron -- these are the only transcendental-function calls needed.
    // Baseline (pre-Phase-3a) values -- used directly when kineticsOverride
    // is nullptr, exactly reproducing the original behavior.
    const float ampaDecayF  = decayFactor(dtMs, kAmpaTauDecayMs);
    const float ampaRiseF   = decayFactor(dtMs, kAmpaTauRiseMs);
    const float nmdaDecayF  = decayFactor(dtMs, kNmdaTauDecayMs);
    const float nmdaRiseF   = decayFactor(dtMs, kNmdaTauRiseMs);
    const float gabaADecayF = decayFactor(dtMs, kGabaATauDecayMs);
    const float gabaARiseF  = decayFactor(dtMs, kGabaATauRiseMs);
    const float gabaBDecayF = decayFactor(dtMs, kGabaBTauDecayMs);
    const float gabaBRiseF  = decayFactor(dtMs, kGabaBTauRiseMs);

    static const spp::neuron::DualExpKernel ampaKernel{kAmpaTauRiseMs, kAmpaTauDecayMs, 0.0f};
    static const spp::neuron::DualExpKernel nmdaKernel{kNmdaTauRiseMs, kNmdaTauDecayMs, 0.0f};
    static const spp::neuron::DualExpKernel gabaAKernel{kGabaATauRiseMs, kGabaATauDecayMs, 0.0f};
    static const spp::neuron::DualExpKernel gabaBKernel{kGabaBTauRiseMs, kGabaBTauDecayMs, 0.0f};
    const float ampaNorm  = ampaKernel.normFactor();
    const float nmdaNorm  = nmdaKernel.normFactor();
    const float gabaANorm = gabaAKernel.normFactor();
    const float gabaBNorm = gabaBKernel.normFactor();

    // Phase 3b desensitization: precompute the per-step rate constants once
    // (dt and the config are both fixed within a call), same pattern as the
    // decay factors above. desensitization == nullptr or ->enabled == false
    // means these are never used below (raw gGABAa is emitted unchanged).
    const bool desenseOn = (desensitization != nullptr) && desensitization->enabled;
    const float kDesenseStep  = desenseOn ? (dtMs / std::max(1.0f, desensitization->tauDesenseMs))  : 0.0f;
    const float kRecoverStep  = desenseOn ? (dtMs / std::max(1.0f, desensitization->tauRecoveryMs)) : 0.0f;
    const float maxAttenuation = desenseOn ? desensitization->maxAttenuation : 0.0f;

    for (std::size_t post = 0; post < neuronCount_; ++post) {
        const std::uint32_t begin = incomingOffsets_[post];
        const std::uint32_t end = incomingOffsets_[post + 1];

        float excInput = 0.0f;
        float inhInput = 0.0f;

        for (std::uint32_t idx = begin; idx < end; ++idx) {
            const std::uint8_t spike = delayBuffer.getDelayedSpike(incomingPre_[idx], incomingDelay_[idx]);
            if (spike == 0U) {
                continue;
            }
            // Phase 3c: vesicle pool release scale, 1.0 (no-op) unless the
            // caller opted in via DelayBuffer::pushReleaseScales -- see
            // Synapse.h's DelayBuffer comment and NeurotransmitterPool.h's
            // baseline-preservation note. Multiplying here rather than
            // gating on it keeps this identical to today's math whenever
            // vesicle pools are disabled.
            const float releaseScale = delayBuffer.getDelayedReleaseScale(incomingPre_[idx], incomingDelay_[idx]);
            if (incomingSign_[idx] > 0) {
                excInput += incomingWeight_[idx] * releaseScale;
            } else {
                inhInput += incomingWeight_[idx] * releaseScale;
            }
        }

        ReceptorConductanceState& s = states[post];

        const float postAmpaDecayF  = kineticsOverride ? kineticsOverride->ampaDecayF[post]  : ampaDecayF;
        const float postNmdaDecayF  = kineticsOverride ? kineticsOverride->nmdaDecayF[post]  : nmdaDecayF;
        const float postGabaADecayF = kineticsOverride ? kineticsOverride->gabaADecayF[post] : gabaADecayF;
        const float postGabaBDecayF = kineticsOverride ? kineticsOverride->gabaBDecayF[post] : gabaBDecayF;
        const float postAmpaNorm  = kineticsOverride ? kineticsOverride->ampaNorm[post]  : ampaNorm;
        const float postNmdaNorm  = kineticsOverride ? kineticsOverride->nmdaNorm[post]  : nmdaNorm;
        const float postGabaANorm = kineticsOverride ? kineticsOverride->gabaANorm[post] : gabaANorm;
        const float postGabaBNorm = kineticsOverride ? kineticsOverride->gabaBNorm[post] : gabaBNorm;

        // Decay first, then add this timestep's input -- the standard
        // "decay then impulse" discretization, exact for a pure exponential
        // between input events. Rise kinetics are never drug-modified (see
        // ReuptakeTransporter.h design note), so ampaRiseF etc. stay the
        // fixed baseline constants regardless of kineticsOverride.
        s.ampaDecay  = s.ampaDecay  * postAmpaDecayF  + excInput;
        s.ampaRise   = s.ampaRise   * ampaRiseF   + excInput;
        s.nmdaDecay  = s.nmdaDecay  * postNmdaDecayF  + excInput;
        s.nmdaRise   = s.nmdaRise   * nmdaRiseF   + excInput;
        s.gabaADecay = s.gabaADecay * postGabaADecayF + inhInput;
        s.gabaARise  = s.gabaARise  * gabaARiseF  + inhInput;
        s.gabaBDecay = s.gabaBDecay * postGabaBDecayF + inhInput;
        s.gabaBRise  = s.gabaBRise  * gabaBRiseF  + inhInput;

        ReceptorConductances& out = outConductances[post];
        out.gAMPA  = std::max(0.0f, postAmpaNorm  * (s.ampaDecay  - s.ampaRise));
        out.gNMDA  = std::max(0.0f, postNmdaNorm  * (s.nmdaDecay  - s.nmdaRise));
        const float gabaARaw = std::max(0.0f, postGabaANorm * (s.gabaADecay - s.gabaARise));
        out.gGABAb = std::max(0.0f, postGabaBNorm * (s.gabaBDecay - s.gabaBRise));

        if (desenseOn) {
            // Euler step on the 0..1 tiredness state, driven by this
            // neuron's own raw (pre-desensitization) GABA-A conductance as
            // the activation proxy -- see DesensitizationConfig comment in
            // Synapse.h for why no literal [GABA] concentration is needed.
            float d = s.gabaADesensitization;
            d += kDesenseStep * gabaARaw * (1.0f - d) - kRecoverStep * d;
            d = std::clamp(d, 0.0f, 1.0f);
            s.gabaADesensitization = d;
            out.gGABAa = gabaARaw * (1.0f - maxAttenuation * d);
        } else {
            out.gGABAa = gabaARaw;
        }
    }
}

} // namespace spp::synapse