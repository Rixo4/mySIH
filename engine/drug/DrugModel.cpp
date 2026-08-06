#include "DrugModel.h"
#include "../synapse/ReuptakeTransporter.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace spp::drug {

namespace {

constexpr float kTiny = 1.0e-12f;

float sanitizePositive(float value, float fallback) {
    if (!std::isfinite(value) || value <= 0.0f) {
        return fallback;
    }
    return value;
}

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

// Shared by all three receptor-drug mechanisms below: only Block is a
// fractional reduction of the raw conductance. Potentiate/Agonist ignore
// this and are handled by their own DrugModel:: static methods. Returns a
// multiplicative residual in [0,1] -- 1.0 (no-op) for any mechanism other
// than Block, including None.
float receptorResidualForBlock(const ReceptorAction& action, float dose) {
    if (action.mechanism != ReceptorMechanism::Block) {
        return 1.0f;
    }
    const float blockFraction = clamp01(DrugModel::hillBlock(dose, action.ec50, action.hill));
    return std::max(0.0f, 1.0f - blockFraction);
}

float sanitizeConductance(float value) {
    if (!std::isfinite(value) || value <= 0.0f) {
        return 0.0f;
    }
    return value;
}

} // namespace

DrugModel::DrugModel()
    : globalDose_(0.0f),
      globalProfile_(),
      perNeuronEnabled_(false) {}

void DrugModel::setGlobalDose(float dose) {
    globalDose_ = std::max(0.0f, dose);
}

void DrugModel::setGlobalProfile(const ChannelDrugProfile& profile) {
    globalProfile_ = profile;
}

void DrugModel::setGlobalReceptorProfile(const ReceptorDrugProfile& profile) {
    globalReceptorProfile_ = profile;
}

void DrugModel::enablePerNeuronProfiles(std::size_t neuronCount) {
    perNeuronEnabled_ = true;
    neuronProfiles_.assign(neuronCount, globalProfile_);
    neuronDoses_.assign(neuronCount, globalDose_);
}

void DrugModel::setNeuronProfile(std::size_t neuronId, const ChannelDrugProfile& profile) {
    if (!perNeuronEnabled_ || neuronId >= neuronProfiles_.size()) {
        throw std::out_of_range("Neuron profile index out of range or per-neuron mode not enabled.");
    }
    neuronProfiles_[neuronId] = profile;
}

void DrugModel::setNeuronDose(std::size_t neuronId, float dose) {
    if (!perNeuronEnabled_ || neuronId >= neuronDoses_.size()) {
        throw std::out_of_range("Neuron dose index out of range or per-neuron mode not enabled.");
    }
    neuronDoses_[neuronId] = std::max(0.0f, dose);
}

ConductanceResult DrugModel::apply(std::size_t neuronId, float gNa, float gK, float gCa) const {
    return applyWithDoseScale(neuronId, gNa, gK, gCa, 1.0f);
}

ConductanceResult DrugModel::applyWithDoseScale(
    std::size_t neuronId,
    float gNa,
    float gK,
    float gCa,
    float doseScale
) const {
    const ChannelDrugProfile* profile = &globalProfile_;
    float dose = globalDose_;

    if (perNeuronEnabled_) {
        if (neuronId >= neuronProfiles_.size() || neuronId >= neuronDoses_.size()) {
            throw std::out_of_range("Neuron drug lookup out of range.");
        }
        profile = &neuronProfiles_[neuronId];
        dose = neuronDoses_[neuronId];
    }

    const float safeDoseScale = std::clamp(doseScale, 0.0f, 2.0f);
    dose *= safeDoseScale;

    float blockNa = hillBlock(dose, profile->ic50Na, profile->hillNa);
    float blockK = hillBlock(dose, profile->ic50K, profile->hillK);
    float blockCa = hillBlock(dose, profile->ic50Ca, profile->hillCa);

    blockNa = clamp01(blockNa);
    blockK = clamp01(blockK);
    blockCa = clamp01(blockCa);

    const float safeGNa = sanitizeConductance(gNa);
    const float safeGK = sanitizeConductance(gK);
    const float safeGCa = sanitizeConductance(gCa);

    ConductanceResult result;
    result.blockNa = blockNa;
    result.blockK = blockK;
    result.blockCa = blockCa;
    result.gNaEff = conductanceFloor(safeGNa, blockNa, kNaConductanceFloor);
    result.gKEff  = conductanceFloor(safeGK,  blockK,  kKConductanceFloor);
    result.gCaEff = conductanceFloor(safeGCa, blockCa, kCaConductanceFloor);
    return result;
}

float DrugModel::conductanceFloor(float safeG, float blockFraction, float floorFraction) {
    const float residual = std::max(0.0f, 1.0f - blockFraction);
    return std::max(floorFraction * safeG, safeG * residual);
}

float DrugModel::hillBlock(float dose, float ic50, float hill) {
    const float safeDose = std::max(0.0f, dose);
    const float safeIc50 = sanitizePositive(ic50, 1.0f);
    const float safeHill = std::clamp(sanitizePositive(hill, 1.0f), 1.0f, 6.0f);

    if (safeDose <= 0.0f) {
        return 0.0f;
    }

    const float dosePow = std::pow(safeDose, safeHill);
    const float ic50Pow = std::pow(safeIc50, safeHill);

    if (!std::isfinite(dosePow) || !std::isfinite(ic50Pow)) {
        const float ratio = safeDose / (safeIc50 + kTiny);
        return clamp01(ratio / (1.0f + ratio));
    }

    return clamp01(dosePow / (dosePow + ic50Pow + kTiny));
}

float DrugModel::hillPotentiationFactor(float dose, float ec50, float hill, float maxPotentiationFactor) {
    // Reuses hillBlock's sigmoid as the underlying occupancy fraction (0..1)
    // -- the *shape* of dose-response is the same Hill equation regardless
    // of whether the bound receptor blocks or potentiates; only what the
    // occupancy fraction is then used for differs.
    const float occupancy = hillBlock(dose, ec50, hill);
    const float safeCeiling = std::isfinite(maxPotentiationFactor)
                                   ? std::max(1.0f, maxPotentiationFactor)
                                   : 1.0f;
    return 1.0f + (safeCeiling - 1.0f) * occupancy;
}

float DrugModel::hillAgonistActivation(float dose, float ec50, float hill) {
    return clamp01(hillBlock(dose, ec50, hill));
}

ReceptorConductanceModifiers DrugModel::computeReceptorModifiers(const ReceptorDrugProfile& profile, float dose) {
    ReceptorConductanceModifiers mods;

    mods.ampaResidual = receptorResidualForBlock(profile.ampa, dose);
    mods.nmdaResidual = receptorResidualForBlock(profile.nmda, dose);

    if (profile.gabaA.mechanism == ReceptorMechanism::Potentiate) {
        mods.gabaAPotentiation = hillPotentiationFactor(
            dose,
            profile.gabaA.ec50,
            profile.gabaA.hill,
            profile.gabaA.maxPotentiationFactor
        );
    }

    if (profile.gabaB.mechanism == ReceptorMechanism::Agonist) {
        mods.gabaBAgonistActivation = hillAgonistActivation(
            dose,
            profile.gabaB.ec50,
            profile.gabaB.hill
        );
    }

    return mods;
}

namespace {

spp::synapse::TransporterDrugEffect toTransporterDrugEffect(const TransporterAction& action) {
    spp::synapse::TransporterDrugEffect effect;
    effect.mechanism = action.mechanism;
    effect.kiUm = action.kiUm;
    effect.hill = action.hill;
    effect.maxExtensionFold = action.maxExtensionFold;
    return effect;
}

} // namespace

ReceptorKineticsModifiers DrugModel::computeReceptorKineticsModifiers(const ReceptorDrugProfile& profile, float dose) {
    ReceptorKineticsModifiers mods;

    const spp::synapse::TransporterDrugEffect eaat = toTransporterDrugEffect(profile.eaat);
    const spp::synapse::TransporterDrugEffect gat1 = toTransporterDrugEffect(profile.gat1);

    // EAAT block extends AMPA's and NMDA's decay -- same glutamatergic
    // release site (see ReuptakeTransporter.h design note).
    mods.ampaTauDecayMs = spp::synapse::effectiveTauDecayMs(
        spp::neuron::ReceptorKinetics::kAmpaTauDecayMs, dose, eaat);
    mods.nmdaTauDecayMs = spp::synapse::effectiveTauDecayMs(
        spp::neuron::ReceptorKinetics::kNmdaTauDecayMs, dose, eaat);

    // GAT1 block extends GABA-A's and GABA-B's decay -- same GABAergic
    // release site.
    mods.gabaATauDecayMs = spp::synapse::effectiveTauDecayMs(
        spp::neuron::ReceptorKinetics::kGabaATauDecayMs, dose, gat1);
    mods.gabaBTauDecayMs = spp::synapse::effectiveTauDecayMs(
        spp::neuron::ReceptorKinetics::kGabaBTauDecayMs, dose, gat1);

    // SERT/DAT/NET: report-only, no receptor current to feed yet (Phase 3c).
    const spp::synapse::TransporterDrugEffect sert = toTransporterDrugEffect(profile.sert);
    const spp::synapse::TransporterDrugEffect dat = toTransporterDrugEffect(profile.dat);
    const spp::synapse::TransporterDrugEffect net = toTransporterDrugEffect(profile.net);
    // fold-change = effectiveTauDecayMs(1.0, dose, effect) -- reuses the
    // same bounded-Hill-extension formula with a unit baseline, rather than
    // re-deriving the same math here.
    mods.sertOccupancy = spp::synapse::transporterOccupancy(dose, sert);
    mods.sertEffectiveTauFoldChange = spp::synapse::effectiveTauDecayMs(1.0f, dose, sert);
    mods.datOccupancy = spp::synapse::transporterOccupancy(dose, dat);
    mods.datEffectiveTauFoldChange = spp::synapse::effectiveTauDecayMs(1.0f, dose, dat);
    mods.netOccupancy = spp::synapse::transporterOccupancy(dose, net);
    mods.netEffectiveTauFoldChange = spp::synapse::effectiveTauDecayMs(1.0f, dose, net);

    return mods;
}

ReceptorConductanceModifiers DrugModel::applyReceptors(float doseScale) const {
    const float safeDoseScale = std::clamp(doseScale, 0.0f, 2.0f);
    const float dose = globalDose_ * safeDoseScale;
    return computeReceptorModifiers(globalReceptorProfile_, dose);
}

float DrugModel::amplifiedDoseForDopamine(const ReceptorDrugProfile& profile, float dose) {
    return spp::synapse::amplifiedDoseUm(dose, toTransporterDrugEffect(profile.dat));
}

float DrugModel::amplifiedDoseForSerotonin(const ReceptorDrugProfile& profile, float dose) {
    return spp::synapse::amplifiedDoseUm(dose, toTransporterDrugEffect(profile.sert));
}

spp::synapse::NeuromodulatorGainModifiers DrugModel::computeNeuromodulatorGainModifiers(
    const ReceptorDrugProfile& profile, float dose, float currentTimeMs) {
    // Phase 3c retrofit: DAT reuptake block amplifies the dose D1/D2 see;
    // SERT reuptake block amplifies the dose 5-HT1A/5-HT2A see. Both are
    // exact no-ops (return `dose` unchanged) when profile.dat/profile.sert
    // are unconfigured (mechanism=None), so this is a bit-identical
    // extension of the pre-existing behavior -- see
    // ReuptakeTransporter.h's amplifiedDoseUm and
    // NeuromodulatorSystem.h's two-dose computeNeuromodulatorGainModifiers
    // comment for the full design rationale.
    const float doseForDopamine  = amplifiedDoseForDopamine(profile, dose);
    const float doseForSerotonin = amplifiedDoseForSerotonin(profile, dose);
    return spp::synapse::computeNeuromodulatorGainModifiers(
        doseForDopamine, doseForSerotonin, profile.neuromod, currentTimeMs);
}

} // namespace spp::drug