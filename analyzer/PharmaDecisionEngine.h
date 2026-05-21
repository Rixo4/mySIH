#pragma once

#include <string>
#include <vector>

namespace spp::analyzer {

struct DoseObservation {
    float dose = 0.0f;
    float meanFiringRateHz = 0.0f;
    float synchronizationIndex = 0.0f;
    float burstIndex = 0.0f;
    float nii = 0.0f;
    float isiCv = 0.0f;
    float seizureProbabilityPct = 0.0f;
    float suppressionPct = 0.0f;
    // Per-dose estimated channel block fractions (0..1)
    float blockNa = 0.0f;
    float blockK = 0.0f;
    float blockCa = 0.0f;
};

struct DoseFeatures {
    double dose = 0.0;
    double rate_change = 0.0;
    double sync = 0.0;
    double isi_cv = 0.0;
    double seizure_prob = 0.0;
    double toxicity_score = 0.0;

    bool is_effective = false;
    bool is_toxic = false;
};

enum class DrugRiskTier {
    Safe,
    ModerateRisk,
    HighRisk,
    Toxic
};

enum class BiologicalState {
    LimitedEffect,
    ControlledSuppression,
    NeuralSilencing,
    Hyperexcitability,
    NetworkStabilization,
    ToxicInstability
};

struct DrugDecisionPoint {
    float dose = 0.0f;
    float seizureProbabilityPct = 0.0f;
    float suppressionPct = 0.0f;
    float riskScore = 0.0f;
    std::string classification;
    float earlyWarningIndex = 0.0f;
    float seizureSlopePctPerDose = 0.0f;
};

struct DecisionStabilityInput {
    float rateStd = 0.0f;
    float toxicityStd = 0.0f;
    std::string stabilityScore = "UNSPECIFIED";
    int runCount = 0;
};

struct PharmaDecisionReport {
    double minTestedDose = 0.0;
    double maxTestedDose = 0.0;
    double stepDose = 0.0;

    std::vector<DoseFeatures> features;

    double maxRateChangePct = 0.0;
    double sigmoidR2 = 0.0;
    std::string curveType = "Flat";
    std::string responseStrength = "Weak";

    bool hasToxicThresholdExact = false;
    double toxicThresholdDoseEval = 0.0;
    std::string toxicThresholdText = "N/A";

    bool hasSuppressionThreshold = false;
    double suppressionThresholdDose = 0.0;

    bool hasContinuousEffectiveWindow = false;
    double effectiveRangeMin = 0.0;
    double effectiveRangeMax = 0.0;
    std::string windowQuality = "Not well-defined";

    std::string recommendation = "INCONCLUSIVE";
    std::string reason = "No stable therapeutic window";
    std::string confidence = "LOW";
    std::string riskLevel = "LOW";
    BiologicalState biologicalState = BiologicalState::LimitedEffect;
    std::string biologicalStateText = "Limited effect";
    std::string primaryChangeText = "Not observed";
    std::string safetyInterpretationText = "Not observed";
    std::string seizureTrendText = "Not observed";
        std::string responseMode = "STANDARD_RESPONSE";
        double syncReductionPct = 0.0;
        double niiReductionPct = 0.0;
        double niiIncreasePct = 0.0;
        double seizureReductionPct = 0.0;
        double burstReductionPct = 0.0;
        double calciumEffectMagnitude = 0.0;
        bool meaningfulCaBlock = false;

    bool hasSafeRange = false;
    float safeMinDose = 0.0f;
    float safeMaxDose = 0.0f;

    bool hasToxicThreshold = false;
    float toxicMinDose = 0.0f;

    bool hasEffectiveDose = false;
    float effectiveMinDose = 0.0f;

    bool hasTherapeuticWindow = false;
    float therapeuticWindow = 0.0f;

    float peakRiskScore = 0.0f;
    float peakSeizureProbabilityPct = 0.0f;
    float peakSuppressionPct = 0.0f;
    float peakEarlyWarningIndex = 0.0f;
    float maxSeizureSlopePctPerDose = 0.0f;

    float rateVariability = 0.0f;
    float toxicityVariability = 0.0f;
    std::string stabilityScore = "UNSPECIFIED";

    DrugRiskTier overallTier = DrugRiskTier::Safe;
    std::vector<DrugDecisionPoint> points;
};

class PharmaDecisionEngine {
public:
    static PharmaDecisionReport evaluate(
        const std::vector<DoseObservation>& observations,
        const DecisionStabilityInput& stabilityInput = {}
    );
    static std::string toString(BiologicalState state);
    static std::string toString(DrugRiskTier tier);
};

} // namespace spp::analyzer
