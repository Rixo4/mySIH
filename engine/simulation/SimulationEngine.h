#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

#include "../drug/DrugModel.h"
#include "../network/Network.h"
#include "../neuron/NeuronModel.h"
#include "../synapse/Synapse.h"
#include "../cuda/CudaSimulator.h"

namespace spp::simulation {

struct SimulationConfig {
    float dtMs = 0.01f;
    float durationMs = 400.0f;
    std::uint32_t randomSeed = 1337U;

    float baseExternalCurrent = 9.5f;
    float externalCurrentStd = 1.8f;
    float baseNoiseStd = 0.35f;

    float refractoryMs = 2.0f;
    float synTauExcMs = 5.0f;
    float synTauInhMs = 10.0f;

    // Small, isolated addition on top of the validated flat-current model:
    // a fraction of the excitatory pulse is routed into a slower pool with
    // its own decay time constant (synTauNmdaMs, literature NMDA decay ~100ms
    // -- see ReceptorModel.h) and multiplied every step by the voltage-
    // dependent Mg2+ unblock fraction (nmdaMgBlockFraction, also from
    // ReceptorModel.h, untouched since Step 1). At resting voltage NMDA is
    // ~94% Mg-blocked, so this pool contributes almost nothing there --
    // diverting nmdaFraction away from the existing fast pool means total
    // excitatory drive at rest is slightly BELOW the previously-validated
    // baseline, not above it (the safe direction to be wrong in). As a
    // neuron depolarizes, the block eases and this pool adds drive back.
    // Everything else (fast exc/inh pools, clamps, K-block rule) is
    // unchanged from the validated baseline.
    float synTauNmdaMs = 100.0f;
    float nmdaFraction = 0.25f;

    // GABA-A moved OFF the lighter reweight model onto the real conductance
    // model (gGABAa*(V-eGABAa), computed via Synapse.cpp's
    // accumulateReceptorConductances + NeuronModel's SynapticConductances --
    // see NeuronModel.h). eGABAa now lives in HHParameters (single source of
    // truth for the reversal potential). gMaxGABAa is the new peak-
    // conductance scale requiring the same empirical-calibration treatment
    // gCa/gAHP needed in Phase 1.
    //
    // First calibration attempt (0.05) looked fine on short sandbox runs
    // (12.50 Hz at 80ms, no collapse) but real-hardware full-duration
    // testing exposed a slow decline invisible in the short check: 12.50 Hz
    // at 80ms -> 2.50 Hz at the full 400ms, 0% silent throughout (not a
    // hard collapse, a genuine slow suppression). Same failure shape as
    // GABA-B's lighter-model tuning: the receptor's own kinetics are fast
    // (15ms decay) so this isn't the accumulator itself failing to decay --
    // more likely the network's recurrent feedback amplifying real-time,
    // depolarization-scaled inhibition into a slow population-level
    // transient over hundreds of ms, the same amplification effect noted
    // in gabaBMaxCurrent's comment below. Reduced 5x (0.05 -> 0.01) as the
    // next attempt. Needs the same FULL 400ms real-hardware re-check --
    // short sandbox/short real runs cannot see this failure mode.
    float gMaxGABAa = 0.01f;

    // Third small addition: GABA-B, a genuinely slow, separate pool (real
    // metabotropic K+ conductance, decay ~1000ms per ReceptorModel.h's
    // literature value -- far slower than anything else in this model). A
    // small fraction of the inhibitory pulse is diverted here (same "split"
    // pattern as NMDA, not the "reweight" pattern used for GABA-A, since
    // this pool's timescale is too different from the fast pool to share
    // it). Reuses the existing eK reversal (GABA-B IS a K+ conductance, see
    // ReceptorModel.h comments).
    //
    // This one fought back hard in sandbox testing: because the pool barely
    // decays within any realistic run length (~18% decay over 200ms), it
    // behaves like a near-lossless integrator rather than a normal decaying
    // pool -- the same failure mode that derailed the original Step 3
    // attempt. Even a conservative 5% fraction with the shared 300 ceiling
    // collapsed the network (28->4.9Hz, 0->11.5% silent). A dedicated
    // ceiling of 20, still far below the shared 300, made no difference --
    // the pool was saturating almost immediately and sitting pinned there
    // regardless. Only at ceiling=1.0 did healthy behavior return (20.70Hz,
    // 0.27% silent, irregularity 0.21 -- higher than the ~0.02-0.03 baseline,
    // plausibly a genuine signature of real GABA-B burst-pause modulation).
    // There's a sharp nonlinear transition somewhere between ceiling 1 and 5
    // -- this network's recurrent feedback clearly amplifies small
    // per-neuron inhibitory changes into large population effects.
    //
    // IMPORTANT: only verified at 200ms in sandbox (the sandbox CPU is too
    // slow to complete a 300-400ms run in the available time budget). GABA-B's
    // whole risk profile is slow accumulation over a run, so this needs
    // checking at the FULL 400ms duration (and --dose-eval's 500ms) on real
    // hardware -- watch specifically for degradation in the back half that
    // wasn't visible in this 200ms check.
    float synTauGabaBMs = 1000.0f;
    float gabaBFraction = 0.03f;
    float gabaBMaxCurrent = 1.0f;

    // Fourth and final small addition: AMPA voltage-dependence -- the last
    // untouched receptor. Same reweighting pattern as GABA-A (no new state,
    // ratio anchored at exactly 1.0x at resting voltage), but with a MUCH
    // tighter clamp range than the inhibitory receptors got.
    //
    // Reason: AMPA's reversal potential (~0mV, see ReceptorModel.h) sits
    // almost exactly at this network's own spike threshold (~0mV). That
    // means the natural driving force (V - eAMPA) SHRINKS toward zero
    // exactly as a neuron approaches threshold -- the opposite of GABA-A/B,
    // whose driving force grows in that direction (a stabilizing effect for
    // inhibition, but a destabilizing one for excitation). This exact
    // mechanism was the leading hypothesis for why the original full
    // conductance-based Step 3 rewrite collapsed repeat firing. Clamping the
    // ratio to a narrow [0.7, 1.3] band (vs [0, 2.0] for GABA-A/B) means
    // AMPA can never lose more than 30% of its drive even very close to
    // threshold -- enough to add genuine voltage-dependence without
    // reproducing the choke-off failure.
    //
    // Sandbox-verified at 200ms only (28-29Hz baseline -> 18.49Hz, 0.20%
    // silent, irregularity 0.36 -- gentle, no collapse); the sandbox could
    // not complete a full 400ms run in the available time. Needs the same
    // full-duration real-hardware check as GABA-B got.
    float eAMPA = 0.0f;
    float ampaFraction = 0.10f;

    float maxSynCurrent = 300.0f;
    float maxTotalCurrent = 400.0f;

    float adaptationTauMs = 220.0f;
    float adaptationIncrement = 0.18f;
    float adaptationMaxCurrent = 6.0f;
    float adaptationInhibitoryScale = 0.55f;

    float drugOnsetTauMs = 120.0f;

    bool useGpu = false;
};

struct SimulationResult {
    std::vector<std::vector<float>> spikeTimes;
    std::vector<std::uint32_t> populationSpikesPerStep;
    std::vector<float> finalVoltages;
    std::vector<std::uint8_t> neuronTypes;

    float dtMs = 0.01f;
    float durationMs = 0.0f;
    float burstWindowMs = 0.0f;
};

class SimulationEngine {
public:
    SimulationEngine(
        std::size_t neuronCount,
        const network::NetworkConfig& networkConfig,
        const SimulationConfig& simulationConfig
    );

    void setDrugModel(const drug::DrugModel& drugModel);

    void initialize();
    SimulationResult run();

    [[nodiscard]] const network::Network& network() const { return network_; }
    [[nodiscard]] const neuron::NeuronPopulation& population() const { return population_; }

private:
    std::size_t neuronCount_;
    network::Network network_;
    neuron::NeuronPopulation population_;
    synapse::DelayBuffer delayBuffer_;

    SimulationConfig config_;
    drug::DrugModel drugModel_;

    std::mt19937 rng_;

    std::unique_ptr<cuda::CudaSimulator> cudaSimulator_;

    void applyNetworkNeuronTypes();
    void cpuStep(
        float timeMs,
        const std::vector<float>& synapticCurrent,
        const std::vector<neuron::SynapticConductances>& synapticConductances,
        const std::vector<float>& externalCurrent,
        const std::vector<float>& noiseCurrent,
        const std::vector<float>& gNaEff,
        const std::vector<float>& gKEff,
        const std::vector<float>& gCaEff,
        std::vector<std::uint8_t>& spikes
    );
};

} // namespace spp::simulation