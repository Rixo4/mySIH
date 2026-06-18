#include "analyzer/PharmaDecisionEngine.h"
#include "drug/DrugModel.h"

#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <algorithm>
#include <vector>

using spp::analyzer::BiologicalState;
using spp::analyzer::DecisionStabilityInput;
using spp::analyzer::DoseObservation;
using spp::analyzer::PharmaDecisionEngine;
using spp::analyzer::PharmaDecisionReport;

namespace {

struct CaseDefinition {
    std::string name;
    double ic50Na;
    double ic50K;
    double ic50Ca;
    BiologicalState expectedState;  // Changed from allowedModes (responseMode) to BiologicalState
    enum class Profile {
        Neutral,
        Suppressive,
        Excitatory,
        Stabilizing,
        MixedWeak
    } profile;
};

float hillBlock(double dose, double ic50) {
    return spp::drug::DrugModel::hillBlock(static_cast<float>(dose), static_cast<float>(ic50), 3.2f);
}

DoseObservation makeObservation(double dose, double ic50Na, double ic50K, double ic50Ca, CaseDefinition::Profile profile) {
    const float blockNa = hillBlock(dose, ic50Na);
    const float blockK = hillBlock(dose, ic50K);
    const float blockCa = hillBlock(dose, ic50Ca);

    DoseObservation observation;
    observation.dose = static_cast<float>(dose);
    observation.blockNa = blockNa;
    observation.blockK = blockK;
    observation.blockCa = blockCa;
    observation.suppressionPct = 5.0f;

    switch (profile) {
        case CaseDefinition::Profile::Neutral:
            observation.meanFiringRateHz = 18.0f - 0.4f * blockNa + 0.2f * blockK - 0.1f * blockCa;
            observation.synchronizationIndex = 0.18f - 0.01f * blockNa + 0.01f * blockK - 0.005f * blockCa;
            observation.burstIndex = 0.05f - 0.01f * blockCa;
            observation.nii = 0.12f - 0.01f * blockCa + 0.005f * blockNa;
            observation.seizureProbabilityPct = 6.0f + 0.5f * blockK;
            observation.suppressionPct = 2.0f + 4.0f * blockNa;
            break;
        case CaseDefinition::Profile::Suppressive:
            observation.meanFiringRateHz = 18.0f - 10.0f * blockNa - 1.0f * blockK - 1.0f * blockCa;
            observation.synchronizationIndex = 0.18f - 0.07f * blockNa - 0.01f * blockK - 0.01f * blockCa;
            observation.burstIndex = 0.05f - 0.02f * blockNa;
            observation.nii = 0.12f - 0.05f * blockNa - 0.01f * blockCa;
            observation.seizureProbabilityPct = 6.0f - 2.5f * blockNa;
            observation.suppressionPct = 10.0f + 65.0f * blockNa;
            break;
        case CaseDefinition::Profile::Excitatory:
            observation.meanFiringRateHz = 18.0f + 10.0f * blockK - 1.0f * blockNa - 1.0f * blockCa;
            observation.synchronizationIndex = 0.18f + 0.06f * blockK + 0.01f * blockNa;
            observation.burstIndex = 0.05f + 0.02f * blockK;
            observation.nii = 0.12f + 0.05f * blockK + 0.01f * blockNa;
            observation.seizureProbabilityPct = 6.0f + 18.0f * blockK;
            observation.suppressionPct = 2.0f;
            break;
        case CaseDefinition::Profile::Stabilizing:
            observation.meanFiringRateHz = 18.0f - 2.0f * blockCa;
            observation.synchronizationIndex = 0.18f - 0.08f * blockCa;
            observation.burstIndex = 0.05f - 0.02f * blockCa;
            observation.nii = 0.12f - 0.06f * blockCa;
            observation.seizureProbabilityPct = 6.0f - 2.0f * blockCa;
            observation.suppressionPct = 6.0f + 10.0f * blockCa;
            break;
        case CaseDefinition::Profile::MixedWeak:
            if (dose < 300.0) {
                observation.meanFiringRateHz = 14.0f - 2.0f * blockNa;
                observation.synchronizationIndex = 0.15f - 0.01f * blockNa;
                observation.burstIndex = 0.04f - 0.005f * blockCa;
                observation.nii = 0.10f - 0.01f * blockCa;
                observation.seizureProbabilityPct = 5.5f - 0.5f * blockNa;
                observation.suppressionPct = 18.0f + 20.0f * blockNa;
            } else {
                observation.meanFiringRateHz = 24.0f + 2.0f * blockK;
                observation.synchronizationIndex = 0.22f + 0.01f * blockK;
                observation.burstIndex = 0.06f + 0.005f * blockK;
                observation.nii = 0.16f + 0.01f * blockK;
                observation.seizureProbabilityPct = 7.2f + 0.5f * blockK;
                observation.suppressionPct = 8.0f;
            }
            break;
    }

    observation.isiCv = 0.12f + 0.01f * static_cast<float>(dose / 20.0);
    return observation;
}

std::vector<DoseObservation> makeCaseObservations(const CaseDefinition& definition, CaseDefinition::Profile profile) {
    return {
        makeObservation(0.0, definition.ic50Na, definition.ic50K, definition.ic50Ca, CaseDefinition::Profile::Neutral),
        makeObservation(200.0, definition.ic50Na, definition.ic50K, definition.ic50Ca, profile),
        makeObservation(400.0, definition.ic50Na, definition.ic50K, definition.ic50Ca, profile)
    };
}

bool runCase(const CaseDefinition& definition) {
    const std::vector<DoseObservation> observations = makeCaseObservations(definition, definition.profile);
    const PharmaDecisionReport report = PharmaDecisionEngine::evaluate(observations, DecisionStabilityInput{0.5f, 0.5f, "MEDIUM", 5});

    const bool stateMatches = (report.biologicalState == definition.expectedState);
    std::cout << definition.name << ": state=" << PharmaDecisionEngine::toString(report.biologicalState)
              << " (expected " << PharmaDecisionEngine::toString(definition.expectedState) << ")"
              << ", recommendation=" << report.recommendation
              << (stateMatches ? " [PASS]" : " [FAIL]") << '\n';
    return stateMatches;
}

} // namespace

int main() {
    const std::vector<CaseDefinition> cases = {
        {"Case A", 1000.0, 1000.0, 1000.0, BiologicalState::LimitedEffect, CaseDefinition::Profile::Neutral},
        {"Case B", 200.0, 1000.0, 1000.0, BiologicalState::ControlledSuppression, CaseDefinition::Profile::Suppressive},
        {"Case C", 1000.0, 200.0, 1000.0, BiologicalState::Hyperexcitability, CaseDefinition::Profile::Excitatory},
        {"Case D", 1000.0, 1000.0, 200.0, BiologicalState::NetworkStabilization, CaseDefinition::Profile::Stabilizing},
        {"Case E", 200.0, 200.0, 1000.0, BiologicalState::LimitedEffect, CaseDefinition::Profile::MixedWeak}
    };

    bool allPassed = true;
    for (const auto& testCase : cases) {
        allPassed = runCase(testCase) && allPassed;
    }

    if (!allPassed) {
        std::cerr << "Biological state validation cases failed." << std::endl;
        return 1;
    }

    std::cout << "Biological state validation cases passed." << std::endl;
    return 0;
}
