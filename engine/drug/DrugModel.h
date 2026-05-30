#pragma once

#include <cstddef>
#include <vector>

namespace spp::drug {

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

private:
    float globalDose_;
    ChannelDrugProfile globalProfile_;

    bool perNeuronEnabled_;
    std::vector<ChannelDrugProfile> neuronProfiles_;
    std::vector<float> neuronDoses_;
};

} // namespace spp::drug
