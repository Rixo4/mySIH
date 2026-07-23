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

    // Second small, isolated addition: a fraction of the EXISTING inhibitory
    // current (no new accumulator/state needed) is reweighted by GABA-A's
    // own voltage-dependent driving force (V - eGABAa), normalized so the
    // reweighting is exactly 1.0 (i.e. identical to the current validated
    // baseline) at resting voltage -- it only pulls away from baseline as a
    // neuron actually depolarizes, growing inhibition as it approaches
    // threshold (real shunting-inhibition behavior), never at rest. eGABAa
    // matches ReceptorKinetics::kGabaAReversalMv from ReceptorModel.h.
    float eGABAa = -70.0f;
    // Started at 0.25 -- sandbox-tested before ever reaching real hardware
    // and found too strong: with a x5 driving-force ratio ceiling it dropped
    // firing rate from ~30Hz to ~8Hz and pushed silent neurons to ~25%.
    // Reducing the ceiling to x2 (see the 2.0f clamp at the call site) and
    // this fraction to 0.10 recovered a healthy result (29.06 Hz, 0.13%
    // silent) nearly identical to the pre-GABA-A baseline (30.00 Hz, 0.33%
    // silent). Confirms real shunting inhibition is a strong effect even in
    // small doses here -- this fraction should stay conservative.
    float gabaAFraction = 0.10f;

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
        const std::vector<float>& externalCurrent,
        const std::vector<float>& noiseCurrent,
        const std::vector<float>& gNaEff,
        const std::vector<float>& gKEff,
        const std::vector<float>& gCaEff,
        std::vector<std::uint8_t>& spikes
    );
};

} // namespace spp::simulation