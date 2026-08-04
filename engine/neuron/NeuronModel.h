#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace spp::neuron {

// ─── Baseline dynamics ─────────────────────────────────────────────────────
// The "fires once then permanently silent" bug (see gCa comment below) was
// mistakenly chased for days as an AHP/drive-current tuning problem before
// its real cause (calcium depolarization block) was found. kCa=0.0005 and
// gAHP were both raised well above realistic values while compensating for
// the wrong thing. kCa is left as-is (calcium no longer over-accumulates
// now that gCa is fixed, verified caCa settles ~0.08, well inside range).
// gAHP has been brought back down -- see its own comment below.
//
// Ca-activated AHP is gAHP*caCa*(v-eK): every spike lets calcium in, caCa
// accumulates, and this term adds an outward (braking) current proportional
// to it. Blocking calcium channels removes this brake along with the
// calcium current itself, so gAHP directly controls how much a Ca-channel
// blocker looks excitatory in this model -- it needs to be realistic, not
// just "whatever stops the network from collapsing" (that job now belongs
// to gCa).
struct HHParameters {
    float cm = 1.0f;             // uF/cm^2
    float gNa = 120.0f;          // mS/cm^2
    float gK = 36.0f;            // mS/cm^2
    // ROOT-CAUSE FIX (baseline collapse / "fires once then silent"):
    // was 8.0, which was ~7% of gNa and larger than 1.5x gL. Because
    // iCa = gCa*s^2*(v - eCa) with eCa = +120 mV, this current is ALWAYS
    // inward (depolarizing), and the s gate has NO inactivation variable --
    // once v crosses ~-20 mV, s activates and stays on. At gCa=8 that
    // produced ~-42 uA/cm^2 of sustained inward current, pinning the cell
    // at a depolarized plateau near -46 mV where Na inactivation collapses
    // (h ~ 0.17). That is textbook DEPOLARIZATION BLOCK: the neuron fires
    // one spike, enters the plateau, and can never spike again -- exactly
    // the observed bug. Confirmed numerically: with gCa=0 the identical
    // neuron fires tonically at every drive level; with gCa=8 it fires
    // once and stops at every drive level from 2.5 to 120 uA/cm^2. No
    // amount of external_current tuning can fix this, which is why raising
    // the drive repeatedly failed.
    // 0.5 keeps calcium as a genuine but subordinate modulatory current
    // (biologically appropriate -- high-threshold Ca conductance is small
    // relative to Na/K in real neurons) and restores tonic firing.
    // Second benefit: at gCa=8 the calcium variable caCa saturated at its
    // clamp ceiling of 1.0, so the Ca-activated AHP was permanently pegged
    // and Ca-channel blockers could produce no graded effect. At gCa=0.5
    // caCa settles ~0.08, well inside its range, so AHP is once again a
    // real dose-sensitive brake.
    float gCa = 0.5f;           // mS/cm^2
    float gL = 0.3f;             // mS/cm^2
    float eNa = 50.0f;           // mV
    float eK = -77.0f;           // mV
    float eCa = 120.0f;          // mV
    float eL = -54.4f;           // mV
    // Was 5.0, tuned during the misdiagnosed collapse-bug era. Numerically
    // characterised (single neuron, ~98% Ca block): gAHP=5.0 -> firing rate
    // +560% on Ca block; gAHP=3.0 -> +371%; gAHP=2.0 -> +175%; gAHP=1.0 ->
    // +18%; gAHP=0.5 -> +3%. Real L-type-specific Ca-blockers (verapamil,
    // diltiazem, nimodipine) are clinically safe, not seizurogenic, so a
    // ~500%+ single-neuron swing was never plausible. 1.0 was picked to
    // target that modest single-neuron estimate.
    //
    // GAP 1.2 RECALIBRATION -- COMMITTED (PRECISION_GAP_CLOSURE_PLAN.md /
    // PHASE1_BIOLOGY_BRIEFING.md). The comment above warned network
    // amplification might differ from the single-neuron estimate and told
    // whoever touched this next to re-measure -- that re-measurement found
    // the network-level swing (+42-48% at ~83-84% Ca-channel occupancy) was
    // far larger than the single-neuron target this value was tuned for,
    // and large enough to misclassify three real, clinically safe Ca-
    // blockers (nimodipine, verapamil, diltiazem) as NOT RECOMMENDED / HIGH
    // RISK. gAHP was swept down (1.0 -> 0.5 -> 0.25 -> 0.2) and re-measured
    // against all three each time; at 0.2 (runs=20, to rule out noise --
    // several intermediate readings were noise, not signal), all three
    // clear the engine's internal excitatory-risk gate with real margin
    // (nimodipine +9.9%, verapamil +8.9%, diltiazem +9.3%, vs. the original
    // +42-48%).
    //
    // Regression-checked against 3 already-audited non-Ca-blocker drugs
    // before committing (gAHP is a SHARED parameter -- it sets baseline
    // spike-frequency adaptation for every neuron in every config, not just
    // Ca-blockers), using clean low/moderate-occupancy dose ranges rather
    // than each drug's coarse 5x-IC50 template (that template has its own
    // pre-existing sharp-collapse-threshold artifact, unrelated to gAHP,
    // that produced a false "regression" reading on lidocaine on the first
    // attempt -- corrected by re-testing on a fair, fine-grained range):
    //   Lidocaine (Na-channel block):  clean, no regression -- R^2/magnitude
    //     essentially unchanged, modest noise increase.
    //   TEA (K-channel block):        clean, no regression -- onset of
    //     Hyperexcitable moved one dose-step earlier (biologically sensible:
    //     weaker AHP brake, slightly more convulsant-prone), magnitude/noise
    //     if anything slightly better.
    //   Diazepam (GABA-A potentiation): NOT clean -- max effect rose from
    //     19.3% to 29.8% (+54% relative, same safe/suppressive direction),
    //     Rate Variability roughly doubled, Stability/Evidence Quality
    //     downgraded MEDIUM -> LOW. Plausible mechanism: this is a SYNAPTIC
    //     receptor pathway, not intrinsic channel block, and a weaker AHP
    //     brake leaves less counter-excitation to offset GABA-A's own
    //     suppression. NMDA and GABA-B mechanisms were not tested.
    //
    // DECISION (deliberate tradeoff, not an oversight): accept this. Three
    // confidently-wrong verdicts on decades-established, clinically safe
    // drugs is a worse error than one receptor-pathway drug's magnitude
    // shifting by an amount we can't yet independently verify either way
    // (no real validation data exists for diazepam's true suppression
    // magnitude any more than for the Ca-blockers). IMPORTANT CAVEAT for
    // anyone citing this: gAHP=0.2 is CALIBRATED to match three known-safe
    // drugs' expected direction, not independently VALIDATED against real
    // experimental/clinical data -- that distinction matters, see
    // PRECISION_GAP_CLOSURE_PLAN.md Tier 3. Do not describe this as "the
    // engine now correctly models Ca-channel blockers" -- describe it as
    // "recalibrated to match known real-world safety direction for the
    // three tested Ca-blockers; not yet independently validated, and the
    // diazepam/GABA-A number should be treated as provisional pending real
    // validation data for that mechanism too." If NMDA or GABA-B mechanisms
    // are later found to have a similar undesirable shift, that would be
    // grounds to revisit this value again.
    float gAHP  = 0.2f;          // Ca-activated K conductance mS/cm² -- COMMITTED (Gap 1.2), see above
    float tauCa = 80.0f;         // Ca decay time constant ms
    float kCa  = 0.0005f;        // calcium accumulation rate per unit Ca current
    // Set to 0: this was added while chasing the collapse bug from the wrong
    // direction. It acts as an extra leak conductance with reversal eK
    // (~+3 uA/cm^2 outward at rest, on top of gL=0.3), which pushes AGAINST
    // sustained firing -- the opposite of what was intended. With the real
    // cause (gCa depolarization block) fixed, it is not needed. Field is
    // retained so the CPU/GPU plumbing stays in sync and it can be re-enabled
    // deliberately if a calcium-independent AHP floor is ever wanted.
    float gAHPFloor = 0.0f;      // calcium-independent baseline AHP component
    float restingVoltage = -65.0f;

    // ─── Synaptic receptor currents -- rebuilt properly, one receptor at a
    // time (GABA-A, then NMDA, now GABA-B). Unlike the earlier full-rewrite
    // attempt that wired all four receptors simultaneously and collapsed
    // the network in a way that took most of a session to diagnose, this
    // build order is GABA-A -> NMDA -> GABA-B -> AMPA, each one verified
    // healthy against a real baseline before the next is added.
    // Value matches ReceptorKinetics::kGabaAReversalMv in ReceptorModel.h,
    // deliberately separate from eK/eL since chloride reversal depends on
    // the cell's own Cl- gradient, not a fixed physical constant.
    float eGABAa = -70.0f;

    // NMDA reversal potential -- mixed Na+/K+/Ca2+ cation channel, same
    // ~0mV reversal as AMPA (both are non-selective cation channels with a
    // reversal near 0mV, unlike Na/K/Ca's ion-specific Nernst potentials).
    // Value matches ReceptorKinetics::kNmdaReversalMv in ReceptorModel.h.
    // eAMPA still lives in SimulationConfig (not here) since AMPA hasn't
    // moved to the true-conductance model yet -- see the build order above.
    float eNMDA = 0.0f;

    // GABA-B has no reversal field of its own -- per ReceptorModel.h's
    // comment, it's a real metabotropic-gated K+ conductance (G-protein ->
    // GIRK channels), so it reuses eK (-77mV) directly rather than
    // introducing a separate constant the way GABA-A's Cl- reversal needed
    // its own eGABAa. gGABAbEff * (v - eK) is computed in computeDerivatives
    // exactly like the other three receptor currents.

    // AMPA reversal potential -- last receptor to move here, now that
    // AMPA has its own true-conductance build step (last in the GABA-A ->
    // NMDA -> GABA-B -> AMPA order). Moved from SimulationConfig (where it
    // lived as a reweighting-model parameter) to sit alongside eGABAa/eNMDA
    // as the single source of truth, matching ReceptorKinetics::
    // kAmpaReversalMv in ReceptorModel.h. Same ~0mV reversal as NMDA (both
    // are non-selective cation channels).
    float eAMPA = 0.0f;
};

struct HHState {
    float v = -65.0f;
    float m = 0.05f;
    float h = 0.6f;
    float n = 0.32f;
    float s = 0.05f;
    float caCa = 0.0f; // intracellular calcium concentration
};

struct HHDerivatives {
    float dv = 0.0f;
    float dm = 0.0f;
    float dh = 0.0f;
    float dn = 0.0f;
    float ds = 0.0f;
    float dcaCa = 0.0f; // derivative of intracellular calcium concentration
};

class NeuronPopulation {
public:
    explicit NeuronPopulation(std::size_t neuronCount);

    void initialize(float baseExternalCurrent, float externalCurrentStd, float baseNoiseStd, std::uint32_t seed);

    [[nodiscard]] std::size_t size() const { return v.size(); }

    HHParameters params;

    std::vector<float> v;
    std::vector<float> m;
    std::vector<float> h;
    std::vector<float> n;
    std::vector<float> s;

    std::vector<float> gNa;
    std::vector<float> gK;
    std::vector<float> gCa;
    std::vector<float> caCa;
    std::vector<float> gL;

    std::vector<float> threshold;
    std::vector<float> noiseStd;
    std::vector<float> extCurrent;
    std::vector<float> lastSpikeTime;
    std::vector<std::uint8_t> neuronType; // 1=Excitatory, 0=Inhibitory
};

float alphaM(float vMv);
float betaM(float vMv);
float alphaH(float vMv);
float betaH(float vMv);
float alphaN(float vMv);
float betaN(float vMv);
float sInf(float vMv);
float tauS(float vMv);

HHState steadyStateAtVoltage(float vMv);

// Per-neuron effective synaptic conductances for one timestep, already
// peak-scaled (gMax * raw 0..1 conductance from Synapse) by the caller --
// mirrors how gNaEff/gKEff/gCaEff arrive already drug-modulated. All four
// fields (gAMPAEff, gNMDAEff, gGABAaEff, gGABAbEff) are wired to real
// currents now -- see eGABAa/eNMDA/eAMPA/GABA-B comments above. All four
// receptors in the GABA-A -> NMDA -> GABA-B -> AMPA build order are done.
struct SynapticConductances {
    float gAMPAEff = 0.0f;
    float gNMDAEff = 0.0f;
    float gGABAaEff = 0.0f;
    float gGABAbEff = 0.0f;
};

HHDerivatives computeDerivatives(
    const HHState& state,
    float iTotal,
    float gNaEff,
    float gKEff,
    float gCaEff,
    const HHParameters& params,
    const SynapticConductances& synaptic = {}
);

void rk4Step(
    HHState& state,
    float dtMs,
    float iTotal,
    float gNaEff,
    float gKEff,
    float gCaEff,
    const HHParameters& params,
    const SynapticConductances& synaptic = {}
);

bool isFiniteState(const HHState& state);
void clampState(HHState& state);

} // namespace spp::neuron