#include "PharmaDecisionEngine.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace spp::analyzer {

namespace {
constexpr float kMeaningfulBlockThreshold = 0.15f;
constexpr float kStrongBlockThreshold = 0.35f;
constexpr float kSilencingRateThreshold = 0.10f;
constexpr float kMeaningfulShiftThreshold = 0.12f;
constexpr float kStrongInstabilityThreshold = 0.35f;
constexpr float kToxicInstabilityThreshold = 0.45f;
constexpr float kDominanceSeparationThreshold = 0.10f;

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float safeNonNegative(float value) {
    if (!std::isfinite(value) || value < 0.0f) {
        return 0.0f;
    }
    return value;
}

double safeNonNegativeD(double value) {
    if (!std::isfinite(value) || value < 0.0) {
        return 0.0;
    }
    return value;
}

double reductionPercent(double baseline, double current) {
    if (!std::isfinite(baseline) || !std::isfinite(current) || baseline <= 1.0e-6) {
        return 0.0;
    }
    return ((baseline - current) / baseline) * 100.0;
}

[[maybe_unused]] double increasePercent(double baseline, double current) {
    if (!std::isfinite(baseline) || !std::isfinite(current) || baseline <= 1.0e-6) {
        return 0.0;
    }
    return ((current - baseline) / baseline) * 100.0;
}

[[maybe_unused]] bool stabilityScoreIsMediumOrHigh(const std::string& stabilityScore) {
    return stabilityScore == "MEDIUM" || stabilityScore == "HIGH";
}

std::string toString(ResponseMode mode) {
    switch (mode) {
        case ResponseMode::NoSignificantResponse:
            return "NO_SIGNIFICANT_RESPONSE";
        case ResponseMode::SuppressiveResponse:
            return "SUPPRESSIVE_RESPONSE";
        case ResponseMode::ExcitatoryResponse:
            return "EXCITATORY_RESPONSE";
        case ResponseMode::StabilizingResponse:
            return "STABILIZING_RESPONSE";
        case ResponseMode::ToxicInstability:
            return "TOXIC_INSTABILITY";
        case ResponseMode::NeuralSilencing:
            return "NEURAL_SILENCING";
        default:
            return "NO_SIGNIFICANT_RESPONSE";
    }
}

BiologicalState biologicalStateFromMode(ResponseMode mode) {
    switch (mode) {
        case ResponseMode::SuppressiveResponse:
            return BiologicalState::ControlledSuppression;
        case ResponseMode::ExcitatoryResponse:
            return BiologicalState::Hyperexcitability;
        case ResponseMode::StabilizingResponse:
            return BiologicalState::NetworkStabilization;
        case ResponseMode::ToxicInstability:
            return BiologicalState::ToxicInstability;
        case ResponseMode::NeuralSilencing:
            return BiologicalState::NeuralSilencing;
        case ResponseMode::NoSignificantResponse:
        default:
            return BiologicalState::LimitedEffect;
    }
}

struct MechanisticContext {
    double rateFraction = 0.0;
    double syncDelta = 0.0;
    double niiDelta = 0.0;
    double seizureDelta = 0.0;
    double syncReductionPct = 0.0;
    double niiReductionPct = 0.0;
    double seizureReductionPct = 0.0;
    double burstReductionPct = 0.0;
    double instabilityIncrease = 0.0;
    double instabilityReduction = 0.0;
    double shiftMagnitude = 0.0;
};

MechanisticContext buildMechanisticContext(
    double baselineRate,
    double baselineSync,
    double baselineBurst,
    double baselineNii,
    double baselineSeizure,
    const DoseObservation& obs
) {
    const double currentRate = safeNonNegativeD(obs.meanFiringRateHz);
    const double currentSync = safeNonNegativeD(obs.synchronizationIndex);
    const double currentBurst = safeNonNegativeD(obs.burstIndex);
    const double currentNii = safeNonNegativeD(obs.nii);
    const double currentSeizure = safeNonNegativeD(obs.seizureProbabilityPct);
    MechanisticContext context;
    context.rateFraction = (currentRate - baselineRate) / std::max(1.0, baselineRate);
    context.syncDelta = currentSync - baselineSync;
    context.niiDelta = currentNii - baselineNii;
    context.seizureDelta = currentSeizure - baselineSeizure;
    context.syncReductionPct = reductionPercent(baselineSync, currentSync);
    context.niiReductionPct = reductionPercent(baselineNii, currentNii);
    context.seizureReductionPct = reductionPercent(baselineSeizure, currentSeizure);
    context.burstReductionPct = reductionPercent(baselineBurst, currentBurst);
    context.instabilityIncrease = clamp01(
        0.40 * clamp01(context.syncDelta) +
        0.35 * clamp01(context.niiDelta) +
        0.25 * clamp01(context.seizureDelta / 100.0)
    );
    context.instabilityReduction = clamp01(
        0.40 * clamp01(context.syncReductionPct / 100.0) +
        0.35 * clamp01(context.niiReductionPct / 100.0) +
        0.25 * clamp01(context.seizureReductionPct / 100.0)
    );
    context.shiftMagnitude = clamp01(
        std::max({
            std::fabs(context.rateFraction),
            std::fabs(context.syncDelta),
            std::fabs(context.niiDelta),
            std::fabs(context.seizureDelta / 100.0)
        })
    );
    return context;
}

MechanisticDominance computeMechanisticDominance(
    const DoseObservation& obs,
    const MechanisticContext& context
) {
    const double naAlignment = clamp01(
        0.45 * clamp01(-context.rateFraction) +
        0.20 * clamp01(context.syncReductionPct / 100.0) +
        0.20 * clamp01(context.niiReductionPct / 100.0) +
        0.15 * clamp01(context.seizureReductionPct / 100.0)
    );
    const double kAlignment = clamp01(
        0.45 * clamp01(context.rateFraction) +
        0.20 * clamp01(context.syncDelta) +
        0.20 * clamp01(context.niiDelta) +
        0.15 * clamp01(context.seizureDelta / 100.0)
    );
    const double caAlignment = clamp01(
        0.40 * clamp01(obs.blockCa) +
        0.20 * clamp01(context.syncReductionPct / 100.0) +
        0.20 * clamp01(context.niiReductionPct / 100.0) +
        0.20 * clamp01(context.seizureReductionPct / 100.0)
    );

    MechanisticDominance dominance;
    dominance.naDominance = clamp01(0.55 * clamp01(obs.blockNa) + 0.45 * naAlignment);
    dominance.kDominance = clamp01(0.55 * clamp01(obs.blockK) + 0.45 * kAlignment);
    dominance.caDominance = clamp01(0.55 * clamp01(obs.blockCa) + 0.45 * caAlignment);
    return dominance;
}

double dominanceSpread(const MechanisticDominance& dominance) {
    const double maxValue = std::max({dominance.naDominance, dominance.kDominance, dominance.caDominance});
    const double secondValue = std::min(std::max(dominance.naDominance, dominance.kDominance), std::max(std::min(dominance.naDominance, dominance.kDominance), dominance.caDominance));
    return maxValue - secondValue;
}

ResponseMode resolveResponseMode(
    const DoseObservation& obs,
    const MechanisticContext& context,
    const MechanisticDominance& dominance,
    bool instabilitySeen
) {
    const double maxDominance = std::max({dominance.naDominance, dominance.kDominance, dominance.caDominance});
    const double spread = dominanceSpread(dominance);
    const bool lowSignal =
        maxDominance < 0.35 &&
        context.shiftMagnitude < kMeaningfulShiftThreshold &&
        std::fabs(context.syncDelta) < 0.08 &&
        std::fabs(context.niiDelta) < 0.08 &&
        std::fabs(context.rateFraction) < 0.12;
    if (lowSignal || (spread < kDominanceSeparationThreshold && context.shiftMagnitude < 0.16)) {
        return ResponseMode::NoSignificantResponse;
    }

    const bool silencing =
        obs.meanFiringRateHz <= kSilencingRateThreshold * std::max(1.0f, obs.meanFiringRateHz + 1.0f) ||
        (dominance.naDominance >= 0.60 && context.rateFraction <= -0.70 && context.syncDelta <= 0.0 && context.niiDelta <= 0.0);
    if (silencing) {
        return ResponseMode::NeuralSilencing;
    }

    const bool toxic =
        instabilitySeen ||
        context.instabilityIncrease >= kToxicInstabilityThreshold ||
        (context.niiDelta > 0.30 && context.seizureDelta > 20.0 && context.syncDelta >= 0.0) ||
        (context.syncDelta > 0.20 && context.niiDelta > 0.20 && context.seizureDelta > 10.0);
    if (toxic) {
        return ResponseMode::ToxicInstability;
    }

    const bool excitatory =
        dominance.kDominance >= 0.45 &&
        (context.rateFraction > 0.12 || context.syncDelta > 0.10 || context.niiDelta > 0.10 || context.seizureDelta > 5.0);
    if (excitatory) {
        return ResponseMode::ExcitatoryResponse;
    }

    const bool stabilizing =
        dominance.caDominance >= 0.40 &&
        obs.blockCa >= kMeaningfulBlockThreshold &&
        context.syncReductionPct >= 12.0 &&
        context.niiReductionPct >= 12.0 &&
        context.seizureReductionPct >= 8.0 &&
        context.rateFraction <= 0.10 &&
        !silencing;
    if (stabilizing) {
        return ResponseMode::StabilizingResponse;
    }

    const bool suppressive =
        dominance.naDominance >= 0.40 &&
        context.rateFraction < -0.10 &&
        context.rateFraction > -0.70 &&
        context.niiDelta <= 0.15 &&
        !silencing &&
        !toxic;
    if (suppressive) {
        return ResponseMode::SuppressiveResponse;
    }

    return ResponseMode::NoSignificantResponse;
}

double modeConsistencyScore(ResponseMode mode, const MechanisticContext& context, const MechanisticDominance& dominance) {
    switch (mode) {
        case ResponseMode::NoSignificantResponse:
            return clamp01(1.0 - std::max({std::fabs(context.rateFraction), std::fabs(context.syncDelta), std::fabs(context.niiDelta), std::fabs(context.seizureDelta / 100.0)}));
        case ResponseMode::SuppressiveResponse:
            return clamp01(0.45 * dominance.naDominance + 0.35 * clamp01(-context.rateFraction) + 0.20 * clamp01(-context.niiDelta));
        case ResponseMode::ExcitatoryResponse:
            return clamp01(0.45 * dominance.kDominance + 0.35 * clamp01(context.rateFraction) + 0.20 * clamp01(std::max(context.syncDelta, context.niiDelta)));
        case ResponseMode::StabilizingResponse:
            return clamp01(0.40 * dominance.caDominance + 0.30 * clamp01(context.syncReductionPct / 100.0) + 0.30 * clamp01(context.niiReductionPct / 100.0));
        case ResponseMode::ToxicInstability:
            return clamp01(0.40 * clamp01(context.instabilityIncrease) + 0.30 * clamp01(context.niiDelta) + 0.30 * clamp01(context.seizureDelta / 100.0));
        case ResponseMode::NeuralSilencing:
            return clamp01(0.50 * dominance.naDominance + 0.50 * clamp01(-context.rateFraction));
        default:
            return 0.0;
    }
}

double mechanisticConfidenceScore(
    ResponseMode mode,
    const MechanisticContext& context,
    const MechanisticDominance& dominance,
    const DecisionStabilityInput& stabilityInput,
    bool contiguousMode,
    double localVariancePenalty
) {
    const double consistency = modeConsistencyScore(mode, context, dominance);
    const double agreement = clamp01(1.0 - localVariancePenalty);
    const double reproducibility = clamp01(
        0.35 * clamp01(stabilityInput.runCount / 10.0) +
        0.35 * clamp01(1.0 - stabilityInput.rateStd / 5.0) +
        0.30 * clamp01(1.0 - stabilityInput.toxicityStd / 25.0)
    );
    const double continuity = contiguousMode ? 1.0 : 0.55;
    const double contradictionPenalty = clamp01(
        0.35 * (1.0 - dominanceSpread(dominance)) +
        0.35 * std::max(0.0, context.shiftMagnitude - 0.18) +
        0.30 * std::max(0.0, -context.instabilityReduction)
    );
    return 100.0 * clamp01(
        0.35 * consistency +
        0.20 * agreement +
        0.20 * reproducibility +
        0.15 * continuity +
        0.10 * (1.0 - contradictionPenalty)
    );
}

struct DoseRiskMetrics {
    double rateChangeFrac = 0.0;
    double syncDelta = 0.0;
    double niiDelta = 0.0;
    double seizureDelta = 0.0;
    double syncReductionPct = 0.0;
    double niiReductionPct = 0.0;
    double seizureReductionPct = 0.0;
    double burstReductionPct = 0.0;
    double seizureNorm = 0.0;
    double suppressionNorm = 0.0;
    double syncNorm = 0.0;
    double burstNorm = 0.0;
    double niiNorm = 0.0;
};

double instabilityMetric(const DoseRiskMetrics& metrics) {
    return clamp01(
        0.45 * metrics.syncNorm +
        0.30 * metrics.burstNorm +
        0.25 * metrics.niiNorm
    );
}

double reboundExcitationMetric(const DoseRiskMetrics& metrics) {
    return clamp01(std::max({
        0.0,
        metrics.rateChangeFrac,
        metrics.syncDelta,
        metrics.niiDelta,
        metrics.seizureDelta
    }));
}

double suppressionProtectionMetric(const DoseRiskMetrics& metrics) {
    return clamp01(
        0.35 * clamp01(-metrics.rateChangeFrac / 0.70) +
        0.20 * clamp01(metrics.syncReductionPct / 100.0) +
        0.20 * clamp01(metrics.niiReductionPct / 100.0) +
        0.15 * clamp01(metrics.seizureReductionPct / 100.0) +
        0.10 * clamp01(metrics.burstReductionPct / 100.0)
    );
}

double computeSuppressiveSeizureRisk(const DoseRiskMetrics& metrics) {
    const double residualInstability = clamp01(
        0.35 * metrics.seizureNorm +
        0.35 * instabilityMetric(metrics) +
        0.30 * reboundExcitationMetric(metrics)
    );
    return clamp01(residualInstability - suppressionProtectionMetric(metrics));
}

double computeNoSignificantResponseRisk(const DoseRiskMetrics& metrics) {
    const double smallShift = clamp01(
        std::max({
            0.0,
            std::fabs(metrics.rateChangeFrac),
            std::fabs(metrics.syncDelta),
            std::fabs(metrics.niiDelta),
            std::fabs(metrics.seizureDelta)
        })
    );
    return clamp01(
        0.35 * metrics.seizureNorm +
        0.35 * instabilityMetric(metrics) +
        0.30 * smallShift
    );
}

double computeExcitatorySeizureRisk(const DoseRiskMetrics& metrics) {
    const double excitationDrive = clamp01(
        0.35 * clamp01(metrics.rateChangeFrac / 0.50) +
        0.25 * clamp01(metrics.syncDelta) +
        0.20 * clamp01(metrics.niiDelta) +
        0.20 * clamp01(metrics.seizureDelta)
    );
    const double residualInstability = clamp01(
        0.30 * metrics.seizureNorm +
        0.40 * instabilityMetric(metrics) +
        0.30 * clamp01(metrics.niiDelta)
    );
    return clamp01(0.55 * residualInstability + 0.45 * excitationDrive);
}

double computeStabilizingSeizureRisk(const DoseRiskMetrics& metrics) {
    const double stabilizationBenefit = clamp01(
        0.35 * clamp01(metrics.syncReductionPct / 100.0) +
        0.25 * clamp01(metrics.niiReductionPct / 100.0) +
        0.20 * clamp01(metrics.seizureReductionPct / 100.0) +
        0.20 * clamp01(metrics.burstReductionPct / 100.0)
    );
    const double residualInstability = clamp01(
        0.35 * metrics.seizureNorm +
        0.35 * instabilityMetric(metrics) +
        0.30 * clamp01(std::max({0.0, metrics.rateChangeFrac, metrics.syncDelta, metrics.niiDelta}))
    );
    return clamp01(residualInstability - 0.55 * stabilizationBenefit);
}

double computeSeizureRiskForMode(const std::string& responseMode, const DoseRiskMetrics& metrics) {
    if (responseMode == "NO_SIGNIFICANT_RESPONSE") {
        return computeNoSignificantResponseRisk(metrics);
    }
    if (responseMode == "NEURAL_SILENCING") {
        return clamp01(
            0.45 * metrics.seizureNorm +
            0.35 * instabilityMetric(metrics) +
            0.20 * clamp01(-metrics.rateChangeFrac)
        );
    }
    if (responseMode == "EXCITATORY_RESPONSE") {
        return computeExcitatorySeizureRisk(metrics);
    }
    if (responseMode == "STABILIZING_RESPONSE") {
        return computeStabilizingSeizureRisk(metrics);
    }
    if (responseMode == "TOXIC_INSTABILITY") {
        return clamp01(
            0.40 * metrics.seizureNorm +
            0.35 * instabilityMetric(metrics) +
            0.25 * clamp01(std::max({0.0, metrics.niiDelta, metrics.syncDelta, metrics.seizureDelta}))
        );
    }
    return computeSuppressiveSeizureRisk(metrics);
}

double computeEarlyWarningIndexForMode(const std::string& responseMode, const DoseRiskMetrics& metrics, double seizureRisk) {
    if (responseMode == "NO_SIGNIFICANT_RESPONSE") {
        return 100.0 * clamp01(
            0.45 * seizureRisk +
            0.35 * instabilityMetric(metrics) +
            0.20 * std::max({0.0, std::fabs(metrics.rateChangeFrac), std::fabs(metrics.syncDelta), std::fabs(metrics.niiDelta)})
        );
    }
    if (responseMode == "EXCITATORY_RESPONSE") {
        return 100.0 * clamp01(
            0.50 * seizureRisk +
            0.30 * reboundExcitationMetric(metrics) +
            0.20 * instabilityMetric(metrics)
        );
    }
    if (responseMode == "NEURAL_SILENCING") {
        return 100.0 * clamp01(
            0.45 * seizureRisk +
            0.35 * clamp01(-metrics.rateChangeFrac) +
            0.20 * instabilityMetric(metrics)
        );
    }
    if (responseMode == "STABILIZING_RESPONSE") {
        const double stabilizationBenefit = clamp01(
            0.35 * clamp01(metrics.syncReductionPct / 100.0) +
            0.25 * clamp01(metrics.niiReductionPct / 100.0) +
            0.20 * clamp01(metrics.seizureReductionPct / 100.0) +
            0.20 * clamp01(metrics.burstReductionPct / 100.0)
        );
        return 100.0 * clamp01(
            0.55 * seizureRisk +
            0.25 * (1.0 - stabilizationBenefit) +
            0.20 * instabilityMetric(metrics)
        );
    }
    if (responseMode == "TOXIC_INSTABILITY") {
        return 100.0 * clamp01(
            0.45 * seizureRisk +
            0.35 * instabilityMetric(metrics) +
            0.20 * clamp01(std::max({0.0, metrics.niiDelta, metrics.syncDelta, metrics.seizureDelta}))
        );
    }
    return 100.0 * clamp01(
        0.55 * seizureRisk +
        0.25 * instabilityMetric(metrics) +
        0.20 * suppressionProtectionMetric(metrics)
    );
}

// Reconciliation helpers to enforce dose-trajectory continuity and collapse
// small ontology islands that are likely threshold jitter rather than true
// mechanistic reversals. These helpers operate only on C++-side mechanistic
// primitives and do not modify any Python/frontend logic.

bool dominanceSimilar(const MechanisticDominance& a, const MechanisticDominance& b, double tol = 0.12) {
    return (std::fabs(a.naDominance - b.naDominance) <= tol) &&
           (std::fabs(a.kDominance - b.kDominance) <= tol) &&
           (std::fabs(a.caDominance - b.caDominance) <= tol);
}

bool contextStableBetween(const MechanisticContext& a, const MechanisticContext& b, double rateTol = 0.20, double shiftTol = 0.18) {
    return (std::fabs(a.rateFraction - b.rateFraction) <= rateTol) &&
           (std::fabs(a.shiftMagnitude - b.shiftMagnitude) <= shiftTol) &&
           (std::fabs(a.instabilityIncrease - b.instabilityIncrease) <= 0.20);
}

bool isOntologyIsland(const std::vector<ResponseMode>& modes,
                      const std::vector<MechanisticContext>& contexts,
                      const std::vector<MechanisticDominance>& dominances,
                      std::size_t idx) {
    const std::size_t n = modes.size();
    if (n < 3U || idx == 0U || idx + 1U >= n) {
        return false;
    }
    if (modes[idx - 1U] == modes[idx + 1U] && modes[idx] != modes[idx - 1U]) {
        // neighbors agree but center differs
        // if center context and dominance are close to neighbors (small shift)
        const bool domLeft = dominanceSimilar(dominances[idx], dominances[idx - 1U]);
        const bool domRight = dominanceSimilar(dominances[idx], dominances[idx + 1U]);
        const bool ctxLeft = contextStableBetween(contexts[idx], contexts[idx - 1U]);
        const bool ctxRight = contextStableBetween(contexts[idx], contexts[idx + 1U]);
        const bool lowInstability = contexts[idx].instabilityIncrease < kStrongInstabilityThreshold;
        if (domLeft && domRight && ctxLeft && ctxRight && lowInstability) {
            return true;
        }
    }
    return false;
}

bool isTrueBiologicalTransition(const MechanisticContext& here, const MechanisticContext& neighbor) {
    // Treat as true transition if instability spikes, large seizure or NII jump,
    // or large rate-fraction reversal.
    if (here.instabilityIncrease >= kStrongInstabilityThreshold || neighbor.instabilityIncrease >= kStrongInstabilityThreshold) {
        return true;
    }
    if (std::fabs(here.seizureDelta - neighbor.seizureDelta) > 12.0) {
        return true;
    }
    if (std::fabs(here.rateFraction - neighbor.rateFraction) > 0.45) {
        return true;
    }
    return false;
}

// Recompute per-dose scientific confidence using neighborhood agreement and
// mechanistic persistence metrics.
double recomputeDoseConfidence(
    std::size_t idx,
    const std::vector<ResponseMode>& modes,
    const std::vector<MechanisticContext>& contexts,
    const std::vector<MechanisticDominance>& dominances,
    const DecisionStabilityInput& stabilityInput
) {
    const std::size_t n = modes.size();
    const ResponseMode mode = modes[idx];
    const MechanisticContext& ctx = contexts[idx];
    const MechanisticDominance& dom = dominances[idx];

    // neighbor agreement (immediate neighbors)
    int agree = 0;
    int total = 0;
    if (idx > 0U) {
        ++total;
        if (modes[idx - 1U] == mode) ++agree;
    }
    if (idx + 1U < n) {
        ++total;
        if (modes[idx + 1U] == mode) ++agree;
    }
    const double neighborAgreement = (total == 0) ? 1.0 : static_cast<double>(agree) / static_cast<double>(total);

    // dominance persistence: compare to neighbors
    double domPersistence = 1.0;
    if (idx > 0U && idx + 1U < n) {
        const MechanisticDominance avg = {
            0.5 * (dominances[idx - 1U].naDominance + dominances[idx + 1U].naDominance),
            0.5 * (dominances[idx - 1U].kDominance + dominances[idx + 1U].kDominance),
            0.5 * (dominances[idx - 1U].caDominance + dominances[idx + 1U].caDominance)
        };
        const double spread = dominanceSpread(avg) - dominanceSpread(dom);
        domPersistence = clamp01(1.0 - std::fabs(spread));
    }

    // slope/continuity: compare rateFraction to neighbors
    double slopeAgreement = 1.0;
    if (idx > 0U && idx + 1U < n) {
        const double neighAvg = 0.5 * (contexts[idx - 1U].rateFraction + contexts[idx + 1U].rateFraction);
        const double diff = std::fabs(ctx.rateFraction - neighAvg);
        slopeAgreement = clamp01(1.0 - (diff / 0.6));
    }

    const double reproducibility = clamp01(
        0.35 * clamp01(stabilityInput.runCount / 10.0) +
        0.35 * clamp01(1.0 - stabilityInput.rateStd / 5.0) +
        0.30 * clamp01(1.0 - stabilityInput.toxicityStd / 25.0)
    );

    const double consistency = modeConsistencyScore(mode, ctx, dom);

    // heavier weight to neighborAgreement and domPersistence to favor continuity
    const double score = clamp01(0.30 * consistency + 0.25 * neighborAgreement + 0.20 * domPersistence + 0.15 * slopeAgreement + 0.10 * reproducibility);
    return 100.0 * score;
}

// Main reconciliation pass: mutates modes and report.doseResults in-place to
// collapse isolated islands and enforce continuity while preserving true
// biological transitions.
void reconcileDoseTrajectory(
    std::vector<ResponseMode>& modes,
    std::vector<MechanisticContext>& contexts,
    std::vector<MechanisticDominance>& dominances,
    std::vector<DoseMechanisticResult>& doseResults,
    const DecisionStabilityInput& stabilityInput
) {
    const std::size_t n = modes.size();
    if (n < 3U) return;

    // Pass 1: detect and collapse simple island patterns (A A B A A -> A A A A A)
    std::vector<ResponseMode> adjusted = modes;
    for (std::size_t i = 1; i + 1U < n; ++i) {
        if (isOntologyIsland(modes, contexts, dominances, i)) {
            // merge center to neighbors' consensus
            adjusted[i] = modes[i - 1U];
        }
    }

    // Pass 2: conservative majority smoothing over window size 3
    for (std::size_t i = 1; i + 1U < n; ++i) {
        if (adjusted[i] != adjusted[i - 1U] && adjusted[i - 1U] == adjusted[i + 1U]) {
            // check if center is small deviation and not a true transition
            if (!isTrueBiologicalTransition(contexts[i], contexts[i - 1U]) && !isTrueBiologicalTransition(contexts[i], contexts[i + 1U])) {
                adjusted[i] = adjusted[i - 1U];
            }
        }
    }

    // Apply adjusted modes and recompute per-dose response strings and confidences
    for (std::size_t i = 0; i < n; ++i) {
        modes[i] = adjusted[i];
        doseResults[i].responseMode = toString(modes[i]);
        doseResults[i].scientificConfidence = recomputeDoseConfidence(i, modes, contexts, dominances, stabilityInput);
        // recompute seizure/early warning using updated mode and confidence
        const MechanisticContext& ctx = contexts[i];
        const double rateChangeFrac = ctx.rateFraction;
        const double syncDeltaLocal = ctx.syncDelta;
        const double niiDeltaLocal = ctx.niiDelta;
        const double seizureDeltaLocal = ctx.seizureDelta / 100.0;
        const double syncReductionPct = ctx.syncReductionPct;
        const double niiReductionPct = ctx.niiReductionPct;
        const double burstReductionPct = ctx.burstReductionPct;
        const double seizureReductionPct = ctx.seizureReductionPct;

        DoseRiskMetrics drm{
            rateChangeFrac,
            syncDeltaLocal,
            niiDeltaLocal,
            seizureDeltaLocal,
            syncReductionPct,
            niiReductionPct,
            seizureReductionPct,
            burstReductionPct,
            clamp01(ctx.seizureDelta / 100.0),
            0.0,
            clamp01(ctx.syncDelta),
            clamp01(ctx.burstReductionPct / 1.0),
            clamp01(ctx.niiDelta)
        };
        doseResults[i].seizureRisk = 100.0 * computeSeizureRiskForMode(doseResults[i].responseMode, drm);
        doseResults[i].earlyWarningIndex = computeEarlyWarningIndexForMode(doseResults[i].responseMode, drm, doseResults[i].scientificConfidence / 100.0);
    }
}

// Helper functions for text generation
std::string primaryChangeTextForState(
    BiologicalState state,
    double maxRateChangePct,
    double finalSyncDelta,
    double finalNiiDelta,
    double finalSeizureDelta
);

std::string safetyInterpretationForState(
    BiologicalState state,
    bool toxicityBeforeTherapy,
    bool toxicityAfterTherapy,
    bool lowStability,
    bool networkStabilizationObserved
);

std::string generateReasonForState(
    BiologicalState state,
    bool toxicityBeforeTherapy,
    bool toxicityAfterTherapy,
    bool lowStability,
    bool fragmentedWindow,
    bool therapeuticWindowExists,
    int runCount
);

// UNIVERSAL MECHANISM-FIRST RESPONSE DETECTOR
// Uses channel dominance, direction of electrophysiological change, and instability propagation.
[[maybe_unused]] ResponseMode detectResponseMode(
    double baselineRate,
    double baselineSync,
    double baselineBurst,
    double baselineNii,
    double baselineSeizure,
    [[maybe_unused]] double baselineIsiCv,
    const DoseObservation& finalObs,
    const std::vector<DoseObservation>& sortedObs,
    [[maybe_unused]] double maxRateChangePct,
    [[maybe_unused]] bool therapeuticWindowExists,
    [[maybe_unused]] double effectiveRangeMin,
    [[maybe_unused]] double effectiveRangeMax
) {
    const MechanisticContext context = buildMechanisticContext(
        baselineRate,
        baselineSync,
        baselineBurst,
        baselineNii,
        baselineSeizure,
        finalObs
    );
    const MechanisticDominance dominance = computeMechanisticDominance(finalObs, context);

    bool instabilitySeen = false;
    for (const auto& obs : sortedObs) {
        const MechanisticContext sweepContext = buildMechanisticContext(
            baselineRate,
            baselineSync,
            baselineBurst,
            baselineNii,
            baselineSeizure,
            obs
        );
        if (sweepContext.instabilityIncrease >= kStrongInstabilityThreshold ||
            sweepContext.niiDelta >= 0.30 ||
            sweepContext.seizureDelta >= 15.0) {
            instabilitySeen = true;
            break;
        }
    }

    const ResponseMode mode = resolveResponseMode(finalObs, context, dominance, instabilitySeen);
    return mode;
}

double computeBestSigmoidR2(const std::vector<double>& dose, const std::vector<double>& effect01) {
    if (dose.size() != effect01.size() || dose.size() < 4U) {
        return 0.0;
    }

    const auto [dminIt, dmaxIt] = std::minmax_element(dose.begin(), dose.end());
    const double dMin = *dminIt;
    const double dMax = *dmaxIt;

    double meanY = 0.0;
    for (double y : effect01) {
        meanY += y;
    }
    meanY /= static_cast<double>(effect01.size());

    double sst = 0.0;
    for (double y : effect01) {
        const double dy = y - meanY;
        sst += dy * dy;
    }
    if (sst <= 1.0e-12) {
        return 1.0;
    }

    double bestSse = std::numeric_limits<double>::infinity();
    constexpr double kMin = 0.02;
    constexpr double kMax = 2.50;
    constexpr double kStep = 0.02;
    const double dStep = std::max(0.25, (dMax - dMin) / 120.0);

    for (double k = kMin; k <= kMax + 1.0e-9; k += kStep) {
        for (double d50 = dMin; d50 <= dMax + 1.0e-9; d50 += dStep) {
            double numer = 0.0;
            double denom = 0.0;
            for (std::size_t i = 0; i < dose.size(); ++i) {
                const double z = std::clamp(-k * (dose[i] - d50), -60.0, 60.0);
                const double s = 1.0 / (1.0 + std::exp(z));
                numer += effect01[i] * s;
                denom += s * s;
            }
            if (denom <= 1.0e-12) {
                continue;
            }
            const double emax = std::clamp(numer / denom, 0.0, 1.0);

            double sse = 0.0;
            for (std::size_t i = 0; i < dose.size(); ++i) {
                const double z = std::clamp(-k * (dose[i] - d50), -60.0, 60.0);
                const double s = 1.0 / (1.0 + std::exp(z));
                const double pred = emax * s;
                const double err = effect01[i] - pred;
                sse += err * err;
            }
            bestSse = std::min(bestSse, sse);
        }
    }

    if (!std::isfinite(bestSse)) {
        return 0.0;
    }
    return std::clamp(1.0 - (bestSse / (sst + 1.0e-12)), -1.0, 1.0);
}

struct Range {
    double lo = 0.0;
    double hi = 0.0;
    std::size_t points = 0U;
};

std::vector<Range> contiguousRanges(const std::vector<double>& doses, double step) {
    std::vector<Range> out;
    if (doses.empty()) {
        return out;
    }

    const double expected = std::max(1.0e-6, step);
    const double eps = 1.0e-6;

    Range current{doses.front(), doses.front(), 1U};
    for (std::size_t i = 1; i < doses.size(); ++i) {
        const double gap = doses[i] - doses[i - 1U];
        if (gap <= expected + eps) {
            current.hi = doses[i];
            ++current.points;
            continue;
        }
        out.push_back(current);
        current = Range{doses[i], doses[i], 1U};
    }
    out.push_back(current);
    return out;
}

DrugRiskTier classifyTier(float seizureNorm, float suppressionNorm, float riskScore) {
    if (seizureNorm > 0.80f || suppressionNorm > 0.80f || riskScore >= 80.0f) {
        return DrugRiskTier::Toxic;
    }
    if (riskScore >= 65.0f) {
        return DrugRiskTier::HighRisk;
    }
    if (riskScore >= 35.0f) {
        return DrugRiskTier::ModerateRisk;
    }
    return DrugRiskTier::Safe;
}

// Implementation of helper functions for text generation
std::string primaryChangeTextForState(
    BiologicalState state,
    [[maybe_unused]] double maxRateChangePct,
    [[maybe_unused]] double finalSyncDelta,
    [[maybe_unused]] double finalNiiDelta,
    [[maybe_unused]] double finalSeizureDelta
) {
    switch (state) {
        case BiologicalState::NeuralSilencing:
            return "Strong sodium-channel block caused neural silencing";
        case BiologicalState::Hyperexcitability:
            return "Excitability and seizure-risk markers increased beyond therapeutic tolerance";
        case BiologicalState::ToxicInstability:
            return "Extreme instability: network metrics (NII, seizure) spiked beyond tolerance";
        case BiologicalState::NetworkStabilization:
            return "Calcium-channel blockade reduced synchronization and neural instability";
        case BiologicalState::ControlledSuppression:
            return "Moderate sodium-channel suppression observed with controlled activity";
        case BiologicalState::LimitedEffect:
        default:
            return "No significant neural response observed within tested dose range";
    }
}

std::string safetyInterpretationForState(
    BiologicalState state,
    bool toxicityBeforeTherapy,
    bool toxicityAfterTherapy,
    bool lowStability,
    [[maybe_unused]] bool networkStabilizationObserved
) {
    switch (state) {
        case BiologicalState::NeuralSilencing:
            return "Neural silencing at low dose is incompatible with therapeutic use";
        case BiologicalState::Hyperexcitability:
            return "Increased excitability and seizure-risk markers present unacceptable safety risk";
        case BiologicalState::ToxicInstability:
            return "Extreme network instability without prior therapeutic window";
        case BiologicalState::NetworkStabilization:
            return "Calcium-channel blockade reduced synchronization and neural instability without toxic excitation";
        case BiologicalState::ControlledSuppression:
            if (toxicityBeforeTherapy) {
                return "Toxicity appears before therapeutic response";
            } else if (toxicityAfterTherapy) {
                return "Therapeutic response observed before toxicity; narrow therapeutic window";
            } else if (lowStability) {
                return "Therapeutic window exists but with moderate variability";
            } else {
                return "Controlled suppression with acceptable safety margin";
            }
        case BiologicalState::LimitedEffect:
        default:
            return "No meaningful biological response detected";
    }
}

[[maybe_unused]] std::string generateReasonForState(
    BiologicalState state,
    bool toxicityBeforeTherapy,
    bool toxicityAfterTherapy,
    bool lowStability,
    bool fragmentedWindow,
    bool therapeuticWindowExists,
    [[maybe_unused]] int runCount
) {
    switch (state) {
        case BiologicalState::NeuralSilencing:
            return "Strong sodium-channel block caused neural silencing";
        case BiologicalState::Hyperexcitability:
            return "Excitability and seizure-risk markers increased beyond therapeutic tolerance";
        case BiologicalState::ToxicInstability:
            return "Extreme network instability detected; unacceptable toxicity without therapeutic benefit";
        case BiologicalState::NetworkStabilization:
            return "Calcium-channel blockade reduced synchronization and neural instability";
        case BiologicalState::ControlledSuppression:
            if (toxicityBeforeTherapy) {
                return "Toxicity appears before therapeutic response; unacceptable safety risk";
            } else if (toxicityAfterTherapy) {
                return "Therapeutic response observed before toxicity; narrow therapeutic window requires careful dosing";
            } else if (!therapeuticWindowExists) {
                return "No continuous therapeutic window identified despite some suppression";
            } else if (fragmentedWindow) {
                return "Fragmented therapeutic response reduces dosing reliability";
            } else if (lowStability) {
                return "Therapeutic window exists but with high inter-run variability";
            } else {
                return "Moderate sodium-channel suppression observed with controlled activity";
            }
        case BiologicalState::LimitedEffect:
        default:
            return "No significant neural response observed within tested dose range";
    }
}

} // namespace


std::string riskLevelText(DrugRiskTier tier) {
    switch (tier) {
        case DrugRiskTier::Safe:
            return "LOW";
        case DrugRiskTier::ModerateRisk:
            return "MODERATE";
        case DrugRiskTier::HighRisk:
        case DrugRiskTier::Toxic:
        default:
            return "HIGH";
    }
}

std::string confidenceFromEvidence(
    const DecisionStabilityInput& stability,
    double sigmoidR2,
    bool therapeuticWindowExists,
    bool fragmentedWindow
) {
    const bool lowStability = (stability.stabilityScore == "LOW") ||
                              (stability.rateStd >= 2.0f) ||
                              (stability.toxicityStd >= 10.0f);
    if (lowStability || stability.runCount < 3) {
        return "LOW";
    }
    if (stability.runCount >= 10 && sigmoidR2 >= 0.95 && therapeuticWindowExists && !fragmentedWindow) {
        return "HIGH";
    }
    if (stability.runCount >= 5 && sigmoidR2 >= 0.85) {
        return "MEDIUM";
    }
    return "LOW";
}

PharmaDecisionReport PharmaDecisionEngine::evaluate(
    const std::vector<DoseObservation>& observations,
    const DecisionStabilityInput& stabilityInput
) {
    PharmaDecisionReport report;
    if (observations.empty()) {
        return report;
    }

    std::vector<DoseObservation> sorted = observations;
    std::sort(sorted.begin(), sorted.end(), [](const DoseObservation& a, const DoseObservation& b) {
        return a.dose < b.dose;
    });

    report.points.reserve(sorted.size());
    report.features.reserve(sorted.size());

    report.minTestedDose = safeNonNegativeD(sorted.front().dose);
    report.maxTestedDose = safeNonNegativeD(sorted.back().dose);

    double inferredStep = 0.0;
    for (std::size_t i = 1; i < sorted.size(); ++i) {
        const double dx = safeNonNegativeD(sorted[i].dose) - safeNonNegativeD(sorted[i - 1U].dose);
        if (dx > 1.0e-6) {
            inferredStep = (inferredStep <= 0.0) ? dx : std::min(inferredStep, dx);
        }
    }
    report.stepDose = inferredStep;
    report.rateVariability = std::max(0.0f, stabilityInput.rateStd);
    report.toxicityVariability = std::max(0.0f, stabilityInput.toxicityStd);
    report.stabilityScore = stabilityInput.stabilityScore.empty() ? "UNSPECIFIED" : stabilityInput.stabilityScore;

    const double baselineRate = std::max(1.0e-6, safeNonNegativeD(sorted.front().meanFiringRateHz));
    const double baselineSync = safeNonNegativeD(sorted.front().synchronizationIndex);
    const double baselineBurst = safeNonNegativeD(sorted.front().burstIndex);
    const double baselineNii = safeNonNegativeD(sorted.front().nii);
    [[maybe_unused]] const double baselineIsiCv = safeNonNegativeD(sorted.front().isiCv);
    const double baselineSeizure = safeNonNegativeD(sorted.front().seizureProbabilityPct);

    double maxRateChangePct = 0.0;
    bool hasOnsetDose = false;
    double onsetDose = 0.0;
    std::vector<double> xDose;
    std::vector<double> suppressionCurveEffect;
    std::vector<double> excitationCurveEffect;
    std::vector<double> stabilizationCurveEffect;
    std::vector<double> noSignificantCurveEffect;
    std::vector<double> therapeuticDoses;
    std::vector<double> safeDoses;
    std::vector<double> excitatoryRiskDoses;
    std::vector<double> overSuppressionDoses;
    std::vector<double> stabilizationSaturationDoses;

    std::vector<ResponseMode> doseModes;
    std::vector<MechanisticContext> doseContexts;
    std::vector<MechanisticDominance> doseDominances;

    auto confidenceLabel = [](double scientificConfidence) {
        if (scientificConfidence >= 80.0) {
            return std::string("HIGH");
        }
        if (scientificConfidence >= 55.0) {
            return std::string("MEDIUM");
        }
        return std::string("LOW");
    };

    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const DoseObservation& obs = sorted[i];

        const float dose = safeNonNegative(obs.dose);
        const float seizurePct = safeNonNegative(obs.seizureProbabilityPct);
        const float suppressionPct = safeNonNegative(obs.suppressionPct);
        const float syncValue = safeNonNegative(obs.synchronizationIndex);
        const float burstValue = safeNonNegative(obs.burstIndex);
        const float niiValue = safeNonNegative(obs.nii);
        const float isiCv = safeNonNegative(obs.isiCv);

        const float seizureNorm = clamp01(seizurePct / 100.0f);
        const float suppressionNorm = clamp01(suppressionPct / 100.0f);
        const float syncNorm = clamp01(syncValue);
        const float burstNorm = clamp01(burstValue / 0.20f);
        const float niiNorm = clamp01(niiValue);

        const MechanisticContext context = buildMechanisticContext(
            baselineRate,
            baselineSync,
            baselineBurst,
            baselineNii,
            baselineSeizure,
            obs
        );
        const MechanisticDominance dominance = computeMechanisticDominance(obs, context);
        const bool sweepInstability =
            context.instabilityIncrease >= kStrongInstabilityThreshold ||
            context.niiDelta >= 0.30 ||
            context.seizureDelta >= 15.0;
        const ResponseMode perDoseMode = resolveResponseMode(obs, context, dominance, sweepInstability);
        const BiologicalState perDoseState = biologicalStateFromMode(perDoseMode);

        doseModes.push_back(perDoseMode);
        doseContexts.push_back(context);
        doseDominances.push_back(dominance);

        const double rateChangeFrac = context.rateFraction;
        const double syncDeltaLocal = context.syncDelta;
        const double niiDeltaLocal = context.niiDelta;
        const double seizureDeltaLocal = context.seizureDelta / 100.0;
        const double syncReductionPct = context.syncReductionPct;
        const double niiReductionPct = context.niiReductionPct;
        const double burstReductionPct = context.burstReductionPct;
        const double seizureReductionPct = context.seizureReductionPct;

        // Toxicity score remains a composite for reporting, but label toxic when state indicates instability
        const double toxicityScore = 100.0 * std::clamp(
            0.55 * static_cast<double>(seizureNorm) +
            0.30 * static_cast<double>(syncNorm) +
            0.15 * std::clamp(0.20 - static_cast<double>(isiCv), 0.0, 0.20) / 0.20,
            0.0,
            1.0
        );

        // Keep direction for biological-state decisions, but expose positive effect magnitude
        // for curve/window analysis so suppressive drugs are not mis-labeled as no response.
        const double signedRateChangePct = rateChangeFrac * 100.0; // + = excitation, - = suppression
        const double suppressionEffectPct = std::max(0.0, -signedRateChangePct);
        const double excitationEffectPct = std::max(0.0, signedRateChangePct);
        const double stabilizationEffectPct =
            (perDoseState == BiologicalState::NetworkStabilization)
                ? std::max({syncReductionPct, niiReductionPct, seizureReductionPct, burstReductionPct})
                : 0.0;
        const double effectMagnitudePct = std::clamp(
            std::max({suppressionEffectPct, excitationEffectPct, stabilizationEffectPct}),
            0.0,
            100.0
        );
        const bool isEffective = (perDoseState == BiologicalState::ControlledSuppression || perDoseState == BiologicalState::NetworkStabilization);
        const bool isToxic = (perDoseState == BiologicalState::ToxicInstability || perDoseState == BiologicalState::NeuralSilencing || perDoseState == BiologicalState::Hyperexcitability);

        report.features.push_back(DoseFeatures{
            static_cast<double>(dose),
            effectMagnitudePct,
            static_cast<double>(syncNorm),
            static_cast<double>(isiCv),
            static_cast<double>(seizureNorm),
            toxicityScore,
            isEffective,
            isToxic
        });

        maxRateChangePct = std::max(maxRateChangePct, effectMagnitudePct);
        xDose.push_back(static_cast<double>(dose));
        suppressionCurveEffect.push_back(std::clamp(suppressionEffectPct / 100.0, 0.0, 1.0));
        excitationCurveEffect.push_back(std::clamp(excitationEffectPct / 100.0, 0.0, 1.0));
        stabilizationCurveEffect.push_back(std::clamp(stabilizationEffectPct / 100.0, 0.0, 1.0));
        noSignificantCurveEffect.push_back(std::clamp(effectMagnitudePct / 100.0, 0.0, 1.0));

        const float instabilityMetric = clamp01(0.50f * syncNorm + 0.30f * burstNorm + 0.20f * niiNorm);
        const float riskNorm = clamp01(0.50f * seizureNorm + 0.30f * suppressionNorm + 0.20f * instabilityMetric);
        const float riskScore = 100.0f * riskNorm;

        float seizureSlopePctPerDose = 0.0f;
        if (i > 0U) {
            const float deltaDose = std::max(1.0e-6f, dose - sorted[i - 1U].dose);
            seizureSlopePctPerDose = (seizurePct - sorted[i - 1U].seizureProbabilityPct) / deltaDose;
        }

        const float slopeNorm = clamp01(std::max(0.0f, seizureSlopePctPerDose) / 25.0f);
        const float earlyWarningIndex = 100.0f * clamp01(
            0.55f * seizureNorm +
            0.30f * slopeNorm +
            0.15f * instabilityMetric
        );

        const DrugRiskTier tier = classifyTier(seizureNorm, suppressionNorm, riskScore);

        report.points.push_back(DrugDecisionPoint{
            dose,
            seizurePct,
            suppressionPct,
            riskScore,
            toString(tier),
            earlyWarningIndex,
            seizureSlopePctPerDose
        });

        report.peakRiskScore = std::max(report.peakRiskScore, riskScore);
        report.peakSeizureProbabilityPct = std::max(report.peakSeizureProbabilityPct, seizurePct);
        report.peakSuppressionPct = std::max(report.peakSuppressionPct, suppressionPct);
        report.peakEarlyWarningIndex = std::max(report.peakEarlyWarningIndex, earlyWarningIndex);
        report.maxSeizureSlopePctPerDose = std::max(report.maxSeizureSlopePctPerDose, seizureSlopePctPerDose);

        // Classify dose ranges according to biological state
        if (perDoseMode == ResponseMode::NoSignificantResponse) {
            safeDoses.push_back(static_cast<double>(dose));
        }
        if (perDoseMode == ResponseMode::SuppressiveResponse || perDoseMode == ResponseMode::StabilizingResponse) {
            therapeuticDoses.push_back(static_cast<double>(dose));
            if (!hasOnsetDose) {
                hasOnsetDose = true;
                onsetDose = static_cast<double>(dose);
                report.hasSuppressionThreshold = true;
                report.suppressionThresholdDose = dose;
                report.hasEffectiveDose = true;
                report.effectiveMinDose = dose;
            }
        }
        const bool excitatoryRiskHere =
            (perDoseMode == ResponseMode::ExcitatoryResponse) ||
            (perDoseMode == ResponseMode::ToxicInstability && (rateChangeFrac > -0.10 || syncDeltaLocal > +0.10 || niiDeltaLocal > +0.10 || seizureDeltaLocal > +0.10)) ||
            (excitationEffectPct > 60.0 && (rateChangeFrac > 0.0 || syncDeltaLocal > 0.0 || niiDeltaLocal > 0.0 || seizureDeltaLocal > 0.0)) ||
            (niiDeltaLocal > +0.40 && (rateChangeFrac > -0.10 || syncDeltaLocal > +0.10 || seizureDeltaLocal > +0.10)) ||
            (seizureDeltaLocal > +0.40 && (rateChangeFrac > -0.10 || syncDeltaLocal > +0.10 || niiDeltaLocal > +0.10));
        const bool overSuppressionHere =
            (suppressionEffectPct > 60.0) ||
            (perDoseMode == ResponseMode::NeuralSilencing);
        const bool stabilizationSaturationHere =
            (perDoseMode == ResponseMode::StabilizingResponse) &&
            (stabilizationEffectPct > 60.0);

        if (perDoseMode == ResponseMode::ExcitatoryResponse || perDoseMode == ResponseMode::ToxicInstability) {
            excitatoryRiskDoses.push_back(static_cast<double>(dose));
        }

        if (overSuppressionHere) {
            overSuppressionDoses.push_back(static_cast<double>(dose));
        }

        if (stabilizationSaturationHere) {
            stabilizationSaturationDoses.push_back(static_cast<double>(dose));
        }

        if ((excitatoryRiskHere || overSuppressionHere) && !report.hasToxicThreshold) {
            report.hasToxicThreshold = true;
            report.toxicMinDose = dose;
            report.hasToxicThresholdExact = true;
            report.toxicThresholdDoseEval = static_cast<double>(dose);
            report.toxicThresholdText = std::to_string(report.toxicThresholdDoseEval);
        }

        const double doseConfidence = mechanisticConfidenceScore(
            perDoseMode,
            context,
            dominance,
            stabilityInput,
            true,
            1.0 - modeConsistencyScore(perDoseMode, context, dominance)
        );
        report.doseResults.push_back(DoseMechanisticResult{
            static_cast<double>(dose),
            toString(perDoseMode),
            dominance,
            doseConfidence,
            100.0 * computeSeizureRiskForMode(toString(perDoseMode), {
                rateChangeFrac,
                syncDeltaLocal,
                niiDeltaLocal,
                seizureDeltaLocal,
                syncReductionPct,
                niiReductionPct,
                seizureReductionPct,
                burstReductionPct,
                seizureNorm,
                suppressionNorm,
                syncNorm,
                burstNorm,
                niiNorm
            }),
            computeEarlyWarningIndexForMode(toString(perDoseMode), {
                rateChangeFrac,
                syncDeltaLocal,
                niiDeltaLocal,
                seizureDeltaLocal,
                syncReductionPct,
                niiReductionPct,
                seizureReductionPct,
                burstReductionPct,
                seizureNorm,
                suppressionNorm,
                syncNorm,
                burstNorm,
                niiNorm
            }, doseConfidence / 100.0),
            context.instabilityIncrease,
            primaryChangeTextForState(perDoseState, 0.0, syncDeltaLocal, niiDeltaLocal, seizureDeltaLocal)
        });
    }

    if (std::isfinite(maxRateChangePct)) {
        report.maxRateChangePct = maxRateChangePct;
    } else {
        report.maxRateChangePct = 0.0;
    }

    // Reconcile per-dose provisional modes into a trajectory-aware set
    // This collapses isolated islands and smooths single-dose flips while
    // preserving true biological transitions (instability, seizure spikes).
    if (!doseModes.empty() && doseModes.size() == report.doseResults.size()) {
        reconcileDoseTrajectory(doseModes, doseContexts, doseDominances, report.doseResults, stabilityInput);
    }

    // Clear and rebuild classification-based dose lists using reconciled modes
    safeDoses.clear();
    therapeuticDoses.clear();
    excitatoryRiskDoses.clear();
    overSuppressionDoses.clear();
    stabilizationSaturationDoses.clear();
    for (std::size_t i = 0; i < report.doseResults.size(); ++i) {
        const auto& dr = report.doseResults[i];
        const double dose = dr.dose;
        const ResponseMode m = (dr.responseMode == "EXCITATORY_RESPONSE") ? ResponseMode::ExcitatoryResponse :
                               (dr.responseMode == "SUPPRESSIVE_RESPONSE") ? ResponseMode::SuppressiveResponse :
                               (dr.responseMode == "STABILIZING_RESPONSE") ? ResponseMode::StabilizingResponse :
                               (dr.responseMode == "NEURAL_SILENCING") ? ResponseMode::NeuralSilencing :
                               (dr.responseMode == "TOXIC_INSTABILITY") ? ResponseMode::ToxicInstability : ResponseMode::NoSignificantResponse;

        if (m == ResponseMode::NoSignificantResponse) {
            safeDoses.push_back(dose);
        }
        if (m == ResponseMode::SuppressiveResponse || m == ResponseMode::StabilizingResponse) {
            therapeuticDoses.push_back(dose);
        }
        if (m == ResponseMode::ExcitatoryResponse || m == ResponseMode::ToxicInstability) {
            excitatoryRiskDoses.push_back(dose);
        }
        if (m == ResponseMode::NeuralSilencing || (m == ResponseMode::SuppressiveResponse && dr.instabilityScore > 0.50)) {
            overSuppressionDoses.push_back(dose);
        }
        if (m == ResponseMode::StabilizingResponse && dr.instabilityScore > 0.25) {
            stabilizationSaturationDoses.push_back(dose);
        }
    }

    std::sort(safeDoses.begin(), safeDoses.end());
    std::sort(therapeuticDoses.begin(), therapeuticDoses.end());
    std::sort(excitatoryRiskDoses.begin(), excitatoryRiskDoses.end());
    std::sort(overSuppressionDoses.begin(), overSuppressionDoses.end());
    std::sort(stabilizationSaturationDoses.begin(), stabilizationSaturationDoses.end());

    safeDoses.erase(
        std::unique(safeDoses.begin(), safeDoses.end(), [](double a, double b) {
            return std::fabs(a - b) <= 1.0e-6;
        }),
        safeDoses.end()
    );
    therapeuticDoses.erase(
        std::unique(therapeuticDoses.begin(), therapeuticDoses.end(), [](double a, double b) {
            return std::fabs(a - b) <= 1.0e-6;
        }),
        therapeuticDoses.end()
    );
    excitatoryRiskDoses.erase(
        std::unique(excitatoryRiskDoses.begin(), excitatoryRiskDoses.end(), [](double a, double b) {
            return std::fabs(a - b) <= 1.0e-6;
        }),
        excitatoryRiskDoses.end()
    );
    overSuppressionDoses.erase(
        std::unique(overSuppressionDoses.begin(), overSuppressionDoses.end(), [](double a, double b) {
            return std::fabs(a - b) <= 1.0e-6;
        }),
        overSuppressionDoses.end()
    );
    stabilizationSaturationDoses.erase(
        std::unique(stabilizationSaturationDoses.begin(), stabilizationSaturationDoses.end(), [](double a, double b) {
            return std::fabs(a - b) <= 1.0e-6;
        }),
        stabilizationSaturationDoses.end()
    );

    const auto safeRanges = contiguousRanges(safeDoses, report.stepDose);
    const auto therapeuticRanges = contiguousRanges(therapeuticDoses, report.stepDose);
    const auto excitatoryRiskRanges = contiguousRanges(excitatoryRiskDoses, report.stepDose);
    const auto overSuppressionRanges = contiguousRanges(overSuppressionDoses, report.stepDose);
    const auto stabilizationSaturationRanges = contiguousRanges(stabilizationSaturationDoses, report.stepDose);

    report.overSuppressionDoses = overSuppressionDoses;
    report.excitatoryRiskDoses = excitatoryRiskDoses;
    report.stabilizationSaturationDoses = stabilizationSaturationDoses;

    if (!safeRanges.empty()) {
        const auto widestSafe = std::max_element(safeRanges.begin(), safeRanges.end(), [](const Range& a, const Range& b) {
            return (a.hi - a.lo) < (b.hi - b.lo);
        });
        report.hasSafeRange = true;
        report.safeMinDose = safeNonNegative(static_cast<float>(widestSafe->lo));
        report.safeMaxDose = safeNonNegative(static_cast<float>(widestSafe->hi));
    }

    if (!therapeuticRanges.empty()) {
        const auto widestTherapeutic = std::max_element(
            therapeuticRanges.begin(),
            therapeuticRanges.end(),
            [](const Range& a, const Range& b) {
                return (a.hi - a.lo) < (b.hi - b.lo);
            }
        );
        report.hasContinuousEffectiveWindow = true;
        report.effectiveRangeMin = widestTherapeutic->lo;
        report.effectiveRangeMax = widestTherapeutic->hi;
        report.windowQuality = "Continuous";
        report.hasTherapeuticWindow = true;
        report.therapeuticWindow = static_cast<float>(report.effectiveRangeMax - report.effectiveRangeMin);
        // Enforce consistency: onset must match the therapeutic range start.
        report.hasSuppressionThreshold = true;
        report.suppressionThresholdDose = report.effectiveRangeMin;
        report.hasEffectiveDose = true;
        report.effectiveMinDose = static_cast<float>(report.effectiveRangeMin);
        onsetDose = report.effectiveRangeMin;
        hasOnsetDose = true;
    } else {
        report.hasContinuousEffectiveWindow = false;
        report.windowQuality = "Not well-defined";
        report.hasTherapeuticWindow = false;
        report.therapeuticWindow = 0.0f;
    }

    const bool therapeuticWindowExists = report.hasContinuousEffectiveWindow && !therapeuticRanges.empty();
    const bool lowStability = (report.stabilityScore == "LOW") ||
                              (report.rateVariability >= 2.0f) ||
                              (report.toxicityVariability >= 10.0f);
    const bool fragmentedWindow = therapeuticRanges.size() > 1U;
    const auto finalObs = sorted.back();
    const ResponseMode finalMode = doseModes.empty() ? ResponseMode::NoSignificantResponse : doseModes.back();
    const MechanisticContext finalContext = doseContexts.empty() ? MechanisticContext{} : doseContexts.back();
    const MechanisticDominance finalDominance = doseDominances.empty() ? MechanisticDominance{} : doseDominances.back();
    const double finalSyncReductionPct = finalContext.syncReductionPct;
    const double finalNiiReductionPct = finalContext.niiReductionPct;
    const double finalNiiIncreasePct = std::max(0.0, finalContext.niiDelta);
    const double finalSeizureReductionPct = finalContext.seizureReductionPct;
    const double finalBurstReductionPct = finalContext.burstReductionPct;
    const bool finalMeaningfulCaBlock = finalObs.blockCa >= kMeaningfulBlockThreshold;
    report.syncReductionPct = std::max(0.0, finalSyncReductionPct);
    report.niiReductionPct = std::max(0.0, finalNiiReductionPct);
    report.niiIncreasePct = std::max(0.0, finalNiiIncreasePct);
    report.seizureReductionPct = std::max(0.0, finalSeizureReductionPct);
    report.burstReductionPct = std::max(0.0, finalBurstReductionPct);
    report.calciumEffectMagnitude = std::max({report.syncReductionPct, report.niiReductionPct, report.seizureReductionPct, report.burstReductionPct});
    report.meaningfulCaBlock = finalMeaningfulCaBlock;

    report.responseMode = toString(finalMode);
    report.biologicalState = biologicalStateFromMode(finalMode);

    std::size_t modeTransitions = 0U;
    for (std::size_t i = 1; i < doseModes.size(); ++i) {
        if (doseModes[i] != doseModes[i - 1U]) {
            ++modeTransitions;
        }
    }
    const bool contiguousMode = modeTransitions == 0U;
    const double localVariancePenalty = doseModes.size() > 1U ? static_cast<double>(modeTransitions) / static_cast<double>(doseModes.size() - 1U) : 0.0;
    report.scientificConfidence = mechanisticConfidenceScore(
        finalMode,
        finalContext,
        finalDominance,
        stabilityInput,
        contiguousMode,
        localVariancePenalty
    );
    report.confidence = confidenceLabel(report.scientificConfidence);

    report.mechanisticSummary =
        report.responseMode == "NO_SIGNIFICANT_RESPONSE" ? "Weak conductance perturbation without coherent mechanistic direction" :
        report.responseMode == "SUPPRESSIVE_RESPONSE" ? "Na-dominant suppression lowered firing without collapse" :
        report.responseMode == "EXCITATORY_RESPONSE" ? "K-dominant excitation propagated instability" :
        report.responseMode == "STABILIZING_RESPONSE" ? "Ca-dominant stabilization reduced sync and NII" :
        report.responseMode == "NEURAL_SILENCING" ? "Catastrophic firing collapse caused neural silencing" :
        "Pathological instability dominated the dose-response sweep";

    report.ontologyReasoning =
        "Dominance: Na=" + std::to_string(finalDominance.naDominance) +
        ", K=" + std::to_string(finalDominance.kDominance) +
        ", Ca=" + std::to_string(finalDominance.caDominance) +
        "; direction=" + report.responseMode +
        "; confidence=" + report.confidence;

    auto buildSegment = [&](std::size_t beginIndex, std::size_t endIndex) {
        double confidenceSum = 0.0;
        double rateSum = 0.0;
        double syncDeltaSum = 0.0;
        double niiDeltaSum = 0.0;
        double seizureDeltaSum = 0.0;
        for (std::size_t i = beginIndex; i <= endIndex; ++i) {
            confidenceSum += report.doseResults[i].scientificConfidence;
            rateSum += doseContexts[i].rateFraction * baselineRate;
            syncDeltaSum += doseContexts[i].syncDelta;
            niiDeltaSum += doseContexts[i].niiDelta;
            seizureDeltaSum += doseContexts[i].seizureDelta;
        }
        const double count = static_cast<double>(endIndex - beginIndex + 1U);
        report.timelineSegments.push_back(TimelineSegment{
            report.doseResults[beginIndex].dose,
            report.doseResults[endIndex].dose,
            report.doseResults[beginIndex].responseMode,
            confidenceSum / count,
            rateSum / count,
            syncDeltaSum / count,
            niiDeltaSum / count,
            seizureDeltaSum / count
        });
    };

    if (!report.doseResults.empty()) {
        std::size_t segmentStart = 0U;
        for (std::size_t i = 1; i < report.doseResults.size(); ++i) {
            if (report.doseResults[i].responseMode != report.doseResults[i - 1U].responseMode) {
                buildSegment(segmentStart, i - 1U);
                segmentStart = i;
            }
        }
        buildSegment(segmentStart, report.doseResults.size() - 1U);
    }

    auto fitCurveForMode = [&](const std::string& mode) -> std::vector<double> {
        std::vector<double> curve;
        curve.reserve(xDose.size());
        const std::vector<double>* source = &suppressionCurveEffect;
        if (mode == "NO_SIGNIFICANT_RESPONSE") {
            source = &noSignificantCurveEffect;
        } else if (mode == "EXCITATORY_RESPONSE") {
            source = &excitationCurveEffect;
        } else if (mode == "STABILIZING_RESPONSE") {
            source = &stabilizationCurveEffect;
        }
        curve = *source;
        for (std::size_t i = 1; i < curve.size(); ++i) {
            if (curve[i] < curve[i - 1U]) {
                curve[i] = curve[i - 1U];
            }
        }
        return curve;
    };

    report.sigmoidR2 = computeBestSigmoidR2(xDose, fitCurveForMode(report.responseMode));
    if (report.sigmoidR2 >= 0.95) {
        report.curveType = "Sigmoidal";
    } else if (report.sigmoidR2 >= 0.85) {
        report.curveType = "Weak Sigmoidal";
    } else {
        report.curveType = "Non-sigmoidal";
    }
    const bool toxicityBeforeTherapy = report.hasToxicThreshold && (!therapeuticWindowExists || report.toxicThresholdDoseEval <= report.effectiveRangeMin);
    const bool toxicityAfterTherapy = report.hasToxicThreshold && therapeuticWindowExists && report.toxicThresholdDoseEval > report.effectiveRangeMax;
    const bool stableTherapeuticWindow = therapeuticWindowExists && !lowStability && !report.hasToxicThreshold && !fragmentedWindow;

    report.biologicalStateText = toString(report.biologicalState);
    report.primaryChangeText = primaryChangeTextForState(report.biologicalState, report.maxRateChangePct, finalContext.syncDelta, finalContext.niiDelta, finalContext.seizureDelta);
    report.safetyInterpretationText = safetyInterpretationForState(report.biologicalState, toxicityBeforeTherapy, toxicityAfterTherapy, lowStability, report.responseMode == "STABILIZING_RESPONSE");
    report.seizureTrendText = finalContext.seizureDelta > 0.0
                                  ? std::string("Seizure-risk markers increased with dose")
                                  : (finalContext.seizureDelta < 0.0
                                         ? std::string("Seizure-risk markers decreased with dose")
                                         : std::string("Seizure-risk markers remained broadly stable"));

    if (report.responseMode == "NO_SIGNIFICANT_RESPONSE") {
        report.recommendation = "LIMITED EFFICACY";
        report.riskLevel = "LOW";
        report.reason = "Weak conductance perturbation without coherent mechanistic direction";
        report.overallTier = DrugRiskTier::Safe;
    } else if (report.responseMode == "SUPPRESSIVE_RESPONSE") {
        report.recommendation = lowStability ? "CAUTION" : "PROMISING";
        report.riskLevel = lowStability ? "MODERATE" : "LOW";
        report.reason = toxicityBeforeTherapy
            ? "Na-dominant suppression appears before a safe therapeutic window"
            : "Na-dominant suppression observed without collapse";
        report.overallTier = lowStability ? DrugRiskTier::ModerateRisk : DrugRiskTier::Safe;
    } else if (report.responseMode == "STABILIZING_RESPONSE") {
        report.recommendation = (report.scientificConfidence >= 70.0 && !lowStability) ? "PROMISING" : "CAUTION";
        report.riskLevel = (report.scientificConfidence >= 70.0 && !lowStability) ? "LOW" : "MODERATE";
        report.reason = "Ca-dominant stabilization reduced synchronization and network instability";
        report.overallTier = (report.scientificConfidence >= 70.0 && !lowStability) ? DrugRiskTier::Safe : DrugRiskTier::ModerateRisk;
    } else if (report.responseMode == "EXCITATORY_RESPONSE") {
        report.recommendation = "NOT RECOMMENDED";
        report.riskLevel = "HIGH";
        report.reason = "K-dominant excitation propagated instability and seizure risk";
        report.overallTier = DrugRiskTier::Toxic;
    } else if (report.responseMode == "NEURAL_SILENCING") {
        report.recommendation = "NOT RECOMMENDED";
        report.riskLevel = "HIGH";
        report.reason = "Catastrophic suppression collapse produced neural silencing";
        report.overallTier = DrugRiskTier::Toxic;
    } else {
        report.recommendation = "NOT RECOMMENDED";
        report.riskLevel = "HIGH";
        report.reason = "Pathological instability dominated the dose-response sweep";
        report.overallTier = DrugRiskTier::Toxic;
    }

    if (report.responseMode == "TOXIC_INSTABILITY") {
        report.recommendation = "NOT RECOMMENDED";
        report.riskLevel = "HIGH";
        report.reason = toxicityBeforeTherapy
            ? "Pathological instability appears before a therapeutic window"
            : "Pathological instability dominates the sweep";
        report.overallTier = DrugRiskTier::Toxic;
    } else if (stableTherapeuticWindow && report.responseMode == "SUPPRESSIVE_RESPONSE") {
        report.recommendation = "PROMISING";
        report.riskLevel = "LOW";
        report.reason = "Controlled sodium-channel suppression observed with coherent dose continuity";
        report.overallTier = DrugRiskTier::Safe;
    }

    report.hasToxicThresholdExact = report.hasToxicThreshold;
    report.toxicThresholdText = report.hasToxicThreshold ? std::to_string(report.toxicThresholdDoseEval)
                                                         : ">" + std::to_string(report.maxTestedDose);

    const double midDose = 0.5 * (report.minTestedDose + report.maxTestedDose);
    if (report.sigmoidR2 >= 0.95 && hasOnsetDose && onsetDose <= midDose) {
        report.responseStrength = "Strong";
    } else if (report.sigmoidR2 >= 0.85) {
        report.responseStrength = "Moderate-to-Strong";
    } else {
        report.responseStrength = "Weak";
    }

    if (report.responseMode == "STABILIZING_RESPONSE" && report.sigmoidR2 >= 0.95) {
        report.curveType = "Sigmoidal Stabilization";
    } else if (report.responseMode == "STABILIZING_RESPONSE") {
        report.curveType = "Stabilizing Response";
    }

    report.hasToxicThresholdExact = report.hasToxicThreshold;
    report.toxicThresholdText = report.hasToxicThreshold ? std::to_string(report.toxicThresholdDoseEval)
                                                         : ">" + std::to_string(report.maxTestedDose);

    return report;
}

std::string PharmaDecisionEngine::toString(ResponseMode mode) {
    return ::spp::analyzer::toString(mode);
}

std::string PharmaDecisionEngine::toString(DrugRiskTier tier) {
    switch (tier) {
        case DrugRiskTier::Safe:
            return "Safe";
        case DrugRiskTier::ModerateRisk:
            return "Moderate Risk";
        case DrugRiskTier::HighRisk:
            return "High Risk";
        case DrugRiskTier::Toxic:
            return "Toxic";
        default:
            return "Safe";
    }
}

std::string PharmaDecisionEngine::toString(BiologicalState state) {
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
            return "Limited Effect";
    }
}

} // namespace spp::analyzer
