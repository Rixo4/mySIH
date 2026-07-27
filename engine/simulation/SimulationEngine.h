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
    //
    // RETIRED (AMPA build step): this field has been removed. It scaled the
    // flat "fast pool" (iExcPulse/iExcState in SimulationEngine.cpp), which
    // was standing in for AMPA's current before AMPA got its own
    // true-conductance build step. That flat pool has now been fully
    // removed from both SimulationEngine.cpp and BatchedSimulationEngine.cpp
    // -- gMaxAMPA below is the only thing controlling AMPA's strength now.
    // (Confirmed via repo-wide search before deleting: excFastPoolScale had
    // no references anywhere outside this file and the two .cpp files being
    // updated alongside it, so removing it outright is safe.)

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

    // Third small addition: GABA-B, moved OFF the earlier diverted-pool/
    // reweight model onto the real conductance model, same pattern as
    // GABA-A and NMDA: gGABAbEff*(V-eK), computed via Synapse.cpp's
    // accumulateReceptorConductances (out.gGABAb was already being computed
    // every step -- see Synapse.cpp -- just not consumed until now) and
    // added inside NeuronModel's computeDerivatives so it sees the correct
    // per-RK4-substage voltage. Reuses eK directly (GABA-B IS a K+
    // conductance -- see ReceptorModel.h and NeuronModel.h comments), no
    // separate reversal field needed.
    //
    // The old diverted-pool version (synTauGabaBMs/gabaBFraction/
    // gabaBMaxCurrent) fought hard in sandbox testing -- a near-lossless
    // slow integrator that needed a very tight dedicated ceiling (1.0) to
    // avoid collapsing the network -- but that whole family of problems was
    // specific to the hand-rolled accumulator/ceiling approach, not GABA-B
    // itself. The real per-receptor kernel in Synapse.cpp already handles
    // its own decay properly (1000ms decay, same literature value), so this
    // rebuild should not need an artificial ceiling the way the old model did.
    //
    // CALIBRATED via real-hardware full-duration (400ms, 1500-neuron) CSV
    // sweep: 0.004 -> decline over the run; 0.0005 -> still declining
    // (12.5Hz, trending down); 0.0001 -> healthy flat 20Hz, low irregularity
    // (~0.084), spike-count histogram confirming normal tonic firing
    // (~8 spikes/neuron, no synchronized-volley signature). This value was
    // verified BEFORE the AMPA conversion but the field default here was
    // never updated to match -- every AMPA test this session ran with the
    // stale 0.004 default, which alone silences the network after the
    // initial volley regardless of AMPA's own settings (confirmed: even
    // gMaxAMPA=0.0 showed the identical "fires once, then dead" pattern,
    // which is only possible if something other than AMPA was responsible).
    // One known gap, unchanged: GABA-B's ~50ms onset delay
    // (ReceptorKinetics::kGabaBOnsetDelayMs) is not applied by
    // accumulateReceptorConductances' decay-accumulator math -- noting
    // GABA-B's nonzero literature value isn't actually modeled yet.
    float gMaxGABAb = 0.0001f;

    // Fourth and final receptor: AMPA, moved OFF the reweighting model
    // (narrow [0.7,1.3]-clamped ratio applied to the old flat "fast pool")
    // onto the real conductance model, same pattern as the other three:
    // gAMPAEff*(V-eAMPA), computed via Synapse.cpp's
    // accumulateReceptorConductances (out.gAMPA was already being computed
    // every step -- see Synapse.cpp -- just not consumed until now) and
    // added inside NeuronModel's computeDerivatives so it sees the correct
    // per-RK4-substage voltage. eAMPA moved to HHParameters (see
    // NeuronModel.h), matching how eGABAa/eNMDA already live there. The old
    // flat "fast pool" (iExcPulse/iExcState/excDecay) that used to stand in
    // for AMPA's current is fully retired now -- AMPA's current comes
    // entirely from this true-conductance path.
    //
    // KNOWN RISK, carried over from the reweighting-model comment history --
    // CONFIRMED on real hardware, not just a hypothesis: AMPA's reversal
    // potential (~0mV, see ReceptorModel.h) sits almost exactly at this
    // network's own spike threshold (~0mV), so the driving force
    // (V - eAMPA) SHRINKS toward zero exactly as a neuron approaches
    // threshold -- the opposite of GABA-A/B, whose driving force grows in
    // that direction (stabilizing for inhibition, destabilizing for
    // excitation).
    //
    // Real-hardware sweep across gMaxAMPA = 0.005 / 0.02 / 0.5 (three
    // orders of magnitude) produced the IDENTICAL failure signature every
    // time: 1499/1500 neurons fire exactly once (the shared initial
    // synchronized volley) then stay permanently silent for the rest of a
    // 400ms run. Because the failure shape didn't change across a 100x
    // sweep, this was diagnosed as a repeat-firing choke-off, not a
    // magnitude/calibration problem -- confirmed via neuron_stats.csv
    // spike-count histogram (uniform "1", not a spread).
    //
    // FIX ATTEMPT 1 (NeuronModel.cpp: floor the driving force magnitude at
    // 15mV near threshold) did NOT fix this -- re-tested on real hardware
    // at gMaxAMPA=0.02 with the floor in place, IDENTICAL failure, now even
    // more absolute (1500/1500 neurons firing exactly once, vs 1499/1500
    // before). Kept in place anyway since it's still a reasonable safety
    // net, but it wasn't the real cause: the floor only engages within
    // +-15mV of eAMPA, and the actual synchronized volley starts from rest
    // (-65mV), far outside that zone, so the floor was never even active
    // when the failure happens.
    //
    // REAL DIAGNOSIS: unlike every other current in this model, gAMPAEff
    // had NO ceiling. The old flat model this replaced always clamped its
    // accumulator to maxSynCurrent; the true-conductance path never got an
    // equivalent. When the network's shared initial synchronized volley
    // hits (external_current + noise, same every run), ~150 simultaneous
    // presynaptic inputs (10% connectivity) sum into one uncapped
    // conductance spike per neuron, at a full -65mV driving force -- almost
    // certainly enough to blow every neuron through threshold in the same
    // instant (explaining the perfect synchrony) and overshoot hard enough
    // to drive the same kind of Na-inactivation depolarization block that
    // caused the original gCa bug (see NeuronModel.h's gCa comment). AMPA
    // is uniquely exposed among the four receptors: excitatory (large
    // resting driving force, unlike GABA-A/B), no self-limiting gate
    // (unlike NMDA's Mg-block), and never given a ceiling (unlike
    // everything else in this file).
    //
    // FIX ATTEMPT 2: ampaConductanceCeiling below caps gAMPAEff directly
    // (applied in SimulationEngine.cpp/BatchedSimulationEngine.cpp where
    // it's assigned), the same protective role maxSynCurrent/
    // gabaBMaxCurrent play for the other currents. Normal (non-synchronized)
    // AMPA activity should sit well under this ceiling and be unaffected;
    // it should only engage during pathological synchronized bursts. NOT
    // yet verified -- needs the same real-hardware sweep as everything
    // else, starting fresh since every prior AMPA result is now obsolete.
    float gMaxAMPA = 0.02f;

    // Dedicated ceiling on gAMPAEff itself (post gMaxAMPA-scaling), not
    // shared with maxSynCurrent -- see the long comment above. Starting
    // guess, not yet calibrated: chosen so a single neuron's peak AMPA
    // current (ceiling * full -65mV resting driving force ~= 65) lands in
    // the same rough scale as other individual currents in this model
    // (e.g. iL, iAHP) rather than dominating them outright the way an
    // unclamped synchronized-burst spike could.
    float ampaConductanceCeiling = 1.0f;

    // PHASE2_PLAN.md step 4: GABA-B direct agonism (baclofen). An agonist
    // activates the receptor from dose alone, independent of whether any
    // presynaptic GABA is being released -- architecturally distinct from
    // Block/Potentiate, which only have an effect where the spike-triggered
    // synaptic pathway is already active (see ReceptorDrugProfile.h). So
    // this is a SEPARATE peak-conductance scale from gMaxGABAb: the agonist
    // drive is ADDED on top of gMaxGABAb*receptorConductances[i].gGABAb,
    // not a multiplier on it, and needs its own calibration since it can
    // sustain a continuous (non-transient) conductance at saturating dose,
    // unlike the pulsed synaptic pathway. Starting guess, NOT yet
    // calibrated -- same as gMaxGABAb's own placeholder before its real-
    // hardware sweep found 0.0001. Needs the same treatment before trusting
    // baclofen validation results: sweep against real hardware, check
    // early/late rate + irregularity + spike-count histogram, same
    // methodology as every other gMax constant in this file.
    // CALIBRATED via real hardware (SPP_FORCE_CPU=1 --dose-eval,
    // Na/K/Ca channel block neutralized, EC50=5/Hill=2, 10 runs/dose):
    //   0.0001 -> weak (+7.7% max effect), poorly-fit (R^2=0.83), WRONG SIGN
    //             (excitatory) -- 3 runs, signal too small, noise dominated.
    //   0.01   -> sign confirmed CORRECT (suppressive, -4.4%), excellent fit
    //             (R^2=0.99) -- confirms the wiring/math is right, the
    //             0.0001 reading was pure noise, not a bug. Still too weak
    //             for a therapeutically meaningful baclofen signature.
    //   0.1    -> strong, smooth, monotonic dose-response: 17.2 -> 17.2 ->
    //             15.6 -> 14.0 -> 11.6 -> 10.4 Hz across dose 0-10 (~39%
    //             suppression at peak). neuron_stats.csv confirmed healthy
    //             at peak dose: spike counts tightly clustered ~5 (10Hz),
    //             no zero-spike neurons, no fire-once/collapse signature,
    //             normal ISI variance. This is the value in use -- may
    //             still want finer tuning against a specific Hz target
    //             later, same as gMaxGABAb/gMaxAMPA, but confirmed correct
    //             and healthy as-is.
    float gMaxGABAbAgonist = 0.1f;

    float maxSynCurrent = 300.0f;
    float maxTotalCurrent = 400.0f;

    float adaptationTauMs = 220.0f;
    float adaptationIncrement = 0.18f;
    float adaptationMaxCurrent = 6.0f;
    float adaptationInhibitoryScale = 0.55f;

    float drugOnsetTauMs = 120.0f;

    bool useGpu = false;

    // Phase 3b: GABA-A desensitization ("receptor tiredness"), see
    // synapse::DesensitizationConfig for the full design comment. OFF by
    // default -- with the default 400ms/500ms run durations this would be a
    // no-op anyway (500ms << 30s tau), but kept as an explicit opt-in so
    // every existing result stays byte-identical unless a test deliberately
    // enables it for a long-duration 3b run.
    bool desensitizationEnabled = false;
    float desensitizationTauDesenseMs = 30000.0f;
    float desensitizationTauRecoveryMs = 124000.0f;
    float desensitizationMaxAttenuation = 0.9f;
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