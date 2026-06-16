// This file implements the DrugEvaluationEngine class, which is responsible for analyzing dose-response data from neural simulations to evaluate the biological effects of a drug. The engine computes various biological scores based on firing rates, synchronization, and other metrics, and classifies the drug's effects into categories such as Controlled Suppression, Neural Silencing, Hyperexcitability, etc. It also identifies key doses (onset, peak, toxic) and generates a comprehensive report with mechanistic interpretations and a final recommendation for the drug's potential as a therapeutic agent.
#include "DrugEvaluationEngine.h"

#include <algorithm>
#include <iomanip>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace spp::analyzer {

namespace {

// ============================================================
// UTILITY FUNCTIONS
// ============================================================

float clamp01(float value) {
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}

float sanitizeDose(float dose) {
    if (!std::isfinite(dose) || dose < 0.0f) return 0.0f;
    return dose;
}

float sanitizeRate(float rateHz) {
    if (!std::isfinite(rateHz) || rateHz < 0.0f) return 0.0f;
    return rateHz;
}

float sanitizeMetric(float metric) {
    if (!std::isfinite(metric)) return 0.0f;
    return std::clamp(metric, 0.0f, 1.0f);
}

std::string formatNumber(double value, int precision = 2) {
    if (!std::isfinite(value)) return "N/A";
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string formatRangeText(float startDose, float endDose) {
    return formatNumber(startDose) + " - " + formatNumber(endDose);
}

float estimateDoseStep(const std::vector<DoseEvalPoint>& points) {
    if (points.size() < 2U) return 0.0f;
    float minPositiveDiff = std::numeric_limits<float>::infinity();
    for (std::size_t i = 1; i < points.size(); ++i) {
        const float diff = points[i].dose - points[i - 1U].dose;
        if (diff > 1.0e-5f) {
            minPositiveDiff = std::min(minPositiveDiff, diff);
        }
    }
    if (!std::isfinite(minPositiveDiff)) return 0.0f;
    return minPositiveDiff;
}

// ============================================================
// BIOLOGICAL SCORING ENGINE
// ============================================================

BiologicalScores computeBiologicalScoresImpl(
    const DoseEvalPoint& point,
    float baselineRate,
    float baselineSync,
    float baselineNii
) {
    BiologicalScores scores;
    
    const float base = std::max(1.0e-6f, baselineRate);
    const float rateChange = (point.firingRateHz - base) / base;
    
    // ---- SUPPRESSION: measure of firing rate reduction ----
    scores.suppressionScore = std::max(0.0f, -rateChange * 100.0f);
    
    // ---- EXCITATION: measure of firing rate increase ----
    scores.excitationScore = std::max(0.0f, rateChange * 100.0f);
    
    // ---- NEURAL SILENCING: when rate drops below 10% of baseline ----
    if (point.firingRateHz < 0.10f * base) {
        scores.silenceScore = 100.0f;
    } else {
        scores.silenceScore = 0.0f;
    }
    
    // ---- SEIZURE RISK: combination of seizure probability and network instability ----
    const float seizureProbComp = clamp01(point.seizureRisk) * 100.0f;
    const float niiComp = clamp01(point.nii) * 100.0f;
    scores.seizureScore = std::max(seizureProbComp, niiComp);
    
    // ---- NETWORK STABILIZATION ----
    // Positive if synchronization decreases AND NII decreases AND seizure risk decreases
    const float syncChange = point.synchronizationIndex - baselineSync;
    const float niiChange = point.nii - baselineNii;
    
    const bool syncImproved = syncChange < -0.05f;  // Reduced by at least 0.05
    const bool niiImproved = niiChange < -0.05f;
    const bool seizureImproved = point.seizureRisk < 0.2f;
    
    if (syncImproved && niiImproved && seizureImproved) {
        scores.stabilizationScore = 50.0f;  // Moderate stabilization score
    } else if ((syncImproved && niiImproved) || (syncImproved && seizureImproved)) {
        scores.stabilizationScore = 25.0f;  // Partial stabilization
    } else {
        scores.stabilizationScore = 0.0f;
    }
    
    return scores;
}

// ============================================================
// DOMINANT STATE DETECTOR
// ============================================================

BiologicalState detectDominantStateImpl(
    const BiologicalScores& scores,
    float blockK
) {
    // Rule 1: Neural silencing takes priority
    if (scores.silenceScore >= 80.0f) {
        return BiologicalState::NeuralSilencing;
    }
    
    // Rule 2: K-channel blockade MUST map to hyperexcitability if firing increases
    // MANDATORY: K-block cannot be suppression
    if (blockK > 0.5f && scores.excitationScore > 20.0f) {
        return BiologicalState::Hyperexcitability;
    }
    
    // Rule 3: Hyperexcitability or seizure risk
    if (scores.excitationScore > 60.0f || scores.seizureScore > 70.0f) {
        return BiologicalState::Hyperexcitability;
    }
    
    // Rule 4: Network stabilization
    if (scores.stabilizationScore > 40.0f) {
        return BiologicalState::NetworkStabilization;
    }
    
    // Rule 5: Controlled suppression (moderate suppression)
    if (scores.suppressionScore > 30.0f && scores.suppressionScore < 70.0f) {
        return BiologicalState::ControlledSuppression;
    }
    
    // Rule 6: Toxic instability (extreme changes)
    if (scores.seizureScore > 80.0f || scores.silenceScore > 50.0f) {
        return BiologicalState::ToxicInstability;
    }
    
    // Default: limited effect
    return BiologicalState::LimitedEffect;
}

// ============================================================
// TOXICITY ENGINE
// ============================================================

std::pair<float, std::string> computeToxicityImpl(const BiologicalScores& scores) {
    float toxScore = 0.0f;
    std::string toxType = "None";
    
    // Toxicity is the maximum of harmful effects
    float maxToxEffect = 0.0f;
    
    if (scores.silenceScore > 50.0f) {
        maxToxEffect = scores.silenceScore;
        toxType = "Neural Silencing";
    }
    
    if (scores.excitationScore > maxToxEffect) {
        maxToxEffect = scores.excitationScore;
        toxType = "Hyperexcitability";
    }
    
    if (scores.seizureScore > maxToxEffect) {
        maxToxEffect = scores.seizureScore;
        toxType = "Seizure Risk";
    }
    
    toxScore = maxToxEffect;
    
    return {toxScore, toxType};
}

// ============================================================
// DOSE MILESTONE DETECTION
// ============================================================

struct DoseMilestonesData {
    bool hasOnset = false;
    float onsetDose = 0.0f;
    bool hasPeak = false;
    float peakDose = 0.0f;
    bool hasToxic = false;
    float toxicDose = 0.0f;
};

DoseMilestonesData analyzeDoseMilestonesImpl(
    const std::vector<DoseAnalysis>& analysis
) {
    DoseMilestonesData milestones;
    
    if (analysis.empty()) {
        return milestones;
    }
    
    float maxScore = 0.0f;
    
    // Find onset dose (first point where any score > 20%)
    for (const auto& pt : analysis) {
        const float maxScoreHere = std::max({
            pt.scores.suppressionScore,
            pt.scores.excitationScore,
            pt.scores.silenceScore,
            pt.scores.seizureScore,
            pt.scores.stabilizationScore
        });
        
        if (!milestones.hasOnset && maxScoreHere > 20.0f) {
            milestones.hasOnset = true;
            milestones.onsetDose = pt.dose;
        }
        
        // Track peak effect
        if (maxScoreHere > maxScore) {
            maxScore = maxScoreHere;
            milestones.hasPeak = true;
            milestones.peakDose = pt.dose;
        }
        
        // Track toxic threshold (first point where toxicity > threshold)
        if (!milestones.hasToxic && pt.toxicityScore > 50.0f) {
            milestones.hasToxic = true;
            milestones.toxicDose = pt.dose;
        }
    }
    
    return milestones;
}

// ============================================================
// RESPONSE TREND ANALYSIS
// ============================================================

std::string analyzeResponseTrendImpl(const std::vector<DoseAnalysis>& analysis) {
    if (analysis.size() < 2) {
        return "Insufficient data";
    }
    
    // Compute slope of max scores across doses
    float firstMaxScore = 0.0f;
    float lastMaxScore = 0.0f;
    
    // Get first score
    firstMaxScore = std::max({
        analysis.front().scores.suppressionScore,
        analysis.front().scores.excitationScore,
        analysis.front().scores.seizureScore
    });
    
    // Get last score
    lastMaxScore = std::max({
        analysis.back().scores.suppressionScore,
        analysis.back().scores.excitationScore,
        analysis.back().scores.seizureScore
    });
    
    const float slope = lastMaxScore - firstMaxScore;
    
    if (std::fabs(slope) < 5.0f) {
        return "Flat response";
    } else if (slope > 10.0f) {
        return "Increasing response";
    } else if (slope < -10.0f) {
        return "Decreasing response";
    } else {
        return "Moderate trend";
    }
}

// ============================================================
// WINDOW QUALITY ANALYSIS
// ============================================================

std::string analyzeWindowQualityImpl(const std::vector<DoseAnalysis>& analysis) {
    if (analysis.empty()) {
        return "No data";
    }
    
    // Count doses in therapeutic range (suppression or stabilization 20-60%)
    int therapeuticCount = 0;
    int gapCount = 0;
    bool inWindow = false;
    
    for (const auto& pt : analysis) {
        const bool isTherapeutic = 
            (pt.scores.suppressionScore >= 20.0f && pt.scores.suppressionScore <= 60.0f) ||
            (pt.scores.stabilizationScore >= 30.0f);
        
        if (isTherapeutic) {
            if (!inWindow) {
                gapCount++;
            }
            inWindow = true;
            therapeuticCount++;
        } else {
            inWindow = false;
        }
    }
    
    if (therapeuticCount == 0) {
        return "Not observed";
    } else if (gapCount > 1) {
        return "Fragmented";
    } else if (therapeuticCount < 3) {
        return "Narrow";
    } else {
        return "Continuous";
    }
}

// ============================================================
// CHANNEL INTERPRETATION
// ============================================================

std::string interpretChannelEffectImpl(
    float blockNa,
    float blockK,
    float blockCa
) {
    blockNa = clamp01(blockNa);
    blockK = clamp01(blockK);
    blockCa = clamp01(blockCa);
    
    if (blockK > 0.6f && (blockNa < 0.3f && blockCa < 0.3f)) {
        return "Potassium channel blocking";
    } else if (blockNa > 0.6f && (blockK < 0.3f && blockCa < 0.3f)) {
        return "Sodium channel blocking";
    } else if (blockCa > 0.6f && (blockNa < 0.3f && blockK < 0.3f)) {
        return "Calcium channel blocking";
    } else if (blockNa > 0.4f && blockCa > 0.4f) {
        return "Mixed Na/Ca blocking";
    } else if (blockNa > 0.4f && blockK > 0.4f) {
        return "Mixed Na/K blocking";
    } else if (blockK > 0.4f && blockCa > 0.4f) {
        return "Mixed K/Ca blocking";
    } else if (blockNa > 0.4f && blockK > 0.4f && blockCa > 0.4f) {
        return "Broad-spectrum blocking";
    } else {
        return "Unknown/Mixed";
    }
}

std::string trendArrowImpl(float changeValue, float threshold = 0.05f) {
    if (changeValue < -threshold) return "↓";
    if (changeValue > threshold) return "↑";
    return "~";
}

// ============================================================
// DECISION LOGIC
// ============================================================

std::tuple<DrugEvalDecision, std::string, std::string> makeFinalDecisionImpl(
    const BiologicalState& state,
    float toxicityScore
) {
    DrugEvalDecision decision;
    std::string recommendation;
    std::string reason;
    
    // Priority 1: Neural silencing is always NOT RECOMMENDED
    if (state == BiologicalState::NeuralSilencing) {
        decision = DrugEvalDecision::NotRecommended;
        reason = "Neural silencing observed: loss of neural activity";
        recommendation = "NOT RECOMMENDED (HIGH RISK)";
        return {decision, recommendation, reason};
    }
    
    // Priority 2: Hyperexcitability or high seizure risk is NOT RECOMMENDED
    if (state == BiologicalState::Hyperexcitability) {
        decision = DrugEvalDecision::NotRecommended;
        reason = "Hyperexcitability or seizure risk detected";
        recommendation = "NOT RECOMMENDED (HIGH RISK)";
        return {decision, recommendation, reason};
    }
    
    // Priority 3: Network stabilization with low toxicity is PROMISING
    if (state == BiologicalState::NetworkStabilization && toxicityScore < 30.0f) {
        decision = DrugEvalDecision::Promising;
        reason = "Network stabilization observed with low toxicity risk";
        recommendation = "PROMISING (LOW RISK)";
        return {decision, recommendation, reason};
    }
    
    // Priority 4: Controlled suppression without toxicity is CAUTION/PROMISING
    if (state == BiologicalState::ControlledSuppression) {
        if (toxicityScore > 50.0f) {
            decision = DrugEvalDecision::Caution;
            reason = "Therapeutic suppression observed but with toxicity threshold";
            recommendation = "CAUTION (MODERATE RISK)";
        } else {
            decision = DrugEvalDecision::Promising;
            reason = "Controlled suppression without significant toxicity";
            recommendation = "PROMISING (LOW RISK)";
        }
        return {decision, recommendation, reason};
    }
    
    // Default: Limited effect or inconclusive
    decision = DrugEvalDecision::LimitedEfficacy;
    reason = "Limited therapeutic response within tested dose range";
    recommendation = "LIMITED EFFICACY (LOW RISK)";
    
    return {decision, recommendation, reason};
}

} // anonymous namespace

// ============================================================
// PUBLIC METHODS
// ============================================================

BiologicalScores DrugEvaluationEngine::computeBiologicalScores(
    const DoseEvalPoint& point,
    float baselineRate,
    float baselineSync,
    float baselineNii
) {
    return computeBiologicalScoresImpl(point, baselineRate, baselineSync, baselineNii);
}

DrugEvaluationEngine::DoseMilestones DrugEvaluationEngine::analyzeDoseMilestones(
    const std::vector<DoseAnalysis>& analysis
) {
    auto data = analyzeDoseMilestonesImpl(analysis);
    DrugEvaluationEngine::DoseMilestones result;
    result.hasOnset = data.hasOnset;
    result.onsetDose = data.onsetDose;
    result.hasPeak = data.hasPeak;
    result.peakDose = data.peakDose;
    result.hasToxic = data.hasToxic;
    result.toxicDose = data.toxicDose;
    return result;
}

DrugEvaluationReport DrugEvaluationEngine::evaluate(std::vector<DoseEvalPoint> points) {
    DrugEvaluationReport report;
    
    if (points.empty()) {
        return report;
    }
    
    // Sanitize input
    for (auto& p : points) {
        p.dose = sanitizeDose(p.dose);
        p.firingRateHz = sanitizeRate(p.firingRateHz);
        p.synchronizationIndex = sanitizeMetric(p.synchronizationIndex);
        p.nii = sanitizeMetric(p.nii);
        p.seizureRisk = sanitizeMetric(p.seizureRisk);
    }
    
    std::sort(points.begin(), points.end(), [](const DoseEvalPoint& a, const DoseEvalPoint& b) {
        return a.dose < b.dose;
    });
    
    // ---- BASELINE ----
    report.doseMin = points.front().dose;
    report.doseMax = points.back().dose;
    report.doseStep = estimateDoseStep(points);
    report.totalDosePoints = points.size();
    report.baselineFiringRateHz = points.front().firingRateHz;
    report.baselineSynchronization = points.front().synchronizationIndex;
    report.baselineNii = points.front().nii;
    
    const float baselineRate = std::max(1.0e-6f, report.baselineFiringRateHz);
    const float baselineSync = report.baselineSynchronization;
    const float baselineNii = report.baselineNii;
    
    // ---- COMPUTE BIOLOGICAL SCORES FOR ALL DOSES ----
    std::vector<DoseAnalysis> doseAnalysis;
    doseAnalysis.reserve(points.size());
    
    for (const auto& pt : points) {
        DoseAnalysis analysis;
        analysis.dose = pt.dose;
        analysis.scores = computeBiologicalScoresImpl(pt, baselineRate, baselineSync, baselineNii);
        analysis.state = detectDominantStateImpl(analysis.scores, pt.blockK);
        
        auto [toxScore, toxType] = computeToxicityImpl(analysis.scores);
        analysis.toxicityScore = toxScore;
        analysis.toxicityType = toxType;
        
        doseAnalysis.push_back(analysis);
    }
    
    report.dosePoints = doseAnalysis;
    
    // ---- PEAK SCORES ----
    for (const auto& analysis : doseAnalysis) {
        report.peakSuppressionScore = std::max(report.peakSuppressionScore, analysis.scores.suppressionScore);
        report.peakExcitationScore = std::max(report.peakExcitationScore, analysis.scores.excitationScore);
        report.peakSilenceScore = std::max(report.peakSilenceScore, analysis.scores.silenceScore);
        report.peakSeizureScore = std::max(report.peakSeizureScore, analysis.scores.seizureScore);
        report.peakStabilizationScore = std::max(report.peakStabilizationScore, analysis.scores.stabilizationScore);
    }
    
    // ---- DOMINANT STATE (at peak effect point) ----
    std::size_t peakIndex = 0;
    float maxPeakScore = 0.0f;
    for (std::size_t i = 0; i < doseAnalysis.size(); ++i) {
        const float maxScore = std::max({
            doseAnalysis[i].scores.suppressionScore,
            doseAnalysis[i].scores.excitationScore,
            doseAnalysis[i].scores.silenceScore,
            doseAnalysis[i].scores.seizureScore,
            doseAnalysis[i].scores.stabilizationScore
        });
        if (maxScore > maxPeakScore) {
            maxPeakScore = maxScore;
            peakIndex = i;
        }
    }
    
    report.dominantState = doseAnalysis[peakIndex].state;
    report.dominantStateText = toString(report.dominantState);
    
    // ---- DOSE MILESTONES ----
    auto milestonesData = analyzeDoseMilestonesImpl(doseAnalysis);
    report.hasOnsetDose = milestonesData.hasOnset;
    report.onsetDose = milestonesData.onsetDose;
    report.hasPeakDose = milestonesData.hasPeak;
    report.peakDose = milestonesData.peakDose;
    report.hasToxicDose = milestonesData.hasToxic;
    report.toxicDose = milestonesData.toxicDose;
    
    // ---- RESPONSE TRENDS ----
    report.responseTrendText = analyzeResponseTrendImpl(doseAnalysis);
    report.windowAnalysisText = analyzeWindowQualityImpl(doseAnalysis);
    
    // ---- TOXICITY ----
    report.toxicityScore = report.peakSeizureScore > 50.0f ? report.peakSeizureScore :
                           report.peakSilenceScore > 50.0f ? report.peakSilenceScore :
                           report.peakExcitationScore > 50.0f ? report.peakExcitationScore : 0.0f;
    
    if (report.peakSilenceScore > 50.0f) {
        report.toxicityTypeText = "Neural Silencing";
        report.toxicityTriggerText = "Firing rate below 10% of baseline";
    } else if (report.peakExcitationScore > 70.0f) {
        report.toxicityTypeText = "Hyperexcitability";
        report.toxicityTriggerText = "Excessive firing rate increase";
    } else if (report.peakSeizureScore > 70.0f) {
        report.toxicityTypeText = "Seizure Risk";
        report.toxicityTriggerText = "Increased seizure probability or network instability";
    } else {
        report.toxicityTypeText = "None";
        report.toxicityTriggerText = "No significant toxicity observed";
    }
    
    // ---- MECHANISTIC INTERPRETATION ----
    if (!doseAnalysis.empty()) {
        report.dominantChannelEffect = interpretChannelEffectImpl(
            points[peakIndex].blockNa,
            points[peakIndex].blockK,
            points[peakIndex].blockCa
        );
        
        // Trends
        const float rateChange = (points[peakIndex].firingRateHz - baselineRate) / baselineRate;
        const float syncChange = points[peakIndex].synchronizationIndex - baselineSync;
        const float niiChange = points[peakIndex].nii - baselineNii;
        
        report.firingRateTrend = trendArrowImpl(rateChange);
        report.synchronizationTrend = trendArrowImpl(syncChange);
        report.niiTrend = trendArrowImpl(niiChange);
    }
    
    // ---- SAFETY SUMMARY ----
    if (report.toxicityScore > 50.0f) {
        report.safetySummaryText = 
            "Significant " + report.toxicityTypeText + " detected. "
            "Recommend baseline and safety monitoring studies before clinical progression.";
    } else if (report.toxicityScore > 30.0f) {
        report.safetySummaryText = 
            "Moderate toxicity signal. Recommend careful dose titration and monitoring.";
    } else {
        report.safetySummaryText = 
            "No significant toxicity observed within tested dose range. "
            "Safety profile appears favorable for further evaluation.";
    }
    
    // ---- FINAL DECISION ----
    auto [decision, rec, reason] = makeFinalDecisionImpl(
        report.dominantState,
        report.toxicityScore
    );
    
    report.finalDecision = decision;
    report.recommendationText = rec;
    report.reasonText = reason;
    
    if (decision == DrugEvalDecision::NotRecommended) {
        report.riskLevelText = "HIGH";
        report.confidenceText = "HIGH";
    } else if (decision == DrugEvalDecision::Promising) {
        report.riskLevelText = "LOW";
        report.confidenceText = "HIGH";
    } else if (decision == DrugEvalDecision::Caution) {
        report.riskLevelText = "MODERATE";
        report.confidenceText = "MEDIUM";
    } else {
        report.riskLevelText = "LOW";
        report.confidenceText = "MEDIUM";
    }
    
    // ---- PRIMARY OBSERVATION TEXT ----
    switch (report.dominantState) {
        case BiologicalState::NeuralSilencing:
            report.primaryObservationText = 
                "Neural activity suppressed below functional threshold. "
                "Drug causes loss of network responsiveness.";
            break;
        case BiologicalState::Hyperexcitability:
            report.primaryObservationText = 
                "Hyperexcitability phenotype detected. "
                "Drug causes excessive neuronal firing and/or network instability.";
            break;
        case BiologicalState::ControlledSuppression:
            report.primaryObservationText = 
                "Moderate firing rate suppression within expected therapeutic range. "
                "Network maintains baseline responsiveness.";
            break;
        case BiologicalState::NetworkStabilization:
            report.primaryObservationText = 
                "Network stabilization phenotype observed. "
                "Drug reduces synchronization and instability markers.";
            break;
        case BiologicalState::ToxicInstability:
            report.primaryObservationText = 
                "Network shows signs of toxicity: excessive changes in activity or instability.";
            break;
        default:
            report.primaryObservationText = 
                "Limited biological effect observed across tested dose range.";
            break;
    }
    
    return report;
}

// ============================================================
// REPORT FORMATTING
// ============================================================

std::string DrugEvaluationEngine::buildReportText(const DrugEvaluationReport& report) {
    std::ostringstream out;
    
    out << "==================================================\n";
    out << "SILICON PATIENT - DRUG EVALUATION REPORT\n";
    out << "==================================================\n\n";
    
    out << "[Dose Sweep]\n";
    out << "Tested Range        : " << formatRangeText(report.doseMin, report.doseMax) << "\n";
    out << "Step Size           : " << formatNumber(report.doseStep, 3) << "\n";
    out << "Total Points        : " << report.totalDosePoints << "\n\n";
    
    out << "--------------------------------------------------\n\n";
    
    out << "[Biological Response]\n";
    out << "Suppression Score   : " << formatNumber(report.peakSuppressionScore, 1) << " %\n";
    out << "Excitation Score    : " << formatNumber(report.peakExcitationScore, 1) << " %\n";
    out << "Neural Silence Score: " << formatNumber(report.peakSilenceScore, 1) << " %\n";
    out << "Seizure Risk Score  : " << formatNumber(report.peakSeizureScore, 1) << " %\n";
    out << "Stabilization Score : " << formatNumber(report.peakStabilizationScore, 1) << " %\n\n";
    
    out << "Dominant State      : " << report.dominantStateText << "\n\n";
    
    out << "Primary Observation :\n"
        << report.primaryObservationText << "\n\n";
    
    out << "--------------------------------------------------\n\n";
    
    out << "[Dose-Dependent Behavior]\n";
    out << "Onset Dose          : "
        << (report.hasOnsetDose ? formatNumber(report.onsetDose, 3) : "Not observed") << "\n";
    out << "Peak Effect Dose    : "
        << (report.hasPeakDose ? formatNumber(report.peakDose, 3) : "Not observed") << "\n";
    out << "Toxic Dose          : "
        << (report.hasToxicDose ? formatNumber(report.toxicDose, 3) : "Not observed") << "\n\n";
    
    out << "Response Trend      : " << report.responseTrendText << "\n";
    out << "Window Analysis     : " << report.windowAnalysisText << "\n\n";
    
    out << "--------------------------------------------------\n\n";
    
    out << "[Toxicity Analysis]\n";
    out << "Toxicity Score      : " << formatNumber(report.toxicityScore, 1) << " %\n";
    out << "Toxicity Type       : " << report.toxicityTypeText << "\n";
    out << "Toxicity Trigger    : " << report.toxicityTriggerText << "\n\n";
    
    out << "Safety Summary      :\n"
        << report.safetySummaryText << "\n\n";
    
    out << "--------------------------------------------------\n\n";
    
    out << "[Mechanistic Interpretation]\n";
    out << "Dominant Channel    : " << report.dominantChannelEffect << "\n\n";
    out << "Network Impact      :\n";
    out << "  Firing Rate       : " << report.firingRateTrend << "\n";
    out << "  Synchronization   : " << report.synchronizationTrend << "\n";
    out << "  NII               : " << report.niiTrend << "\n\n";
    
    out << "--------------------------------------------------\n\n";
    
    out << "[Final Decision]\n";
    out << "Recommendation      : " << report.recommendationText << "\n";
    out << "Risk Level          : " << report.riskLevelText << "\n";
    out << "Reason              : " << report.reasonText << "\n";
    out << "Confidence          : " << report.confidenceText << "\n\n";
    
    out << "==================================================\n";
    
    return out.str();
}

// ============================================================
// ENUM CONVERSIONS
// ============================================================

std::string DrugEvaluationEngine::toString(CurveBehavior behavior) {
    switch (behavior) {
        case CurveBehavior::Sigmoid:
            return "Sigmoidal";
        case CurveBehavior::WeakSigmoidal:
            return "Weak Sigmoidal";
        case CurveBehavior::Flat:
            return "Flat / Non-sigmoidal";
        case CurveBehavior::NonResponsive:
            return "Non-responsive";
        default:
            return "Unknown";
    }
}

std::string DrugEvaluationEngine::toString(DrugEvalDecision decision) {
    switch (decision) {
        case DrugEvalDecision::Promising:
            return "PROMISING";
        case DrugEvalDecision::Caution:
            return "CAUTION";
        case DrugEvalDecision::LimitedEfficacy:
            return "LIMITED EFFICACY";
        case DrugEvalDecision::NotRecommended:
        default:
            return "NOT RECOMMENDED";
    }
}

std::string DrugEvaluationEngine::toString(BiologicalState state) {
    switch (state) {
        case BiologicalState::LimitedEffect:
            return "Limited Effect";
        case BiologicalState::ControlledSuppression:
            return "Controlled Suppression";
        case BiologicalState::NeuralSilencing:
            return "Neural Silencing";
        case BiologicalState::Hyperexcitability:
            return "Hyperexcitability";
        case BiologicalState::NetworkStabilization:
            return "Network Stabilization";
        case BiologicalState::ToxicInstability:
            return "Toxic Instability";
        default:
            return "Unknown";
    }
}

} // namespace spp::analyzer
