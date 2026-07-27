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
// probability proxy).
struct DopamineD2Action {
    float ec50 = 1.0e9f;
    float hill = 1.0f;
    float maxReleaseReductionFrac = 0.0f;
};

// Serotonin 5-HT1A -- see header note. maxKGainFold is the ceiling fold-
// increase in gKEff at saturating occupancy (>=1).
struct Serotonin5HT1AAction {
    float ec50 = 1.0e9f;
    float hill = 1.0f;
    float maxKGainFold = 1.0f;
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
// modifiers. Dose=0 or a fully-inert profile returns NeuromodulatorGainModifiers{}
// (all 1.0) exactly -- see DrugModel.cpp's call site for how this gets
// applied per-block per-timestep.
NeuromodulatorGainModifiers computeNeuromodulatorGainModifiers(
    float dose,
    const NeuromodulatorProfile& profile
);

} // namespace spp::synapse