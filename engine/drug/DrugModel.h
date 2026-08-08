#pragma once

#include <cstddef>
#include <vector>

#include "ReceptorDrugProfile.h"
#include "../neuron/ReceptorModel.h"

namespace spp::drug {

// Output of applying a ReceptorDrugProfile at a given dose -- see
// ReceptorDrugProfile.h for the mechanism definitions. Each field is a
// ready-to-multiply-or-add modifier for the corresponding receptor's
// synaptic conductance, computed once per step (not per neuron -- receptor
// drug profiles are global-only for now, no per-neuron heterogeneity, since
// none of the Phase 2 validation drugs need it; see DrugModel.h comment on
// applyReceptors for why this is a deliberate scope decision, not an
// oversight).
struct ReceptorConductanceModifiers {
    // AMPA/NMDA: multiplicative residual in [0,1], meaningful only when that
    // receptor's mechanism is Block. 1.0 (no-op) otherwise, including for
    // Potentiate/Agonist tags on AMPA/NMDA -- those combinations aren't part
    // of the Phase 2 validation set (see ReceptorDrugProfile.h), so they're
    // deliberately treated as inert rather than guessed at.
    float ampaResidual = 1.0f;
    float nmdaResidual = 1.0f;

    // GABA-A: multiplicative factor >= 1.0, meaningful only when GABA-A's
    // mechanism is Potentiate. 1.0 (no-op) otherwise.
    float gabaAPotentiation = 1.0f;

    // GABA-B: activation fraction in [0,1], meaningful only when GABA-B's
    // mechanism is Agonist. 0.0 (no-op) otherwise. This is ADDED to the
    // synaptic GABA-B pathway by the caller (scaled by its own peak-drive
    // constant), not multiplied into it -- see ReceptorDrugProfile.h's
    // Agonist description for why.
    float gabaBAgonistActivation = 0.0f;
};

// Phase 3a output: effective decay time constants (ms) for the four
// receptors after applying any transporter-block mechanisms, plus report-
// only occupancy/effective-tau numbers for the three transmitters with no
// receptor current in this engine yet. Defaults are exactly the compile-time
// ReceptorKinetics baseline constants, so a profile with no transporter
// action produces bit-identical kinetics to pre-Phase-3a behavior.
struct ReceptorKineticsModifiers {
    float ampaTauDecayMs  = spp::neuron::ReceptorKinetics::kAmpaTauDecayMs;
    float nmdaTauDecayMs  = spp::neuron::ReceptorKinetics::kNmdaTauDecayMs;
    float gabaATauDecayMs = spp::neuron::ReceptorKinetics::kGabaATauDecayMs;
    float gabaBTauDecayMs = spp::neuron::ReceptorKinetics::kGabaBTauDecayMs;

    // Report-only (no receptor to feed yet -- see TransporterAction comment
    // in ReceptorDrugProfile.h).
    float sertOccupancy = 0.0f;
    float sertEffectiveTauFoldChange = 1.0f;
    float datOccupancy = 0.0f;
    float datEffectiveTauFoldChange = 1.0f;
    float netOccupancy = 0.0f;
    float netEffectiveTauFoldChange = 1.0f;
};

struct ChannelDrugProfile {
    float ic50Na = 1000.0f;
    float ic50K = 1000.0f;
    float ic50Ca = 1000.0f;

    float hillNa = 1.0f;
    float hillK = 1.0f;
    float hillCa = 1.0f;
};

struct ConductanceResult {
    float gNaEff = 0.0f;
    float gKEff = 0.0f;
    float gCaEff = 0.0f;

    float blockNa = 0.0f;
    float blockK = 0.0f;
    float blockCa = 0.0f;
};

class DrugModel {
public:
    DrugModel();

    void setGlobalDose(float dose);
    [[nodiscard]] float globalDose() const { return globalDose_; }

    void setGlobalProfile(const ChannelDrugProfile& profile);
    [[nodiscard]] const ChannelDrugProfile& globalProfile() const { return globalProfile_; }

    // Receptor pharmacology (PHASE2_PLAN.md §3/§4, step 4). Global-only, no
    // per-neuron override -- unlike ChannelDrugProfile, there is no
    // enablePerNeuronProfiles-style storage for receptor profiles yet. None
    // of the six Phase 2 validation drugs need per-neuron receptor
    // heterogeneity, so that machinery is deliberately not built until an
    // actual use case needs it (same "don't build ahead of the plan"
    // discipline the K-block dead code deferral followed).
    void setGlobalReceptorProfile(const ReceptorDrugProfile& profile);
    [[nodiscard]] const ReceptorDrugProfile& globalReceptorProfile() const { return globalReceptorProfile_; }

    // Computes this step's receptor conductance modifiers from the global
    // receptor profile and the global dose (scaled by doseScale, same onset-
    // ramp scale already used for applyWithDoseScale -- callers should pass
    // the same doseScale to both so channel and receptor pharmacology share
    // one onset timeline).
    [[nodiscard]] ReceptorConductanceModifiers applyReceptors(float doseScale = 1.0f) const;

    // Stateless version of the same computation, for callers that don't have
    // (or don't want) a DrugModel instance carrying global dose state --
    // e.g. BatchedSimulationEngine, which already computes per-block Hill
    // fractions directly via the static hillBlock rather than through an
    // instance (see its ChannelDrugProfile handling for the same pattern).
    // applyReceptors(doseScale) is implemented in terms of this.
    static ReceptorConductanceModifiers computeReceptorModifiers(const ReceptorDrugProfile& profile, float dose);

    // Phase 3a: effective receptor decay time constants after transporter
    // block, plus report-only SERT/DAT/NET numbers. Stateless, same pattern
    // as computeReceptorModifiers -- see ReceptorKineticsModifiers comment.
    static ReceptorKineticsModifiers computeReceptorKineticsModifiers(const ReceptorDrugProfile& profile, float dose);

    // Phase 3c: neuromodulator gain modifiers (D1/D2/5-HT1A/5-HT2A), see
    // engine/synapse/NeuromodulatorSystem.h. Stateless, same pattern as
    // computeReceptorKineticsModifiers -- thin pass-through to the synapse-
    // level math, kept here so callers (BatchedSimulationEngine) go through
    // the same DrugModel entry point as every other mechanism family rather
    // than reaching into engine/synapse directly.
    // Tier 2.1: currentTimeMs threads through to 5-HT1A's autoreceptor
    // desensitization term only -- see NeuromodulatorSystem.h's comment.
    // Default 0.0f preserves this function's prior time-independent
    // behavior for any caller that doesn't pass it explicitly.
    static spp::synapse::NeuromodulatorGainModifiers computeNeuromodulatorGainModifiers(
        const ReceptorDrugProfile& profile, float dose, float currentTimeMs = 0.0f);

    // Phase 3c retrofit: SERT/DAT reuptake block amplifies the effective
    // dose the dopamine (D1/D2) or serotonin (5-HT1A/5-HT2A) receptors see
    // -- see ReuptakeTransporter.h's amplifiedDoseUm comment. These are the
    // SAME two amplified doses computeNeuromodulatorGainModifiers computes
    // internally; exposed here so callers that need to DISPLAY an
    // occupancy/gain number (main.cpp's report, buildDoseObservation) use
    // the identical effective dose the simulation actually ran with,
    // instead of silently recomputing occupancy from the raw dose and
    // drifting out of sync with what's really driving the network (the
    // exact class of bug found and fixed this session for the report's
    // "Max Effect" field).
    static float amplifiedDoseForDopamine(const ReceptorDrugProfile& profile, float dose);
    static float amplifiedDoseForSerotonin(const ReceptorDrugProfile& profile, float dose);
    // Tier 2.2: NET reuptake block amplifies the effective dose the alpha-2
    // receptor sees -- same pattern as the two above, using profile.net.
    static float amplifiedDoseForNorepinephrine(const ReceptorDrugProfile& profile, float dose);

    void enablePerNeuronProfiles(std::size_t neuronCount);
    [[nodiscard]] bool hasPerNeuronProfiles() const { return perNeuronEnabled_; }

    void setNeuronProfile(std::size_t neuronId, const ChannelDrugProfile& profile);
    void setNeuronDose(std::size_t neuronId, float dose);

    ConductanceResult apply(std::size_t neuronId, float gNa, float gK, float gCa) const;
    ConductanceResult applyWithDoseScale(
        std::size_t neuronId,
        float gNa,
        float gK,
        float gCa,
        float doseScale
    ) const;

    static float hillBlock(float dose, float ic50, float hill);

    // Minimum fraction of a channel's original conductance retained even at
    // 100% Hill-block occupancy (channels are modeled as never fully
    // closing). Na and K share one floor; Ca has its own, lower floor.
    //
    // IMPORTANT -- these three constants, and the conductanceFloor() formula
    // right below, are duplicated by hand in engine/cuda/NeuronUpdate.cu
    // (search that file for "0.05f" / "0.02f" near hillBlockDevice). The
    // CUDA kernel runs in its own translation unit compiled by nvcc and
    // cannot call this C++ function directly, so it re-implements the same
    // math independently. If you change any of these three values or the
    // conductanceFloor() formula, you MUST make the matching edit in
    // NeuronUpdate.cu -- otherwise GPU-backend runs (the default backend)
    // will silently keep the old behavior while CPU runs pick up the change.
    // This exact silent divergence cost real debugging time during the
    // Gap 1.2 Ca-blocker calibration investigation (see
    // PRECISION_GAP_CLOSURE_PLAN.md) -- a host-only diagnostic patch
    // produced bit-for-bit identical GPU output across two "confirmed
    // rebuild" rounds before the independent device-side copy was found.
    static constexpr float kNaConductanceFloor = 0.05f;
    static constexpr float kKConductanceFloor  = 0.05f;
    static constexpr float kCaConductanceFloor = 0.02f;

    // Shared floor formula: max(floorFraction * safeG, safeG * residual)
    // where residual = max(0, 1 - blockFraction). Extracted here so every
    // host-side caller -- this class's own apply()/applyWithDoseScale() AND
    // BatchedSimulationEngine.cpp, which previously carried its own
    // hand-typed copy of this exact formula -- are guaranteed to compute it
    // identically instead of three independently-maintained copies drifting
    // apart on the host side too. See the constants comment above for the
    // one duplication (CUDA) that can't be closed this way.
    static float conductanceFloor(float safeG, float blockFraction, float floorFraction);

    // Multiplicative potentiation factor, >= 1.0, saturating toward
    // maxPotentiationFactor as dose rises. Reuses hillBlock's sigmoid shape
    // as the underlying occupancy fraction (0..1) -- see PHASE2_PLAN.md §3.
    static float hillPotentiationFactor(float dose, float ec50, float hill, float maxPotentiationFactor);

    // Activation fraction in [0,1] for a direct agonist, saturating toward 1
    // at high dose. Same underlying sigmoid as hillBlock, different
    // interpretation (occupancy/activation instead of block).
    static float hillAgonistActivation(float dose, float ec50, float hill);

private:
    float globalDose_;
    ChannelDrugProfile globalProfile_;
    ReceptorDrugProfile globalReceptorProfile_;

    bool perNeuronEnabled_;
    std::vector<ChannelDrugProfile> neuronProfiles_;
    std::vector<float> neuronDoses_;
};

} // namespace spp::drug