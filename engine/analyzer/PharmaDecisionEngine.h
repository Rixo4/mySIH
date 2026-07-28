// PharmaDecisionEngine.h
// Receives AnalyzedDose[] from NetworkAnalyzer and produces the final
// drug evaluation report. No raw metrics, no baseline computation here.
#pragma once

#include <string>
#include <vector>
#include "AnalyzedDose.h"
#include "DoseObservation.h"

namespace spp::analyzer {

// ─── Risk tier ───────────────────────────────────────────────────────────────
enum class DrugRiskTier {
    Safe,
    ModerateRisk,
    HighRisk,
    Toxic
};

// ─── Per-dose decision point ──────────────────────────────────────────────────
struct DrugDecisionPoint {
    float dose                    = 0.0f;
    float seizureProbabilityPct   = 0.0f;
    float suppressionPct          = 0.0f;
    float riskScore               = 0.0f;
    std::string classification;
    float earlyWarningIndex       = 0.0f;
    float seizureSlopePctPerDose  = 0.0f;
};

// ─── Stability input from multi-run aggregation ───────────────────────────────
struct DecisionStabilityInput {
    float rateStd             = 0.0f;
    float toxicityStd         = 0.0f;
    std::string stabilityScore = "UNSPECIFIED";
    int runCount              = 0;
};

// ─── Final report ─────────────────────────────────────────────────────────────
struct PharmaDecisionReport {

    // Dose range
    double minTestedDose = 0.0;
    double maxTestedDose = 0.0;
    double stepDose      = 0.0;

    // Response characterization
    std::string responseMode     = "NO_SIGNIFICANT_RESPONSE";
    std::string curveType        = "Flat";
    std::string responseStrength = "Weak";
    double sigmoidR2             = 0.0;

    // Mechanism
    MechanismSignature dominantMechanism = MechanismSignature::Unknown;
    std::string mechanismText            = "Unknown";

    // Per-dose data
    std::vector<DrugDecisionPoint> points;
    std::vector<AnalyzedDose>      analyzedDoses;
    std::vector<double>            excitatoryRiskDoses;

    // Dose milestones
    bool  hasEffectiveDose   = false;
    float effectiveMinDose   = 0.0f;
    bool  hasToxicThreshold  = false;
    float toxicMinDose       = 0.0f;
    bool  hasToxicThresholdExact  = false;
    double toxicThresholdDoseEval = 0.0;
    std::string toxicThresholdText = "N/A";

    // Therapeutic window
    bool  hasContinuousEffectiveWindow = false;
    double effectiveRangeMin = 0.0;
    double effectiveRangeMax = 0.0;
    bool  hasTherapeuticWindow = false;
    float therapeuticWindow    = 0.0f;
    std::string windowQuality  = "Not observed";

    // Safety margin: ratio of toxic threshold dose to the top of the
    // therapeutic window, when a real window exists and toxicity appears
    // strictly above it (see buildNarrowMarginNote in .cpp for how this
    // feeds the recommendation -- distinguishes "narrow but real margin"
    // e.g. classic narrow-therapeutic-index drugs like phenytoin, from
    // "no usable margin at all").
    bool   hasSafetyMarginRatio = false;
    double safetyMarginRatio    = 0.0;
    bool   narrowSafetyMargin   = false;

    // Safe range
    bool  hasSafeRange  = false;
    float safeMinDose   = 0.0f;
    float safeMaxDose   = 0.0f;

    // Peak scores
    float peakRiskScore             = 0.0f;
    float peakSeizureProbabilityPct = 0.0f;
    float peakSuppressionPct        = 0.0f;
    float peakEarlyWarningIndex     = 0.0f;
    float maxSeizureSlopePctPerDose = 0.0f;

    // BUG FIX: the report used to print a DIFFERENT "Max Effect" number
    // (main.cpp's own weighted composite of rateDrive/burstDrive/irregDrive)
    // than the one this file actually gates the `excitatory` recommendation
    // bool against (raw rateChangePct-derived effMag, see evaluate()'s
    // maxEffectPct local). Found via a DOI/5-HT2A test showing "Max Effect:
    // 8%, Weak" printed directly next to "NOT RECOMMENDED / HIGH RISK" --
    // the two numbers disagreed on which side of the 10% notable-effect
    // threshold the drug fell on (displayed 8% < 10%, but the actual
    // decision-driving raw rate change was 11.3% > 10%), producing a report
    // that visibly contradicted its own verdict. Exposing the real
    // decision-driving value here so main.cpp can print the SAME number it
    // was silently deciding on instead of recomputing an unrelated one.
    float decisionMaxEffectPct      = 0.0f;

    // Stabilization metrics (Ca-block)
    double syncReductionPct       = 0.0;
    double seizureReductionPct    = 0.0;
    double burstReductionPct      = 0.0;
    double niiReductionPct        = 0.0;
    double calciumEffectMagnitude = 0.0;
    bool   meaningfulCaBlock      = false;
    float  stabilizationScore     = 0.0f;

    // Stability
    float rateVariability     = 0.0f;
    float toxicityVariability = 0.0f;
    std::string stabilityScore_str = "UNSPECIFIED";

    // Narrative
    std::string primaryChangeText        = "Not observed";
    std::string safetyInterpretationText = "Not assessed";
    std::string seizureTrendText         = "No significant change";

    // Final decision
    std::string recommendation = "INCONCLUSIVE";
    std::string reason         = "Insufficient data";
    std::string confidence     = "LOW";
    std::string riskLevel      = "LOW";
    DrugRiskTier overallTier   = DrugRiskTier::Safe;

    // Deprecated — kept so report_parser.py still finds these fields
    std::string stabilityScore = "UNSPECIFIED";
};

// ─── Engine ───────────────────────────────────────────────────────────────────
class PharmaDecisionEngine {
public:
    // Main entry point
    // Receives fully analyzed doses from NetworkAnalyzer
    static PharmaDecisionReport evaluate(
        const std::vector<AnalyzedDose>& analyzedDoses,
        const DecisionStabilityInput& stabilityInput = {}
    );

    // toString helpers
    static std::string toString(DrugRiskTier tier);
    static std::string toString(NetworkState state);
    static std::string toString(MechanismSignature signature);
};

} // namespace spp::analyzer