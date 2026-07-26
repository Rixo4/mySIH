#pragma once

// ─── Phase 3a: Reuptake transporter pharmacology ───────────────────────────
//
// DESIGN NOTE (why this is a tau-extension, not a cleft-concentration ODE):
// The original Phase 3a plan proposed a full synaptic-cleft [NT](t)
// concentration state feeding a Michaelis-Menten receptor-activation curve.
// Two pieces of evidence ruled that out during implementation:
//
//   1. Real electrophysiology shows GAT1/EAAT block barely changes a
//      single quantal (miniature) IPSC/EPSC's decay (<30% effect) --
//      phasic decay is dominated by receptor gating/desensitization, not
//      transporter clearance. But STIMULUS-EVOKED IPSCs (the relevant case
//      for a spike-driven network) ARE measurably prolonged by GAT
//      inhibition (Overstreet & Westbrook-type studies). So the real,
//      observable, literature-supported effect of reuptake block IS a
//      bounded extension of the existing phasic decay time constant --
///     which this engine already models via ReceptorKinetics::k*TauDecayMs.
//   2. The genuinely "ambient/tonic" reuptake-block effect (extrasynaptic
//      NMDA tonic current, GABA-B tonic activation) builds up over a
//      10-20 MINUTE timescale in the literature (Overstreet, Jensen et al
//      GAT1-knockout tonic-current studies) -- this engine's dose-eval runs
//      simulate ~400-500ms of biological time (see main.cpp sim_time),
//      roughly 3000x too short for that slow process to develop at all.
//      Building a cleft-concentration pool calibrated to the real ambient
//      timescale would show ~0% effect in every run; building one
//      recalibrated to an arbitrary faster timescale would not be a real
//      literature value, just a knob tuned to "make the demo move."
//
// So Phase 3a's mechanism is: a transporter-blocking drug extends the
// existing dual-exponential decay time constant of the receptor(s) sharing
// that transmitter's release site, by a dose-dependent (Hill) fraction,
// bounded to a literature-realistic maximum fold-change -- reusing exactly
// the same Hill-occupancy math already validated for Phase 2's Block/
// Potentiate mechanisms, applied to a kinetic time constant instead of a
// conductance ceiling.
//
// EAAT block -> extends AMPA's AND NMDA's tauDecay (both driven by the same
//               glutamatergic release site).
// GAT1 block -> extends GABA-A's AND GABA-B's tauDecay (same GABAergic
//               release site).
// SERT/DAT/NET -> serotonin, dopamine, and norepinephrine have NO receptor
//               current in this engine at all (that's Phase 3c's
//               neuromodulator gain system: D1/D2/5-HT1A/5-HT2A modulating
///              gNa/K+/release probability, not built yet). Their kinetics
//               are modeled here with the same literature rigor -- so
//               cocaine/SSRI/reboxetine's transporter pharmacology is real
//               and reportable ("Effective clearance extended Nx") -- but
//               deliberately produce NO network/current effect until Phase
//               3c exists. Documented rather than silently faked, same
//               discipline as the topiramate/kainate flag in Phase 2.

namespace spp::synapse {

enum class TransporterBlockType {
    None = 0,
    Competitive = 1,     // blocker competes with substrate for the same site (raises apparent Km)
    NonCompetitive = 2    // blocker acts independent of substrate concentration (lowers Vmax)
};

// A transporter-blocking drug's effect on ONE transporter.
// doseUm/ki/hill drive a standard Hill occupancy fraction (same formula as
// Phase 1/2's channel Block mechanism); maxExtensionFold is the ceiling on
// how much the decay time constant can be stretched at full occupancy --
// literature-bounded per transporter (see .cpp), not an unbounded knob.
struct TransporterDrugEffect {
    TransporterBlockType mechanism = TransporterBlockType::None;
    float kiUm = 0.0f;             // uM, drug's inhibition constant at this transporter
    float hill = 1.0f;
    float maxExtensionFold = 1.0f; // ceiling fold-change in clearance tau at saturating dose
};

// Literature-sourced baseline Km for each transporter (uM) -- used only for
// documentation/report context (e.g. "GAT1 Km = 4-11 uM, tiagabine Ki =
// 700 nM" in a drug's printed profile), not as a live simulation input,
// since Km alone doesn't determine an ambient clearance tau without a
// system-specific Vmax that isn't portable across assay preparations (see
// header note above). Full citations in ReuptakeTransporter.cpp.
namespace TransporterLibrary {
    extern const float kEaatKmUm;  // glutamate transporter (EAAT1/2)
    extern const float kGat1KmUm;  // GABA transporter (GAT1)
    extern const float kSertKmUm;  // serotonin transporter -- inert until Phase 3c
    extern const float kDatKmUm;   // dopamine transporter  -- inert until Phase 3c
    extern const float kNetKmUm;   // norepinephrine transporter -- inert until Phase 3c
}

// Fractional transporter occupancy (0..1) at a given dose -- standard Hill
// equation, mechanism-agnostic (see header note: at the bounded-extension
// scale this engine operates at, competitive vs non-competitive doesn't
// change the shape of the dose-response, only report labeling).
float transporterOccupancy(float doseUm, const TransporterDrugEffect& drug);

// Effective decay time constant (ms) for a receptor sharing this
// transporter's transmitter, after applying a dose of a transporter-
// blocking drug. Returns tauBaselineMs unchanged at dose=0 or
// mechanism=None (exact baseline preservation by construction).
float effectiveTauDecayMs(float tauBaselineMs, float doseUm, const TransporterDrugEffect& drug);

} // namespace spp::synapse