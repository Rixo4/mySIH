// This file defines the PharmaDecisionEngine class and related
// ARCHITECTURE: 12-stage universal computational neuropharmacology pipeline
// The analyzer thinks only in emergent biology, never in drug names or classes.
#pragma once

#include <string>
#include <vector>
#include "SeizureDetector.h"

namespace spp::analyzer {

// ============================================================
// STAGE 1-3: Channel Blockade → Neuron Dynamics → Network Dynamics
// These are already computed by the simulation engine
// and delivered here in DoseObservation format.
// ============================================================

struct DoseObservation {
    float dose = 0.0f;
    
    // STAGE 3: Network-level metrics (emergent biology)
    float meanFiringRateHz = 0.0f;
    float synchronizationIndex = 0.0f;
    float burstIndex = 0.0f;
    float nii = 0.0f;  // Neural Instability Index
    float isiCv = 0.0f;
    float seizureProbabilityPct = 0.0f;
    float suppressionPct = 0.0f;
    
    // Molecular blockade information (for mechanistic evidence only)
    // Never used for direct prediction, only for explaining observed biology
    float blockNa = 0.0f;
    float blockK = 0.0f;
    float blockCa = 0.0f;
    NetworkState networkState = NetworkState::Stable;
    bool suppressionHasBaseline = false;
};

// ============================================================
// STAGE 4: Per-Dose Metrics collected
// ============================================================

struct PerDoseMetrics {
    double dose = 0.0;
    double baseline_rate = 0.0;
    double baseline_sync = 0.0;
    double baseline_burst = 0.0;
    double baseline_nii = 0.0;
    double baseline_seizure = 0.0;
    
    // Observed metrics at this dose
    double rate_hz = 0.0;
    double sync_index = 0.0;
    double burst_index = 0.0;
    double nii = 0.0;
    double seizure_pct = 0.0;
};

// ============================================================
// STAGE 5: Per-Dose Biological Interpretation
// Changes relative to baseline
// ============================================================

struct PerDoseBiologicalInterpretation {
    double dose = 0.0;
    
    // Absolute changes from baseline
    double rate_change_pct = 0.0;
    double rate_change_frac = 0.0;
    double sync_delta = 0.0;
    double sync_reduction_pct = 0.0;
    double burst_delta = 0.0;
    double burst_reduction_pct = 0.0;
    double nii_delta = 0.0;
    double nii_reduction_pct = 0.0;
    double nii_increase_pct = 0.0;
    double seizure_delta = 0.0;
    double seizure_reduction_pct = 0.0;
    
    // Normalized forms for metric comparison
    float rate_norm = 0.0f;
    float sync_norm = 0.0f;
    float burst_norm = 0.0f;
    float nii_norm = 0.0f;
    float seizure_norm = 0.0f;
};

// ============================================================
// STAGE 6: Mechanistic Evidence Classification
// What biology actually emerged at this dose?
// ============================================================

enum class MechanisticMechanism {
    Suppression,           // Firing ↓, Propagation ↓
    Excitation,            // Firing ↑, NII ↑, Seizure ↑
    Stabilization,         // Sync ↓, Burst ↓, NII ↓ while rate stable
    Silencing,             // Near-total firing collapse
    LimitedEffect          // No meaningful change
};

struct MechanisticEvidence {
    double dose = 0.0;
    MechanisticMechanism mechanism = MechanisticMechanism::LimitedEffect;
    std::string description;
    
    // Evidence strength (0.0 - 1.0)
    double evidence_strength = 0.0;
    bool is_consistent = true;
};

// ============================================================
// STAGE 7: Per-Dose Classification
// Each dose classified independently based on metrics
// ============================================================

enum class BiologicalState {
    LimitedEffect,
    ControlledSuppression,
    NeuralSilencing,
    Hyperexcitability,
    NetworkStabilization,
    ToxicInstability
};

struct PerDoseClassification {
    double dose = 0.0;
    BiologicalState state = BiologicalState::LimitedEffect;
    double confidence = 0.0;
};

// ============================================================
// STAGE 8: Dose-Response Analysis
// Patterns across all doses
// ============================================================

struct DoseResponseAnalysis {
    double response_onset_dose = 0.0;
    double response_peak_dose = 0.0;
    double response_saturation_dose = 0.0;
    double toxicity_threshold_dose = 0.0;
    
    bool is_continuous = false;
    bool is_sigmoidal = false;
    double r2_value = 0.0;
    
    bool has_therapeutic_window = false;
    double window_min = 0.0;
    double window_max = 0.0;
};

// ============================================================
// STAGE 9: Mechanistic Dominance
// What mechanism dominates the entire curve?
// ============================================================

struct MechanisticDominance {
    MechanisticMechanism primary_mechanism = MechanisticMechanism::LimitedEffect;
    double prevalence_pct = 0.0;  // What % of doses show this?
    bool is_consistent_across_curve = false;
};

// ============================================================
// STAGE 10: Safety Analysis (Independent)
// ============================================================

struct SafetyAnalysis {
    bool seizure_risk_elevated = false;
    bool network_instability_observed = false;
    bool network_collapse_observed = false;
    bool toxicity_before_effect = false;
    bool safe_margin_exists = false;
    
    std::string primary_safety_concern;
    double overall_safety_score = 0.0;  // 0 = safe, 1 = toxic
};

// ============================================================
// STAGE 11: Confidence Analysis
// How trustworthy is the conclusion?
// ============================================================

struct ConfidenceAnalysis {
    double curve_quality = 0.0;      // Based on R² and continuity
    double mechanism_consistency = 0.0;  // How consistent is the mechanism?
    double dose_consistency = 0.0;       // How consistent across doses?
    double window_continuity = 0.0;      // Is the therapeutic window continuous?
    double data_variability = 0.0;       // Inverse of variability
    
    double overall_confidence = 0.0;  // 0 = low, 1 = high
    std::string confidence_level;     // "LOW", "MEDIUM", "HIGH"
};

// ============================================================
// Old data structures (kept for backward compatibility)
// ============================================================

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
    // ============================================================
    // Basic information
    // ============================================================
    double minTestedDose = 0.0;
    double maxTestedDose = 0.0;
    double stepDose = 0.0;

    // ============================================================
    // STAGE 4-7: Per-dose data and classifications
    // ============================================================
    std::vector<DoseFeatures> features;  // Backward compat
    std::vector<PerDoseBiologicalInterpretation> per_dose_interpretations;
    std::vector<PerDoseClassification> per_dose_classifications;
    std::vector<MechanisticEvidence> mechanistic_evidence;

    // ============================================================
    // STAGE 8: Dose-Response Analysis
    // ============================================================
    DoseResponseAnalysis dose_response;
    double maxRateChangePct = 0.0;
    double sigmoidR2 = 0.0;
    std::string curveType = "Flat";
    std::string responseStrength = "Weak";

    // ============================================================
    // STAGE 9: Mechanistic Dominance
    // ============================================================
    MechanisticDominance mechanistic_dominance;

    // ============================================================
    // STAGE 10: Safety Analysis
    // ============================================================
    SafetyAnalysis safety;
    
    bool hasToxicThresholdExact = false;
    double toxicThresholdDoseEval = 0.0;
    std::string toxicThresholdText = "N/A";

    // ============================================================
    // STAGE 11: Confidence Analysis
    // ============================================================
    ConfidenceAnalysis confidence_analysis;

    // ============================================================
    // STAGE 7: Overall Biological State (summary)
    // ============================================================
    BiologicalState biologicalState = BiologicalState::LimitedEffect;
    std::string biologicalStateText = "Limited effect";
    std::string primaryChangeText = "Not observed";
    std::string safetyInterpretationText = "Not observed";

    // ============================================================
    // Therapeutic Window (backward compat)
    // ============================================================
    bool hasSuppressionThreshold = false;
    double suppressionThresholdDose = 0.0;

    bool hasContinuousEffectiveWindow = false;
    double effectiveRangeMin = 0.0;
    double effectiveRangeMax = 0.0;
    std::string windowQuality = "Not well-defined";

    // ============================================================
    // STAGE 12: Final Summary (only describing, not deciding)
    // ============================================================
    std::string recommendation = "INCONCLUSIVE";
    std::string reason = "No stable therapeutic window";
    std::string confidence = "LOW";
    std::string riskLevel = "LOW";
    std::string responseMode = "NO_SIGNIFICANT_RESPONSE";
    
    double syncReductionPct = 0.0;
    double niiReductionPct = 0.0;
    double niiIncreasePct = 0.0;
    double seizureReductionPct = 0.0;
    double burstReductionPct = 0.0;
    double calciumEffectMagnitude = 0.0;
    bool meaningfulCaBlock = false;
    std::string seizureTrendText = "No significant change in seizure-risk markers";
    bool hasSafeRange = false;
    float safeMinDose = 0.0f;
    float safeMaxDose = 0.0f;

    bool hasToxicThreshold = false;
    float toxicMinDose = 0.0f;

    std::vector<double> excitatoryRiskDoses;
    std::vector<double> overSuppressionDoses;
    std::vector<double> stabilizationSaturationDoses;
    
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