// This file defines the DrugEvaluationEngine class, which is responsible for analyzing dose-response data from neural simulations to evaluate the biological effects of a drug. The engine computes various biological scores based on firing rates, synchronization, and other metrics, and classifies the drug's effects into categories such as Controlled Suppression, Neural Silencing, Hyperexcitability, etc. It also identifies key doses (onset, peak, toxic) and generates a comprehensive report with mechanistic interpretations and a final recommendation for the drug's potential as a therapeutic agent.
#pragma once

#include <string>
#include <vector>

namespace spp::analyzer {

// ============================================================
// UNIVERSAL BIOLOGICAL EVALUATION STRUCTURES
// ============================================================

struct DoseEvalPoint {
    float dose = 0.0f;
    float firingRateHz = 0.0f;
    float synchronizationIndex = 0.0f;
    float nii = 0.0f;
    float seizureRisk = 0.0f;
    float blockNa = 0.0f;  // Estimated channel blockade (0..1)
    float blockK = 0.0f;
    float blockCa = 0.0f;
    float burstIndex = 0.0f;
    bool suppressionHasBaseline = false;
};

struct BiologicalScores {
    float suppressionScore = 0.0f;  // % firing rate reduction
    float excitationScore = 0.0f;   // % firing rate increase
    float silenceScore = 0.0f;      // Neural silencing (rate < 10% baseline)
    float seizureScore = 0.0f;      // Seizure/instability risk
    float stabilizationScore = 0.0f;  // Network stabilization
};

enum class BiologicalState {
    LimitedEffect,
    ControlledSuppression,
    NeuralSilencing,
    Hyperexcitability,
    NetworkStabilization,
    ToxicInstability
};

struct DoseAnalysis {
    float dose = 0.0f;
    BiologicalScores scores;
    BiologicalState state;
    float toxicityScore = 0.0f;  // Max of silence/excitation/seizure
    std::string toxicityType;     // "Neural Silencing" / "Hyperexcitability" / "Seizure Risk" / "None"
};

struct DoseRange {
    float startDose = 0.0f;
    float endDose = 0.0f;
};

enum class CurveBehavior {
    Sigmoid,
    WeakSigmoidal,
    Flat,
    NonResponsive
};

enum class DrugEvalDecision {
    Promising,
    Caution,
    LimitedEfficacy,
    NotRecommended
};

struct DrugEvaluationReport {
    // ---- DOSE SWEEP INFO ----
    float doseMin = 0.0f;
    float doseMax = 0.0f;
    float doseStep = 0.0f;
    int totalDosePoints = 0;

    // ---- BASELINE ----
    float baselineFiringRateHz = 0.0f;
    float baselineSynchronization = 0.0f;
    float baselineNii = 0.0f;

    // ---- PEAK BIOLOGICAL SCORES ----
    float peakSuppressionScore = 0.0f;
    float peakExcitationScore = 0.0f;
    float peakSilenceScore = 0.0f;
    float peakSeizureScore = 0.0f;
    float peakStabilizationScore = 0.0f;

    // ---- DOMINANT STATE ----
    BiologicalState dominantState = BiologicalState::LimitedEffect;
    std::string dominantStateText = "Limited Effect";
    std::string primaryObservationText = "";

    // ---- DOSE-DEPENDENT BEHAVIOR ----
    bool hasOnsetDose = false;
    float onsetDose = 0.0f;
    
    bool hasPeakDose = false;
    float peakDose = 0.0f;
    
    bool hasToxicDose = false;
    float toxicDose = 0.0f;

    std::string responseTrendText = "Not determined";
    std::string windowAnalysisText = "Not determined";

    // ---- TOXICITY ----
    float toxicityScore = 0.0f;
    std::string toxicityTypeText = "None";
    std::string toxicityTriggerText = "Not observed";
    std::string safetySummaryText = "";

    // ---- MECHANISTIC INTERPRETATION ----
    std::string dominantChannelEffect = "Unknown";
    std::string firingRateTrend = "stable";     // ↑ / ↓ / stable
    std::string synchronizationTrend = "stable";
    std::string niiTrend = "stable";

    // ---- DECISION ----
    DrugEvalDecision finalDecision = DrugEvalDecision::LimitedEfficacy;
    std::string recommendationText = "LIMITED EFFICACY";
    std::string riskLevelText = "LOW";
    std::string reasonText = "";
    std::string confidenceText = "LOW";

    // ---- INTERNAL ANALYSIS DATA ----
    std::vector<DoseAnalysis> dosePoints;
    double sigmoidR2 = 0.0;
    CurveBehavior curveBehavior = CurveBehavior::Flat;
    std::string curveTypeText = "";
    std::vector<DoseRange> therapeuticWindows;
    bool noResponse = false;
    std::string stabilityScoreText = "UNKNOWN";
};

class DrugEvaluationEngine {
public:
    // Evaluate dose points and produce comprehensive report
    static DrugEvaluationReport evaluate(std::vector<DoseEvalPoint> points);

    // Format report to pharma-grade text output
    static std::string buildReportText(const DrugEvaluationReport& report);

    // Helper: convert enum to string
    static std::string toString(CurveBehavior behavior);
    static std::string toString(DrugEvalDecision decision);
    static std::string toString(BiologicalState state);

private:
    // ---- BIOLOGICAL SCORING ----
    
    // Compute all biological scores for a single dose point
    static BiologicalScores computeBiologicalScores(
        const DoseEvalPoint& point,
        float baselineRate,
        float baselineSync,
        float baselineNii
    );

    // Identify dominant biological state from scores
    static BiologicalState detectDominantState(
        const BiologicalScores& scores,
        float syncChange,
        float niiChange,
        float blockK
    );

    // ---- TOXICITY ENGINE ----

    // Compute universal toxicity score and type
    static std::pair<float, std::string> computeToxicity(
        const BiologicalScores& scores
    );

    // ---- DOSE SWEEP ANALYSIS ----

    // Find key doses in sweep (onset, peak, toxic)
    struct DoseMilestones {
        bool hasOnset = false;
        float onsetDose = 0.0f;
        bool hasPeak = false;
        float peakDose = 0.0f;
        bool hasToxic = false;
        float toxicDose = 0.0f;
    };

    static DoseMilestones analyzeDoseMilestones(const std::vector<DoseAnalysis>& analysis);
};

} // namespace spp::analyzer
