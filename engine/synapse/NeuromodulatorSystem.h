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

// Norepinephrine alpha-2 -- Tier 2.2 (PRECISION_GAP_CLOSURE_PLAN.md). Unlike
// D1/D2/5-HT1A/5-HT2A above (each a single G-protein-family receptor with an
// optional second curve for a genuinely distinct anatomical pathway), alpha-2
// itself has TWO well-documented, mechanistically distinct roles that both
// matter clinically -- neither is a "bonus precision" addition, both are
// built in the same pass:
//
//   PRESYNAPTIC autoreceptor (Gi-coupled, classical clonidine mechanism):
//     inhibits presynaptic Ca2+ channels, reduces norepinephrine release
//     probability -- exactly the same signaling logic as D2's presynaptic
//     pathway above (same G-protein family, same lever). Modeled here as:
//     scales DOWN excitatoryWeightScale, mirroring DopamineD2Action's
//     maxReleaseReductionFrac exactly. presynapticEc50/Hill default to fully
//     inert (huge EC50), maxPresynapticReleaseReductionFrac defaults to 0.
//
//   POSTSYNAPTIC PFC pyramidal dendritic spine role (guanfacine's real
//     clinical mechanism, NOT the presynaptic autoreceptor): alpha-2A
//     receptors on PFC pyramidal spines inhibit cAMP production, which
//     closes HCN (hyperpolarization-activated cyclic-nucleotide-gated)
//     channels, strengthening PFC network connectivity and working-memory
//     performance (Wang et al 2007, Cell 129:397 -- "alpha2A-Adrenoceptors
//     Strengthen Working Memory Networks by Inhibiting cAMP-HCN Channel
//     Signaling in Prefrontal Cortex"). This is a Gs-cascade-INHIBITION
//     pathway, same net "less braking / more excitable" direction as D1's
//     adaptation-reduction lever, so it reuses that exact target. Modeled
//     here as: scales DOWN adaptationScale, mirroring DopamineD1Action's
//     maxAdaptationReductionFrac exactly. postsynapticEc50/Hill default to
//     fully inert (huge EC50), maxPostsynapticAdaptationReductionFrac
//     defaults to 0.
//
// Both curves default to fully inert -- any config that doesn't set these
// fields is a bit-identical no-op, same guarantee as every other field in
// this file. No literature-sourced NE-specific EC50 was found this session
// for either curve (order-of-magnitude anchors only: clonidine ~17nM for
// presynaptic glutamate-release inhibition, UK14304 ~173nM for presynaptic
// dopamine-release inhibition) -- both EC50 fields are illustrative
// placeholders until a real drug-specific value is sourced, same "flagged
// placeholder" policy as 5-HT1A's max_autoreceptor_suppression magnitude.
struct Alpha2Action {
    float presynapticEc50 = 1.0e9f;
    float presynapticHill = 1.0f;
    float maxPresynapticReleaseReductionFrac = 0.0f;
    float postsynapticEc50 = 1.0e9f;
    float postsynapticHill = 1.0f;
    float maxPostsynapticAdaptationReductionFrac = 0.0f;
};

// Norepinephrine beta -- Tier 2.2 completion (PRECISION_GAP_CLOSURE_PLAN.md).
// IMPORTANT DIRECTION NOTE, found via literature check before building (not
// assumed from G-protein family alone): beta receptors are Gs-coupled, same
// family as D1 above, but the naive "same family = same lever direction"
// assumption does NOT hold here. Beta1-AR activation in PFC pyramidal cells
// OPENS HCN channels via the cAMP pathway (Yi et al, PMC5701640,
// "Noradrenaline Modulates the Membrane Potential ... via beta1-Adrenergic
// Receptors and HCN Channels") -- the MIRROR IMAGE of alpha-2's postsynaptic
// action above (which CLOSES the same HCN channels, Wang et al 2007). Same
// channel target, opposite G-protein family (Gs vs Gi), opposite direction.
// HCN opening increases dendritic leak and dampens persistent firing --
// consistent with the broader finding that high noradrenaline/cAMP-PKA
// SUPPRESSES persistent firing in PFC and impairs working memory at high
// levels (Ramos & Arnsten 2007, Pharmacol Ther 113:523-536; Arnsten's
// inverted-U catecholamine model). Modeled here as: INCREASES
// adaptationScale (more braking) -- the mirror image of D1's/alpha-2-
// postsynaptic's DEcrease-adaptation lever, same target, opposite
// direction, deliberately NOT copied from D1 despite the shared Gs
// coupling. maxAdaptationIncreaseFold is a ceiling FOLD-INCREASE (>=1,
// same "fold" convention as D1's maxNmdaGainFold), not a 0..1 reduction
// fraction -- inert default is 1.0 (no increase). No presynaptic/
// postsynaptic split built: unlike alpha-2, no comparably strong dual-role
// literature was found this session for beta in this engine's scope, so it
// stays single-pathway (same "don't build ahead of evidence" discipline as
// everywhere else in this file). No literature-sourced NE-specific EC50
// found this session -- illustrative placeholder, same flagged-placeholder
// policy as alpha-2's EC50s.
struct BetaAction {
    float ec50 = 1.0e9f;
    float hill = 1.0f;
    float maxAdaptationIncreaseFold = 1.0f;
};

// Norepinephrine alpha-1 -- Tier 2.2 completion (PRECISION_GAP_CLOSURE_PLAN.md).
// Same direction-check discipline as beta above: alpha-1 is Gq-coupled,
// same family as 5-HT2A, but literature does NOT support copying 5-HT2A's
// "less braking" direction. Alpha-1 stimulation in primate dorsolateral PFC
// SUPPRESSES pyramidal neuron firing via a calcium-PKC cascade (Arnsten
// lab: Birnbaum et al 1999, Biol Psychiatry 46:1266; Mao et al 2019, J
// Neurosci 39:2722 -- "Noradrenergic alpha1-Adrenoceptor Actions in the
// Primate Dorsolateral Prefrontal Cortex"), specifically engaged under
// high-norepinephrine/stress conditions -- the opposite functional
// direction from 5-HT2A's K+-leak-reduction/"less braking" role (Herzog et
// al 2025) despite the shared Gq coupling. No K+-conductance-specific
// citation was found this session for alpha-1 (unlike 5-HT2A's Herzog et al
// finding), so only the adaptation lever is modeled here, not a gKEff lever
// -- avoids inventing a mechanism beyond what was actually found. Modeled
// here as: INCREASES adaptationScale (more braking), same lever/target and
// direction as beta above (both suppress PFC firing via different
// G-protein routes converging on the same net effect), opposite of
// 5-HT2A's decrease. maxAdaptationIncreaseFold is a ceiling fold-increase
// (>=1), same convention as beta's field above. No literature-sourced
// NE-specific EC50 found this session -- illustrative placeholder, same
// flagged-placeholder policy as beta/alpha-2's EC50s.
struct Alpha1Action {
    float ec50 = 1.0e9f;
    float hill = 1.0f;
    float maxAdaptationIncreaseFold = 1.0f;
};

// One shared profile for the whole batch (the compound under test) -- same
// "single drug, one profile" pattern as ReceptorDrugProfile/
// ReuptakeTransporter's per-drug structs.
struct NeuromodulatorProfile {
    DopamineD1Action d1;
    DopamineD2Action d2;
    Serotonin5HT1AAction ht1a;
    Serotonin5HT2AAction ht2a;
    Alpha2Action alpha2;
    BetaAction beta;
    Alpha1Action alpha1;
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

    // Tier 2.4 part 1 (PRECISION_GAP_CLOSURE_PLAN.md): cell-type-selective
    // addition to adaptationScale, layered ON TOP of the shared
    // adaptationScale above rather than replacing it. Why a separate pair
    // of fields instead of just making adaptationScale itself cell-type-
    // selective: D1/5-HT2A/alpha-2-postsynaptic already validated on real
    // hardware applying their (de)increase uniformly to both cell types via
    // the shared field above -- changing that field's meaning would
    // silently alter their already-proven behavior. Beta and alpha-1's
    // source literature (Yi et al PMC5701640; Arnsten lab) is specifically
    // about PFC PYRAMIDAL (excitatory) neurons, so they populate
    // adaptationScaleExcitatory ONLY, leaving adaptationScaleInhibitory
    // inert -- this is the mechanism believed responsible for Tier 2.2's
    // paradoxical population-level excitatory reading (leading hypothesis:
    // uniformly suppressing both cell types was disinhibiting the network;
    // restricting the effect to excitatory cells removes that path). Both
    // default to 1.0 (fully inert), so any config not using beta/alpha-1 is
    // a bit-identical no-op -- same guarantee as every other field in this
    // file. Consumers multiply BOTH the shared adaptationScale AND the
    // matching per-cell-type field into each neuron's adaptation step (see
    // BatchedSimulationEngine.cpp).
    float adaptationScaleExcitatory = 1.0f;
    float adaptationScaleInhibitory = 1.0f;
};

// Standard Hill occupancy fraction (0..1), same formula used everywhere else
// in this engine (Phase 1 channel block, Phase 2 receptor mechanisms, Phase
// 3a transporter occupancy). Shared here since all four neuromodulator
// receptors use the identical dose-response shape.
float neuromodulatorOccupancy(float dose, float ec50, float hill);

// Composes all five receptors' contributions into one set of gain
// modifiers. Takes THREE doses rather than one -- see ReuptakeTransporter.h's
// amplifiedDoseUm comment: SERT/DAT/NET reuptake block (Phase 3a) amplifies
// the EFFECTIVE dose the corresponding receptor "sees", and since SERT only
// touches serotonin (5-HT1A/5-HT2A), DAT only touches dopamine (D1/D2), and
// NET only touches norepinephrine (alpha-2, Tier 2.2), the three systems can
// have genuinely different effective doses once a transporter-blocking drug
// is layered on top of a direct receptor-agonist dose. doseForDopamine,
// doseForSerotonin, and doseForNorepinephrine are identical (all equal to
// the raw dose) whenever no SERT/DAT/NET transporter block is configured --
// see DrugModel::computeNeuromodulatorGainModifiers for where the
// amplification is actually computed. Dose=0 or a fully-inert profile
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
    float doseForNorepinephrine,
    const NeuromodulatorProfile& profile,
    float currentTimeMs = 0.0f
);

} // namespace spp::synapse