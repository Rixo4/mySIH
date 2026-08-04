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

    // contiguousRanges() (see .cpp) tolerates a single missing dose point
    // (gap <= 2*step) when deciding Continuous vs Fragmented, so one noisy
    // dropout inside an otherwise solid window doesn't wrongly report
    // "Fragmented". That means a dose can fall INSIDE the reported
    // effectiveRangeMin/Max span yet still be absent from the per-dose
    // Ineffective/Therapeutic classification -- expected, not a bug, but
    // confusing to read without an explanation. This lists those doses so
    // main.cpp can print a clarifying note next to the window.
    std::vector<double> toleratedNoiseDoses;

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

    // Index into analyzedDoses of whichever dose actually produced
    // decisionMaxEffectPct above (Gap 1.1 audit fix, found via cocaine).
    // IMPORTANT: this is a DIFFERENT dose selection than report/
    // LiabilityReport.cpp's and LegacyLiabilityReport.cpp's own locally
    // computed "peakIdx" (highest composite max(suppressionScore,
    // excitabilityScore, stabilizationScore), used for Network Impact /
    // Neuromodulator Gain Profile / Peak Efficiency). That composite score
    // and this raw rateChangePct-derived max can legitimately peak at
    // different doses -- cocaine's own audit found decisionMaxEffectPct
    // paired with the composite peakIdx's dose printed "54.2% change at
    // dose 2.7000" while dose 2.7's own Rate Change field read 49.0%, i.e.
    // the 54.2% actually happened at a different dose. Reports must pair
    // decisionMaxEffectPct with THIS index, not the composite peakIdx.
    std::size_t decisionMaxEffectDoseIdx = 0;

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

    // True when NOT RECOMMENDED/HIGH RISK was reached via the categorical
    // EXCITATORY_RESPONSE + maxEffectPct>10 magnitude-floor path (see the
    // `excitatory` comment in the .cpp -- 4-AP precedent: real per-dose
    // danger states are the primary signal, this is the fallback for a drug
    // whose aggregate rate swing is large enough to be dangerous even though
    // no single dose crossed a structural instability threshold). Found
    // worth flagging explicitly (Phase 3 10-drug validation, DOI: every
    // dose classified Stable, yet still NOT RECOMMENDED) -- without this,
    // the report looks self-contradictory: "Per Dose Network State" all
    // Stable next to "NOT RECOMMENDED / HIGH RISK" with no visible link
    // between them. main.cpp prints an explanatory note when this is true.
    bool excitatoryVerdictViaMagnitudeFloor = false;

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