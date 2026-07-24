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

    // NMDA moved OFF the lighter "divert a fraction of the flat excitatory
    // pulse into a separate decaying pool" model onto the real conductance
    // model, same pattern as GABA-A: gNMDAEff*mgUnblock(V)*(V-eNMDA),
    // computed via Synapse.cpp's accumulateReceptorConductances +
    // NeuronModel's SynapticConductances (see NeuronModel.h). eNMDA now
    // lives in HHParameters. gMaxNMDA is the new peak-conductance scale and
    // needs the same empirical-calibration treatment gMaxGABAa required.
    //
    // Per Synapse.cpp's comment, excitatory edges drive BOTH the AMPA and
    // NMDA conductance accumulators from the same spike input (co-expressed
    // at real glutamatergic synapses) -- biologically NMDA no longer
    // "diverts" current away from the fast excitatory pool.
    //
    // First attempt let excAccum draw the full undivided iExcPulse. A
    // sandbox check (700 neurons, external_current=6.6) then showed a
    // burst/silence oscillation (20Hz / 0Hz repeating) even with gMaxNMDA=0
    // -- initially read as a regression from dropping the old 25%
    // diversion. Turned out to be a false alarm: re-testing the exact
    // pre-NMDA-edit code at the same 700-neuron/6.6 operating point
    // produces the identical oscillation. That calibration point was only
    // ever verified against the real ~1500-neuron network (see main.cpp's
    // external_current comment); the smaller sandbox network apparently
    // sits in a different, more bursty dynamical regime at this drive
    // level regardless of any receptor changes. So this was never a
    // confirmed bug -- just an unreliable smaller-scale proxy.
    //
    // UPDATE (real-hardware test): the 0.75 value above was WRONG, and the
    // real ~1500-neuron network is what proved it. Isolation test done
    // properly this time: set gMaxNMDA=0.0 (NMDA current fully off) with
    // excFastPoolScale still at 0.75, everything else identical to the
    // already-verified 12.5Hz healthy baseline. Result: 5.0Hz flat (early
    // 5.0 -> late 5.2, no decline, so not a synchronization/collapse
    // pattern -- just a starved network). With NMDA's own current
    // completely zeroed, the ONLY remaining difference from the verified
    // baseline was this 25% cut to the fast excitatory pool, and that alone
    // was enough to more than halve the rate. So this was never a harmless
    // "just in case" precaution -- it was silently starving the network the
    // whole time, which also explains why gMaxNMDA=0.001 collapsed into two
    // synchronized volleys then silence: a starved network has much less
    // margin and tips into synchronized/collapsing behavior far more easily
    // than a healthily-driven one. Reverted to 1.0 (no scaling, full
    // undivided pulse) -- the value that was actually verified healthy.
    // Needs a fresh gMaxNMDA=0.0 real-hardware re-check to confirm this
    // restores ~12.5Hz before any new gMaxNMDA value is tried.
    float excFastPoolScale = 1.0f;

    // Calibration risk: NMDA's driving force at rest (V-eNMDA = -65-0 =
    // -65mV) is far larger in magnitude than GABA-A's (V-eGABAa = +5mV), so
    // gMaxNMDA needs to be considerably smaller than gMaxGABAa for
    // comparable-scale current -- partially offset by the Mg2+ block being
    // ~94% closed at rest (nmdaMgBlockFraction(-65mV) ~= 0.06), which keeps
    // NMDA's resting-state contribution small regardless of gMax. Starting
    // value below is a first estimate, NOT yet verified against a real
    // full-duration baseline run -- needs the same early/late-rate CSV
    // sweep external_current and gMaxGABAa both required before trusting
    // it. See main.cpp's external_current comment for that methodology.
    float gMaxNMDA = 0.003f;

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