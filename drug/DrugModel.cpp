#include "DrugModel.h"

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

    const float naResidual = std::max(0.0f, 1.0f - blockNa);
    const float caResidual = std::max(0.0f, 1.0f - blockCa);

    ConductanceResult result;
    result.blockNa = blockNa;
    result.blockK = blockK;
    result.blockCa = blockCa;
    result.gNaEff = std::max(0.05f * safeGNa, safeGNa * naResidual);
    result.gKEff = std::max(0.05f * safeGK, safeGK * (1.0f - 2.5f * blockK));
    result.gCaEff = std::max(0.02f * safeGCa, safeGCa * caResidual);
    return result;
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

} // namespace spp::drug
