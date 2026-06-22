// This file implements the DrugEvaluationEngine class, which is responsible for
// analyzing dose-response data from neural simulations to evaluate the biological
// effects of a drug. The engine computes various biological scores based on firing
// rates, synchronization, burst activity, and other metrics, and classifies the
// drug's effects into categories such as Controlled Suppression, Neural Silencing,
// Hyperexcitability, etc. It also identifies key doses (onset, peak, toxic) and
// generates a comprehensive report with mechanistic interpretations and a final
// recommendation for the drug's potential as a therapeutic agent.
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
    float baselineNii,
    bool  suppressionReliable   // Bug 3: caller tells us whether suppressionHasBaseline
) {
    BiologicalScores scores;

    const float base = std::max(1.0e-6f, baselineRate);
    const float rateChange = (point.firingRateHz - base) / base;

    // ---- SUPPRESSION ----
    // Bug 3 FIX: when suppression is unreliable (no baseline was passed to
    // Metrics.cpp) we still compute it from the rate ratio — which is all we
    // have — but cap the score at 50 so it cannot single-handedly drive a
    // classification. A comment in the report will flag this via the
    // suppressionReliable field propagated to the caller.
    {
        const float rawSuppressionScore = std::max(0.0f, -rateChange * 100.0f);
        scores.suppressionScore = suppressionReliable
            ? rawSuppressionScore
            : std::min(rawSuppressionScore, 50.0f);
    }

    // ---- EXCITATION ----
    // Bug 7 FIX: clamp to [0, 100]. A 5× rate increase is already extreme;
    // values above 100 make the report table unreadable and confuse threshold
    // comparisons. The raw rateChange is available to callers who need it.
    scores.excitationScore = std::clamp(
        std::max(0.0f, rateChange * 100.0f),
        0.0f, 100.0f
    );

    // ---- NEURAL SILENCING ----
    // Bug 5 FIX: replace the binary 0/100 step with a continuous score so
    // that one simulation-noise tick cannot flip the classification.
    //   at 100 % of baseline → 0
    //   at  10 % of baseline → 50  (old hard threshold)
    //   at   0 % of baseline → 100
    // Formula: silenceScore = clamp(100 * (1 - firingHz / (0.10 * base)), 0, 100)
    // which reaches 100 only when firing is completely silenced.
    {
        const float silenceFraction = 1.0f - (point.firingRateHz / (0.10f * base));
        scores.silenceScore = std::clamp(silenceFraction * 100.0f, 0.0f, 100.0f);
    }

    // ---- SEIZURE RISK ----
    // Bug 1 FIX: burstIndex (primary epileptiform marker, weight 0.50 in
    //   Metrics.cpp seizure score and 0.285 in NII) was completely absent.
    //   Added as a contributing component.
    // Bug 2 FIX: replace max(seizureProb, NII) with a weighted combination.
    //   max() silently discards whichever signal is lower; with independent
    //   markers this throws away real information. Weights mirror the relative
    //   importance assigned in Metrics.cpp:
    //     seizureProbability 0.55 (strongest direct ictal indicator)
    //     nii                0.30 (composite instability)
    //     burstIndex         0.15 (burst contributes but is already inside NII)
    {
        const float seizureProbComp = clamp01(point.seizureRisk);
        const float niiComp         = clamp01(point.nii);
        const float burstComp       = clamp01(point.burstIndex);

        scores.seizureScore = std::clamp(
            (0.55f * seizureProbComp + 0.30f * niiComp + 0.15f * burstComp) * 100.0f,
            0.0f, 100.0f
        );
    }

    // ---- NETWORK STABILIZATION ----
    // Bug 6 FIX: replace the three-tier cliff (50/25/0) with a continuous
    // score. Each of the three improvement signals contributes independently
    // and proportionally, so a drug that partially improves two markers scores
    // between 0 and 100 smoothly.
    //
    // Each component delivers up to 33.3 points:
    //   syncImprovement  = how much sync fell below baseline (0 → 0, -0.20 → 33.3)
    //   niiImprovement   = how much NII fell below baseline
    //   seizureImprovement = how far seizureRisk is below 0.2 (good)
    {
        // Sync improvement: clamp negative change to [0, 0.20], normalise to [0,1]
        const float syncChange = point.synchronizationIndex - baselineSync;
        const float syncImprovNorm = std::clamp(-syncChange / 0.20f, 0.0f, 1.0f);

        // NII improvement
        const float niiChange = point.nii - baselineNii;
        const float niiImprovNorm  = std::clamp(-niiChange  / 0.20f, 0.0f, 1.0f);

        // Seizure improvement: 0 at seizureRisk=0.20, 1 at seizureRisk=0.0
        const float seizureImprovNorm = std::clamp(1.0f - point.seizureRisk / 0.20f, 0.0f, 1.0f);

        scores.stabilizationScore = std::clamp(
            (syncImprovNorm + niiImprovNorm + seizureImprovNorm) * (100.0f / 3.0f),
            0.0f, 100.0f
        );
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
    // Rule 1: Neural silencing takes priority.
    // Bug 5 fix propagated: silenceScore is now continuous, so the threshold
    // 80 means "firing has dropped to ~2% of baseline" — genuinely silent.
    if (scores.silenceScore >= 80.0f) {
        return BiologicalState::NeuralSilencing;
    }

    // Rule 2: K-channel blockade with firing increase → Hyperexcitability.
    if (blockK > 0.5f && scores.excitationScore > 20.0f) {
        return BiologicalState::Hyperexcitability;
    }

    // Bug 4 FIX: ToxicInstability was unreachable because Rule 3
    // (excitationScore > 60 → Hyperexcitability) always fired first whenever
    // seizureScore was high, since high seizure risk co-occurs with excitation.
    // Solution: check the combined toxic signature BEFORE the plain
    // hyperexcitability rule. ToxicInstability requires BOTH seizure risk AND
    // abnormal silence, capturing the specific pattern where the network is
    // simultaneously over-excited AND beginning to fail.
    if (scores.seizureScore > 80.0f && scores.silenceScore > 40.0f) {
        return BiologicalState::ToxicInstability;
    }

    // Rule 3: Pure hyperexcitability or high seizure risk.
    if (scores.excitationScore > 60.0f || scores.seizureScore > 70.0f) {
        return BiologicalState::Hyperexcitability;
    }

    // Rule 4: Network stabilization.
    if (scores.stabilizationScore > 40.0f) {
        return BiologicalState::NetworkStabilization;
    }

    // Rule 5: Controlled suppression (moderate, not silenced).
    if (scores.suppressionScore > 30.0f && scores.suppressionScore < 70.0f) {
        return BiologicalState::ControlledSuppression;
    }

    // Rule 6: Residual toxic instability not caught by Rule 2 above
    // (e.g. extreme silencing score alone).
    if (scores.silenceScore > 50.0f) {
        return BiologicalState::ToxicInstability;
    }

    return BiologicalState::LimitedEffect;
}

// ============================================================
// TOXICITY ENGINE
// ============================================================

// Bug 8 FIX: per-dose and report-level toxicity now use the same formula.
// Previously computeToxicityImpl used max(silence, excitation, seizure) but
// the report-level code used a priority chain that discarded excitation when
// seizure was already > 50. Unified here: toxicity = max of the three harmful
// scores, with the type label set by whichever is dominant.
std::pair<float, std::string> computeToxicityImpl(const BiologicalScores& scores) {
    float toxScore = 0.0f;
    std::string toxType = "None";

    // Evaluate all three harmful signals and keep the worst.
    struct { float score; const char* label; } candidates[] = {
        { scores.silenceScore,    "Neural Silencing"  },
        { scores.excitationScore, "Hyperexcitability" },
        { scores.seizureScore,    "Seizure Risk"      },
    };

    for (const auto& c : candidates) {
        if (c.score > toxScore) {
            toxScore = c.score;
            toxType  = c.label;
        }
    }

    return {toxScore, toxType};
}

// ============================================================
// DOSE MILESTONE DETECTION
// ============================================================

struct DoseMilestonesData {
    bool  hasOnset  = false;
    float onsetDose = 0.0f;
    bool  hasPeak   = false;
    float peakDose  = 0.0f;
    bool  hasToxic  = false;
    float toxicDose = 0.0f;
};

DoseMilestonesData analyzeDoseMilestonesImpl(
    const std::vector<DoseAnalysis>& analysis
) {
    DoseMilestonesData milestones;
    if (analysis.empty()) return milestones;

    float maxScore = 0.0f;

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

        if (maxScoreHere > maxScore) {
            maxScore = maxScoreHere;
            milestones.hasPeak = true;
            milestones.peakDose = pt.dose;
        }

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

// Bug 9 FIX: stabilizationScore is now included in the trend calculation.
// A drug whose primary effect is network stabilization previously showed
// "Flat response" because stabilizationScore was excluded, making the best
// therapeutic outcome indistinguishable from no effect.
std::string analyzeResponseTrendImpl(const std::vector<DoseAnalysis>& analysis) {
    if (analysis.size() < 2U) return "Insufficient data";

    const auto maxSignedScore = [](const DoseAnalysis& pt) {
        // Positive signals (therapeutic/excitatory) vs negative (suppressive).
        // Stabilization is positive; suppression is treated as negative to
        // detect dose-response directionality.
        return std::max({
            pt.scores.excitationScore,
            pt.scores.seizureScore,
            pt.scores.stabilizationScore
        }) - pt.scores.suppressionScore;
    };

    const float firstScore = maxSignedScore(analysis.front());
    const float lastScore  = maxSignedScore(analysis.back());
    const float slope      = lastScore - firstScore;

    if (std::fabs(slope) < 5.0f)  return "Flat response";
    if (slope >  10.0f)           return "Increasing response";
    if (slope < -10.0f)           return "Decreasing response";
    return "Moderate trend";
}

// ============================================================
// WINDOW QUALITY ANALYSIS
// ============================================================

// Bug 10 FIX: the variable was named gapCount but counted therapeutic windows
// (runs), not gaps between them. A single continuous run gave gapCount=1,
// which the old code labelled "Fragmented". Now renamed windowRunCount and
// the condition is corrected: > 1 run means fragmented; exactly 1 run means
// continuous.
std::string analyzeWindowQualityImpl(const std::vector<DoseAnalysis>& analysis) {
    if (analysis.empty()) return "No data";

    int  therapeuticCount = 0;
    int  windowRunCount   = 0;   // Bug 10: was gapCount, logic was inverted
    bool inWindow         = false;

    for (const auto& pt : analysis) {
        const bool isTherapeutic =
            (pt.scores.suppressionScore   >= 20.0f && pt.scores.suppressionScore   <= 60.0f) ||
            (pt.scores.stabilizationScore >= 30.0f);

        if (isTherapeutic) {
            if (!inWindow) {
                ++windowRunCount;   // Start of a new therapeutic run
                inWindow = true;
            }
            ++therapeuticCount;
        } else {
            inWindow = false;
        }
    }

    if (therapeuticCount == 0)  return "Not observed";
    if (windowRunCount   >  1)  return "Fragmented";
    if (therapeuticCount <  3)  return "Narrow";
    return "Continuous";
}

// ============================================================
// CHANNEL INTERPRETATION
// ============================================================

// Bug 11 FIX: broad-spectrum check (all three > 0.4) was unreachable because
// the mixed Na/K check (blockNa > 0.4 && blockK > 0.4) is a subset of it and
// fired first. Broad-spectrum is now checked before any two-channel mixed case.
std::string interpretChannelEffectImpl(float blockNa, float blockK, float blockCa) {
    blockNa = clamp01(blockNa);
    blockK  = clamp01(blockK);
    blockCa = clamp01(blockCa);

    // Broad-spectrum must be tested first — it is the most specific condition.
    if (blockNa > 0.4f && blockK > 0.4f && blockCa > 0.4f) {
        return "Broad-spectrum blocking";
    }

    if (blockK  > 0.6f && blockNa < 0.3f && blockCa < 0.3f) return "Potassium channel blocking";
    if (blockNa > 0.6f && blockK  < 0.3f && blockCa < 0.3f) return "Sodium channel blocking";
    if (blockCa > 0.6f && blockNa < 0.3f && blockK  < 0.3f) return "Calcium channel blocking";
    if (blockNa > 0.4f && blockCa > 0.4f)                   return "Mixed Na/Ca blocking";
    if (blockNa > 0.4f && blockK  > 0.4f)                   return "Mixed Na/K blocking";
    if (blockK  > 0.4f && blockCa > 0.4f)                   return "Mixed K/Ca blocking";

    return "Unknown/Mixed";
}

std::string trendArrowImpl(float changeValue, float threshold = 0.05f) {
    if (changeValue < -threshold) return "↓";
    if (changeValue >  threshold) return "↑";
    return "~";
}

// ============================================================
// DECISION LOGIC
// ============================================================

std::tuple<DrugEvalDecision, std::string, std::string> makeFinalDecisionImpl(
    const BiologicalState& state,
    float toxicityScore,
    std::size_t totalDosePoints   // Bug 12: needed for confidence
) {
    DrugEvalDecision decision;
    std::string recommendation;
    std::string reason;

    if (state == BiologicalState::NeuralSilencing) {
        decision       = DrugEvalDecision::NotRecommended;
        reason         = "Neural silencing observed: loss of neural activity";
        recommendation = "NOT RECOMMENDED (HIGH RISK)";
        return {decision, recommendation, reason};
    }

    if (state == BiologicalState::Hyperexcitability) {
        decision       = DrugEvalDecision::NotRecommended;
        reason         = "Hyperexcitability or seizure risk detected";
        recommendation = "NOT RECOMMENDED (HIGH RISK)";
        return {decision, recommendation, reason};
    }

    if (state == BiologicalState::ToxicInstability) {
        decision       = DrugEvalDecision::NotRecommended;
        reason         = "Toxic network instability: simultaneous excitation and silencing";
        recommendation = "NOT RECOMMENDED (HIGH RISK)";
        return {decision, recommendation, reason};
    }

    if (state == BiologicalState::NetworkStabilization && toxicityScore < 30.0f) {
        decision       = DrugEvalDecision::Promising;
        reason         = "Network stabilization observed with low toxicity risk";
        recommendation = "PROMISING (LOW RISK)";
        return {decision, recommendation, reason};
    }

    if (state == BiologicalState::ControlledSuppression) {
        if (toxicityScore > 50.0f) {
            decision       = DrugEvalDecision::Caution;
            reason         = "Therapeutic suppression observed but with toxicity signal";
            recommendation = "CAUTION (MODERATE RISK)";
        } else {
            decision       = DrugEvalDecision::Promising;
            reason         = "Controlled suppression without significant toxicity";
            recommendation = "PROMISING (LOW RISK)";
        }
        return {decision, recommendation, reason};
    }

    (void)totalDosePoints; // Available for future data-quality weighting.
    decision       = DrugEvalDecision::LimitedEfficacy;
    reason         = "Limited therapeutic response within tested dose range";
    recommendation = "LIMITED EFFICACY (LOW RISK)";
    return {decision, recommendation, reason};
}

// Bug 12 FIX: confidence now reflects data quantity and response quality,
// not just the decision category.
std::string computeConfidenceText(
    DrugEvalDecision decision,
    std::size_t totalDosePoints,
    float peakEffectScore
) {
    // Minimum meaningful dose sweep: < 5 points is low confidence regardless.
    if (totalDosePoints < 5U) return "LOW";

    // Clear, strong effect with enough data → high confidence.
    if (totalDosePoints >= 10U && peakEffectScore >= 50.0f) return "HIGH";

    // Adequate data but weak or ambiguous response.
    if (totalDosePoints >= 5U && peakEffectScore >= 30.0f)  return "MEDIUM";

    // Very limited effect seen even with adequate data — hard to be confident.
    if (decision == DrugEvalDecision::LimitedEfficacy)      return "LOW";

    return "MEDIUM";
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
    // Public overload: suppression reliability unknown, treat as unreliable
    // (conservative — caps suppressionScore at 50 per Bug 3 fix).
    return computeBiologicalScoresImpl(
        point, baselineRate, baselineSync, baselineNii,
        /*suppressionReliable=*/false
    );
}

DrugEvaluationEngine::DoseMilestones DrugEvaluationEngine::analyzeDoseMilestones(
    const std::vector<DoseAnalysis>& analysis
) {
    auto data = analyzeDoseMilestonesImpl(analysis);
    DrugEvaluationEngine::DoseMilestones result;
    result.hasOnset  = data.hasOnset;
    result.onsetDose = data.onsetDose;
    result.hasPeak   = data.hasPeak;
    result.peakDose  = data.peakDose;
    result.hasToxic  = data.hasToxic;
    result.toxicDose = data.toxicDose;
    return result;
}

DrugEvaluationReport DrugEvaluationEngine::evaluate(std::vector<DoseEvalPoint> points) {
    DrugEvaluationReport report;

    if (points.empty()) return report;

    // Sanitize input.
    for (auto& p : points) {
        p.dose                 = sanitizeDose(p.dose);
        p.firingRateHz         = sanitizeRate(p.firingRateHz);
        p.synchronizationIndex = sanitizeMetric(p.synchronizationIndex);
        p.nii                  = sanitizeMetric(p.nii);
        p.seizureRisk          = sanitizeMetric(p.seizureRisk);
        p.burstIndex           = sanitizeMetric(p.burstIndex);  // Bug 1: now sanitized
    }

    std::sort(points.begin(), points.end(), [](const DoseEvalPoint& a, const DoseEvalPoint& b) {
        return a.dose < b.dose;
    });

    // ---- BASELINE ----
    report.doseMin             = points.front().dose;
    report.doseMax             = points.back().dose;
    report.doseStep            = estimateDoseStep(points);
    report.totalDosePoints     = points.size();
    report.baselineFiringRateHz    = points.front().firingRateHz;
    report.baselineSynchronization = points.front().synchronizationIndex;
    report.baselineNii             = points.front().nii;

    const float baselineRate = std::max(1.0e-6f, report.baselineFiringRateHz);
    const float baselineSync = report.baselineSynchronization;
    const float baselineNii  = report.baselineNii;

    // Bug 3: determine suppression reliability from the first point's flag.
    // If the caller did not provide a NetworkMetrics baseline to Metrics.cpp,
    // suppressionHasBaseline will be false on every point. We read it from the
    // first dose point and use it consistently across the whole sweep.
    const bool suppressionReliable =
        !points.empty() && points.front().suppressionHasBaseline;

    // ---- COMPUTE BIOLOGICAL SCORES FOR ALL DOSES ----
    std::vector<DoseAnalysis> doseAnalysis;
    doseAnalysis.reserve(points.size());

    for (const auto& pt : points) {
        DoseAnalysis analysis;
        analysis.dose   = pt.dose;
        analysis.scores = computeBiologicalScoresImpl(
            pt, baselineRate, baselineSync, baselineNii,
            suppressionReliable
        );
        analysis.state = detectDominantStateImpl(analysis.scores, pt.blockK);

        auto [toxScore, toxType]  = computeToxicityImpl(analysis.scores);
        analysis.toxicityScore    = toxScore;
        analysis.toxicityType     = toxType;

        doseAnalysis.push_back(analysis);
    }

    report.dosePoints = doseAnalysis;

    // ---- PEAK SCORES ----
    for (const auto& analysis : doseAnalysis) {
        report.peakSuppressionScore    = std::max(report.peakSuppressionScore,    analysis.scores.suppressionScore);
        report.peakExcitationScore     = std::max(report.peakExcitationScore,     analysis.scores.excitationScore);
        report.peakSilenceScore        = std::max(report.peakSilenceScore,        analysis.scores.silenceScore);
        report.peakSeizureScore        = std::max(report.peakSeizureScore,        analysis.scores.seizureScore);
        report.peakStabilizationScore  = std::max(report.peakStabilizationScore,  analysis.scores.stabilizationScore);
    }

    // ---- DOMINANT STATE (at peak effect point) ----
    std::size_t peakIndex    = 0;
    float       maxPeakScore = 0.0f;
    for (std::size_t i = 0; i < doseAnalysis.size(); ++i) {
        const float s = std::max({
            doseAnalysis[i].scores.suppressionScore,
            doseAnalysis[i].scores.excitationScore,
            doseAnalysis[i].scores.silenceScore,
            doseAnalysis[i].scores.seizureScore,
            doseAnalysis[i].scores.stabilizationScore
        });
        if (s > maxPeakScore) {
            maxPeakScore = s;
            peakIndex    = i;
        }
    }

    report.dominantState     = doseAnalysis[peakIndex].state;
    report.dominantStateText = toString(report.dominantState);

    // ---- DOSE MILESTONES ----
    auto milestonesData    = analyzeDoseMilestonesImpl(doseAnalysis);
    report.hasOnsetDose    = milestonesData.hasOnset;
    report.onsetDose       = milestonesData.onsetDose;
    report.hasPeakDose     = milestonesData.hasPeak;
    report.peakDose        = milestonesData.peakDose;
    report.hasToxicDose    = milestonesData.hasToxic;
    report.toxicDose       = milestonesData.toxicDose;

    // ---- RESPONSE TRENDS ----
    report.responseTrendText  = analyzeResponseTrendImpl(doseAnalysis);
    report.windowAnalysisText = analyzeWindowQualityImpl(doseAnalysis);

    // ---- TOXICITY (unified formula — Bug 8 FIX) ----
    // Report-level toxicity now uses the same max() formula as per-dose
    // computeToxicityImpl, so the two are always consistent.
    report.toxicityScore = std::max({
        report.peakSilenceScore    > 0.0f ? report.peakSilenceScore    : 0.0f,
        report.peakExcitationScore > 0.0f ? report.peakExcitationScore : 0.0f,
        report.peakSeizureScore    > 0.0f ? report.peakSeizureScore    : 0.0f
    });

    if (report.peakSeizureScore >= report.peakSilenceScore &&
        report.peakSeizureScore >= report.peakExcitationScore &&
        report.peakSeizureScore > 0.0f) {
        report.toxicityTypeText    = "Seizure Risk";
        report.toxicityTriggerText = "Increased seizure probability or network instability";
    } else if (report.peakSilenceScore >= report.peakExcitationScore &&
               report.peakSilenceScore > 0.0f) {
        report.toxicityTypeText    = "Neural Silencing";
        report.toxicityTriggerText = "Firing rate suppressed toward zero";
    } else if (report.peakExcitationScore > 0.0f) {
        report.toxicityTypeText    = "Hyperexcitability";
        report.toxicityTriggerText = "Excessive firing rate increase";
    } else {
        report.toxicityTypeText    = "None";
        report.toxicityTriggerText = "No significant toxicity observed";
    }

    // ---- MECHANISTIC INTERPRETATION ----
    if (!doseAnalysis.empty()) {
        report.dominantChannelEffect = interpretChannelEffectImpl(
            points[peakIndex].blockNa,
            points[peakIndex].blockK,
            points[peakIndex].blockCa
        );

        const float rateChange = (points[peakIndex].firingRateHz - baselineRate) / baselineRate;
        const float syncChange = points[peakIndex].synchronizationIndex - baselineSync;
        const float niiChange  = points[peakIndex].nii - baselineNii;

        report.firingRateTrend      = trendArrowImpl(rateChange);
        report.synchronizationTrend = trendArrowImpl(syncChange);
        report.niiTrend             = trendArrowImpl(niiChange);
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

    if (!suppressionReliable) {
        report.safetySummaryText +=
            " [Note: suppression scores computed without a drug-free baseline — "
            "provide NetworkMetrics baseline to Metrics.cpp for accurate suppression analysis.]";
    }

    // ---- FINAL DECISION ----
    auto [decision, rec, reason] = makeFinalDecisionImpl(
        report.dominantState,
        report.toxicityScore,
        report.totalDosePoints
    );

    report.finalDecision       = decision;
    report.recommendationText  = rec;
    report.reasonText          = reason;

    // Bug 12 FIX: confidence reflects data quantity and response strength.
    const float peakEffectScore = std::max({
        report.peakSuppressionScore,
        report.peakExcitationScore,
        report.peakSeizureScore,
        report.peakStabilizationScore
    });
    report.confidenceText = computeConfidenceText(
        decision,
        report.totalDosePoints,
        peakEffectScore
    );

    if (decision == DrugEvalDecision::NotRecommended) {
        report.riskLevelText = "HIGH";
    } else if (decision == DrugEvalDecision::Promising) {
        report.riskLevelText = "LOW";
    } else if (decision == DrugEvalDecision::Caution) {
        report.riskLevelText = "MODERATE";
    } else {
        report.riskLevelText = "LOW";
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
                "Network shows signs of toxic instability: simultaneous excitation "
                "and suppression of activity with elevated seizure risk.";
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
    out << "Suppression Score   : " << formatNumber(report.peakSuppressionScore,   1) << " %\n";
    out << "Excitation Score    : " << formatNumber(report.peakExcitationScore,    1) << " %\n";
    out << "Neural Silence Score: " << formatNumber(report.peakSilenceScore,       1) << " %\n";
    out << "Seizure Risk Score  : " << formatNumber(report.peakSeizureScore,       1) << " %\n";
    out << "Stabilization Score : " << formatNumber(report.peakStabilizationScore, 1) << " %\n\n";

    out << "Dominant State      : " << report.dominantStateText << "\n\n";

    out << "Primary Observation :\n"
        << report.primaryObservationText << "\n\n";

    out << "--------------------------------------------------\n\n";

    out << "[Dose-Dependent Behavior]\n";
    out << "Onset Dose          : "
        << (report.hasOnsetDose ? formatNumber(report.onsetDose, 3) : "Not observed") << "\n";
    out << "Peak Effect Dose    : "
        << (report.hasPeakDose  ? formatNumber(report.peakDose,  3) : "Not observed") << "\n";
    out << "Toxic Dose          : "
        << (report.hasToxicDose ? formatNumber(report.toxicDose, 3) : "Not observed") << "\n\n";

    out << "Response Trend      : " << report.responseTrendText  << "\n";
    out << "Window Analysis     : " << report.windowAnalysisText << "\n\n";

    out << "--------------------------------------------------\n\n";

    out << "[Toxicity Analysis]\n";
    out << "Toxicity Score      : " << formatNumber(report.toxicityScore, 1) << " %\n";
    out << "Toxicity Type       : " << report.toxicityTypeText    << "\n";
    out << "Toxicity Trigger    : " << report.toxicityTriggerText << "\n\n";

    out << "Safety Summary      :\n"
        << report.safetySummaryText << "\n\n";

    out << "--------------------------------------------------\n\n";

    out << "[Mechanistic Interpretation]\n";
    out << "Dominant Channel    : " << report.dominantChannelEffect << "\n\n";
    out << "Network Impact      :\n";
    out << "  Firing Rate       : " << report.firingRateTrend      << "\n";
    out << "  Synchronization   : " << report.synchronizationTrend << "\n";
    out << "  NII               : " << report.niiTrend             << "\n\n";

    out << "--------------------------------------------------\n\n";

    out << "[Final Decision]\n";
    out << "Recommendation      : " << report.recommendationText << "\n";
    out << "Risk Level          : " << report.riskLevelText      << "\n";
    out << "Reason              : " << report.reasonText         << "\n";
    out << "Confidence          : " << report.confidenceText     << "\n\n";

    out << "==================================================\n";

    return out.str();
}

// ============================================================
// ENUM CONVERSIONS
// ============================================================

std::string DrugEvaluationEngine::toString(CurveBehavior behavior) {
    switch (behavior) {
        case CurveBehavior::Sigmoid:        return "Sigmoidal";
        case CurveBehavior::WeakSigmoidal:  return "Weak Sigmoidal";
        case CurveBehavior::Flat:           return "Flat / Non-sigmoidal";
        case CurveBehavior::NonResponsive:  return "Non-responsive";
        default:                            return "Unknown";
    }
}

std::string DrugEvaluationEngine::toString(DrugEvalDecision decision) {
    switch (decision) {
        case DrugEvalDecision::Promising:       return "PROMISING";
        case DrugEvalDecision::Caution:         return "CAUTION";
        case DrugEvalDecision::LimitedEfficacy: return "LIMITED EFFICACY";
        case DrugEvalDecision::NotRecommended:
        default:                                return "NOT RECOMMENDED";
    }
}

std::string DrugEvaluationEngine::toString(BiologicalState state) {
    switch (state) {
        case BiologicalState::LimitedEffect:        return "Limited Effect";
        case BiologicalState::ControlledSuppression:return "Controlled Suppression";
        case BiologicalState::NeuralSilencing:      return "Neural Silencing";
        case BiologicalState::Hyperexcitability:    return "Hyperexcitability";
        case BiologicalState::NetworkStabilization: return "Network Stabilization";
        case BiologicalState::ToxicInstability:     return "Toxic Instability";
        default:                                    return "Unknown";
    }
}

} // namespace spp::analyzer