#include "PharmaDecisionEngine.h"
#include "NetworkAnalyzer.h"
#include "AnalyzedDose.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace spp::analyzer {

namespace {

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
double clamp01d(double v) { return std::clamp(v, 0.0, 1.0); }

float safeNonNegative(float v) {
    return (!std::isfinite(v) || v < 0.0f) ? 0.0f : v;
}
double safeNonNegativeD(double v) {
    return (!std::isfinite(v) || v < 0.0) ? 0.0 : v;
}

// ─── Risk tier ───────────────────────────────────────────────────────────────
DrugRiskTier classifyTier(float seizureProb, float suppressionScore, float riskScore) {
    if (seizureProb > 0.80f || suppressionScore > 0.80f || riskScore >= 80.0f)
        return DrugRiskTier::Toxic;
    if (riskScore >= 65.0f) return DrugRiskTier::HighRisk;
    if (riskScore >= 35.0f) return DrugRiskTier::ModerateRisk;
    return DrugRiskTier::Safe;
}

// ─── Contiguous range helper ──────────────────────────────────────────────────
struct Range { double lo = 0.0, hi = 0.0; std::size_t points = 0U; };

std::vector<Range> contiguousRanges(const std::vector<double>& doses, double step) {
    std::vector<Range> out;
    if (doses.empty()) return out;
    const double expected = std::max(1.0e-6, step);
    Range current{doses.front(), doses.front(), 1U};
    for (std::size_t i = 1; i < doses.size(); ++i) {
        if (doses[i] - doses[i-1U] <= 2.0 * expected + 1.0e-6) {
            current.hi = doses[i]; ++current.points;
        } else {
            out.push_back(current);
            current = {doses[i], doses[i], 1U};
        }
    }
    out.push_back(current);
    return out;
}

// ─── Sigmoid R² fit ───────────────────────────────────────────────────────────
double computeBestSigmoidR2(
    const std::vector<double>& dose,
    const std::vector<double>& effect01)
{
    if (dose.size() != effect01.size() || dose.size() < 4U) return 0.0;
    const auto [dminIt, dmaxIt] = std::minmax_element(dose.begin(), dose.end());
    const double dMin = *dminIt, dMax = *dmaxIt;
    double meanY = 0.0;
    for (double y : effect01) meanY += y;
    meanY /= static_cast<double>(effect01.size());
    double sst = 0.0;
    for (double y : effect01) { double dy = y - meanY; sst += dy*dy; }
    if (sst <= 1.0e-12) return 1.0;
    double bestSse = std::numeric_limits<double>::infinity();
    // BUG FIX (Phase 2 receptor-drug validation): this used to be
    // std::max(0.25, (dMax-dMin)/120.0) -- an absolute floor of 0.25 dose
    // units on the d50 grid-search step. That was fine for Phase 1's
    // channel drugs, whose dose ranges run tens to hundreds of units wide,
    // but Phase 2's real-pharmacology receptor drugs (e.g. perampanel,
    // IC50=0.063uM, dose range 0-0.3) have dose ranges *narrower* than the
    // 0.25 floor itself. That collapsed the d50 search to just 1-2
    // candidate midpoints across the ENTIRE dose range instead of the
    // intended ~120, producing a near-worthless sigmoid fit (R^2 ~0.3) on
    // data that dose_response.csv showed was actually a smooth, clearly
    // graded 52% suppression curve. Fix: scale purely off the dose range
    // (still ~120 candidate midpoints regardless of unit scale), with only
    // a degenerate-range guard (dMax==dMin) instead of an absolute floor.
    const double doseRange = dMax - dMin;
    const double dStep = (doseRange > 1.0e-9) ? (doseRange / 120.0) : 1.0;
    // SECOND INSTANCE OF THE SAME BUG CLASS: k (sigmoid steepness) used to
    // sweep 0.02 to 2.51 in ABSOLUTE 1/dose units, same hardcoded-for-
    // Phase-1-ranges assumption as the old dStep floor. A logistic
    // transitions from ~10% to ~90% over a width of ~4.39/k dose units, so
    // fixed absolute k bounds only span a sensible range of *relative*
    // steepness (k*doseRange) for dose ranges in roughly the same
    // tens-to-hundreds-of-units ballpark Phase 1 always used. Confirmed
    // broken for felbamate (dose range 0-3500uM): even at k's old minimum
    // (0.02), the transition width is only ~220 units -- 6% of the range,
    // forcing an artificially steep/narrow fit onto a response that's
    // genuinely spread smoothly across the whole range (same relative
    // dose:IC50 shape as ketamine/memantine, which fit at R^2 0.94-0.95;
    // felbamate got R^2 0.78 on an equivalent curve purely from this).
    // Fix: sweep k in RELATIVE terms (k*doseRange, dimensionless) instead
    // of absolute units, so "how sharp is the transition relative to the
    // tested dose range" is what's searched, independent of whether that
    // range is 0.3uM or 3500uM.
    const double safeDoseRange = (doseRange > 1.0e-9) ? doseRange : 1.0;
    for (double kRel = 0.05; kRel <= 12.0; kRel += 0.05) {
        const double k = kRel / safeDoseRange;
        for (double d50 = dMin; d50 <= dMax + 1.0e-9; d50 += dStep) {
            double numer = 0.0, denom = 0.0;
            for (std::size_t i = 0; i < dose.size(); ++i) {
                const double z = std::clamp(-k*(dose[i]-d50), -60.0, 60.0);
                const double s = 1.0/(1.0+std::exp(z));
                numer += effect01[i]*s; denom += s*s;
            }
            if (denom <= 1.0e-12) continue;
            const double emax = std::clamp(numer/denom, 0.0, 1.0);
            double sse = 0.0;
            for (std::size_t i = 0; i < dose.size(); ++i) {
                const double z = std::clamp(-k*(dose[i]-d50), -60.0, 60.0);
                const double s = 1.0/(1.0+std::exp(z));
                const double err = effect01[i] - emax*s;
                sse += err*err;
            }
            bestSse = std::min(bestSse, sse);
        }
    }
    if (!std::isfinite(bestSse)) return 0.0;
    return std::clamp(1.0 - bestSse/(sst+1.0e-12), -1.0, 1.0);
}

// ─── Confidence ───────────────────────────────────────────────────────────────
std::string computeConfidence(
    NetworkState state,
    double sigmoidR2,
    bool therapeuticWindowExists,
    bool hasContinuousWindow,
    bool fragmentedWindow,
    float rateVariability,
    float toxicityVariability,
    double maxEffectPct,
    float peakRiskScore,
    float peakSeizureProb,
    int runCount)
{
    double score = 0.0;

    const bool isClear = (state != NetworkState::Stable);
    const bool isTherapeutic = (state == NetworkState::Stable); // handled below
    const bool isDanger = (state == NetworkState::SeizureActive  ||
                           state == NetworkState::SeizureRisk    ||
                           state == NetworkState::NeuralSuppression ||
                           state == NetworkState::DepolarizationBlock ||
                           state == NetworkState::Hyperexcitable);

    if (isClear) { score += 30.0; if (isDanger) score += 10.0; }
    else { score += (maxEffectPct > 10.0) ? 8.0 : 2.0; }

    if (isDanger) {
        if      (peakRiskScore >= 70.0f || peakSeizureProb >= 0.70f) score += 20.0;
        else if (peakRiskScore >= 50.0f || peakSeizureProb >= 0.50f) score += 15.0;
        else                                                           score += 10.0;
    } else {
        score += (maxEffectPct > 20.0) ? 10.0 : (maxEffectPct > 10.0) ? 5.0 : 0.0;
    }

    if      (sigmoidR2 >= 0.95) score += 20.0;
    else if (sigmoidR2 >= 0.90) score += 17.0;
    else if (sigmoidR2 >= 0.85) score += 14.0;
    else if (sigmoidR2 >= 0.75) score +=  9.0;
    else if (sigmoidR2 >= 0.65) score +=  5.0;

    if      (therapeuticWindowExists && hasContinuousWindow && !fragmentedWindow) score += 10.0;
    else if (therapeuticWindowExists && hasContinuousWindow)                       score +=  8.0;
    else if (therapeuticWindowExists)                                              score +=  5.0;
    else if (isDanger)                                                             score +=  5.0;

    if      (rateVariability < 1.0f  && toxicityVariability <  5.0f) score += 10.0;
    else if (rateVariability < 1.5f  && toxicityVariability <  7.5f) score +=  8.0;
    else if (rateVariability < 2.0f  && toxicityVariability < 10.0f) score +=  5.0;
    else                                                               score +=  2.0;

    const double clamped = std::clamp(score, 0.0, 100.0);
    if (runCount < 2) {
        if (clamped >= 80.0) return "HIGH";
        if (clamped >= 70.0) return "MEDIUM";
        return "LOW";
    }
    if (clamped >= 80.0) return "HIGH";
    if (clamped >= 50.0) return "MEDIUM";
    return "LOW";
}

// ─── Narrative generation ─────────────────────────────────────────────────────
std::string buildNarrative(
    const AnalyzedDose& peakDose,
    MechanismSignature mechanism,
    const std::string& responseMode,
    double maxEffectPct)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);

    if (responseMode == "EXCITATORY_RESPONSE") {
        // Gap 1.3 (PRECISION_GAP_CLOSURE_PLAN.md 1.3): "Burst rate increased by
        // X Hz from baseline" removed -- burstRateDelta is permanently ~0 in
        // this network's firing regime (see NetworkAnalyzer.cpp classifyState
        // comment), so this clause always read "0.0 Hz" regardless of the
        // real excitatory effect being described.
        oss << "Irregularity index rose by "
            << std::max(0.0f, peakDose.irregularityDelta) << " units. ";
        if (peakDose.rateChangePct > 5.0f)
            oss << "Firing rate increased " << peakDose.rateChangePct << "% from baseline. ";
        // Same magnitude gate as Step 9's `excitatory` bool below (maxEffectPct
        // > 10.0) -- responseMode alone is categorical (see comment on
        // `excitatory` in evaluate()). Without this, a 2% Nimodipine wobble
        // printed "Network excitability exceeded safe stability limits"
        // directly above a LOW RISK / LIMITED EFFICACY final decision.
        if (maxEffectPct > 10.0)
            oss << "Network excitability exceeded safe stability limits.";
        else
            oss << "Effect magnitude was minimal and did not represent a safety concern.";

    } else if (responseMode == "STABILIZING_RESPONSE") {
        // Gap 1.3: "Burst duration shortened by X ms" removed -- same dead-
        // metric finding, meanBurstDurationMs is 0 whenever burstRateHz is 0.
        oss << "Synchronization reduced by " << peakDose.syncReductionPct << "% from baseline. "
            << "Firing rate remained within viable range ("
            << (peakDose.rateChangePct >= 0 ? "+" : "")
            << peakDose.rateChangePct << "% from baseline).";

    } else if (responseMode == "SUPPRESSIVE_RESPONSE") {
        oss << "Firing rate reduced by "
            << std::fabs(std::min(0.0f, peakDose.rateChangePct)) << "% from baseline. "
            << "Silent neuron fraction increased by "
            << std::max(0.0f, peakDose.silentNeuronDelta) << "%.";

    } else {
        oss << "No biologically significant change detected across the tested dose range.";
    }

    (void)mechanism;
    return oss.str();
}

std::string buildSafetyText(
    NetworkState dominantState,
    bool toxicityBeforeTherapy,
    bool toxicityAfterTherapy,
    bool lowStability,
    bool excitatoryVerdict,
    bool narrowMarginCarveOut,
    double safetyMarginRatio)
{
    // Checked before the dominantState switch below: a real, if narrow,
    // safety margin changes the honest description even when the worst
    // per-dose state is severe (e.g. Neural Suppression at 1.5x the top of
    // the therapeutic window). Without this, drugs like phenytoin printed
    // "Complete suppression of neural activity is incompatible with
    // therapeutic use" -- true of the toxic dose, false as a description of
    // the drug's overall safety profile, which does have a usable window.
    if (narrowMarginCarveOut) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1)
            << "A real but narrow therapeutic window was identified. Dangerous "
               "network states were observed only at roughly " << safetyMarginRatio
            << "x the top of that window, not within it. This is not an immediate "
               "safety failure at therapeutic doses, but the margin for dosing "
               "error is small and careful titration is required.";
        return oss.str();
    }
    switch (dominantState) {
        case NetworkState::NeuralSuppression:
            return "Complete suppression of neural activity is incompatible with "
                   "therapeutic use.";
        case NetworkState::Hyperexcitable:
            return "Observed increases in firing rate, burst activity, or irregularity "
                   "represent an unacceptable safety risk.";
        case NetworkState::SeizureActive:
        case NetworkState::SeizureRisk:
            return "Seizure-risk markers escalated to dangerous levels within the "
                   "tested dose range.";
        case NetworkState::DepolarizationBlock:
            return "Depolarization block pattern detected; network activity collapsed "
                   "following initial excitation.";
        default:
            // FIX: this switch used to key ONLY on per-dose worst network
            // state (dominantState) and toxic-threshold timing, completely
            // independent of responseMode/the recommendation logic below.
            // A drug can earn EXCITATORY_RESPONSE / NOT RECOMMENDED / HIGH
            // RISK from cumulative evidence without any single dose ever
            // reaching a severe per-dose state (stays "Stable" throughout) --
            // e.g. 4-Aminopyridine. That fell through to a "therapeutic
            // window" / "favorable" message that directly contradicted the
            // verdict printed two lines below it. Checking the same
            // excitatory signal the recommendation uses closes that gap.
            if (excitatoryVerdict)
                return "Observed increases in firing rate, burst activity, or irregularity "
                       "represent an unacceptable safety risk.";
            if (toxicityBeforeTherapy)
                return "Toxicity markers appeared before the therapeutic window; "
                       "dose-response ordering is unsafe.";
            if (toxicityAfterTherapy)
                return "Therapeutic window exists but is narrow; careful dose titration required.";
            if (lowStability)
                return "Therapeutic window present but inter-run variability reduces confidence.";
            return "No significant toxicity observed within the tested dose range. "
                   "Safety profile appears favorable.";
    }
}

} // anonymous namespace

// ─── riskLevelText ────────────────────────────────────────────────────────────
std::string riskLevelText(DrugRiskTier tier) {
    switch (tier) {
        case DrugRiskTier::Safe:         return "LOW";
        case DrugRiskTier::ModerateRisk: return "MODERATE";
        default:                          return "HIGH";
    }
}

// ════════════════════════════════════════════════════════════════════════════
// PharmaDecisionEngine::evaluate
// Input: AnalyzedDose[] from NetworkAnalyzer (already has deltas + scores + state)
// ════════════════════════════════════════════════════════════════════════════

PharmaDecisionReport PharmaDecisionEngine::evaluate(
    const std::vector<AnalyzedDose>& analyzedDoses,
    const DecisionStabilityInput& stabilityInput)
{
    PharmaDecisionReport report;
    if (analyzedDoses.empty()) return report;

    // Sort by dose ascending
    std::vector<AnalyzedDose> sorted = analyzedDoses;
    std::sort(sorted.begin(), sorted.end(),
        [](const AnalyzedDose& a, const AnalyzedDose& b){ return a.dose < b.dose; });

    report.minTestedDose = safeNonNegativeD(sorted.front().dose);
    report.maxTestedDose = safeNonNegativeD(sorted.back().dose);
    report.rateVariability     = std::max(0.0f, stabilityInput.rateStd);
    report.toxicityVariability = std::max(0.0f, stabilityInput.toxicityStd);
    report.stabilityScore      = stabilityInput.stabilityScore.empty()
                                   ? "UNSPECIFIED" : stabilityInput.stabilityScore;

    double inferredStep = 0.0;
    for (std::size_t i = 1; i < sorted.size(); ++i) {
        const double dx = safeNonNegativeD(sorted[i].dose) - safeNonNegativeD(sorted[i-1].dose);
        if (dx > 1.0e-6)
            inferredStep = (inferredStep <= 0.0) ? dx : std::min(inferredStep, dx);
    }
    report.stepDose = inferredStep;

    // ─── Step 1: Evidence accumulation ───────────────────────────────────────
    // All scores already computed by NetworkAnalyzer. Just sum them up.
    double cumulativeSuppression    = 0.0;
    double cumulativeExcitation     = 0.0;
    double cumulativeStabilization  = 0.0;
    double cumulativeToxicity       = 0.0;
    double maxEffectPct             = 0.0;
    // Gap 1.1 audit fix (cocaine): tracks WHICH dose produced maxEffectPct,
    // so the report can pair the percentage with the dose that actually
    // caused it instead of an unrelated composite-score peak dose. See
    // PharmaDecisionEngine.h's decisionMaxEffectDoseIdx comment.
    std::size_t maxEffectDoseIdx    = 0;

    std::vector<double> xDose;
    std::vector<double> suppressionCurve;
    std::vector<double> excitationCurve;
    std::vector<double> stabilizationCurve;
    std::vector<double> therapeuticDoses;
    std::vector<double> safeDoses;
    std::vector<double> excitatoryRiskDoses;
    std::vector<double> overSuppressionDoses;

    bool hasOnsetDose    = false;
    bool sawToxicEffect  = false;
    double onsetDose     = 0.0;

    report.points.reserve(sorted.size());
    report.analyzedDoses = sorted;

    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const AnalyzedDose& dose = sorted[i];

        cumulativeSuppression   += static_cast<double>(dose.suppressionScore);
        cumulativeExcitation    += static_cast<double>(dose.excitabilityScore);
        cumulativeStabilization += static_cast<double>(dose.stabilizationScore);
        cumulativeToxicity      += static_cast<double>(dose.seizureProbability);

        // Effect magnitude for curve fitting
        const double suppEff  = std::max(0.0, -static_cast<double>(dose.rateChangePct) / 100.0);
        const double exciEff  = std::max(0.0,  static_cast<double>(dose.rateChangePct) / 100.0);
        const double stabilEff= static_cast<double>(dose.stabilizationScore);
        const double effMag   = std::clamp(std::max({suppEff, exciEff, stabilEff}), 0.0, 1.0);
        if (effMag * 100.0 > maxEffectPct) {
            maxEffectPct = effMag * 100.0;
            maxEffectDoseIdx = i;
        }

        xDose.push_back(static_cast<double>(dose.dose));
        suppressionCurve.push_back(suppEff);
        excitationCurve.push_back(exciEff);
        stabilizationCurve.push_back(stabilEff);

        // Risk scoring per dose
        const float seizureProb  = std::clamp(dose.seizureProbability, 0.0f, 1.0f);
        const float suppScore    = std::clamp(dose.suppressionScore,   0.0f, 1.0f);
        const float riskScore    = 100.0f * std::clamp(
            0.55f * seizureProb + 0.30f * static_cast<float>(dose.nii) +
            0.15f * suppScore, 0.0f, 1.0f);

        const DrugRiskTier tier = classifyTier(seizureProb, suppScore, riskScore);

        float seizureSlopePctPerDose = 0.0f;
        if (i > 0U) {
            const float deltaDose = std::max(1.0e-6f, dose.dose - sorted[i-1U].dose);
            seizureSlopePctPerDose =
                (dose.seizureProbability - sorted[i-1U].seizureProbability) / deltaDose;
        }

        const float earlyWarningIndex = 100.0f * std::clamp(
            0.55f * seizureProb +
            0.30f * std::clamp(std::max(0.0f, seizureSlopePctPerDose) / 25.0f, 0.0f, 1.0f) +
            0.15f * std::clamp(static_cast<float>(dose.nii), 0.0f, 1.0f),
            0.0f, 1.0f);

        report.points.push_back(DrugDecisionPoint{
            dose.dose,
            dose.seizureProbability * 100.0f,
            dose.suppressionScore  * 100.0f,
            riskScore,
            toString(tier),
            earlyWarningIndex,
            seizureSlopePctPerDose
        });

        report.peakRiskScore             = std::max(report.peakRiskScore, riskScore);
        report.peakSeizureProbabilityPct = std::max(report.peakSeizureProbabilityPct,
                                                      dose.seizureProbability * 100.0f);
        report.peakSuppressionPct        = std::max(report.peakSuppressionPct,
                                                      dose.suppressionScore * 100.0f);
        report.peakEarlyWarningIndex     = std::max(report.peakEarlyWarningIndex, earlyWarningIndex);
        report.maxSeizureSlopePctPerDose = std::max(report.maxSeizureSlopePctPerDose,
                                                      seizureSlopePctPerDose);

        // Classify dose band
        // Phase 3c fix: originally only suppressionScore/stabilizationScore
        // could mark a dose "effective" -- both are structurally zero for
        // any excitatory response (suppressionScore only computes when rate
        // DROPS; stabilizationScore requires sync/burst-duration
        // improvement). That made it impossible for a genuinely beneficial
        // excitatory drug (D1/D2/5-HT1A/5-HT2A neuromodulator gain is often
        // *supposed* to be mildly excitatory -- arousal, cognition,
        // wakefulness) to ever register a therapeutic window, no matter how
        // real or clean the effect: e.g. a D1 agonist test showed a real,
        // correctly-signed +9.7% rate increase, Stable state, R^2=0.68, yet
        // fell through to "LIMITED EFFICACY / no meaningful biological
        // response". Added a third OR branch: rateChangePct > 5.0f while
        // still in Stable/MildInstability state. The state gate already
        // excludes Hyperexcitable/SeizureRisk/etc (those are mutually
        // exclusive NetworkState values, see classifyState), and Step 9's
        // `excitatory` bool (maxEffectPct > 10.0 && EXCITATORY_RESPONSE)
        // still overrides this with NOT RECOMMENDED for any genuinely
        // dangerous excitatory drug -- this only affects the safe,
        // mild-magnitude (5-10%) excitatory case that was previously
        // invisible to the decision engine entirely. 5% floor is half of
        // that existing 10% "notable effect" threshold, i.e. deliberately
        // more permissive for what counts as a real signal worth reporting
        // than what counts as dangerous.
        const bool isEffective =
            (dose.networkState == NetworkState::MildInstability ||
             dose.networkState == NetworkState::Stable) &&
            (dose.suppressionScore > 0.15f || dose.stabilizationScore > 0.10f ||
             dose.rateChangePct > 5.0f);
        // Publish the single source of truth onto the report's copy of this
        // dose (see AnalyzedDose.h's isEffective comment) so main.cpp's
        // Dose Classification Summary doesn't re-derive its own threshold.
        report.analyzedDoses[i].isEffective = isEffective;

        const bool isToxic =
            (dose.networkState == NetworkState::SeizureActive ||
             dose.networkState == NetworkState::SeizureRisk   ||
             dose.networkState == NetworkState::NeuralSuppression ||
             dose.networkState == NetworkState::DepolarizationBlock ||
             dose.networkState == NetworkState::Hyperexcitable);

        if (isEffective) {
            therapeuticDoses.push_back(static_cast<double>(dose.dose));
            if (!hasOnsetDose) {
                hasOnsetDose = true;
                onsetDose    = static_cast<double>(dose.dose);
                report.hasEffectiveDose = true;
                report.effectiveMinDose = dose.dose;
            }
        } else if (isToxic) {
            excitatoryRiskDoses.push_back(static_cast<double>(dose.dose));
            if (!sawToxicEffect) {
                sawToxicEffect = true;
                report.hasToxicThreshold      = true;
                report.toxicMinDose           = dose.dose;
                report.hasToxicThresholdExact = true;
                report.toxicThresholdDoseEval = static_cast<double>(dose.dose);
                report.toxicThresholdText     = std::to_string(report.toxicThresholdDoseEval);
            }
        } else {
            safeDoses.push_back(static_cast<double>(dose.dose));
        }
    }

    // ─── Step 2: Response mode ────────────────────────────────────────────────
    // K-block: even when rate drops, if irregularity and burst rose
// AND mechanism is K-block, classify as excitatory
    const bool kBlockExcitation = (report.dominantMechanism == MechanismSignature::KBlock) &&
        (cumulativeExcitation > cumulativeSuppression * 0.5);

    if (kBlockExcitation || (cumulativeExcitation > cumulativeSuppression * 1.2 && cumulativeExcitation > cumulativeStabilization)) {
        report.responseMode = "EXCITATORY_RESPONSE";
    } else if (cumulativeStabilization > cumulativeSuppression * 1.2 && cumulativeStabilization > cumulativeExcitation) {
        report.responseMode = "STABILIZING_RESPONSE";
    } else if (cumulativeSuppression < 0.5 && cumulativeExcitation < 0.5 && cumulativeStabilization < 0.5) {
        report.responseMode = "NO_SIGNIFICANT_RESPONSE";
    } else if (cumulativeSuppression >= cumulativeExcitation && cumulativeSuppression >= cumulativeStabilization) {
        report.responseMode = "SUPPRESSIVE_RESPONSE";
    } else if (cumulativeExcitation >= cumulativeStabilization) {
        report.responseMode = "EXCITATORY_RESPONSE";
    } else {
        report.responseMode = "STABILIZING_RESPONSE";
    }

    // ─── Step 3: Dominant mechanism ───────────────────────────────────────────
    // PHASE2_PLAN.md step 5 / Phase 3a / Phase 3c: extended from the original
    // 4-way (Na/K/Ca/Mixed) tally to all 14 MechanismSignature values
    // (Unknown + 13 real mechanisms: Na/K/Ca channel blocks + Mixed + AMPA/
    // NMDA/GABA-A/GABA-B + Phase 3a's Gat1ReuptakeBlock + Phase 3c's
    // D1Gain/D2Gain/Ht1aGain/Ht2aGain), same "count occurrences across
    // doses, pick the max" logic -- just over a bigger set now that
    // NetworkAnalyzer::detectMechanism can return any of the nine
    // receptor/transporter/neuromodulator signatures too.
    // BUG FIX (tiagabine validation, Phase 3a): this was once
    // std::array<int, 9>, sized for the enum's count BEFORE
    // Gat1ReuptakeBlock was added as its 10th value (index 9). Writing
    // counts[indexOf(Gat1ReuptakeBlock)] (index 9) into a 9-element array
    // (valid indices 0-8) was an out-of-bounds write -- undefined behavior,
    // not just a wrong answer. This is exactly why tiagabine's per-dose
    // labels correctly read "GABA Reuptake Block (GAT1)" (that's
    // NetworkAnalyzer::detectMechanism, unaffected by this) while the
    // report's top-level Mechanism/FINAL DECISION still said Unknown (that
    // aggregation step's tally never validly recorded a single
    // Gat1ReuptakeBlock vote). Any enum addition to MechanismSignature must
    // keep this array's size in sync -- there is no compile-time check
    // tying the two together. Phase 3c added 4 more enum values
    // (D1Gain/D2Gain/Ht1aGain/Ht2aGain), so this was bumped 10 -> 14 to
    // avoid repeating the exact same bug.
    std::array<int, 14> counts{};
    auto indexOf = [](MechanismSignature s) -> std::size_t {
        return static_cast<std::size_t>(s);
    };
    for (const auto& dose : sorted) {
        ++counts[indexOf(dose.mechanismSignature)];
    }
    // Index 0 is Unknown -- excluded from the "which mechanism dominates"
    // comparison so a handful of real detections outvote a majority of
    // Unknown doses (e.g. sub-threshold early doses correctly reading
    // Unknown shouldn't hide a clear mechanism seen at higher doses).
    std::size_t bestIdx = 0;
    int bestCount = 0;
    for (std::size_t i = 1; i < counts.size(); ++i) {
        if (counts[i] > bestCount) { bestCount = counts[i]; bestIdx = i; }
    }
    report.dominantMechanism = (bestCount == 0)
        ? MechanismSignature::Unknown
        : static_cast<MechanismSignature>(bestIdx);
    report.mechanismText = toString(report.dominantMechanism);

    // ─── Step 4: Dominant network state (worst state seen across doses) ───────
    NetworkState dominantState = NetworkState::Stable;
    auto severity = [](NetworkState s) -> int {
        switch (s) {
            case NetworkState::NeuralSuppression:   return 6;
            case NetworkState::DepolarizationBlock: return 5;
            case NetworkState::SeizureActive:       return 4;
            case NetworkState::SeizureRisk:         return 3;
            case NetworkState::Hyperexcitable:      return 2;
            case NetworkState::MildInstability:     return 1;
            default:                                return 0;
        }
    };
    for (const auto& dose : sorted) {
        if (severity(dose.networkState) > severity(dominantState))
            dominantState = dose.networkState;
    }

    // ─── Step 5: Therapeutic window ───────────────────────────────────────────
    auto sortDedup = [](std::vector<double>& v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end(),
            [](double a, double b){ return std::fabs(a-b)<=1.0e-6; }), v.end());
    };
    sortDedup(safeDoses);
    sortDedup(therapeuticDoses);
    sortDedup(excitatoryRiskDoses);

    const auto therapeuticRanges = contiguousRanges(therapeuticDoses, inferredStep);
    const auto safeRanges        = contiguousRanges(safeDoses,        inferredStep);

    if (!therapeuticRanges.empty()) {
        const auto& widest = *std::max_element(therapeuticRanges.begin(), therapeuticRanges.end(),
            [](const Range& a, const Range& b){ return (a.hi-a.lo) < (b.hi-b.lo); });
        report.hasContinuousEffectiveWindow = true;
        report.effectiveRangeMin  = widest.lo;
        report.effectiveRangeMax  = widest.hi;
        report.hasTherapeuticWindow = true;
        report.therapeuticWindow  = static_cast<float>(widest.hi - widest.lo);
        report.windowQuality      = therapeuticRanges.size() > 1U ? "Fragmented" : "Continuous";
        onsetDose    = widest.lo;
        hasOnsetDose = true;

        // Doses that fall inside the widest window's [lo,hi] span but are
        // NOT in therapeuticDoses are exactly the gap-tolerated dropouts
        // contiguousRanges() smoothed over (see field comment in the
        // header). Surface them explicitly rather than leaving the
        // Continuous/effectiveRange numbers looking inconsistent with the
        // per-dose classification.
        for (const auto& dose : sorted) {
            const double d = static_cast<double>(dose.dose);
            if (d < widest.lo - 1.0e-6 || d > widest.hi + 1.0e-6) continue;
            const bool inTherapeutic = std::any_of(
                therapeuticDoses.begin(), therapeuticDoses.end(),
                [&](double td){ return std::fabs(td - d) <= 1.0e-6; });
            if (!inTherapeutic) report.toleratedNoiseDoses.push_back(d);
        }
    } else {
        report.hasContinuousEffectiveWindow = false;
        report.hasTherapeuticWindow = false;
        report.therapeuticWindow    = 0.0f;
        report.windowQuality        = "Not observed";
    }

    if (!safeRanges.empty()) {
        const auto& widest = *std::max_element(safeRanges.begin(), safeRanges.end(),
            [](const Range& a, const Range& b){ return (a.hi-a.lo) < (b.hi-b.lo); });
        report.hasSafeRange = true;
        report.safeMinDose  = safeNonNegative(static_cast<float>(widest.lo));
        report.safeMaxDose  = safeNonNegative(static_cast<float>(widest.hi));
    }

    report.excitatoryRiskDoses = excitatoryRiskDoses;

    const bool therapeuticWindowExists = report.hasContinuousEffectiveWindow;
    const bool fragmentedWindow        = therapeuticRanges.size() > 1U;
    const bool lowStability = (report.stabilityScore == "LOW") ||
                               (report.rateVariability    >= 2.0f) ||
                               (report.toxicityVariability >= 10.0f);
    const bool toxicityBeforeTherapy =
        report.hasToxicThreshold &&
        (!therapeuticWindowExists ||
         report.toxicThresholdDoseEval <= report.effectiveRangeMin);
    const bool toxicityAfterTherapy =
        report.hasToxicThreshold && therapeuticWindowExists &&
        report.toxicThresholdDoseEval > report.effectiveRangeMax;

    // ─── Safety margin ─────────────────────────────────────────────────────
    // Only meaningful when there IS a real continuous therapeutic window AND
    // toxicity/danger appears strictly above it. Ratio = toxic dose / top of
    // window -- e.g. Phenytoin: toxic threshold 60, window top 40 -> 1.5x.
    // Thresholds follow the clinical "therapeutic index" convention: <2x is
    // considered a narrow therapeutic index (phenytoin, carbamazepine,
    // digoxin, lithium are the textbook examples and are still prescribed
    // drugs); >=3x is comfortably wide. 1.2x floor below "narrow" guards
    // against calling a razor-thin, essentially-zero gap a "real margin"
    // just because the next tested dose happens to be toxic.
    double safetyMarginRatio = 0.0;
    bool hasQualifyingWindow = toxicityAfterTherapy && report.effectiveRangeMax > 1.0e-9;
    if (hasQualifyingWindow)
        safetyMarginRatio = report.toxicThresholdDoseEval / report.effectiveRangeMax;
    const bool marginTooThin = !hasQualifyingWindow || safetyMarginRatio < 1.2;
    const bool marginNarrow  = hasQualifyingWindow && safetyMarginRatio >= 1.2 && safetyMarginRatio < 3.0;
    // >=3x is a comfortably wide margin -- don't blanket-fail these either;
    // let them fall through to the existing therapeuticWindowExists &&
    // toxicityAfterTherapy CAUTION/MODERATE branch further down, which is
    // strictly more lenient than the narrow-margin tier above it.
    const bool marginWide    = hasQualifyingWindow && safetyMarginRatio >= 3.0;

    // ─── Step 6: Peak dose for narrative ─────────────────────────────────────
    std::size_t peakIdx = 0;
    float peakScore = 0.0f;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const float s = std::max({sorted[i].suppressionScore,
                                  sorted[i].excitabilityScore,
                                  sorted[i].stabilizationScore});
        if (s > peakScore) { peakScore = s; peakIdx = i; }
    }

    // ─── Step 7: Sigmoid fit ──────────────────────────────────────────────────
    const std::vector<double>* fitSource = &suppressionCurve;
    if (report.responseMode == "EXCITATORY_RESPONSE")  fitSource = &excitationCurve;
    if (report.responseMode == "STABILIZING_RESPONSE") fitSource = &stabilizationCurve;
    std::vector<double> monotonicCurve = *fitSource;
    for (std::size_t i = 1; i < monotonicCurve.size(); ++i)
        if (monotonicCurve[i] < monotonicCurve[i-1]) monotonicCurve[i] = monotonicCurve[i-1];
    report.sigmoidR2 = computeBestSigmoidR2(xDose, monotonicCurve);
    if      (report.sigmoidR2 >= 0.95) report.curveType = "Sigmoidal";
    else if (report.sigmoidR2 >= 0.85) report.curveType = "Weak Sigmoidal";
    else                                report.curveType = "Non-sigmoidal";
    if (report.responseMode == "STABILIZING_RESPONSE")
        report.curveType = (report.sigmoidR2 >= 0.95)
                            ? "Sigmoidal Stabilization" : "Stabilizing Response";

    // Computed here (rather than in Step 9 below) so Step 8's safety text
    // and Step 9's recommendation are guaranteed to agree -- both are built
    // from these same two booleans instead of being derived independently.
    const bool dangerous =
        dominantState == NetworkState::NeuralSuppression ||
        dominantState == NetworkState::SeizureActive     ||
        dominantState == NetworkState::DepolarizationBlock;
    // responseMode=="EXCITATORY_RESPONSE" is a categorical label from Step 2 --
    // it fires whenever cumulative excitation edges out suppression/
    // stabilization, with no regard for size. That's correct for a real
    // convulsant (4-Aminopyridine: large swings, dominantState never leaves
    // Stable per-dose, still genuinely dangerous). It is WRONG for a drug
    // whose peak effect is a few percent of baseline -- e.g. Nimodipine at
    // gAHP=1.0 showed Max Effect 2%, Rate Change 4.1%, Burst Delta 0.00 Hz,
    // Irregularity Delta -0.137 (down, not up), Stability Score HIGH -- yet
    // was still escalated to "NOT RECOMMENDED / HIGH RISK / exceeded safe
    // stability limits", a description contradicted by its own printed
    // numbers. dominantState==Hyperexcitable is already a real per-dose
    // severity check and needs no gate; only the categorical responseMode
    // path needs a magnitude floor. 10% matches the threshold this file
    // already uses elsewhere (computeConfidence) to distinguish a notable
    // effect from noise.
    const bool excitatory =
        dominantState == NetworkState::Hyperexcitable ||
        (report.responseMode == "EXCITATORY_RESPONSE" && maxEffectPct > 10.0);
    const bool stabilizing = report.responseMode == "STABILIZING_RESPONSE";

    // Expose the exact value the `excitatory` gate above was just computed
    // against, so main.cpp can print this SAME number instead of a
    // differently-formulated "Max Effect" that can silently disagree with
    // it (see PharmaDecisionEngine.h's decisionMaxEffectPct comment).
    report.decisionMaxEffectPct = static_cast<float>(maxEffectPct);
    report.decisionMaxEffectDoseIdx = maxEffectDoseIdx;

    // The narrow-margin carve-out only applies to the suppression/"dangerous"
    // path, not to excitatory or before-therapy toxicity -- those never have
    // a real window to measure a margin from (see hasQualifyingWindow above,
    // which is already gated on toxicityAfterTherapy).
    const bool narrowMarginCarveOut = dangerous && marginNarrow;

    report.hasSafetyMarginRatio = hasQualifyingWindow;
    report.safetyMarginRatio    = safetyMarginRatio;
    report.narrowSafetyMargin   = narrowMarginCarveOut;

    // ─── Step 8: Narrative ────────────────────────────────────────────────────
    report.primaryChangeText = buildNarrative(
        sorted[peakIdx], report.dominantMechanism, report.responseMode, maxEffectPct);
    report.safetyInterpretationText = buildSafetyText(
        dominantState, toxicityBeforeTherapy, toxicityAfterTherapy, lowStability, excitatory,
        narrowMarginCarveOut, safetyMarginRatio);

    // Metric reductions at final dose
    const AnalyzedDose& finalDose = sorted.back();
    report.syncReductionPct    = std::max(0.0f, finalDose.syncReductionPct);
    report.seizureReductionPct = std::max(0.0f, -finalDose.syncDelta * 100.0f);
    report.niiReductionPct     = 0.0f; // niiDelta not directly stored; computed in NetworkAnalyzer
    report.burstReductionPct   = std::max(0.0f, -finalDose.burstRateDelta);
    report.calciumEffectMagnitude = std::max({
        static_cast<double>(report.syncReductionPct),
        static_cast<double>(report.burstReductionPct)
    });
    report.meaningfulCaBlock = (report.dominantMechanism == MechanismSignature::CaBlock &&
                                 report.stabilizationScore > 0.30f);

    // ─── Step 9: Recommendation ───────────────────────────────────────────────
    // dangerous/excitatory/stabilizing computed earlier, just above Step 8,
    // so the safety narrative and this recommendation can't disagree.
    //
    // narrowMarginCarveOut is checked BEFORE the blanket dangerous/excitatory
    // branch. Previously `dangerous` alone (any dose ever hitting Neural
    // Suppression/Seizure/Depolarization Block) always won this if-chain,
    // so a genuine narrow-therapeutic-index drug (phenytoin: toxic dose
    // only 1.5x the top of its own continuous 20-40 therapeutic window)
    // got the identical "NOT RECOMMENDED / exceeded safe stability limits"
    // verdict as a drug with no usable window at all. Real narrow-TI drugs
    // (phenytoin, carbamazepine, lithium, digoxin, warfarin) are still
    // prescribed clinically -- collapsing "narrow but real" into "unusable"
    // loses exactly the distinction a triage tool should be making.
    if (narrowMarginCarveOut) {
        report.recommendation = "CAUTION";
        report.riskLevel      = "HIGH";
        report.overallTier    = DrugRiskTier::HighRisk;
        std::ostringstream r;
        r << std::fixed << std::setprecision(1)
          << "A real therapeutic window was identified, but the safety margin "
             "to dangerous network states is narrow (approximately "
          << safetyMarginRatio << "x). Careful dose titration and monitoring "
             "are required; this is a narrow-therapeutic-index profile, not "
             "an absence of therapeutic use.";
        report.reason = r.str();

    } else if ((dangerous && !marginWide) || excitatory || toxicityBeforeTherapy) {
        report.recommendation = "NOT RECOMMENDED";
        report.riskLevel      = "HIGH";
        report.overallTier    = DrugRiskTier::Toxic;
        if (excitatory && dominantState != NetworkState::Hyperexcitable) {
            // BUG FIX (Phase 3 10-drug validation, DOI): `excitatory` also fires via the
            // categorical EXCITATORY_RESPONSE + maxEffectPct>10 path above (dominantState
            // stays Stable the whole run -- see that branch's comment, same as the
            // 4-AP/Nimodipine precedent). The old boilerplate text always blamed "burst
            // rate" even when Burst Rate Delta was flat (e.g. DOI: 0.00 Hz, driven purely
            // by a +12.2% rate change), contradicting the report's own printed numbers.
            report.excitatoryVerdictViaMagnitudeFloor = true;
            std::ostringstream r;
            r << std::fixed << std::setprecision(1)
              << "Firing rate rose " << finalDose.rateChangePct << "% from baseline, a net "
                 "excitatory effect large enough to exceed this engine's safety floor for a "
                 "categorical excitatory response, across the tested dose range.";
            report.reason = r.str();
        } else if (excitatory)
            // Gap 1.3: "Burst rate" dropped from this sentence -- it's a
            // permanently-dead signal in this network's firing regime (see
            // classifyState comment in NetworkAnalyzer.cpp); irregularity and
            // excitability markers are what actually drive this branch.
            report.reason = "Irregularity and excitability markers exceeded "
                            "safe neural stability limits across the tested dose range.";
        else if (dangerous)
            report.reason = "Network activity collapsed or seizure-level instability "
                            "was observed within the tested dose range, with no "
                            "meaningful safety margin from the therapeutic window.";
        else
            report.reason = "Instability markers appeared at doses below the therapeutic "
                            "window; the dose-response ordering is unsafe.";

    } else if (stabilizing && !report.hasToxicThreshold && !lowStability) {
        report.recommendation = "PROMISING";
        report.riskLevel      = "LOW";
        report.overallTier    = DrugRiskTier::Safe;
        report.reason = "Synchronization, burst duration, and network instability reduced "
                        "from baseline while firing activity was preserved.";

    } else if (therapeuticWindowExists && toxicityAfterTherapy) {
        report.recommendation = "CAUTION";
        report.riskLevel      = "MODERATE";
        report.overallTier    = DrugRiskTier::ModerateRisk;
        report.reason = "A therapeutic window was identified but is narrow; "
                        "instability markers appeared at higher doses.";

    } else if (therapeuticWindowExists) {
        report.recommendation = lowStability ? "CAUTION"    : "PROMISING";
        report.riskLevel      = lowStability ? "MODERATE"   : "LOW";
        report.overallTier    = lowStability ? DrugRiskTier::ModerateRisk : DrugRiskTier::Safe;
        // BUG FIX (Phase 3 10-drug validation, bromocriptine): this branch used
        // to always say "Continuous therapeutic window" even when
        // report.windowQuality was actually "Fragmented" (therapeuticRanges.size()
        // > 1, i.e. multiple disjoint therapeutic sub-ranges with an ineffective
        // dose in between) -- report text and report.windowQuality disagreed.
        // fragmentedWindow already existed for the confidence calc below; just
        // wasn't consulted here.
        if (lowStability)
            report.reason = "Therapeutic response detected but inter-run variability reduces confidence.";
        else if (fragmentedWindow)
            report.reason = "A therapeutic window was identified, but it is fragmented -- some "
                            "doses within the tested range fell back below the effectiveness "
                            "threshold rather than forming one unbroken window. No accompanying "
                            "instability was observed.";
        else
            report.reason = "Continuous therapeutic window identified with no accompanying instability.";

    } else {
        report.recommendation = "LIMITED EFFICACY";
        report.riskLevel      = "LOW";
        report.overallTier    = DrugRiskTier::Safe;
        report.reason = "No meaningful biological response detected across the tested dose range.";
    }

    // ─── Step 10: Confidence ──────────────────────────────────────────────────
    report.confidence = computeConfidence(
        dominantState, report.sigmoidR2,
        therapeuticWindowExists, report.hasContinuousEffectiveWindow,
        fragmentedWindow, report.rateVariability, report.toxicityVariability,
        maxEffectPct, report.peakRiskScore, report.peakSeizureProbabilityPct / 100.0f,
        stabilityInput.runCount);

    report.hasToxicThresholdExact = report.hasToxicThreshold;
    report.toxicThresholdText = report.hasToxicThreshold
        ? std::to_string(report.toxicThresholdDoseEval)
        : ">" + std::to_string(report.maxTestedDose);

    const double midDose = 0.5 * (report.minTestedDose + report.maxTestedDose);
    if (report.sigmoidR2 >= 0.95 && hasOnsetDose && onsetDose <= midDose)
        report.responseStrength = "Strong";
    else if (report.sigmoidR2 >= 0.85)
        report.responseStrength = "Moderate-to-Strong";
    else
        report.responseStrength = "Weak";

    report.seizureTrendText =
        (finalDose.seizureProbability > sorted.front().seizureProbability + 0.05f)
            ? "Seizure-risk markers increased with dose"
        : (finalDose.seizureProbability < sorted.front().seizureProbability - 0.05f)
            ? "Seizure-risk markers decreased with dose"
            : "Seizure-risk markers remained broadly stable";

    return report;
}

// ─── toString helpers ─────────────────────────────────────────────────────────
std::string PharmaDecisionEngine::toString(DrugRiskTier tier) {
    switch (tier) {
        case DrugRiskTier::Safe:         return "Safe";
        case DrugRiskTier::ModerateRisk: return "Moderate Risk";
        case DrugRiskTier::HighRisk:     return "High Risk";
        case DrugRiskTier::Toxic:        return "Toxic";
        default:                          return "Safe";
    }
}

std::string PharmaDecisionEngine::toString(NetworkState state) {
    switch (state) {
        case NetworkState::Stable:             return "Stable";
        case NetworkState::MildInstability:    return "Mild Instability";
        case NetworkState::Hyperexcitable:     return "Hyperexcitable";
        case NetworkState::SeizureRisk:        return "Seizure Risk";
        case NetworkState::SeizureActive:      return "Active Seizure";
        case NetworkState::DepolarizationBlock:return "Depolarization Block";
        case NetworkState::NeuralSuppression:  return "Neural Suppression";
        default:                               return "Unknown";
    }
}

std::string PharmaDecisionEngine::toString(MechanismSignature sig) {
    switch (sig) {
        case MechanismSignature::NaBlock:         return "Na-Channel Block";
        case MechanismSignature::KBlock:          return "K-Channel Block";
        case MechanismSignature::CaBlock:         return "Ca-Channel Block";
        case MechanismSignature::Mixed:           return "Mixed Channel Block";
        // PHASE2_PLAN.md step 5
        case MechanismSignature::AmpaBlock:       return "AMPA Receptor Block";
        case MechanismSignature::NmdaBlock:       return "NMDA Receptor Block";
        case MechanismSignature::GabaAPotentiate: return "GABA-A Potentiation";
        case MechanismSignature::GabaBAgonist:    return "GABA-B Agonism";
        // Phase 3a
        case MechanismSignature::Gat1ReuptakeBlock: return "GABA Reuptake Block (GAT1)";
        // Phase 3c
        case MechanismSignature::D1Gain:          return "D1 Neuromodulator Gain";
        case MechanismSignature::D2Gain:          return "D2 Neuromodulator Gain";
        case MechanismSignature::Ht1aGain:        return "5-HT1A Neuromodulator Gain";
        case MechanismSignature::Ht2aGain:        return "5-HT2A Neuromodulator Gain";
        default:                                  return "Unknown";
    }
}

} // namespace spp::analyzer