#pragma once

#include <cstdint>
#include <random>

// ─── Phase 3c: Vesicle Pool Dynamics (PHASE3_PLAN.md section 4) ────────────
//
// DESIGN NOTE (scope: per-PRESYNAPTIC-NEURON pools, not literally per-synapse):
// The plan's diagram describes three pools "per synapse". This engine has no
// per-synapse state at all -- SynapseMatrix::accumulateReceptorConductances
// (see Synapse.h/.cpp) accumulates ALL of a postsynaptic neuron's incoming
// edges into one shared conductance accumulator; there is no existing
// per-edge storage to hang three new pool floats off of. Building true
// per-synapse pools would mean a new per-EDGE state array -- edges typically
// outnumber neurons by a large factor in a connected network -- plus
// restructuring the inner edge-accumulation loop that the plan itself
// already names as the current performance bottleneck. Given that, and
// given real vesicle depletion is substantially a property of the
// PREsynaptic bouton's own firing history (shared reasonably across a
// neuron's outgoing connections as a first approximation, per Rizzoli & Betz
// 2005's whole-terminal pool framing), pools here are tracked one triplet
// per neuron (as the presynaptic side), not per edge. This is a scope
// decision made explicitly to keep the same performance/complexity profile
// as every other Phase 3 piece -- flagged here rather than silently
// deviating from the plan's diagram.
//
// DESIGN NOTE (timescale honesty, same discipline as ReuptakeTransporter.h's
// tau-extension finding for SERT/GAT1): Rizzoli & Betz (Nat Rev Neurosci,
// 2005) and Dobrunz (Int J Dev Neurosci, 2002) put RRP refilling at roughly
// 1 minute and reserve-pool refilling at a several-minute half-time under
// sustained stimulation -- both far longer than this engine's default
// ~400-500ms dose-eval window (see main.cpp sim_time). Depletion itself,
// though, is fast: docked-vesicle Ca-triggered release can measurably
// deplete a small (5-20 vesicle) RRP within a handful of spikes at ordinary
// firing rates, so within-run "synaptic fatigue" IS a real, observable
// signal at default sim_time. Recovery/refill dynamics are NOT observable
// within a default run and need a deliberately extended sim_time_ms, exactly
// the same opt-in long-duration pattern already used for Phase 3b's
// desensitization plateau/calibration configs. Report fields built on top
// of this should say so honestly rather than implying a 500ms run showed
// "recovery" it structurally could not have shown.
//
// DESIGN NOTE (why relaxation-to-target, not the plan's literal mass-action
// ODEs): PHASE3_PLAN.md section 4 writes refill as
//   dRRP/dt     = (Reserve * k_mobilise) - (release events)
//   dReserve/dt = Recovery * k_recycle - Reserve * k_mobilise
// with Recovery treated as an unlimited source. Implemented literally, a
// depleted Reserve would refill from an infinite source at an unbounded
// rate. Instead both pools relax toward their own fresh/target size on
// their respective tau, the same bounded-exponential-relaxation family
// already used throughout this engine (decay accumulators, Phase 3b's
// desensitization state) -- RRP's relaxation rate is additionally scaled by
// Reserve's own fractional availability, so a depleted Reserve genuinely
// slows RRP refilling rather than that coupling being lost. Faithful to the
// plan's INTENT (fast pool refills from a slower one, slower one refills
// from an effectively unlimited source), simplified to the same numerically
// stable form as everything else in this file's neighborhood.

namespace spp::synapse {

// Opt-in configuration -- default-constructed with enabled=false, meaning
// callers that never touch this get exactly today's behavior (release scale
// of 1.0 always, see triggerRelease's baseline-preservation note below).
struct VesiclePoolConfig {
    bool enabled = false;

    // Pool sizes (vesicle counts). Defaults are literature-informed orders
    // of magnitude for a single cortical/hippocampal active zone (Rizzoli &
    // Betz 2005): RRP ~5-20 vesicles, Reserve ~50-200. Recovery is not
    // tracked as a finite pool -- see PHASE3_PLAN.md's "essentially
    // unlimited supply" framing -- it only appears here as Reserve's
    // relaxation target ceiling.
    float rrpSize     = 10.0f;
    float reserveSize = 100.0f;

    // Refill time constants (ms). ~1-2s for RRP<-Reserve, ~10-30s for
    // Reserve<-Recovery, per PHASE3_PLAN.md section 4's stated orders of
    // magnitude (broadly consistent with the minute-scale refill timescales
    // in Rizzoli & Betz 2005 and Dobrunz 2002, see header note above on why
    // these are far longer than a default dose-eval run).
    float rrpRefillTauMs     = 1500.0f;
    float reserveRefillTauMs = 20000.0f;

    // Ca-dependent per-vesicle release probability, per PHASE3_PLAN.md
    // section 4: releaseProb = 1 - exp(-calciumFactor * driveProxy). Higher
    // calciumFactor saturates release probability at a lower drive level.
    // Calibrated at implementation/validation time against a real drug
    // config, same discipline as every other Phase 3 ceiling constant.
    float calciumFactor = 1.0f;
};

// Per-presynaptic-neuron pool state. Persists across timesteps like
// ReceptorConductanceState. Default-constructed at each pool's fresh/full
// size, matching VesiclePoolConfig's own defaults (kept in sync manually --
// see BatchedSimulationEngine's construction site).
struct VesiclePoolState {
    float rrp     = 10.0f;
    float reserve = 100.0f;
};

// Continuous refill -- called every timestep for EVERY neuron, whether or
// not it spiked this step (refill is a background process, independent of
// firing). No-op (exact passthrough) when cfg.enabled is false.
void refillVesiclePools(VesiclePoolState& state, const VesiclePoolConfig& cfg, float dtMs);

// Spike-triggered release -- called only for a neuron that fired this step.
// Draws a binomial release count from the current (possibly depleted) RRP
// using the Ca-dependent per-vesicle release probability, depletes RRP by
// that count, and returns a 0..1+ "release scale" meant to multiply this
// spike's contribution to every outgoing synapse (see Synapse.h's
// DelayBuffer release-scale extension).
//
// BASELINE-PRESERVATION NOTE: the returned scale is releasedCount divided by
// the EXPECTED release count a completely fresh (undepleted) pool would
// produce for this same driveProxy -- not divided by rrpSize, and not the
// raw releaseProb. This means a fresh/undepleted pool returns ~1.0 in
// expectation (matching today's un-scaled behavior exactly, so every
// already-calibrated synaptic weight/conductance ceiling from Phase 1/2/3a/
//3b keeps working unchanged the instant a run starts), with only actual
// depletion below the fresh size pulling the scale below 1.0. Natural
// binomial sampling noise around 1.0 at a fresh pool is expected and
// harmless -- spikes are already a stochastic process end to end.
float triggerVesicleRelease(
    VesiclePoolState& state,
    const VesiclePoolConfig& cfg,
    float excitatoryDriveProxy,
    std::mt19937& rng
);

} // namespace spp::synapse