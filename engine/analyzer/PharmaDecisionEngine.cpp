#include "PharmaDecisionEngine.h"
// This file implements the core logic for analyzing dose-response data and generating a PharmaDecisionReport.
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace spp::analyzer {

namespace {
constexpr float kMeaningfulKBlockThreshold = 0.15f;

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

double increasePercent(double baseline, double current) {
    if (!std::isfinite(baseline) || !std::isfinite(current) || baseline <= 1.0e-6) {
        return 0.0;
    }
    return ((current - baseline) / baseline) * 100.0;
}

bool stabilityScoreIsMediumOrHigh(const std::string& stabilityScore) {
    return stabilityScore == "MEDIUM" || stabilityScore == "HIGH";
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
    if (responseMode == "EXCITATORY_RESPONSE") {
        return computeExcitatorySeizureRisk(metrics);
    }
    if (responseMode == "STABILIZING_RESPONSE") {
        return computeStabilizingSeizureRisk(metrics);
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
    return 100.0 * clamp01(
        0.55 * seizureRisk +
        0.25 * instabilityMetric(metrics) +
        0.20 * suppressionProtectionMetric(metrics)
    );
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

// UNIVERSAL BIOLOGICAL STATE DETECTOR
// Uses multi-dimensional metrics: firing rate, synchronization, NII, seizure, ISI_CV
// Detects K-channel (hyperexcitability), Ca-channel (stabilization), and Na-channel effects
BiologicalState detectBiologicalState(
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
    const double currentRate = safeNonNegativeD(finalObs.meanFiringRateHz);
    const double rateChange =
        (currentRate - baselineRate) / std::max(1.0, baselineRate);

    const double currentSync =
        safeNonNegativeD(finalObs.synchronizationIndex);
    const double currentBurst =
        safeNonNegativeD(finalObs.burstIndex);

    const double syncChange = currentSync - baselineSync;

    const double syncReductionPct =
        reductionPercent(baselineSync, currentSync);

    const double burstReductionPct =
        reductionPercent(baselineBurst, currentBurst);

    const double currentNii =
        safeNonNegativeD(finalObs.nii);

    const double niiChange =
        currentNii - baselineNii;

    const double niiReductionPct =
        reductionPercent(baselineNii, currentNii);

    const double currentSeiz =
        safeNonNegativeD(finalObs.seizureProbabilityPct) / 100.0;

    const double seizChange =
        currentSeiz - (baselineSeizure / 100.0);

    const double seizureReductionPct =
        reductionPercent(
            baselineSeizure,
            safeNonNegativeD(finalObs.seizureProbabilityPct));

    bool sawToxicInstability = false;
    bool sawNetworkStabilization = false;

    for (const auto& obs : sortedObs) {

        const double oNiiChange =
            safeNonNegativeD(obs.nii) - baselineNii;

        const double oSeizChange =
            safeNonNegativeD(obs.seizureProbabilityPct) / 100.0 -
            (baselineSeizure / 100.0);

        if (oNiiChange > 0.40 || oSeizChange > 0.40) {
            sawToxicInstability = true;
        }

        const double oRate =
            safeNonNegativeD(obs.meanFiringRateHz);

        const double oRateChange =
            (oRate - baselineRate) /
            std::max(1.0, baselineRate);

        if (obs.blockK >= kMeaningfulKBlockThreshold) {
            if (oRateChange > 0.25 ||
                oNiiChange > 0.20 ||
                oSeizChange > 0.20) {
                return BiologicalState::Hyperexcitability;
            }
        }

        const double oSyncReductionPct =
            reductionPercent(
                baselineSync,
                safeNonNegativeD(obs.synchronizationIndex));

        const double oNiiReductionPct =
            reductionPercent(
                baselineNii,
                safeNonNegativeD(obs.nii));

        const double oSeizReductionPct =
            reductionPercent(
                baselineSeizure,
                safeNonNegativeD(obs.seizureProbabilityPct));

        const double oBurstReductionPct =
            reductionPercent(
                baselineBurst,
                safeNonNegativeD(obs.burstIndex));

        if (obs.blockCa >= kMeaningfulKBlockThreshold &&
            oSyncReductionPct >= 15.0 &&
            oNiiReductionPct >= 15.0 &&
            oSeizReductionPct >= 15.0) {

            sawNetworkStabilization = true;
        }
    }

    // 1. Neural silencing
    if (currentRate < 0.10 * baselineRate) {
        return BiologicalState::NeuralSilencing;
    }

    const bool reboundExcitation =
        (rateChange > -0.10) &&
        (syncChange > 0.10 ||
         niiChange > 0.10 ||
         seizChange > 0.10);

    // 2. Hyperexcitability
    if (rateChange > 0.25 ||
        syncChange > 0.15 ||
        (niiChange > 0.20 && reboundExcitation) ||
        (seizChange > 0.20 && reboundExcitation)) {

        return BiologicalState::Hyperexcitability;
    }

    // 3. Toxic instability
    if (niiChange > 0.40 ||
        seizChange > 0.40 ||
        sawToxicInstability) {

        return BiologicalState::ToxicInstability;
    }

    // 4. Controlled suppression
    if (rateChange <= -0.20 &&
        rateChange > -0.90 &&
        niiChange <= 0.10) {

        return BiologicalState::ControlledSuppression;
    }

    // 5. Network stabilization
    if (sawNetworkStabilization &&
        niiChange <= 0.0 &&
        seizChange <= 0.0) {

        return BiologicalState::NetworkStabilization;
    }

    return BiologicalState::LimitedEffect;
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

std::string generateReasonForState(
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

// BIOLOGY-DRIVEN CONFIDENCE SCORING
// Emerges from: biological coherence, mechanistic consistency, dose-response fit quality,
// window definition, and stability metrics. NOT primarily dependent on run count.
std::string computeConfidenceFromBiology(
    BiologicalState state,
    double sigmoidR2,
    bool therapeuticWindowExists,
    bool hasContinuousEffectiveWindow,
    bool fragmentedWindow,
    float rateVariability,
    float toxicityVariability,
    double syncReductionPct,
    double niiReductionPct,
    double seizureReductionPct,
    double burstReductionPct,
    double maxRateChangePct,
    float peakRiskScore,
    float peakSeizureProbabilityPct,
    int runCount
) {
    double confidenceScore = 0.0;

    // ==================== COMPONENT 1: BIOLOGICAL COHERENCE (40 max) ====================
    // Clear, unambiguous biological state detected?
    const bool isClearState = (state != BiologicalState::LimitedEffect);
    const bool isTherapeuticState = (state == BiologicalState::ControlledSuppression || 
                                      state == BiologicalState::NetworkStabilization);
    const bool isDangerState = (state == BiologicalState::NeuralSilencing || 
                                state == BiologicalState::Hyperexcitability || 
                                state == BiologicalState::ToxicInstability);

    if (isClearState) {
        confidenceScore += 30.0;  // Clear state detected
        if (isTherapeuticState) {
            confidenceScore += 10.0;  // Therapeutic effect is well-characterized
        } else if (isDangerState) {
            confidenceScore += 10.0;  // Danger signal is unambiguous
        }
    } else {
        // Limited effect: still award points if effect magnitude is detectable
        if (maxRateChangePct > 10.0) {
            confidenceScore += 8.0;
        } else {
            confidenceScore += 2.0;  // Minimal effect detected
        }
    }

    // ==================== COMPONENT 2: MECHANISTIC CONSISTENCY (20 max) ====================
    // Do the biological metrics align with the detected state?
    
    // For suppressive/stabilizing states: check if appropriate reductions occurred
    if (isTherapeuticState) {
        int metricsAligned = 0;
        if (syncReductionPct >= 15.0) metricsAligned++;
        if (niiReductionPct >= 15.0) metricsAligned++;
        if (seizureReductionPct >= 15.0) metricsAligned++;
        if (burstReductionPct >= 15.0) metricsAligned++;

        if (metricsAligned >= 3) {
            confidenceScore += 20.0;  // Strong consensus across metrics
        } else if (metricsAligned == 2) {
            confidenceScore += 15.0;  // Multiple metrics agree
        } else if (metricsAligned == 1) {
            confidenceScore += 10.0;  // At least one metric shows effect
        } else if (maxRateChangePct > 30.0) {
            confidenceScore += 8.0;   // Rate change significant even if other metrics weak
        }
    }
    // For danger states: high peak metrics indicate strong signal
    else if (isDangerState) {
        if (peakRiskScore >= 70.0 || peakSeizureProbabilityPct >= 70.0) {
            confidenceScore += 20.0;  // Strong danger signal
        } else if (peakRiskScore >= 50.0 || peakSeizureProbabilityPct >= 50.0) {
            confidenceScore += 15.0;  // Moderate danger signal
        } else {
            confidenceScore += 10.0;  // Detectable but weaker danger signal
        }
    }
    // For limited effect: check if there's ANY meaningful magnitude
    else {
        if (maxRateChangePct > 20.0) {
            confidenceScore += 10.0;  // Significant change despite limited state
        } else if (maxRateChangePct > 10.0) {
            confidenceScore += 5.0;
        }
    }

    // ==================== COMPONENT 3: DOSE-RESPONSE QUALITY (20 max) ====================
    // How well does the data fit a sigmoid/smooth curve?
    if (sigmoidR2 >= 0.95) {
        confidenceScore += 20.0;  // Excellent fit, highly predictable response
    } else if (sigmoidR2 >= 0.90) {
        confidenceScore += 17.0;  // Very good fit
    } else if (sigmoidR2 >= 0.85) {
        confidenceScore += 14.0;  // Good fit, interpretable dose-response
    } else if (sigmoidR2 >= 0.75) {
        confidenceScore += 9.0;   // Moderate fit, some noise
    } else if (sigmoidR2 >= 0.65) {
        confidenceScore += 5.0;   // Weak fit, highly variable
    } else {
        confidenceScore += 0.0;   // No coherent dose-response
    }

    // ==================== COMPONENT 4: WINDOW DEFINITION (10 max) ====================
    // Is there a well-defined therapeutic or safety window?
    if (therapeuticWindowExists && hasContinuousEffectiveWindow && !fragmentedWindow) {
        confidenceScore += 10.0;  // Clear, continuous window
    } else if (therapeuticWindowExists && hasContinuousEffectiveWindow) {
        confidenceScore += 8.0;   // Continuous window exists, some fragmentation
    } else if (therapeuticWindowExists) {
        confidenceScore += 5.0;   // Window exists but fragmented
    } else if (isDangerState) {
        confidenceScore += 5.0;   // No window needed; clear danger is definitive
    } else {
        confidenceScore += 0.0;   // No window, unclear signal
    }

    // ==================== COMPONENT 5: STABILITY & VARIABILITY (10 max) ====================
    // How consistent are the measurements across experimental runs/doses?
    if (rateVariability < 1.0f && toxicityVariability < 5.0f) {
        confidenceScore += 10.0;  // Excellent consistency
    } else if (rateVariability < 1.5f && toxicityVariability < 7.5f) {
        confidenceScore += 8.0;   // Good consistency
    } else if (rateVariability < 2.0f && toxicityVariability < 10.0f) {
        confidenceScore += 5.0;   // Acceptable consistency
    } else {
        confidenceScore += 2.0;   // High variability, noisy data
    }

    // ==================== FINAL CONVERSION TO CONFIDENCE BAND ====================
    // Ensure minimum meaningful thresholds
    const double clampedScore = std::clamp(confidenceScore, 0.0, 100.0);

    // Run count used for validation only (minimum bar)
    if (runCount < 2) {
        // Single run: still can be HIGH if all evidence converges perfectly
        // But downgrade MEDIUM→LOW if very few runs
        if (clampedScore >= 80.0) {
            return "HIGH";
        } else if (clampedScore >= 70.0) {
            return "MEDIUM";
        } else {
            return "LOW";
        }
    }

    if (clampedScore >= 80.0) {
        return "HIGH";
    } else if (clampedScore >= 50.0) {
        return "MEDIUM";
    } else {
        return "LOW";
    }
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
    const double baselineIsiCv = safeNonNegativeD(sorted.front().isiCv);
    const double baselineSeizure = safeNonNegativeD(sorted.front().seizureProbabilityPct);

    double maxRateChangePct = -std::numeric_limits<double>::infinity();
    double peakEffect = -std::numeric_limits<double>::infinity();
    bool sawHighDoseEffect = false;
    bool hasOnsetDose = false;
    double onsetDose = 0.0;
    bool sawNetworkStabilization = false;
    bool sawHyperexcitability = false;
    bool sawNeuralSilencing = false;
    bool sawMeaningfulCaBlock = false;
    double peakCalciumEffectMagnitude = 0.0;
    double maxSyncDelta = 0.0;
    double maxBurstDelta = 0.0;
    double maxNiiDelta = 0.0;
    double maxIsiCvDelta = 0.0;
    double maxSeizureDelta = 0.0;
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

    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const DoseObservation& obs = sorted[i];

        const float dose = safeNonNegative(obs.dose);
        const float meanRate = safeNonNegative(obs.meanFiringRateHz);
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

        // New multi-dimensional changes (fractional for rate, absolute for others)
        const double rateChangeFrac = (static_cast<double>(meanRate) - baselineRate) / std::max(1.0, baselineRate);
        const double syncDeltaLocal = static_cast<double>(syncValue) - baselineSync;
        const double niiDeltaLocal = static_cast<double>(niiValue) - baselineNii;
        const double seizureDeltaLocal = static_cast<double>(seizurePct) / 100.0 - (baselineSeizure / 100.0);
        const double syncReductionPct = reductionPercent(baselineSync, static_cast<double>(syncValue));
        const double niiReductionPct = reductionPercent(baselineNii, static_cast<double>(niiValue));
        const double burstReductionPct = reductionPercent(baselineBurst, static_cast<double>(burstValue));
        const double seizureReductionPct = reductionPercent(baselineSeizure, static_cast<double>(seizurePct));
        const bool meaningfulCaBlock = obs.blockCa >= kMeaningfulKBlockThreshold;
        const bool calciumStabilization = meaningfulCaBlock &&
                                          (syncReductionPct >= 15.0 ||
                                           niiReductionPct >= 15.0 ||
                                           seizureReductionPct >= 15.0 ||
                                           burstReductionPct >= 15.0);

        // Per-dose biological state detection (mirrors global detector rules)
        BiologicalState perDoseState = BiologicalState::LimitedEffect;
        // K-block override: require meaningful K block plus a real excitability shift
        const bool meaningfulKBlock = obs.blockK >= kMeaningfulKBlockThreshold;
        const bool reboundExcitation = (rateChangeFrac > -0.10) && (syncDeltaLocal > 0.10 || niiDeltaLocal > 0.10 || seizureDeltaLocal > 0.10);
        if (meaningfulKBlock && (rateChangeFrac > 0.25 || syncDeltaLocal > 0.15 || (niiDeltaLocal > 0.20 && reboundExcitation) || (seizureDeltaLocal > 0.20 && reboundExcitation))) {
            perDoseState = BiologicalState::Hyperexcitability;
        } else if (static_cast<double>(meanRate) < 0.10 * baselineRate) {
            perDoseState = BiologicalState::NeuralSilencing;
        } else if (calciumStabilization) {
            perDoseState = BiologicalState::NetworkStabilization;
        } else if (rateChangeFrac > +0.25 || syncDeltaLocal > +0.15 || (niiDeltaLocal > +0.20 && reboundExcitation) || (seizureDeltaLocal > +0.20 && reboundExcitation)) {
            perDoseState = BiologicalState::Hyperexcitability;
        } else if (rateChangeFrac < -0.20 && rateChangeFrac > -0.70 && niiDeltaLocal <= +0.10) {
            perDoseState = BiologicalState::ControlledSuppression;
        } else if (syncDeltaLocal < -0.10 && niiDeltaLocal < 0.0 && seizureDeltaLocal <= 0.0) {
            // never label meaningful K-block stabilization if excitability is also rising
            if (meaningfulKBlock && (rateChangeFrac > 0.25 || niiDeltaLocal > 0.20 || seizureDeltaLocal > 0.20)) {
                perDoseState = BiologicalState::Hyperexcitability;
            } else {
                perDoseState = BiologicalState::NetworkStabilization;
            }
        } else if (niiDeltaLocal > +0.40 || seizureDeltaLocal > +0.40) {
            perDoseState = BiologicalState::ToxicInstability;
        } else {
            perDoseState = BiologicalState::LimitedEffect;
        }

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
        if (effectMagnitudePct > peakEffect) {
            peakEffect = effectMagnitudePct;
        }

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

        maxSyncDelta = std::max(maxSyncDelta, static_cast<double>(syncValue) - baselineSync);
        maxBurstDelta = std::max(maxBurstDelta, static_cast<double>(burstValue) - baselineBurst);
        maxNiiDelta = std::max(maxNiiDelta, static_cast<double>(niiValue) - baselineNii);
        maxIsiCvDelta = std::max(maxIsiCvDelta, static_cast<double>(isiCv) - baselineIsiCv);
        maxSeizureDelta = std::max(maxSeizureDelta, static_cast<double>(seizurePct) - baselineSeizure);

        // Classify dose ranges according to biological state
        if (perDoseState == BiologicalState::LimitedEffect) {
            safeDoses.push_back(static_cast<double>(dose));
        }
        if (perDoseState == BiologicalState::ControlledSuppression || perDoseState == BiologicalState::NetworkStabilization) {
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
            (perDoseState == BiologicalState::Hyperexcitability) ||
            (perDoseState == BiologicalState::ToxicInstability && (rateChangeFrac > -0.10 || syncDeltaLocal > +0.10 || niiDeltaLocal > +0.10 || seizureDeltaLocal > +0.10)) ||
            (excitationEffectPct > 60.0 && (rateChangeFrac > 0.0 || syncDeltaLocal > 0.0 || niiDeltaLocal > 0.0 || seizureDeltaLocal > 0.0)) ||
            (niiDeltaLocal > +0.40 && (rateChangeFrac > -0.10 || syncDeltaLocal > +0.10 || seizureDeltaLocal > +0.10)) ||
            (seizureDeltaLocal > +0.40 && (rateChangeFrac > -0.10 || syncDeltaLocal > +0.10 || niiDeltaLocal > +0.10));
        const bool overSuppressionHere =
            (suppressionEffectPct > 60.0) ||
            (perDoseState == BiologicalState::NeuralSilencing);
        const bool stabilizationSaturationHere =
            (perDoseState == BiologicalState::NetworkStabilization) &&
            (stabilizationEffectPct > 60.0);

        if (perDoseState == BiologicalState::NetworkStabilization) {
            sawNetworkStabilization = true;
            sawMeaningfulCaBlock = sawMeaningfulCaBlock || meaningfulCaBlock;
            peakCalciumEffectMagnitude = std::max(peakCalciumEffectMagnitude, std::max(0.0, stabilizationEffectPct));
        } else if (perDoseState == BiologicalState::Hyperexcitability) {
            sawHyperexcitability = true;
        } else if (perDoseState == BiologicalState::NeuralSilencing) {
            sawNeuralSilencing = true;
        }

        if (excitatoryRiskHere) {
            sawHyperexcitability = true;
            excitatoryRiskDoses.push_back(static_cast<double>(dose));
        }

        if (overSuppressionHere) {
            overSuppressionDoses.push_back(static_cast<double>(dose));
        }

        if (stabilizationSaturationHere) {
            stabilizationSaturationDoses.push_back(static_cast<double>(dose));
        }

        if (excitatoryRiskHere || overSuppressionHere) {
            if (!sawHighDoseEffect) {
                sawHighDoseEffect = true;
                report.hasToxicThreshold = true;
                report.toxicMinDose = dose;
                report.hasToxicThresholdExact = true;
                report.toxicThresholdDoseEval = static_cast<double>(dose);
                report.toxicThresholdText = std::to_string(report.toxicThresholdDoseEval);
            }
        }
    }

    if (std::isfinite(maxRateChangePct)) {
        report.maxRateChangePct = maxRateChangePct;
    } else {
        report.maxRateChangePct = 0.0;
    }

    auto fitCurveForMode = [&](const std::string& mode) -> std::vector<double> {
        std::vector<double> curve;
        curve.reserve(xDose.size());
        const std::vector<double>* source = &suppressionCurveEffect;
        if (mode == "NO_SIGNIFICANT_RESPONSE") {
            source = &noSignificantCurveEffect;
        }
        if (mode == "EXCITATORY_RESPONSE") {
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
    const double finalSyncReductionPct = reductionPercent(baselineSync, safeNonNegativeD(finalObs.synchronizationIndex));
    const double finalNiiReductionPct = reductionPercent(baselineNii, safeNonNegativeD(finalObs.nii));
    const double finalNiiIncreasePct = increasePercent(baselineNii, safeNonNegativeD(finalObs.nii));
    const double finalSeizureReductionPct = reductionPercent(baselineSeizure, safeNonNegativeD(finalObs.seizureProbabilityPct));
    const double finalBurstReductionPct = reductionPercent(baselineBurst, safeNonNegativeD(finalObs.burstIndex));
    const bool finalMeaningfulCaBlock = finalObs.blockCa >= kMeaningfulKBlockThreshold;
    const double finalCalciumEffectMagnitude = std::max({finalSyncReductionPct, finalNiiReductionPct, finalSeizureReductionPct, finalBurstReductionPct});
    report.syncReductionPct = std::max(0.0, finalSyncReductionPct);
    report.niiReductionPct = std::max(0.0, finalNiiReductionPct);
    report.niiIncreasePct = std::max(0.0, finalNiiIncreasePct);
    report.seizureReductionPct = std::max(0.0, finalSeizureReductionPct);
    report.burstReductionPct = std::max(0.0, finalBurstReductionPct);
    report.calciumEffectMagnitude = std::max(0.0, std::max(peakCalciumEffectMagnitude, finalCalciumEffectMagnitude));
    report.meaningfulCaBlock = finalMeaningfulCaBlock;

    // UNIVERSAL BIOLOGICAL STATE DETECTION (multi-dimensional, not suppression-biased)
    report.biologicalState = detectBiologicalState(
        baselineRate,
        baselineSync,
        baselineBurst,
        baselineNii,
        baselineSeizure,
        baselineIsiCv,
        finalObs,
        sorted,
        report.maxRateChangePct,
        therapeuticWindowExists,
        report.effectiveRangeMin,
        report.effectiveRangeMax
    );

    const bool stabilizingResponseObserved = sawNetworkStabilization && !sawHyperexcitability && !sawNeuralSilencing && (sawMeaningfulCaBlock || finalMeaningfulCaBlock);
    if (stabilizingResponseObserved) {
        report.biologicalState = BiologicalState::NetworkStabilization;
    }

    // Compute deltas for reporting (used in decision logic)
    const double finalSyncDelta = safeNonNegativeD(finalObs.synchronizationIndex) - baselineSync;
    const double finalNiiDelta = safeNonNegativeD(finalObs.nii) - baselineNii;
    const double finalSeizureDelta = safeNonNegativeD(finalObs.seizureProbabilityPct) - baselineSeizure;

    const bool neuralSilencingDetected = (report.biologicalState == BiologicalState::NeuralSilencing);
    const bool hyperexcitabilityDetected = (report.biologicalState == BiologicalState::Hyperexcitability);
    const bool toxicInstabilityDetected = (report.biologicalState == BiologicalState::ToxicInstability);
    const bool networkStabilizationObserved = (report.biologicalState == BiologicalState::NetworkStabilization);
    const bool controlledSuppressionObserved = (report.biologicalState == BiologicalState::ControlledSuppression);
    const bool noSignificantResponseObserved =
        (report.biologicalState == BiologicalState::LimitedEffect) ||
        (!therapeuticWindowExists && report.maxRateChangePct < 20.0 && !report.hasToxicThreshold);
    if (stabilizingResponseObserved) {
        report.responseMode = "STABILIZING_RESPONSE";
    } else if (sawHyperexcitability || hyperexcitabilityDetected || report.biologicalState == BiologicalState::Hyperexcitability) {
        report.responseMode = "EXCITATORY_RESPONSE";
    } else if (noSignificantResponseObserved) {
        report.responseMode = "NO_SIGNIFICANT_RESPONSE";
    } else {
        report.responseMode = "SUPPRESSIVE_RESPONSE";
    }

    report.peakRiskScore = 0.0f;
    report.peakEarlyWarningIndex = 0.0f;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const DoseObservation& obs = sorted[i];
        const float meanRate = safeNonNegative(obs.meanFiringRateHz);
        const float seizurePct = safeNonNegative(obs.seizureProbabilityPct);
        const float suppressionPct = safeNonNegative(obs.suppressionPct);
        const float syncValue = safeNonNegative(obs.synchronizationIndex);
        const float burstValue = safeNonNegative(obs.burstIndex);
        const float niiValue = safeNonNegative(obs.nii);

        const float seizureNorm = clamp01(seizurePct / 100.0f);
        const float suppressionNorm = clamp01(suppressionPct / 100.0f);
        const float syncNorm = clamp01(syncValue);
        const float burstNorm = clamp01(burstValue / 0.20f);
        const float niiNorm = clamp01(niiValue);

        const DoseRiskMetrics metrics{
            (static_cast<double>(meanRate) - baselineRate) / std::max(1.0, baselineRate),
            static_cast<double>(syncValue) - baselineSync,
            static_cast<double>(niiValue) - baselineNii,
            static_cast<double>(seizurePct) / 100.0 - (baselineSeizure / 100.0),
            reductionPercent(baselineSync, static_cast<double>(syncValue)),
            reductionPercent(baselineNii, static_cast<double>(niiValue)),
            reductionPercent(baselineSeizure, static_cast<double>(seizurePct)),
            reductionPercent(baselineBurst, static_cast<double>(burstValue)),
            seizureNorm,
            suppressionNorm,
            syncNorm,
            burstNorm,
            niiNorm
        };

        const double seizureRiskPct = 100.0 * computeSeizureRiskForMode(report.responseMode, metrics);
        const double earlyWarningIndex = computeEarlyWarningIndexForMode(report.responseMode, metrics, seizureRiskPct / 100.0);
        const DrugRiskTier tier = classifyTier(seizureNorm, suppressionNorm, static_cast<float>(seizureRiskPct));

        report.points[i].riskScore = static_cast<float>(seizureRiskPct);
        report.points[i].classification = toString(tier);
        report.points[i].earlyWarningIndex = static_cast<float>(earlyWarningIndex);

        report.peakRiskScore = std::max(report.peakRiskScore, static_cast<float>(seizureRiskPct));
        report.peakEarlyWarningIndex = std::max(report.peakEarlyWarningIndex, static_cast<float>(earlyWarningIndex));
    }

    report.sigmoidR2 = computeBestSigmoidR2(xDose, fitCurveForMode(report.responseMode));
    if (report.sigmoidR2 >= 0.95) {
        report.curveType = "Sigmoidal";
    } else if (report.sigmoidR2 >= 0.85) {
        report.curveType = "Weak Sigmoidal";
    } else {
        report.curveType = "Non-sigmoidal";
    }
    if (stabilizingResponseObserved) {
        report.curveType = report.sigmoidR2 >= 0.95 ? "Sigmoidal Stabilization" : "Stabilizing Response";
    }
    if (stabilizingResponseObserved) {
        report.biologicalStateText = toString(report.biologicalState);
        report.primaryChangeText = "Calcium-channel blockade reduced synchronization and neural instability";
        report.safetyInterpretationText = "Calcium-channel blockade reduced synchronization and neural instability without toxic excitation";
        report.seizureTrendText = finalSeizureReductionPct > 0.0
                                      ? std::string("Seizure-risk markers decreased with dose")
                                      : (finalSeizureReductionPct < 0.0
                                             ? std::string("Seizure-risk markers increased with dose")
                                             : std::string("Seizure-risk markers remained broadly stable"));
    }

    const bool toxicityBeforeTherapy =
        report.hasToxicThreshold &&
        (!therapeuticWindowExists || report.toxicThresholdDoseEval <= report.effectiveRangeMin);

    const bool toxicityAfterTherapy =
        report.hasToxicThreshold &&
        therapeuticWindowExists &&
        report.toxicThresholdDoseEval > report.effectiveRangeMax;

    const bool stableTherapeuticWindow = therapeuticWindowExists && !lowStability && !sawHighDoseEffect && !fragmentedWindow;

    if (!stabilizingResponseObserved) {
        report.biologicalStateText = toString(report.biologicalState);
        report.primaryChangeText = primaryChangeTextForState(report.biologicalState, report.maxRateChangePct, finalSyncDelta, finalNiiDelta, finalSeizureDelta);
        report.safetyInterpretationText = safetyInterpretationForState(report.biologicalState, toxicityBeforeTherapy, toxicityAfterTherapy, lowStability, networkStabilizationObserved);
        report.seizureTrendText = finalSeizureDelta > 0.0
                                      ? std::string("Seizure-risk markers increased with dose")
                                      : (finalSeizureDelta < 0.0
                                             ? std::string("Seizure-risk markers decreased with dose")
                                             : std::string("Seizure-risk markers remained broadly stable"));
    }

    // Decision engine strictly driven by biological state and therapeutic window rules
    if (neuralSilencingDetected) {
        report.recommendation = "NOT RECOMMENDED";
        report.riskLevel = "HIGH";
        report.reason = generateReasonForState(report.biologicalState, toxicityBeforeTherapy, toxicityAfterTherapy, lowStability, fragmentedWindow, therapeuticWindowExists, stabilityInput.runCount);
        report.overallTier = DrugRiskTier::Toxic;
    } else if (hyperexcitabilityDetected) {
        report.recommendation = "NOT RECOMMENDED";
        report.riskLevel = "HIGH";
        report.reason = generateReasonForState(report.biologicalState, toxicityBeforeTherapy, toxicityAfterTherapy, lowStability, fragmentedWindow, therapeuticWindowExists, stabilityInput.runCount);
        report.overallTier = DrugRiskTier::Toxic;
    } else if (toxicInstabilityDetected) {
        report.recommendation = "NOT RECOMMENDED";
        report.riskLevel = "HIGH";
        report.reason = generateReasonForState(report.biologicalState, toxicityBeforeTherapy, toxicityAfterTherapy, lowStability, fragmentedWindow, therapeuticWindowExists, stabilityInput.runCount);
        report.overallTier = DrugRiskTier::Toxic;
    } else if (toxicityBeforeTherapy) {
        report.recommendation = "NOT RECOMMENDED";
        report.riskLevel = "HIGH";
        report.reason = "Toxicity appears before a therapeutic window; unsafe dose-response ordering";
        report.overallTier = DrugRiskTier::Toxic;
    } else if (stabilizingResponseObserved) {
        if (stabilityScoreIsMediumOrHigh(report.stabilityScore)) {
            report.recommendation = "PROMISING";
            report.riskLevel = "LOW";
            report.reason = "Calcium-channel blockade reduced synchronization and neural instability without toxic excitation";
            report.overallTier = DrugRiskTier::Safe;
        } else {
            report.recommendation = "CAUTION";
            report.riskLevel = "MODERATE";
            report.reason = "Calcium-channel blockade showed stabilizing response, but variability requires caution";
            report.overallTier = DrugRiskTier::ModerateRisk;
        }
    } else if (stableTherapeuticWindow || (networkStabilizationObserved && !report.hasToxicThreshold && !lowStability)) {
        report.recommendation = "PROMISING";
        report.riskLevel = "LOW";
        report.reason = generateReasonForState(report.biologicalState, toxicityBeforeTherapy, toxicityAfterTherapy, lowStability, fragmentedWindow, therapeuticWindowExists, stabilityInput.runCount);
        report.overallTier = DrugRiskTier::Safe;
    } else if (therapeuticWindowExists && (toxicityAfterTherapy || report.hasToxicThreshold)) {
        // Therapeutic effect precedes toxicity but safety margin may be narrow.
        report.recommendation = "CAUTION";
        report.riskLevel = "MODERATE";
        report.reason = controlledSuppressionObserved
                            ? "Controlled sodium-channel suppression observed with over-suppression at higher dose"
                            : "Therapeutic response observed before over-suppression; narrow therapeutic window requires caution";
        report.overallTier = DrugRiskTier::ModerateRisk;
    } else if (therapeuticWindowExists) {
        report.recommendation = lowStability ? "CAUTION" : "PROMISING";
        report.riskLevel = lowStability ? "MODERATE" : "LOW";
        report.reason = lowStability
                            ? "Therapeutic response detected but variability reduces dosing confidence"
                            : "Therapeutic response detected within tested range with acceptable safety margin";
        report.overallTier = lowStability ? DrugRiskTier::ModerateRisk : DrugRiskTier::Safe;
    } else {
        report.recommendation = "LIMITED EFFICACY";
        report.riskLevel = "LOW";
        report.reason = generateReasonForState(report.biologicalState, toxicityBeforeTherapy, toxicityAfterTherapy, lowStability, fragmentedWindow, therapeuticWindowExists, stabilityInput.runCount);
        report.overallTier = DrugRiskTier::Safe;
    }

    report.confidence = computeConfidenceFromBiology(
        report.biologicalState,
        report.sigmoidR2,
        therapeuticWindowExists,
        report.hasContinuousEffectiveWindow,
        fragmentedWindow,
        report.rateVariability,
        report.toxicityVariability,
        report.syncReductionPct,
        report.niiReductionPct,
        report.seizureReductionPct,
        report.burstReductionPct,
        report.maxRateChangePct,
        report.peakRiskScore,
        report.peakSeizureProbabilityPct,
        stabilityInput.runCount
    );
    report.hasToxicThresholdExact = report.hasToxicThreshold;
    report.toxicThresholdText = report.hasToxicThreshold ? std::to_string(report.toxicThresholdDoseEval)
                                                         : ">" + std::to_string(report.maxTestedDose);

    if (report.responseMode == "EXCITATORY_RESPONSE") {
        report.reason = "Excitability and seizure-risk markers increased beyond safe neural stability limits";
    }

    const double midDose = 0.5 * (report.minTestedDose + report.maxTestedDose);
    if (report.sigmoidR2 >= 0.95 && hasOnsetDose && onsetDose <= midDose) {
        report.responseStrength = "Strong";
    } else if (report.sigmoidR2 >= 0.85) {
        report.responseStrength = "Moderate-to-Strong";
    } else {
        report.responseStrength = "Weak";
    }

    return report;
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
