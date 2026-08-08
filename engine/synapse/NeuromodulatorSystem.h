#pragma once

// ─── Phase 3c: Neuromodulator gain system ──────────────────────────────────
//
// Dopamine, serotonin, norepinephrine, and acetylcholine are NOT classical
// neurotransmitters in the AMPA/NMDA/GABA-A/GABA-B sense -- they don't drive
// their own fast synaptic current at a dedicated release site. They change
// HOW the network responds to the input it already has: think dimmer
// switches (gain control), not on/off switches. PHASE3_PLAN.md §4 frames
// this explicitly and states this is "a better way to build D1/D2/5-HT1A/
// 5-HT2A effects than treating them as new GABA-B-shaped conductance
// types -- gain modulation of the existing system is closer to how these
// receptors actually work biologically."
//
// DESIGN: each of the four receptors below is modeled as a standard Hill-
// occupancy dose-response (identical math to every other mechanism in this
// engine -- Phase 1's channel block, Phase 2's receptor Block/Potentiate/
// Agonist, Phase 3a's transporter occupancy) that produces a 0..1 occupancy
// fraction, which then scales existing simulation parameters this engine
// already has -- gKEff (intrinsic K+ conductance), gMaxNMDA (NMDA peak
// conductance), the adaptation-current constants, and excitatory synaptic
// weight (as a release-probability proxy). No new receptor current, no new
// per-timestep state beyond what a Hill calculation needs -- reusing the
// exact same "occupancy computed fresh per neuron per timestep from that
// block's dose" pattern already used for Phase 2's receptor mechanisms.
//
// LITERATURE BASIS for each mapping (representative/simplified at the
// "Phase 3 level" the plan specifies -- full G-protein cascade detail is
// explicitly deferred to a conceptual Phase 4, same treatment as nicotinic/
// mu-opioid receptors in PHASE3_PLAN.md §5):
//
//   D1 (Gs-coupled): real recordings show D1 activation reduces spike-
//     frequency adaptation and enhances NMDA transmission (Durstewitz &
//     Seamans 2002, Neural Netw 15:561-572; Gee et al 2012, J Neurosci
//     32:10516 -- D1 modulation of layer 5 PFC pyramidal firing). Modeled
//     here as: shrinks the adaptation-current parameters, and scales up
//     gMaxNMDA. (Real D1 gain effects are more complex than a simple slope
//     increase at all input levels -- Gee et al found opposite effects at
//     low vs high inputs relative to naive gain models -- but a single
//     representative simplification is used here, same "one representative
//     value" policy as every other receptor in this engine.)
//
//   D2 (Gi-coupled): confirmed to inhibit presynaptic Ca2+ channels AND the
//     secretory machinery downstream of Ca2+ influx, reducing transmitter
//     release probability (Congar et al 2002, J Neurophysiol 87:1046;
//     Bunney lab presynaptic autoreceptor studies, J Neurosci 21:9134).
//     Modeled here as: scales down excitatory synaptic weight -- a release-
//     probability proxy, same spirit as Phase 3a's transporter-tau proxy for
//     "more/less transmitter available."
//
//   5-HT1A: strongly confirmed to open GIRK K+ channels, hyperpolarizing
//     and reducing excitability globally (Andrade & Nicoll-type hippocampal/
//     raphe studies; see review PMC10144171). Modeled here as: scales UP
//     gKEff -- literally the mirror image of this engine's existing K+-
//     channel-block mechanism (Phase 1), just increasing instead of
//     decreasing.
//
//     Tier 2.1 addition (PRECISION_GAP_CLOSURE_PLAN.md): 5-HT1A also has a
//     well-documented presynaptic (somatodendritic) autoreceptor role on
//     raphe serotonin neurons, distinct from the postsynaptic gKEff action
//     above -- this is the textbook mechanism behind SSRIs' delayed
//     (2-4 week) clinical onset: early on, autoreceptor activation
//     suppresses serotonergic tone; the autoreceptor desensitizes over
//     time, letting the postsynaptic effect emerge (el Mansari et al 2005,
//     J Neurosci 21:8188; Blier & de Montigny, review). No new neuron
//     population needed to represent this -- modeled as a SECOND
//     Hill-occupancy curve on the same dose, with its own (typically
//     lower, i.e. more sensitive) EC50, that ATTENUATES the postsynaptic
//     gKEff effect rather than acting independently. This reproduces the
//     qualitative early-suppression-then-emerging-effect pattern without
//     modeling actual raphe neuron firing or endogenous serotonin release/
//     clearance -- see maxAutoreceptorSuppressionFrac field comment below
//     for the exact combination formula. Representative literature ratio:
//     autoreceptor-preferring vs postsynaptic-preferring 5-HT1A ligands
//     (F13714 vs F15599) differ in binding affinity by roughly 30x (Ki=0.1nM
//     vs 3.4nM respectively) -- used as a rough order-of-magnitude anchor
//     for autoreceptorEc50, not a drug-specific measured value. Same "one
//     representative value" policy as the rest of this file.
//
//   5-HT2A: confirmed to reduce K+ leak conductance (opposite direction from
//     5-HT1A) and inhibit the post-burst afterhyperpolarization current,
//     depolarizing and increasing excitability/firing (computational model
//     in Herzog et al 2025 bioRxiv 2025.10.20.683366 explicitly links 5-HT2A
//     activation to reduced K+ leak conductance; IAHP-inhibition literature
//     reviewed in PMC3630391). Modeled here as: scales DOWN gKEff, and also
//     shrinks the adaptation-current parameters (same lever as D1, opposite
//     biological pathway, same net "less braking" effect).
//
// All four default to fully inert (ec50 huge / gain ceilings = 1.0), so an
// unconfigured drug run is a bit-identical no-op -- same baseline-
// preservation policy as every other Phase 1/2/3 mechanism.

namespace spp::synapse {

// Dopamine D1 -- see header note. maxAdaptationReductionFrac is the FRACTION
// of the adaptation-current parameters removed at saturating occupancy
// (0 = no effect, 1 = adaptation fully abolished); maxNmdaGainFold is the
// ceiling FOLD-INCREASE in gMaxNMDA at saturating occupancy (>=1).
struct DopamineD1Action {
    float ec50 = 1.0e9f;
    float hill = 1.0f;
    float maxAdaptationReductionFrac = 0.0f;
    float maxNmdaGainFold = 1.0f;
};

// Dopamine D2 -- see header note. maxReleaseReductionFrac is the fraction of
// excitatory synaptic weight removed at saturating occupancy (release-
// probability proxy) -- this is the PRESYNAPTIC autoreceptor pathway
// (classical Gi/cAMP signaling, inhibits presynaptic Ca2+ channels and the
// secretory machinery).
//
// Tier 2.1 D2 postsynaptic split (PRECISION_GAP_CLOSURE_PLAN.md): D2 also
// has a real, distinct POSTSYNAPTIC role on striatal medium spiny neurons --
// suppresses L-type Ca2+ currents and excitability via a PLCbeta1-IP3-
// calcineurin cascade, EXPLICITLY NOT the classical Gi/cAMP pathway the
// presynaptic fields above use (Hernandez-Lopez et al 2000, J Neurosci
// 20:8987 -- confirmed the suppression is not mediated by adenylyl cyclase
// inhibition, ruling out a shared-mechanism shortcut). Modeled as a SECOND,
// independent Hill-occupancy curve on the same dose (own EC50/Hill, since
// it's a genuinely different signaling route, not just a relabeled version
// of the presynaptic one) that scales DOWN gCaEff -- the mirror image of
// 5-HT1A's postsynaptic gKEff-boost lever, same "second curve, own struct
// fields" pattern as 5-HT1A's autoreceptor addition. postsynapticEc50/Hill
// default to fully inert (huge EC50), maxPostsynapticCaReductionFrac
// defaults to 0 -- any config that doesn't set these three fields is a
// bit-identical no-op, same guarantee as every other field in this file.
struct DopamineD2Action {
    float ec50 = 1.0e9f;
    float hill = 1.0f;
    float maxReleaseReductionFrac = 0.0f;
    float postsynapticEc50 = 1.0e9f;
    float postsynapticHill = 1.0f;
    float maxPostsynapticCaReductionFrac = 0.0f;
};

// Serotonin 5-HT1A -- see header note. maxKGainFold is the ceiling fold-
// increase in gKEff at saturating occupancy (>=1). autoreceptorEc50/Hill
// and maxAutoreceptorSuppressionFrac model the presynaptic autoreceptor
// pathway (Tier 2.1) -- a second, independent occupancy curve on the same
// dose that attenuates (does not replace) the postsynaptic gKEff effect
// above. All fields default to fully inert (huge EC50 / 0 suppression),
// same zero-drift-when-unconfigured guarantee as every other field here.
//
// Tier 2.1 correction: an earlier version of this struct only had the
// three fields above, making the autoreceptor's suppression purely
// DOSE-dependent (evaluated fresh each instant from occupancy alone). That
// does not reproduce real SSRIs' delayed clinical onset, which is a
// TIME-dependent phenomenon: the autoreceptor desensitizes over continued
// exposure at a FIXED dose (el Mansari et al 2005, J Neurosci 21:8188;
// Blier & de Montigny). autoreceptorTauDesenseMs/autoreceptorTauRecoveryMs
// add that time dependence using the exact same desensitization ODE
// already validated for GABA-A (see Synapse.h's DesensitizationConfig):
//   dD/dt = kDesense * drive * (1-D) - kRecover * D
// but solved in CLOSED FORM rather than stepped numerically, because
// `drive` here (the autoreceptor's own Hill occupancy) is constant for the
// whole duration of any fixed-dose run -- unlike GABA-A's drive, which is
// a genuinely time-varying per-neuron synaptic conductance and therefore
// needs real per-neuron persistent state. For constant drive, the ODE has
// an exact analytic solution (see computeNeuromodulatorGainModifiers),
// which avoids needing any new per-neuron GPU memory allocation.
// autoreceptorTauDesenseMs default (huge/inert) means "never desensitizes"
// -- backward compatible with the dose-only behavior when unconfigured.
//
// Tier 2.1 real-timescale fix (PRECISION_GAP_CLOSURE_PLAN.md, 2026-08-06):
// literature gives a real, citable time course for this desensitization --
// Blier & de Montigny's chronic 5-HT1A-agonist electrophysiology (dorsal
// raphe firing-rate recovery): ~2 days treatment = firing still markedly
// suppressed (autoreceptor essentially undesensitized), 7 days = partial
// recovery, 14 days = complete recovery. Fitting the same closed-form
// D(t)=Dss*(1-exp(-rate*t)) to those three anchor points gives
// autoreceptorTauDesenseMs ~= 7 days (604,800,000 ms) as the default real
// value (1 tau at the 7-day "partial recovery" point, ~86% recovered by
// 14 days, consistent with "complete" given the papers' qualitative
// resolution). autoreceptorTauRecoveryMs (resensitization after the drug
// is stopped) has NO equivalent literature value found this session --
// still an illustrative/unsourced placeholder, flagged as such wherever
// it's used, same as buspirone's max_autoreceptor_suppression magnitude.
//
// Problem this real tau creates: at dt=0.04ms, actually SIMULATING 7-14
// days of network dynamics end-to-end is computationally impossible
// (~1.5e13 timesteps). But the fast network dynamics (ms-scale HH/spiking)
// and the slow autoreceptor state (day/week-scale, already closed-form,
// doesn't need per-step integration) don't need to share a clock.
// autoreceptorExposureOffsetMs decouples them: it's ADDED to the run's own
// elapsed sim time only when evaluating the autoreceptor's D(t) term, so a
// short, cheap "probe" simulation (seconds of network dynamics, enough for
// stable firing-rate metrics) can measure "what does the network look like
// after N days of continuous exposure" by setting this offset to N days,
// mirroring real chronic-dosing-then-brief-recording electrophysiology
// protocols exactly (the Blier & de Montigny papers above ARE brief
// recordings taken after N days of chronic dosing, not N-day-long
// recordings). Defaults to 0.0f (fully inert, matches this run's own
// elapsed time exactly, same behavior as before this field existed).
struct Serotonin5HT1AAction {
    float ec50 = 1.0e9f;
    float hill = 1.0f;
    float maxKGainFold = 1.0f;
    float autoreceptorEc50 = 1.0e9f;
    float autoreceptorHill = 1.0f;
    float maxAutoreceptorSuppressionFrac = 0.0f;
    float autoreceptorTauDesenseMs = 1.0e12f;
    float autoreceptorTauRecoveryMs = 1.0e9f;
    float autoreceptorExposureOffsetMs = 0.0f;
};

// Serotonin 5-HT2A -- see header note. maxKReductionFrac is the fraction of
// gKEff removed at saturating occupancy; maxAdaptationReductionFrac mirrors
// D1's adaptation lever (same target, independent ceiling).
struct Serotonin5HT2AAction {
    float ec50 = 1.0e9f;
    float hill = 1.0f;
    float maxKReductionFrac = 0.0f;
    float maxAdaptationReductionFrac = 0.0f;
};

// One shared profile for the whole batch (the compound under test) -- same
// "single drug, one profile" pattern as ReceptorDrugProfile/
// ReuptakeTransporter's per-drug structs.
struct NeuromodulatorProfile {
    DopamineD1Action d1;
    DopamineD2Action d2;
    Serotonin5HT1AAction ht1a;
    Serotonin5HT2AAction ht2a;
};

// Final composed multiplicative scale factors to apply to this engine's
// existing simulation parameters. All default to 1.0 (fully inert) --
// computeNeuromodulatorGainModifiers returns exactly these defaults when
// every action in the profile is unconfigured (ec50 at its inert default),
// guaranteeing zero drift for any caller that doesn't opt in.
struct NeuromodulatorGainModifiers {
    float gKEffScale             = 1.0f; // multiplies intrinsic K+ conductance
    float gCaEffScale            = 1.0f; // multiplies intrinsic Ca2+ conductance (D2 postsynaptic, Tier 2.1)
    float gMaxNmdaScale          = 1.0f; // multiplies NMDA peak conductance
    float adaptationScale        = 1.0f; // multiplies adaptation-current constants
    float excitatoryWeightScale  = 1.0f; // multiplies excitatory synaptic weight
};

// Standard Hill occupancy fraction (0..1), same formula used everywhere else
// in this engine (Phase 1 channel block, Phase 2 receptor mechanisms, Phase
// 3a transporter occupancy). Shared here since all four neuromodulator
// receptors use the identical dose-response shape.
float neuromodulatorOccupancy(float dose, float ec50, float hill);

// Composes all four receptors' contributions into one set of gain
// modifiers. Takes TWO doses rather than one -- see ReuptakeTransporter.h's
// amplifiedDoseUm comment: SERT/DAT reuptake block (Phase 3a) amplifies the
// EFFECTIVE dose the corresponding receptor "sees", and since SERT only
// touches serotonin (5-HT1A/5-HT2A) while DAT only touches dopamine
// (D1/D2), the two systems can have genuinely different effective doses
// once a transporter-blocking drug is layered on top of a direct
// receptor-agonist dose. doseForDopamine and doseForSerotonin are identical
// (both equal to the raw dose) whenever no SERT/DAT transporter block is
// configured -- see DrugModel::computeNeuromodulatorGainModifiers for where
// the amplification is actually computed. Dose=0 or a fully-inert profile
// returns NeuromodulatorGainModifiers{} (all 1.0) exactly.
// Tier 2.1: currentTimeMs is the elapsed simulation time (0 at the start
// of a run), needed only for 5-HT1A's autoreceptor desensitization term --
// every other receptor/lever in this function ignores it completely.
// Passing 0.0f exactly reproduces this function's pre-time-dependence
// behavior (D(0) = 0, i.e. freshly-undesensitized), so any caller that
// doesn't care about time-dependence can pass 0.0f safely.
NeuromodulatorGainModifiers computeNeuromodulatorGainModifiers(
    float doseForDopamine,
    float doseForSerotonin,
    const NeuromodulatorProfile& profile,
    float currentTimeMs = 0.0f
);

} // namespace spp::synapse