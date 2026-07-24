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

// One drug's full receptor pharmacology: an action per receptor type. A drug
// that only touches one receptor leaves the other three at mechanism None.
struct ReceptorDrugProfile {
    ReceptorAction ampa;    // typically Block (e.g. perampanel) or None
    ReceptorAction nmda;    // typically Block (e.g. ketamine, memantine) or None
    ReceptorAction gabaA;   // typically Potentiate (benzos/barbiturates) or None
    ReceptorAction gabaB;   // typically Agonist (baclofen) or None
};

} // namespace spp::drug
