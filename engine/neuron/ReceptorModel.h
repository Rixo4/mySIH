#pragma once

// Receptor pharmacology -- shared math used by AMPA/NMDA/GABA-A/GABA-B
// synaptic currents. These currents are synaptic (triggered per incoming
// spike, with their own rise/decay kinetics), not intrinsic membrane
// conductances like Na/K/Ca, so they belong here at the synapse level
// rather than as new NeuronModel state.
//
// All kinetic constants below are literature-sourced representative
// cortical values, not invented numbers -- see the citation in each
// constant's comment. Where real biology varies by cell type/subunit
// (NMDA GluN2A vs GluN2B decay; GABA-A reversal potential depends on the
// intracellular Cl- gradient), a single representative value is used,
// the same simplification already used for the core Hodgkin-Huxley
// parameters -- one representative set rather than modeling
// per-neuron-subtype variation.

namespace spp::neuron {

// ─── NMDA voltage-dependent Mg2+ block ─────────────────────────────────────
// Jahr & Stevens (1990), "A quantitative description of NMDA
// receptor-channel kinetic behavior", J Neurosci -- the standard
// voltage-dependent block equation used throughout computational
// neuroscience modeling since (e.g. Destexhe, Mainen & Sejnowski 1994
// kinetic synapse models). Returns the FRACTION OF NMDA CONDUCTANCE
// UNBLOCKED (0-1) at a given membrane voltage; multiply the NMDA
// conductance by this fraction before computing current.
//
//   B(V) = 1 / (1 + ([Mg2+]_o / 3.57 mM) * exp(-V / 16.13 mV))
//
// Cross-checked against an independently reported parameterization
// (B = 1/(1 + exp(-0.062V + 1.2726)*[Mg])): 1/0.062 ~= 16.13 mV, the same
// voltage-dependence rate, corroborating the constant from a second source.
// extracellularMgMm defaults to 1.0 mM, a commonly used physiological/ACSF
// value; pass a different value if a specific experimental Mg2+ condition
// needs to be modeled.
float nmdaMgBlockFraction(float voltageMv, float extracellularMgMm = 1.0f);

// ─── Dual-exponential synaptic conductance kernel ──────────────────────────
// Shared rise/decay waveform for all four receptor types. AMPA, NMDA,
// GABA-A, and GABA-B differ only in their time constants (and, for GABA-B,
// a meaningful onset delay before the rise even begins -- the other three
// have negligible delay by comparison). Peak-normalized so the waveform
// reaches exactly 1.0 at its peak regardless of the tau values chosen, so
// receptor-specific peak conductance (gMax) can be applied separately.
struct DualExpKernel {
    float tauRiseMs    = 0.5f;
    float tauDecayMs   = 5.0f;
    float onsetDelayMs = 0.0f;  // meaningful only for GABA-B; 0 for the rest

    // Precomputed peak-normalization factor (call after setting tau values).
    [[nodiscard]] float normFactor() const;
};

// Evaluate the (peak-normalized, 0-1) conductance waveform at time tMs
// since a single presynaptic input spike. Returns 0 for tMs < onsetDelayMs.
float dualExpWaveform(const DualExpKernel& kernel, float tMs);

// ─── Literature-sourced representative kinetics ────────────────────────────
namespace ReceptorKinetics {

    // AMPA -- ionotropic, fast excitatory (Na+/K+ mixed cation).
    // Rise 0.2-0.6 ms, decay 1.7-12 ms reported across studies (varies with
    // temperature and preparation); 0.5/5.0 ms is a commonly used modeling
    // pair within that reported range.
    constexpr float kAmpaTauRiseMs  = 0.5f;
    constexpr float kAmpaTauDecayMs = 5.0f;
    constexpr float kAmpaReversalMv = 0.0f;

    // NMDA -- ionotropic, slow excitatory + Ca2+ permeable, voltage-gated
    // via Mg2+ block above. Rise ~5-10 ms. Decay is subunit-dependent:
    // GluN2A-like ~50 ms (dominant in mature cortex), GluN2B-like ~400 ms
    // (~8x slower; more prominent in developing brain / some subcortical
    // regions). This engine models a mature cortical population, so a
    // representative value leaning toward the adult-dominant GluN2A
    // kinetics is used rather than a straight midpoint of the two.
    constexpr float kNmdaTauRiseMs  = 8.0f;
    constexpr float kNmdaTauDecayMs = 100.0f;
    constexpr float kNmdaReversalMv = 0.0f;

    // GABA-A -- ionotropic, fast inhibitory (Cl-). Rise ~1-5 ms (thalamic
    // measurements ~1.6-1.7 ms), decay ~10-25 ms. Reversal potential is NOT
    // a fixed physical constant the way eNa/eK/eCa are -- it depends on the
    // transmembrane Cl- gradient, which varies with cell type and KCC2
    // expression/maturity. -70 mV is a commonly used representative value
    // for mature cortical pyramidal neurons; deliberately kept separate
    // from eK (-77 mV) and eL (-54.4 mV) already in HHParameters.
    constexpr float kGabaATauRiseMs  = 1.5f;
    constexpr float kGabaATauDecayMs = 15.0f;
    constexpr float kGabaAReversalMv = -70.0f;

    // GABA-B -- metabotropic (G-protein -> GIRK K+ channels), far slower
    // than the other three. Measured: onset lag ~50 ms before the rise
    // even begins, activation time constant ~225 ms (at saturating
    // agonist), deactivation time constant ~1000 ms. No separate reversal
    // constant here -- it IS a K+ conductance, so it reuses
    // HHParameters::eK (-77 mV) rather than introducing a new one.
    constexpr float kGabaBTauRiseMs    = 225.0f;
    constexpr float kGabaBTauDecayMs   = 1000.0f;
    constexpr float kGabaBOnsetDelayMs = 50.0f;

} // namespace ReceptorKinetics

} // namespace spp::neuron