#include "PharmaDecisionEngine.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <array>
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
    double baselineIsiCv,
    const DoseObservation& finalObs,
    const std::vector<DoseObservation>& sortedObs,
    double maxRateChangePct,
    bool therapeuticWindowExists,
    double effectiveRangeMin,
    double effectiveRangeMax
) {
    (void)baselineIsiCv;
    (void)maxRateChangePct;
    (void)therapeuticWindowExists;
    (void)effectiveRangeMin;
    (void)effectiveRangeMax;
    // Compute final numeric changes using the user's specified convention
    const double currentRate = safeNonNegativeD(finalObs.meanFiringRateHz);
    const double rateChange = (currentRate - baselineRate) / std::max(1.0, baselineRate); // fractional change
    const double currentSync = safeNonNegativeD(finalObs.synchronizationIndex);
    const double currentBurst = safeNonNegativeD(finalObs.burstIndex);
    const double syncChange = currentSync - baselineSync;
    const double syncReductionPct = reductionPercent(baselineSync, currentSync);
    const double burstReductionPct = reductionPercent(baselineBurst, currentBurst);
    const double currentNii = safeNonNegativeD(finalObs.nii);
    const double niiChange = currentNii - baselineNii;
    const double niiReductionPct = reductionPercent(baselineNii, currentNii);
    const double currentSeiz = safeNonNegativeD(finalObs.seizureProbabilityPct) / 100.0; // normalized 0..1
    const double seizChange = currentSeiz - (baselineSeizure / 100.0);
    const double seizureReductionPct = reductionPercent(baselineSeizure, safeNonNegativeD(finalObs.seizureProbabilityPct));
    const bool meaningfulCaBlock = finalObs.blockCa >= kMeaningfulKBlockThreshold;
    const bool calciumStabilizationObserved = meaningfulCaBlock &&
                                              (syncReductionPct >= 15.0 ||
                                               niiReductionPct >= 15.0 ||
                                               seizureReductionPct >= 15.0 ||
                                               burstReductionPct >= 15.0);

    // Scan entire sweep for extreme instability and to apply K-block rules
    bool sawToxicInstability = false;
    for (const auto& obs : sortedObs) {
        const double oRate = safeNonNegativeD(obs.meanFiringRateHz);
        const double oRateChange = (oRate - baselineRate) / std::max(1.0, baselineRate);
        const double oNiiChange = safeNonNegativeD(obs.nii) - baselineNii;
        const double oSeizChange = safeNonNegativeD(obs.seizureProbabilityPct) / 100.0 - (baselineSeizure / 100.0);
        const double oSyncReductionPct = reductionPercent(baselineSync, safeNonNegativeD(obs.synchronizationIndex));
        const double oNiiReductionPct = reductionPercent(baselineNii, safeNonNegativeD(obs.nii));
        const double oSeizureReductionPct = reductionPercent(baselineSeizure, safeNonNegativeD(obs.seizureProbabilityPct));
        const double oBurstReductionPct = reductionPercent(baselineBurst, safeNonNegativeD(obs.burstIndex));

        if (oNiiChange > 0.40 || oSeizChange > 0.40) {
            sawToxicInstability = true;
        }

        const bool meaningfulKBlock = obs.blockK >= kMeaningfulKBlockThreshold;
        if (meaningfulKBlock) {
            // If K block is meaningful and excitability rises materially -> hyperexcitability
            if (oRateChange > 0.25 || oNiiChange > 0.20 || oSeizChange > 0.20) {
                return BiologicalState::Hyperexcitability;
            }
        }

        if (obs.blockCa >= kMeaningfulKBlockThreshold &&
            (oSyncReductionPct >= 15.0 ||
             oNiiReductionPct >= 15.0 ||
             oSeizureReductionPct >= 15.0 ||
             oBurstReductionPct >= 15.0)) {
            return BiologicalState::NetworkStabilization;
        }
    }

    // Apply exact rule set (order matters)
    // NEURAL_SILENCING: currentRate < 10% baseline
    if (currentRate < 0.10 * baselineRate) {
        return BiologicalState::NeuralSilencing;
    }

    // HYPEREXCITABILITY: any of the listed increases
    if (rateChange > +0.25 || syncChange > +0.15 || niiChange > +0.20 || seizChange > +0.20) {
        return BiologicalState::Hyperexcitability;
    }

    // CONTROLLED_SUPPRESSION: rateChange between -20% and -70% and niiChange <= +0.10
    if (rateChange < -0.20 && rateChange > -0.70 && niiChange <= +0.10) {
        return BiologicalState::ControlledSuppression;
    }

    // NETWORK_STABILIZATION: meaningful calcium block plus measurable reductions in instability markers
    if (calciumStabilizationObserved) {
        // Meaningful K-block alone is not enough; require excitability markers.
        if ((finalObs.blockK >= kMeaningfulKBlockThreshold) &&
            (rateChange > 0.25 || niiChange > 0.20 || seizChange > 0.20)) {
            return BiologicalState::Hyperexcitability;
        }
        return BiologicalState::NetworkStabilization;
    }

    // TOXIC_INSTABILITY: large nii or seizure increases
    if (niiChange > +0.40 || seizChange > +0.40 || sawToxicInstability) {
        return BiologicalState::ToxicInstability;
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
    double maxRateChangePct,
    double finalSyncDelta,
    double finalNiiDelta,
    double finalSeizureDelta
) {
    (void)maxRateChangePct;
    (void)finalSyncDelta;
    (void)finalNiiDelta;
    (void)finalSeizureDelta;
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

std::string responseModeForState(BiologicalState state) {
    switch (state) {
        case BiologicalState::ControlledSuppression:
            return "SUPPRESSIVE_RESPONSE";
        case BiologicalState::NeuralSilencing:
            return "NEURAL_SILENCING";
        case BiologicalState::Hyperexcitability:
            return "EXCITATORY_RESPONSE";
        case BiologicalState::NetworkStabilization:
            return "STABILIZING_RESPONSE";
        case BiologicalState::ToxicInstability:
            return "TOXIC_INSTABILITY";
        case BiologicalState::LimitedEffect:
        default:
            return "NO_SIGNIFICANT_RESPONSE";
    }
}

BiologicalState stateForResponseMode(const std::string& mode) {
    if (mode == "SUPPRESSIVE_RESPONSE") {
        return BiologicalState::ControlledSuppression;
    }
    if (mode == "NEURAL_SILENCING") {
        return BiologicalState::NeuralSilencing;
    }
    if (mode == "EXCITATORY_RESPONSE") {
        return BiologicalState::Hyperexcitability;
    }
    if (mode == "STABILIZING_RESPONSE") {
        return BiologicalState::NetworkStabilization;
    }
    if (mode == "TOXIC_INSTABILITY") {
        return BiologicalState::ToxicInstability;
    }
    return BiologicalState::LimitedEffect;
}

struct ModeSummary {
    BiologicalState state = BiologicalState::LimitedEffect;
    std::size_t count = 0U;
    std::size_t currentRun = 0U;
    std::size_t longestRun = 0U;
    double weight = 0.0;
    double evidence = 0.0;
    double peakEvidence = 0.0;
};

struct MechanisticDominance {
    std::map<std::string, ModeSummary> modeSummaries;
    std::string dominantMode = "STANDARD_RESPONSE";
    BiologicalState dominantState = BiologicalState::LimitedEffect;
    double dominantShare = 0.0;
    double dominantMargin = 0.0;
    double dominantEvidence = 0.0;
    double totalMeaningfulWeight = 0.0;
    bool hasMeaningfulEffect = false;
    // Channel-level dominance (normalized shares)
    double naShare = 0.0;
    double kShare = 0.0;
    double caShare = 0.0;
    // Spread between top two channel shares
    double dominanceSpread = 0.0;
    // True when two channels compete closely
    bool mixedMechanism = false;
};

double dominantChannelScore(const DoseObservation& obs, BiologicalState state) {
    const double dominant = std::max({static_cast<double>(obs.blockNa), static_cast<double>(obs.blockK), static_cast<double>(obs.blockCa)});
    const double runnerUp = std::max(
        dominant == static_cast<double>(obs.blockNa) ? static_cast<double>(obs.blockK) : static_cast<double>(obs.blockNa),
        dominant == static_cast<double>(obs.blockCa) ? static_cast<double>(obs.blockK) : static_cast<double>(obs.blockCa)
    );
    const double dominance = std::clamp(dominant - runnerUp, 0.0, 1.0);

    switch (state) {
        case BiologicalState::ControlledSuppression:
        case BiologicalState::NeuralSilencing:
            return std::clamp(static_cast<double>(obs.blockNa) + dominance, 0.0, 1.0);
        case BiologicalState::Hyperexcitability:
            return std::clamp(static_cast<double>(obs.blockK) + dominance, 0.0, 1.0);
        case BiologicalState::NetworkStabilization:
            return std::clamp(static_cast<double>(obs.blockCa) + dominance, 0.0, 1.0);
        case BiologicalState::ToxicInstability:
            return std::clamp(0.50 * static_cast<double>(obs.blockNa) + 0.50 * static_cast<double>(obs.blockK) + dominance, 0.0, 1.0);
        case BiologicalState::LimitedEffect:
        default:
            return std::clamp(1.0 - dominant, 0.0, 1.0);
    }
}

double mechanisticEvidenceScore(
    const DoseObservation& obs,
    BiologicalState state,
    double rateChangeFrac,
    double syncDelta,
    double niiDelta,
    double seizureDelta,
    double syncReductionPct,
    double niiReductionPct,
    double seizureReductionPct,
    double burstReductionPct,
    double baselineRate
) {
    const double channelScore = dominantChannelScore(obs, state);
    const double improvementScore = std::clamp(
        std::max({syncReductionPct, niiReductionPct, seizureReductionPct, burstReductionPct}) / 100.0,
        0.0,
        1.0
    );

    double directionScore = 0.0;
    switch (state) {
        case BiologicalState::ControlledSuppression:
            directionScore = std::clamp(((-rateChangeFrac) - 0.10) / 0.60, 0.0, 1.0) + improvementScore;
            break;
        case BiologicalState::NeuralSilencing:
            directionScore = std::clamp((0.10 * baselineRate - std::max(0.0, baselineRate * (1.0 + rateChangeFrac))) / std::max(1.0, baselineRate), 0.0, 1.0) + improvementScore;
            break;
        case BiologicalState::Hyperexcitability:
            directionScore = std::clamp(std::max({rateChangeFrac, syncDelta, niiDelta, seizureDelta}) / 0.40, 0.0, 1.0) + std::clamp((seizureDelta > 0.0 ? seizureDelta : 0.0) / 0.25, 0.0, 1.0);
            break;
        case BiologicalState::NetworkStabilization:
            directionScore = improvementScore + std::clamp((std::max({syncReductionPct, niiReductionPct, seizureReductionPct, burstReductionPct}) - 10.0) / 35.0, 0.0, 1.0);
            break;
        case BiologicalState::ToxicInstability:
            directionScore = std::clamp(std::max({niiDelta, seizureDelta}) / 0.40, 0.0, 1.0) + std::clamp((std::max({niiDelta, seizureDelta}) - 0.20) / 0.30, 0.0, 1.0);
            break;
        case BiologicalState::LimitedEffect:
        default:
            directionScore = std::clamp(1.0 - std::max({static_cast<double>(obs.blockNa), static_cast<double>(obs.blockK), static_cast<double>(obs.blockCa)}), 0.0, 1.0);
            break;
    }

    return std::clamp(0.40 * channelScore + 0.60 * std::clamp(directionScore, 0.0, 2.0) / 2.0, 0.0, 1.0);
}

MechanisticDominance computeMechanisticDominance(const std::vector<BiologicalState>& doseStates, const std::vector<double>& doseWeights, const std::vector<double>& naBlocks, const std::vector<double>& kBlocks, const std::vector<double>& caBlocks) {
    MechanisticDominance dominance;
    if (doseStates.empty() || doseStates.size() != doseWeights.size()) {
        dominance.dominantMode = "NO_SIGNIFICANT_RESPONSE";
        dominance.dominantState = BiologicalState::LimitedEffect;
        dominance.dominantShare = 1.0;
        dominance.dominantMargin = 1.0;
        return dominance;
    }

    std::string previousMode;
    for (std::size_t i = 0; i < doseStates.size(); ++i) {
        const BiologicalState state = doseStates[i];
        const std::string mode = responseModeForState(state);
        const double weight = std::max(0.0, doseWeights[i]);

        auto& summary = dominance.modeSummaries[mode];
        summary.state = state;
        summary.count += 1U;
        summary.weight += weight;

        if (!previousMode.empty() && previousMode == mode) {
            summary.currentRun += 1U;
        } else {
            summary.currentRun = 1U;
        }
        summary.longestRun = std::max(summary.longestRun, summary.currentRun);
        previousMode = mode;

        if (state != BiologicalState::LimitedEffect) {
            dominance.hasMeaningfulEffect = true;
            dominance.totalMeaningfulWeight += weight;
        }
    }

    // Compute channel-level weighted shares from per-dose block fractions
    double naSum = 0.0;
    double kSum = 0.0;
    double caSum = 0.0;
    double weightSum = 0.0;
    for (std::size_t i = 0; i < doseWeights.size(); ++i) {
        const double w = std::max(0.0, doseWeights[i]);
        weightSum += w;
        if (i < naBlocks.size()) naSum += w * std::clamp(naBlocks[i], 0.0, 1.0);
        if (i < kBlocks.size()) kSum += w * std::clamp(kBlocks[i], 0.0, 1.0);
        if (i < caBlocks.size()) caSum += w * std::clamp(caBlocks[i], 0.0, 1.0);
    }
    if (weightSum > 1.0e-12) {
        dominance.naShare = naSum / weightSum;
        dominance.kShare = kSum / weightSum;
        dominance.caShare = caSum / weightSum;
    }
    // compute spread and mixed mechanism flag
    std::array<std::pair<double,std::string>,3> shares = {{{dominance.naShare,"NA"},{dominance.kShare,"K"},{dominance.caShare,"CA"}}};
    std::sort(shares.begin(), shares.end(), [](auto &a, auto &b){ return a.first > b.first; });
    dominance.dominanceSpread = shares[0].first - shares[1].first;
    dominance.mixedMechanism = (shares[0].first >= 0.15 && shares[1].first >= 0.15 && dominance.dominanceSpread < 0.12);

    // Also detect mixed mechanism when two mode summaries have near-equal weight
    if (!dominance.modeSummaries.empty()) {
        std::vector<std::pair<double,std::string>> modeWeights;
        for (const auto &entry : dominance.modeSummaries) {
            modeWeights.push_back({entry.second.weight, entry.first});
        }
        std::sort(modeWeights.begin(), modeWeights.end(), [](auto &a, auto &b){ return a.first > b.first; });
        if (modeWeights.size() > 1) {
            const double top = modeWeights[0].first;
            const double second = modeWeights[1].first;
            const double total = dominance.totalMeaningfulWeight > 1.0e-12 ? dominance.totalMeaningfulWeight : 1.0;
            const double topShare = top / total;
            const double secondShare = second / total;
            if (topShare >= 0.15 && secondShare >= 0.15 && (topShare - secondShare) < 0.12) {
                dominance.mixedMechanism = true;
            }
        }
    }

    if (!dominance.hasMeaningfulEffect || dominance.totalMeaningfulWeight <= 1.0e-6) {
        dominance.dominantMode = "NO_SIGNIFICANT_RESPONSE";
        dominance.dominantState = BiologicalState::LimitedEffect;
        dominance.dominantShare = 1.0;
        dominance.dominantMargin = 1.0;
        return dominance;
    }

    std::vector<std::pair<std::string, const ModeSummary*>> ranked;
    ranked.reserve(dominance.modeSummaries.size());
    for (const auto& entry : dominance.modeSummaries) {
        ranked.push_back({entry.first, &entry.second});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.second->weight != b.second->weight) {
            return a.second->weight > b.second->weight;
        }
        return a.second->count > b.second->count;
    });

    const auto bestIt = ranked.begin();
    const auto secondIt = ranked.size() > 1U ? std::next(ranked.begin()) : ranked.end();
    if (bestIt != ranked.end()) {
        dominance.dominantMode = bestIt->first;
        dominance.dominantState = bestIt->second->state;
        dominance.dominantShare = bestIt->second->weight / dominance.totalMeaningfulWeight;
        dominance.dominantEvidence = bestIt->second->count == 0U ? 0.0 : bestIt->second->evidence / static_cast<double>(bestIt->second->count);
        dominance.dominantMargin = secondIt != ranked.end()
                                        ? std::max(0.0, bestIt->second->weight - secondIt->second->weight) / dominance.totalMeaningfulWeight
                                        : dominance.dominantShare;
    }

    return dominance;
}

std::string resolveResponseMode(
    const MechanisticDominance& dominance,
    bool finalMeaningfulCaBlock,
    double finalCalciumEffectMagnitude,
    bool lowStability,
    bool therapeuticWindowExists,
    bool fragmentedWindow,
    double maxRateChangePct
) {
    (void)lowStability;
    (void)therapeuticWindowExists;
    (void)fragmentedWindow;
    (void)maxRateChangePct;

    const auto modeSummary = [&](const std::string& mode) -> const ModeSummary* {
        const auto it = dominance.modeSummaries.find(mode);
        return it == dominance.modeSummaries.end() ? nullptr : &it->second;
    };

    const auto sustained = [](const ModeSummary* summary) -> bool {
        return summary != nullptr && summary->count >= 2U && summary->longestRun >= 2U;
    };

    const ModeSummary* toxicSummary = modeSummary("TOXIC_INSTABILITY");
    const ModeSummary* silencingSummary = modeSummary("NEURAL_SILENCING");
    const ModeSummary* suppressiveSummary = modeSummary("SUPPRESSIVE_RESPONSE");
    const ModeSummary* excitatorySummary = modeSummary("EXCITATORY_RESPONSE");
    const ModeSummary* stabilizingSummary = modeSummary("STABILIZING_RESPONSE");
    const ModeSummary* noResponseSummary = modeSummary("NO_SIGNIFICANT_RESPONSE");

    const double toxicShare = toxicSummary ? toxicSummary->weight / dominance.totalMeaningfulWeight : 0.0;
    const double silencingShare = silencingSummary ? silencingSummary->weight / dominance.totalMeaningfulWeight : 0.0;
    const double suppressiveShare = suppressiveSummary ? suppressiveSummary->weight / dominance.totalMeaningfulWeight : 0.0;
    const double excitatoryShare = excitatorySummary ? excitatorySummary->weight / dominance.totalMeaningfulWeight : 0.0;
    const double stabilizingShare = stabilizingSummary ? stabilizingSummary->weight / dominance.totalMeaningfulWeight : 0.0;
    const double noResponseShare = noResponseSummary ? noResponseSummary->weight / dominance.totalMeaningfulWeight : 0.0;

    if (!dominance.hasMeaningfulEffect || dominance.totalMeaningfulWeight <= 1.0e-6 || noResponseShare >= 0.95) {
        return "NO_SIGNIFICANT_RESPONSE";
    }

    // Toxic or sustained neural silencing outrank mechanism dominance
    if ((sustained(toxicSummary) && toxicShare >= 0.20)) {
        return "TOXIC_INSTABILITY";
    }
    if ((sustained(silencingSummary) && silencingShare >= 0.20)) {
        return "NEURAL_SILENCING";
    }

    // Mixed channel contributions -> do not force single ontology; keep ambiguous
    if (dominance.mixedMechanism) {
        return "STANDARD_RESPONSE";
    }

    // Channel-first classification
    const double naShare = dominance.naShare;
    const double kShare = dominance.kShare;
    const double caShare = dominance.caShare;

    // Calcium-dominant stabilization: require calcium dominance and measurable calcium effect
    if (caShare > naShare && caShare > kShare) {
        if (finalMeaningfulCaBlock && finalCalciumEffectMagnitude >= 15.0 && stabilizingShare >= 0.30 && sustained(stabilizingSummary)) {
            return "STABILIZING_RESPONSE";
        }
        // If calcium not providing stabilizing evidence, fall through to standard
    }

    // Potassium-dominant => excitatory
    if (kShare > naShare && kShare > caShare) {
        if (sustained(excitatorySummary) && excitatoryShare >= 0.20) {
            return "EXCITATORY_RESPONSE";
        }
        // If excitatory evidence weak, still allow STANDARD
    }

    // Sodium-dominant => suppression (but do not pick suppression if excitatory/toxic markers are present)
    if (naShare > kShare && naShare > caShare) {
        if (sustained(suppressiveSummary) && suppressiveShare >= 0.25 && excitatoryShare < 0.15 && toxicShare < 0.15) {
            return "SUPPRESSIVE_RESPONSE";
        }
        if (sustained(silencingSummary) && silencingShare >= 0.20) {
            return "NEURAL_SILENCING";
        }
    }

    // If one dominant mode has strong evidence (fallback to dominantMode)
    if (dominance.dominantShare >= 0.55 && dominance.dominantMargin >= 0.10) {
        if (dominance.dominantMode == "STABILIZING_RESPONSE") {
            return finalMeaningfulCaBlock && finalCalciumEffectMagnitude >= 15.0 ? "STABILIZING_RESPONSE" : "STANDARD_RESPONSE";
        }
        return dominance.dominantMode;
    }

    // Conservative default: ambiguous when evidence conflicts
    return "STANDARD_RESPONSE";
}

std::string safetyInterpretationForState(
    BiologicalState state,
    bool toxicityBeforeTherapy,
    bool toxicityAfterTherapy,
    bool lowStability,
    bool networkStabilizationObserved
) {
    (void)networkStabilizationObserved;
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
    int runCount
) {
    (void)runCount;
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
    const double baselineIsiCv = safeNonNegativeD(sorted.front().isiCv);
    const double baselineSeizure = safeNonNegativeD(sorted.front().seizureProbabilityPct);

    double maxRateChangePct = -std::numeric_limits<double>::infinity();
    double peakEffect = -std::numeric_limits<double>::infinity();
    bool sawHighDoseEffect = false;
    bool hasOnsetDose = false;
    double onsetDose = 0.0;
    double peakCalciumEffectMagnitude = 0.0;
    double maxSyncDelta = 0.0;
    double maxBurstDelta = 0.0;
    double maxNiiDelta = 0.0;
    double maxIsiCvDelta = 0.0;
    double maxSeizureDelta = 0.0;
    std::vector<double> xDose;
    std::vector<double> yEffect;
    std::vector<double> therapeuticDoses;
    std::vector<double> safeDoses;
    std::vector<double> excitatoryRiskDoses;
    std::vector<double> overSuppressionDoses;
    std::vector<BiologicalState> doseStates;
    std::vector<double> doseWeights;
    std::vector<double> doseBlockNa;
    std::vector<double> doseBlockK;
    std::vector<double> doseBlockCa;
    doseStates.reserve(sorted.size());
    doseWeights.reserve(sorted.size());

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
        if (meaningfulKBlock && (rateChangeFrac > 0.25 || syncDeltaLocal > 0.15 || niiDeltaLocal > 0.20 || seizureDeltaLocal > 0.20)) {
            perDoseState = BiologicalState::Hyperexcitability;
        } else if (static_cast<double>(meanRate) < 0.10 * baselineRate) {
            perDoseState = BiologicalState::NeuralSilencing;
        } else if (calciumStabilization) {
            perDoseState = BiologicalState::NetworkStabilization;
        } else if (rateChangeFrac > +0.25 || syncDeltaLocal > +0.15 || niiDeltaLocal > +0.20 || seizureDeltaLocal > +0.20) {
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
        yEffect.push_back(std::clamp(effectMagnitudePct / 100.0, 0.0, 1.0));

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
        if ((perDoseState == BiologicalState::ControlledSuppression || perDoseState == BiologicalState::NetworkStabilization) && !isToxic) {
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
            (perDoseState == BiologicalState::Hyperexcitability ||
             perDoseState == BiologicalState::ToxicInstability);
        const bool overSuppressionHere =
            (suppressionEffectPct > 60.0) ||
            (perDoseState == BiologicalState::NeuralSilencing);

        if (perDoseState == BiologicalState::NetworkStabilization) {
            peakCalciumEffectMagnitude = std::max(peakCalciumEffectMagnitude, std::max(0.0, stabilizationEffectPct));
        }

        if (excitatoryRiskHere) {
            excitatoryRiskDoses.push_back(static_cast<double>(dose));
        }

        if (overSuppressionHere) {
            overSuppressionDoses.push_back(static_cast<double>(dose));
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

        const double mechanisticEvidence = mechanisticEvidenceScore(
            obs,
            perDoseState,
            rateChangeFrac,
            syncDeltaLocal,
            niiDeltaLocal,
            seizureDeltaLocal,
            syncReductionPct,
            niiReductionPct,
            seizureReductionPct,
            burstReductionPct,
            baselineRate
        );

        doseStates.push_back(perDoseState);
        doseWeights.push_back(1.0 + (perDoseState == BiologicalState::LimitedEffect ? 0.0 : 0.35) + 0.65 * mechanisticEvidence);
        doseBlockNa.push_back(static_cast<double>(obs.blockNa));
        doseBlockK.push_back(static_cast<double>(obs.blockK));
        doseBlockCa.push_back(static_cast<double>(obs.blockCa));
    }

    if (std::isfinite(maxRateChangePct)) {
        report.maxRateChangePct = maxRateChangePct;
    } else {
        report.maxRateChangePct = 0.0;
    }

    std::vector<double> yEffectMonotonic = yEffect;
    for (std::size_t i = 1; i < yEffectMonotonic.size(); ++i) {
        if (yEffectMonotonic[i] < yEffectMonotonic[i - 1U]) {
            yEffectMonotonic[i] = yEffectMonotonic[i - 1U];
        }
    }

    bool isMonotonic = true;
    for (std::size_t i = 1; i < yEffect.size(); ++i) {
        if (yEffect[i] + 1.0e-9 < yEffect[i - 1U]) {
            isMonotonic = false;
            break;
        }
    }

    (void)isMonotonic;

    report.sigmoidR2 = computeBestSigmoidR2(xDose, yEffectMonotonic);
    if (report.sigmoidR2 >= 0.95) {
        report.curveType = "Sigmoidal";
    } else if (report.sigmoidR2 >= 0.85) {
        report.curveType = "Weak Sigmoidal";
    } else {
        report.curveType = "Non-sigmoidal";
    }

    std::sort(safeDoses.begin(), safeDoses.end());
    std::sort(therapeuticDoses.begin(), therapeuticDoses.end());
    std::sort(excitatoryRiskDoses.begin(), excitatoryRiskDoses.end());
    std::sort(overSuppressionDoses.begin(), overSuppressionDoses.end());

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

    const auto safeRanges = contiguousRanges(safeDoses, report.stepDose);
    const auto therapeuticRanges = contiguousRanges(therapeuticDoses, report.stepDose);
    const auto excitatoryRiskRanges = contiguousRanges(excitatoryRiskDoses, report.stepDose);
    const auto overSuppressionRanges = contiguousRanges(overSuppressionDoses, report.stepDose);

    report.excitatoryRiskDoses = excitatoryRiskDoses;

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

    const BiologicalState detectedState = detectBiologicalState(
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

    const MechanisticDominance dominance = computeMechanisticDominance(doseStates, doseWeights, doseBlockNa, doseBlockK, doseBlockCa);
    const std::string resolvedResponseMode = resolveResponseMode(
        dominance,
        finalMeaningfulCaBlock,
        finalCalciumEffectMagnitude,
        lowStability,
        therapeuticWindowExists,
        fragmentedWindow,
        report.maxRateChangePct
    );

    if (resolvedResponseMode == "NO_SIGNIFICANT_RESPONSE") {
        report.biologicalState = BiologicalState::LimitedEffect;
    } else if (resolvedResponseMode == "STANDARD_RESPONSE") {
        // When ambiguous (STANDARD_RESPONSE) and channels indicate mixed mechanism,
        // prefer the observed detectedState rather than forcing the dominantState.
        if (dominance.mixedMechanism) {
            report.biologicalState = detectedState;
        } else {
            report.biologicalState = dominance.dominantState == BiologicalState::LimitedEffect ? detectedState : dominance.dominantState;
        }
    } else {
        report.biologicalState = stateForResponseMode(resolvedResponseMode);
    }

    switch (report.biologicalState) {
        case BiologicalState::ControlledSuppression:
            report.responseMode = "SUPPRESSIVE_RESPONSE";
            break;
        case BiologicalState::NeuralSilencing:
            report.responseMode = "NEURAL_SILENCING";
            break;
        case BiologicalState::Hyperexcitability:
            report.responseMode = "EXCITATORY_RESPONSE";
            break;
        case BiologicalState::NetworkStabilization:
            report.responseMode = "STABILIZING_RESPONSE";
            break;
        case BiologicalState::ToxicInstability:
            report.responseMode = "TOXIC_INSTABILITY";
            break;
        case BiologicalState::LimitedEffect:
        default:
            report.responseMode = "NO_SIGNIFICANT_RESPONSE";
            break;
    }

    // If channels indicate a true mixed mechanism, force ambiguous responseMode
    if (dominance.mixedMechanism) {
        report.responseMode = "STANDARD_RESPONSE";
    }

    const bool stabilizingResponseObserved = report.responseMode == "STABILIZING_RESPONSE";

    // Compute deltas for reporting (used in decision logic)
    const double finalSyncDelta = safeNonNegativeD(finalObs.synchronizationIndex) - baselineSync;
    const double finalBurstDelta = safeNonNegativeD(finalObs.burstIndex) - baselineBurst;
    const double finalNiiDelta = safeNonNegativeD(finalObs.nii) - baselineNii;
    const double finalIsiCvDelta = safeNonNegativeD(finalObs.isiCv) - baselineIsiCv;
    const double finalSeizureDelta = safeNonNegativeD(finalObs.seizureProbabilityPct) - baselineSeizure;

    (void)finalBurstDelta;
    (void)finalIsiCvDelta;

    const bool neuralSilencingDetected = (report.biologicalState == BiologicalState::NeuralSilencing);
    const bool hyperexcitabilityDetected = (report.biologicalState == BiologicalState::Hyperexcitability);
    const bool toxicInstabilityDetected = (report.biologicalState == BiologicalState::ToxicInstability);
    const bool networkStabilizationObserved = (report.biologicalState == BiologicalState::NetworkStabilization);
    const bool controlledSuppressionObserved = (report.biologicalState == BiologicalState::ControlledSuppression);
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

    report.confidence = confidenceFromEvidence(stabilityInput, report.sigmoidR2, therapeuticWindowExists, fragmentedWindow);
    // If channels indicate a true mixed mechanism, reduce confidence to reflect mechanistic ambiguity
    if (dominance.mixedMechanism) {
        report.confidence = "LOW";
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

std::string reportToString(const PharmaDecisionReport& report) {
    std::ostringstream out;
    out << "Recommendation     : " << report.recommendation << "\n";
    out << "Risk Level        : " << report.riskLevel << "\n";
    out << "Biological State  : " << report.biologicalStateText << "\n";
    out << "Primary Change    : " << report.primaryChangeText << "\n";
    out << "Safety Interpretation: " << report.safetyInterpretationText << "\n";
    out << "Seizure Trend     : " << report.seizureTrendText << "\n";
    out << "Reasoning         : " << report.reason << "\n";
    return out.str();
}

} // namespace spp::analyzer