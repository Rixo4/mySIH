#pragma once

// Receptor pharmacology drug profiles -- Phase 2, §3/§4 of PHASE2_PLAN.md.
//
// The existing ChannelDrugProfile (DrugModel.h) only encodes fractional
// *block* of the intrinsic Na/K/Ca conductances. That single equation family
// is not enough for the receptor drugs Phase 2 has to model. Three distinct
// mechanisms are needed, and the distinction is the actual new conceptual
// capability here -- not "four more IC50 fields":
//
//   BLOCK      -- reduces a receptor conductance (AMPA blockers, NMDA
//                 blockers). Same math as ChannelDrugProfile::hillBlock:
//                 a 0->1 fractional reduction of an existing synaptic
//                 conductance. Only has an effect where that receptor is
//                 actually being driven by presynaptic activity.
//
//   POTENTIATE -- amplifies the response to synaptic transmitter that is
//                 already there (benzodiazepines / barbiturates on GABA-A).
//                 Does not block anything and does not create current on its
//                 own; it scales up the peak/decay of GABA-A conductance that
//                 the spike-triggered pathway is already producing, up to a
//                 capped ceiling (the pharmacological maximum -- e.g. the
//                 benzodiazepine ceiling). Effect is zero where there is no
//                 GABA-A synaptic activity to amplify.
//
//   AGONIST    -- activates the receptor directly from dose, independent of
//                 whether presynaptic transmitter is being released at all
//                 (baclofen on GABA-B). Architecturally distinct from every
//                 other mechanism in the engine: a silent network still shows
//                 tonic GABA-B activation under an agonist. Drives the target
//                 conductance from a dose->activation Hill curve, added on top
//                 of (not multiplied into) the spike-triggered pathway.
//
// EC50/IC50 and Hill values are per-drug configuration, sourced from the
// literature at wiring time (same standard as Phase 1's channel IC50s) rather
// than invented here -- this header only fixes the *shape* of the equations,
// not any specific drug's numbers. Defaults below are deliberately inert
// (mechanism None, EC50 huge) so an unconfigured profile is a no-op.

#include <cstdint>

#include "../synapse/ReuptakeTransporter.h"
#include "../synapse/NeuromodulatorSystem.h"

namespace spp::drug {

// Per-receptor mechanism tag. A single drug's ReceptorDrugProfile carries one
// of these per receptor, so a drug can (for example) block NMDA while leaving
// AMPA untouched, or potentiate GABA-A while doing nothing to GABA-B.
enum class ReceptorMechanism : std::uint8_t {
    None       = 0,  // this receptor is untouched by the drug
    Block      = 1,  // fractional reduction of the receptor conductance
    Potentiate = 2,  // amplify existing synaptic response toward a ceiling
    Agonist    = 3   // drive the conductance directly from dose (spike-independent)
};

// Parameters for one receptor's drug action. Which fields matter depends on
// `mechanism`:
//   Block      -> ec50 (as IC50), hill
//   Potentiate -> ec50, hill, maxPotentiationFactor (the ceiling, e.g. ~3x for
//                 a benzodiazepine; set very high / effectively uncapped for a
//                 barbiturate, which has no benzodiazepine-style ceiling)
//   Agonist    -> ec50, hill (maxPotentiationFactor unused)
//   None       -> all ignored
struct ReceptorAction {
    ReceptorMechanism mechanism = ReceptorMechanism::None;

    // Half-maximal concentration. For Block this is the classic IC50 (dose at
    // which the conductance is reduced by half at the ceiling); for Potentiate
    // and Agonist it is the EC50 (dose at half-maximal potentiation/activation).
    float ec50 = 1.0e9f;

    // Hill slope. Clamped to [1, 6] downstream, matching hillBlock.
    float hill = 1.0f;

    // Ceiling for POTENTIATE only: the maximum multiplicative boost applied to
    // the receptor's conductance at saturating dose. 1.0 => no potentiation;
    // 3.0 => up to 3x. Ignored for every other mechanism. A benzodiazepine has
    // a finite ceiling here; a barbiturate is modeled with a much larger (or
    // effectively unbounded) ceiling, which is why it is more dangerous in
    // overdose -- see PHASE2_PLAN.md §3 / open decision #3.
    float maxPotentiationFactor = 1.0f;
};

// ─── Phase 3a: reuptake transporter block ──────────────────────────────────
// A DIFFERENT axis from ReceptorAction above -- transporter block doesn't
// touch a receptor's peak conductance (gMax) or occupancy, it extends the
// receptor's own dual-exponential DECAY time constant (see
// engine/synapse/ReuptakeTransporter.h for the full design rationale: this
// is a bounded, literature-supported extension of an existing kinetic
// constant, not a new cleft-concentration state). EAAT block extends
// AMPA's and NMDA's decay (shared glutamatergic release site); GAT1 block
// extends GABA-A's and GABA-B's decay (shared GABAergic release site).
// SERT/DAT/NET are included for completeness and report purposes but have
// no receptor of their own in this engine yet (Phase 3c neuromodulator gain
// system) -- setting one of these is legal and will compute a real
// occupancy/effective-tau number, but produces no network/current effect
// until Phase 3c exists. Default is None (inert), so an unconfigured
// profile behaves exactly as it did before Phase 3a existed.
struct TransporterAction {
    spp::synapse::TransporterBlockType mechanism = spp::synapse::TransporterBlockType::None;
    float kiUm = 1.0e9f;             // uM, drug's inhibition constant at this transporter
    float hill = 1.0f;
    float maxExtensionFold = 1.0f;   // literature-bounded ceiling on decay-tau stretch
};

// ─── Tier 2.4 part 2: NMDA activity-dependent open-channel trapping ────────
// Ketamine's real interneuron-selectivity is NOT a receptor-density
// difference -- it is a kinetic consequence of ketamine being an OPEN-
// CHANNEL blocker: interneurons fire tonically at higher rates than
// pyramidal cells, so their NMDA channels sit open more of the time, giving
// ketamine more opportunity to bind and get trapped inside. Pyramidal cells
// fire less often, so their channels are blocked less. This is distinct
// from the flat `nmda.ec50/hill` Block mechanism above, which applies the
// identical fractional block to every neuron regardless of its own firing
// state. (Glasgow et al., J Neurosci 2017 -- see
// PRECISION_GAP_CLOSURE_PLAN.md Tier 2.4 for the full literature note.)
//
// Deliberately kept a DRUG-profile property (here), not an intrinsic
// receptor property like GABA-A desensitization (Synapse.h's
// DesensitizationConfig, which lives on the run-level SimulationConfig
// instead) -- memantine, the other NMDA blocker in this project's set,
// does NOT show this same activity-dependent interneuron lean per the same
// literature, so this cannot be a fixed property of the NMDA receptor
// itself; it has to travel with ketamine's own profile.
//
// Modeled as a persistent per-neuron 0..1 "trapped channel fraction" T,
// evolving each timestep as:
//   dT/dt = (1/tauTrapMs) * occupancy(dose) * drive * (1-T) - (1/tauUntrapMs) * T
// where occupancy(dose) is the ordinary Hill occupancy already computed
// from nmda.ec50/hill above (reused, not duplicated) and `drive` is this
// neuron's own raw (pre-block) NMDA conductance, 0..1-ish -- the same
// "existing conductance as activation proxy" trick already used for GABA-A
// desensitization (see DesensitizationConfig comment in Synapse.h). NMDA's
// effective residual becomes (1-T) instead of the flat (1-occupancy) when
// this is enabled, so block strength is driven by each neuron's own
// activity history, not just dose -- no cell-type flag anywhere; higher-
// firing neurons end up more blocked purely because they fire more.
//
// Deliberately opt-in / off by default (enabled=false): nmda.mechanism ==
// Block with this left disabled reproduces today's exact flat-block
// behavior byte-identically (the original test_ketamine.json, memantine,
// dextromethorphan, felbamate all keep working unchanged unless a test
// explicitly opts in). tauTrapMs/tauUntrapMs below are literature-informed
// STARTING ESTIMATES, not validated rate constants -- only the *shape* of
// the mechanism (activity-dependent trapping, fast-on/slow-off) is
// literature-grounded; the exact numbers need real domain-expert
// (electrophysiology) review before being treated as a validated claim.
struct NmdaActivityDependentBlock {
    bool enabled = false;
    float tauTrapMs = 300.0f;     // fast: binding while a drug-occupied channel is open
    float tauUntrapMs = 6000.0f;  // slow: ketamine's hallmark "trapping blocker" off-rate
};

// One drug's full receptor pharmacology: an action per receptor type. A drug
// that only touches one receptor leaves the other three at mechanism None.
struct ReceptorDrugProfile {
    ReceptorAction ampa;    // typically Block (e.g. perampanel) or None
    ReceptorAction nmda;    // typically Block (e.g. ketamine, memantine) or None
    ReceptorAction gabaA;   // typically Potentiate (benzos/barbiturates) or None
    ReceptorAction gabaB;   // typically Agonist (baclofen) or None

    // Tier 2.4 part 2: opt-in activity-dependent refinement of `nmda` above
    // -- see NmdaActivityDependentBlock comment. Only meaningful when
    // nmda.mechanism == Block; ignored otherwise.
    NmdaActivityDependentBlock nmdaActivityBlock;

    // Phase 3a additions -- see TransporterAction comment above. All default
    // to None/inert.
    TransporterAction eaat; // glutamate transporter (EAAT1/2) -> extends AMPA+NMDA decay
    TransporterAction gat1; // GABA transporter (GAT1) -> extends GABA-A+GABA-B decay
    TransporterAction sert; // serotonin transporter -- report-only, no receptor yet
    TransporterAction dat;  // dopamine transporter -- report-only, no receptor yet
    TransporterAction net;  // norepinephrine transporter -- report-only, no receptor yet

    // Phase 3c additions -- see engine/synapse/NeuromodulatorSystem.h for the
    // full design rationale. All default to inert (ec50 huge / gain
    // ceilings = 1.0), so an unconfigured profile is a no-op, same policy
    // as every other field in this struct.
    spp::synapse::NeuromodulatorProfile neuromod;
};

} // namespace spp::drug