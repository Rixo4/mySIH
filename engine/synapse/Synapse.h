#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace spp::synapse {

// ─── Per-receptor conductance tracking ─────────────────────────────────────
// Added alongside the existing current-injection path below (untouched, so
// the currently-validated network baseline keeps working) rather than
// replacing it in place. This produces the four receptor conductances but
// deliberately stops there -- turning them into currents needs the
// neuron's own voltage (for the (V-E) driving force and, for NMDA, the
// Mg2+-block fraction), which belongs in the neuron/network integration
// step, not here.
//
// Each receptor's conductance is tracked as a pair of decaying accumulators
// rather than by replaying spike history: g(t) = norm*(decayAcc - riseAcc),
// where decayAcc and riseAcc each decay exponentially (with the receptor's
// own tauDecay/tauRise) and both get incremented by the same input on every
// spike. This is the standard technique for simulating dual-exponential
// synapses in O(1) per timestep instead of re-summing every past spike
// still within the kernel's support -- essential here since GABA-B's decay
// constant is ~1000 ms, which would otherwise mean remembering roughly a
// second of spike history per neuron. It reproduces the identical waveform
// shape as ReceptorModel::dualExpWaveform (verified numerically), just
// computed incrementally.
//
// One simplification worth being explicit about: GABA-B's ~50 ms onset
// delay (see ReceptorKinetics::kGabaBOnsetDelayMs) is not reproduced here.
// Its 225 ms rise time constant already dominates the timescale (time to
// peak works out to several hundred ms either way), so the incremental
// form omits the hard delay for simplicity. The reference
// ReceptorModel::dualExpWaveform function does implement the delay exactly,
// for anyone checking this simplification's impact directly.
struct ReceptorConductanceState {
    float ampaDecay = 0.0f, ampaRise = 0.0f;
    float nmdaDecay = 0.0f, nmdaRise = 0.0f;
    float gabaADecay = 0.0f, gabaARise = 0.0f;
    float gabaBDecay = 0.0f, gabaBRise = 0.0f;

    // Phase 3b: GABA-A desensitization state, 0 (fresh) .. 1 (fully tired).
    // Persists across timesteps like the accumulators above. Left at 0.0f
    // and never touched unless a DesensitizationConfig is passed in below --
    // zero behavior change for any caller that doesn't opt in.
    float gabaADesensitization = 0.0f;
};

// Peak-normalized (0..~1) conductances for one neuron at one timestep.
// Receptor-specific peak conductance (how large this current is relative
// to Na/K/Ca) is applied by the caller, not here -- kept out of this file
// so the kinetics (this step) stay separate from the magnitude decision
// (made when wiring into the neuron's current balance).
struct ReceptorConductances {
    float gAMPA = 0.0f;
    float gNMDA = 0.0f;
    float gGABAa = 0.0f;
    float gGABAb = 0.0f;
};

// ─── Phase 3a: per-neuron drug-modified decay kinetics ─────────────────────
// A transporter-blocking drug (see engine/drug/ReceptorDrugProfile.h's
// TransporterAction / engine/synapse/ReuptakeTransporter.h) extends a
// receptor's decay time constant. Because dose-eval batches many different
// doses at once (one per block, see BatchedSimulationEngine), the effective
// decay factor/norm can differ per neuron depending on which dose-block it
// belongs to -- so these are precomputed per-neuron ONCE before a run starts
// (dose is fixed for the whole run), not recomputed per timestep.
// Passing nullptr to accumulateReceptorConductances (the default) uses the
// original fixed ReceptorKinetics constants exactly as before Phase 3a --
// zero behavior change for any caller that doesn't opt in.
struct ReceptorKineticsOverride {
    std::vector<float> ampaDecayF, ampaNorm;
    std::vector<float> nmdaDecayF, nmdaNorm;
    std::vector<float> gabaADecayF, gabaANorm;
    std::vector<float> gabaBDecayF, gabaBNorm;
};

// ─── Phase 3b: GABA-A desensitization (receptor "tiredness") ───────────────
// Real GABA-A receptors lose responsiveness under sustained activation and
// recover once left alone -- an intrinsic receptor property, not something a
// drug turns on. It is modeled as a single 0..1 "tiredness" state D per
// neuron (ReceptorConductanceState::gabaADesensitization) evolving each
// timestep as:
//
//   dD/dt = (1/tauDesenseMs) * drive * (1 - D) - (1/tauRecoveryMs) * D
//
// where `drive` is the neuron's own instantaneous (pre-desensitization)
// GABA-A conductance (already 0..1, peak-normalized) -- used as the
// activation proxy instead of a literal [GABA] concentration, since that's
// exactly what the existing accumulator already computes. The receptor's
// output conductance is then scaled by (1 - maxAttenuation*D).
//
// tauDesenseMs ~30000 (30s) and tauRecoveryMs ~124000 (124s) are literature
// values for GABA-A desensitization/recovery under continuous agonist
// exposure (Serpa & Ozawa 2002 and related rapid-perfusion patch-clamp
// work); maxAttenuation ~0.9 reflects "near-complete" (not literal 100%)
// desensitization reported at saturating, prolonged exposure.
//
// This is deliberately opt-in and OFF by default (enabled=false / nullptr):
// with dt in the tens-of-ms and typical dose-eval runs lasting ~0.4-0.5s,
// D never accumulates meaningfully anyway (500ms << 30s tau), so leaving it
// enabled would be a no-op for those runs -- but it's kept as an explicit
// switch so every already-validated Phase 2/3a result stays byte-identical
// unless a test deliberately opts in for a long-duration 3b run.
struct DesensitizationConfig {
    bool enabled = false;
    float tauDesenseMs = 30000.0f;
    float tauRecoveryMs = 124000.0f;
    float maxAttenuation = 0.9f;
};

enum class SynapseType : std::uint8_t {
    Excitatory = 0,
    Inhibitory = 1
};

struct SynapseEdge {
    std::uint32_t preNeuron = 0;
    std::uint32_t postNeuron = 0;
    float weight = 0.0f;
    std::uint32_t delaySteps = 1;
    SynapseType type = SynapseType::Excitatory;
};

class DelayBuffer {
public:
    DelayBuffer();
    DelayBuffer(std::size_t neuronCount, std::size_t delaySteps);

    void resize(std::size_t neuronCount, std::size_t delaySteps);
    void clear();

    [[nodiscard]] std::size_t neuronCount() const { return neuronCount_; }
    [[nodiscard]] std::size_t delaySteps() const { return delaySteps_; }

    void pushSpikes(const std::vector<std::uint8_t>& spikes);
    [[nodiscard]] std::uint8_t getDelayedSpike(std::size_t neuronId, std::size_t delaySteps) const;

private:
    std::size_t neuronCount_;
    std::size_t delaySteps_;
    std::size_t head_;
    std::vector<std::uint8_t> buffer_;

    [[nodiscard]] std::size_t rowIndexForDelay(std::size_t delaySteps) const;
};

class SynapseMatrix {
public:
    void build(std::size_t neuronCount, const std::vector<SynapseEdge>& edges);

    [[nodiscard]] std::size_t neuronCount() const { return neuronCount_; }
    [[nodiscard]] std::size_t edgeCount() const { return incomingPre_.size(); }
    [[nodiscard]] const std::vector<std::uint32_t>& incomingOffsets() const { return incomingOffsets_; }
    [[nodiscard]] const std::vector<std::uint32_t>& incomingPre() const { return incomingPre_; }
    [[nodiscard]] const std::vector<std::uint32_t>& incomingDelay() const { return incomingDelay_; }
    [[nodiscard]] const std::vector<float>& incomingWeight() const { return incomingWeight_; }
    [[nodiscard]] const std::vector<std::int8_t>& incomingSign() const { return incomingSign_; }

    void accumulateSynapticCurrents(
        const DelayBuffer& delayBuffer,
        std::vector<float>& excitatoryCurrent,
        std::vector<float>& inhibitoryCurrent
    ) const;

    // New receptor-conductance path (see struct comments above). Excitatory
    // edges drive both AMPA and NMDA accumulators with the same input --
    // real glutamatergic synapses co-express both receptors at the same
    // release site, so this isn't an approximation of convenience, it's
    // the biologically standard picture. Inhibitory edges drive both
    // GABA-A and GABA-B accumulators the same way; this one IS a
    // simplification (real GABA-B is more spillover/tonic-activation-
    // dominated rather than driven by the same phasic release as GABA-A),
    // adopted to keep the edge representation symmetric for a first
    // implementation rather than introducing a second inhibitory edge type.
    // `states` must have one entry per neuron and persists across calls
    // (it's the running conductance state); `outConductances` is
    // overwritten each call with this timestep's result.
    void accumulateReceptorConductances(
        const DelayBuffer& delayBuffer,
        std::vector<ReceptorConductanceState>& states,
        float dtMs,
        std::vector<ReceptorConductances>& outConductances,
        const ReceptorKineticsOverride* kineticsOverride = nullptr,
        const DesensitizationConfig* desensitization = nullptr
    ) const;

private:
    std::size_t neuronCount_ = 0;
    std::vector<std::uint32_t> incomingOffsets_;
    std::vector<std::uint32_t> incomingPre_;
    std::vector<std::uint32_t> incomingDelay_;
    std::vector<float> incomingWeight_;
    std::vector<std::int8_t> incomingSign_;
};

} // namespace spp::synapse