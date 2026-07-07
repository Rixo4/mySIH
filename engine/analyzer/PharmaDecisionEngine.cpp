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

// ─── Numeric helpers ────────────────────────────────────────────────────────

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
double clamp01d(double v) { return std::clamp(v, 0.0, 1.0); }

float safeNonNegative(float v) {
    return (!std::isfinite(v) || v < 0.0f) ? 0.0f : v;
}
double safeNonNegativeD(double v) {
    return (!std::isfinite(v) || v < 0.0) ? 0.0 : v;
}

double reductionPercent(double baseline, double current) {
    if (!std::isfinite(baseline) || !std::isfinite(current) || baseline <= 1.0e-6)
        return 0.0;
    return ((baseline - current) / baseline) * 100.0;
}

double increasePercent(double baseline, double current) {
    if (!std::isfinite(baseline) || !std::isfinite(current) || baseline <= 1.0e-6)
        return 0.0;
    return ((current - baseline) / baseline) * 100.0;
}

bool stabilityScoreIsMediumOrHigh(const std::string& s) {
    return s == "MEDIUM" || s == "HIGH";
}

// ─── Per-dose biological evidence scores ────────────────────────────────────
//
// These six continuous scores replace threshold-triggered classification.
// Every score is derived exclusively from measured biological metrics.
// No channel block fields are read here.

struct BioEvidence {
    double suppression    = 0.0;  // [0,1]  rate reduction, propagation loss
    double excitation     = 0.0;  // [0,1]  rate increase, sync/NII/seizure rise
    double stabilization  = 0.0;  // [0,1]  sync/NII/seizure reduction at viable firing
    double silencing      = 0.0;  // [0,1]  near-total firing collapse
    double toxicity       = 0.0;  // [0,1]  seizure escalation, instability
    double noEffect       = 0.0;  // [0,1]  magnitude of all changes (for inactivity)
};

// Normalised metric deltas from baseline — all biology-derived.
struct MetricDeltas {
    double rateChangeFrac    = 0.0;   // (rate - baseline) / baseline
    double syncDelta         = 0.0;   // sync - baselineSync
    double niiDelta          = 0.0;   // nii  - baselineNii
    double seizureDelta      = 0.0;   // seizure01 - baselineSeizure01
    double syncReductPct     = 0.0;   // percent reduction in sync
    double niiReductPct      = 0.0;   // percent reduction in nii
    double seizureReductPct  = 0.0;   // percent reduction in seizure%
    double burstReductPct    = 0.0;   // percent reduction in burst
    // Normalised absolute values (0..1)
    double seizureNorm       = 0.0;
    double suppressionNorm   = 0.0;
    double syncNorm          = 0.0;
    double burstNorm         = 0.0;
    double niiNorm           = 0.0;
};

BioEvidence computeEvidenceScores(const MetricDeltas& d, double rateFrac) {
    BioEvidence e;

    // ── SILENCING: near-total firing collapse ──────────────────────────────
    // Emerges from firing rate falling below 10 % of baseline.
    e.silencing = clamp01d(std::max(0.0, (-rateFrac - 0.90) / 0.10));
    // Smooth onset: already near-zero above threshold, sharp rise below.

    // ── SUPPRESSION: rate reduction + network quieting ─────────────────────
    // Firing rate reduction is the primary signal.
    // Corroborated by reduction in network recruitment (sync, NII, seizure).
    if (rateFrac < 0.0 && e.silencing < 0.80) {
        const double rateSuppression = clamp01d(-rateFrac / 0.70);
        const double networkSilencing = clamp01d(
            0.30 * clamp01d(d.syncReductPct   / 100.0) +
            0.30 * clamp01d(d.niiReductPct    / 100.0) +
            0.25 * clamp01d(d.seizureReductPct / 100.0) +
            0.15 * clamp01d(d.burstReductPct  / 100.0)
        );
        // Require rate reduction as anchor; network quieting amplifies it.
        e.suppression = clamp01d(0.60 * rateSuppression + 0.40 * networkSilencing);
    }

    // ── EXCITATION: rate/sync/NII/seizure increase ─────────────────────────
    // Any combination of rising firing rate, synchronisation, NII, or
    // seizure probability constitutes excitatory evidence.
    {
        const double rateDrive    = clamp01d( rateFrac        / 0.50);
        const double syncDrive    = clamp01d( d.syncDelta     / 0.30);
        const double niiDrive     = clamp01d( d.niiDelta      / 0.40);
        const double seizureDrive = clamp01d( d.seizureDelta  / 0.40);
        e.excitation = clamp01d(
            0.35 * rateDrive    +
            0.25 * syncDrive    +
            0.20 * niiDrive     +
            0.20 * seizureDrive
        );
    }

    // ── STABILISATION: sync/NII/seizure reduction at preserved firing ──────
    // Emerges from simultaneous reduction in network instability markers
    // while firing rate is maintained (not suppressed to near-zero).
    // No channel information whatsoever.
    {
        const bool firingViable = (rateFrac > -0.40);
        if (firingViable) {
            const double syncBenefit    = clamp01d(d.syncReductPct    / 100.0);
            const double niiBenefit     = clamp01d(d.niiReductPct     / 100.0);
            const double seizureBenefit = clamp01d(d.seizureReductPct / 100.0);
            const double burstBenefit   = clamp01d(d.burstReductPct   / 100.0);
            const double netBenefit = clamp01d(
                0.35 * syncBenefit    +
                0.30 * niiBenefit     +
                0.20 * seizureBenefit +
                0.15 * burstBenefit
            );
            // Stabilisation requires at least some benefit and non-excitation.
            e.stabilization = clamp01d(netBenefit * (1.0 - 0.8 * e.excitation));
        }
    }

    // ── TOXICITY: seizure escalation + instability escalation ─────────────
    // Emerges from severe increases in instability markers, not from
    // channel block depth.
    {
        const double seizureEscalation  = clamp01d( d.seizureDelta / 0.50);
        const double niiEscalation      = clamp01d( d.niiDelta     / 0.50);
        const double instability = clamp01d(
            0.45 * d.syncNorm  +
            0.30 * d.burstNorm +
            0.25 * d.niiNorm
        );
        e.toxicity = clamp01d(
            0.40 * seizureEscalation +
            0.30 * niiEscalation     +
            0.30 * instability
        );
        // Suppression-driven pathological collapse also constitutes toxicity.
        if (e.silencing > 0.80) {
            e.toxicity = std::max(e.toxicity, 0.70);
        }
    }

    // ── NO SIGNIFICANT EFFECT: magnitude of all changes ───────────────────
    {
        const double anyChange = clamp01d(std::max({
            std::fabs(rateFrac),
            std::fabs(d.syncDelta)    / 0.30,
            std::fabs(d.niiDelta)     / 0.40,
            std::fabs(d.seizureDelta) / 0.40
        }));
        e.noEffect = 1.0 - clamp01d(anyChange * 3.0);
    }

    return e;
}

// ─── Per-dose state from continuous evidence ─────────────────────────────────
//
// The dominant evidence score determines the state.
// Ties are resolved by the priority ordering below.
BiologicalState stateFromEvidence(const BioEvidence& e) {
    // Silencing overrides everything — complete network shutdown.
    if (e.silencing >= 0.80) return BiologicalState::NeuralSilencing;

    // Collect scores for the four main contestable states.
    struct Candidate { BiologicalState state; double score; };
    const Candidate candidates[] = {
        { BiologicalState::ToxicInstability,    e.toxicity       },
        { BiologicalState::Hyperexcitability,   e.excitation     },
        { BiologicalState::NetworkStabilization, e.stabilization },
        { BiologicalState::ControlledSuppression, e.suppression  },
    };

    // Minimum evidence threshold — below this nothing is declared.
    constexpr double kMinEvidence = 0.15;

    double bestScore = kMinEvidence;
    BiologicalState bestState = BiologicalState::LimitedEffect;
    for (const auto& c : candidates) {
        if (c.score > bestScore) {
            bestScore = c.score;
            bestState = c.state;
        }
    }
    return bestState;
}

// ─── DoseRiskMetrics (unchanged — used for seizure-risk sub-models) ─────────

struct DoseRiskMetrics {
    double rateChangeFrac    = 0.0;
    double syncDelta         = 0.0;
    double niiDelta          = 0.0;
    double seizureDelta      = 0.0;
    double syncReductionPct  = 0.0;
    double niiReductionPct   = 0.0;
    double seizureReductionPct = 0.0;
    double burstReductionPct = 0.0;
    double seizureNorm       = 0.0;
    double suppressionNorm   = 0.0;
    double syncNorm          = 0.0;
    double burstNorm         = 0.0;
    double niiNorm           = 0.0;
};

double instabilityMetric(const DoseRiskMetrics& m) {
    return clamp01d(0.45 * m.syncNorm + 0.30 * m.burstNorm + 0.25 * m.niiNorm);
}

double reboundExcitationMetric(const DoseRiskMetrics& m) {
    return clamp01d(std::max({0.0, m.rateChangeFrac, m.syncDelta, m.niiDelta, m.seizureDelta}));
}

double suppressionProtectionMetric(const DoseRiskMetrics& m) {
    return clamp01d(
        0.35 * clamp01d(-m.rateChangeFrac / 0.70) +
        0.20 * clamp01d(m.syncReductionPct   / 100.0) +
        0.20 * clamp01d(m.niiReductionPct    / 100.0) +
        0.15 * clamp01d(m.seizureReductionPct / 100.0) +
        0.10 * clamp01d(m.burstReductionPct  / 100.0)
    );
}

double computeSuppressiveSeizureRisk(const DoseRiskMetrics& m) {
    const double residual = clamp01d(
        0.35 * m.seizureNorm         +
        0.35 * instabilityMetric(m)  +
        0.30 * reboundExcitationMetric(m)
    );
    return clamp01d(residual - suppressionProtectionMetric(m));
}

double computeNoSignificantResponseRisk(const DoseRiskMetrics& m) {
    const double smallShift = clamp01d(std::max({
        0.0,
        std::fabs(m.rateChangeFrac),
        std::fabs(m.syncDelta),
        std::fabs(m.niiDelta),
        std::fabs(m.seizureDelta)
    }));
    return clamp01d(
        0.35 * m.seizureNorm         +
        0.35 * instabilityMetric(m)  +
        0.30 * smallShift
    );
}

double computeExcitatorySeizureRisk(const DoseRiskMetrics& m) {
    const double excitationDrive = clamp01d(
        0.35 * clamp01d(m.rateChangeFrac  / 0.50) +
        0.25 * clamp01d(m.syncDelta)               +
        0.20 * clamp01d(m.niiDelta)                +
        0.20 * clamp01d(m.seizureDelta)
    );
    const double residual = clamp01d(
        0.30 * m.seizureNorm         +
        0.40 * instabilityMetric(m)  +
        0.30 * clamp01d(m.niiDelta)
    );
    return clamp01d(0.55 * residual + 0.45 * excitationDrive);
}

double computeStabilizingSeizureRisk(const DoseRiskMetrics& m) {
    const double stabilizationBenefit = clamp01d(
        0.35 * clamp01d(m.syncReductionPct    / 100.0) +
        0.25 * clamp01d(m.niiReductionPct     / 100.0) +
        0.20 * clamp01d(m.seizureReductionPct / 100.0) +
        0.20 * clamp01d(m.burstReductionPct   / 100.0)
    );
    const double residual = clamp01d(
        0.35 * m.seizureNorm +
        0.35 * instabilityMetric(m) +
        0.30 * clamp01d(std::max({0.0, m.rateChangeFrac, m.syncDelta, m.niiDelta}))
    );
    return clamp01d(residual - 0.55 * stabilizationBenefit);
}

double computeSeizureRiskForMode(const std::string& responseMode, const DoseRiskMetrics& m) {
    if (responseMode == "NO_SIGNIFICANT_RESPONSE")  return computeNoSignificantResponseRisk(m);
    if (responseMode == "EXCITATORY_RESPONSE")      return computeExcitatorySeizureRisk(m);
    if (responseMode == "STABILIZING_RESPONSE")     return computeStabilizingSeizureRisk(m);
    return computeSuppressiveSeizureRisk(m);
}

double computeEarlyWarningIndexForMode(const std::string& mode,
                                       const DoseRiskMetrics& m,
                                       double seizureRisk) {
    if (mode == "NO_SIGNIFICANT_RESPONSE") {
        return 100.0 * clamp01d(
            0.45 * seizureRisk +
            0.35 * instabilityMetric(m) +
            0.20 * std::max({0.0, std::fabs(m.rateChangeFrac),
                             std::fabs(m.syncDelta), std::fabs(m.niiDelta)})
        );
    }
    if (mode == "EXCITATORY_RESPONSE") {
        return 100.0 * clamp01d(
            0.50 * seizureRisk +
            0.30 * reboundExcitationMetric(m) +
            0.20 * instabilityMetric(m)
        );
    }
    if (mode == "STABILIZING_RESPONSE") {
        const double benefit = clamp01d(
            0.35 * clamp01d(m.syncReductionPct    / 100.0) +
            0.25 * clamp01d(m.niiReductionPct     / 100.0) +
            0.20 * clamp01d(m.seizureReductionPct / 100.0) +
            0.20 * clamp01d(m.burstReductionPct   / 100.0)
        );
        return 100.0 * clamp01d(
            0.55 * seizureRisk        +
            0.25 * (1.0 - benefit)    +
            0.20 * instabilityMetric(m)
        );
    }
    return 100.0 * clamp01d(
        0.55 * seizureRisk              +
        0.25 * instabilityMetric(m)     +
        0.20 * suppressionProtectionMetric(m)
    );
}

// ─── Metric-driven narrative generation ──────────────────────────────────────
//
// RULE: Every sentence must be derivable from a named metric and its direction.
// No channel names. No hardcoded mechanism phrases.
//
// Helper: describe the dominant biological change in plain English.
struct MetricNarrative {
    double rateChangePct;          // signed %
    double syncReductPct;          // >0 = reduced
    double niiReductPct;
    double seizureReductPct;
    double burstReductPct;
    double syncIncreasePct;        // >0 = increased
    double niiIncreasePct;
    double seizureIncreasePct;
};

std::string describeDominantChange(const MetricNarrative& n, BiologicalState state) {
    std::ostringstream oss;
    switch (state) {
        case BiologicalState::NeuralSilencing:
            oss << "Mean firing rate fell to near-zero ("
                << std::fixed << std::setprecision(1) << n.rateChangePct
                << "% change from baseline), indicating near-complete network shutdown.";
            break;

        case BiologicalState::Hyperexcitability:
            if (n.rateChangePct > 5.0) {
                oss << "Mean firing rate increased by " << std::setprecision(1)
                    << n.rateChangePct << "% from baseline";
            } else {
                oss << "Firing rate remained near baseline";
            }
            if (n.seizureIncreasePct > 5.0) {
                oss << "; seizure-risk markers rose by "
                    << std::setprecision(1) << n.seizureIncreasePct << "%.";
            } else if (n.niiIncreasePct > 5.0) {
                oss << "; network instability index (NII) increased by "
                    << std::setprecision(1) << n.niiIncreasePct << "%.";
            } else if (n.syncIncreasePct > 5.0) {
                oss << "; synchronisation index rose by "
                    << std::setprecision(1) << n.syncIncreasePct << "%.";
            } else {
                oss << "; network instability markers increased.";
            }
            break;

        case BiologicalState::ToxicInstability:
            oss << "Severe instability observed: seizure-risk markers";
            if (n.seizureIncreasePct > 0.0)
                oss << " rose by " << std::setprecision(1) << n.seizureIncreasePct << "%";
            oss << " and NII";
            if (n.niiIncreasePct > 0.0)
                oss << " increased by " << std::setprecision(1) << n.niiIncreasePct << "%";
            oss << " beyond tolerability thresholds.";
            break;

        case BiologicalState::NetworkStabilization:
            oss << "Synchronisation reduced by "
                << std::setprecision(1) << n.syncReductPct
                << "%, NII by " << n.niiReductPct
                << "%, and seizure-risk markers by " << n.seizureReductPct
                << "%, while mean firing rate remained within viable range ("
                << (n.rateChangePct >= 0 ? "+" : "") << n.rateChangePct << "%).";
            break;

        case BiologicalState::ControlledSuppression:
            oss << "Mean firing rate reduced by "
                << std::setprecision(1) << std::fabs(n.rateChangePct)
                << "% from baseline without toxic instability (NII change: "
                << (n.niiIncreasePct >= 0 ? "+" : "") << n.niiIncreasePct << "%).";
            break;

        case BiologicalState::LimitedEffect:
        default:
            oss << "No biologically significant change in firing rate, synchronisation, "
                << "NII, or seizure-risk markers across the tested dose range.";
            break;
    }
    return oss.str();
}

std::string safetyNarrativeFromMetrics(
    BiologicalState state,
    bool toxicityBeforeTherapy,
    bool toxicityAfterTherapy,
    bool lowStability,
    bool networkStabilizationObserved
) {
    switch (state) {
        case BiologicalState::NeuralSilencing:
            return "Complete suppression of neural activity is incompatible with "
                   "therapeutic use: viable physiological function requires maintained "
                   "firing activity.";

        case BiologicalState::Hyperexcitability:
            return "Observed increases in firing rate, synchronisation, or seizure-risk "
                   "markers represent an unacceptable safety risk.";

        case BiologicalState::ToxicInstability:
            return "Extreme escalation of instability metrics (NII, seizure probability) "
                   "without a preceding therapeutic window indicates unacceptable toxicity.";

        case BiologicalState::NetworkStabilization:
            if (networkStabilizationObserved)
                return "Observed reductions in synchronisation, NII, and seizure-risk "
                       "markers—without excitatory rebound or pathological suppression—"
                       "indicate a favourable biological safety profile.";
            return "Partial network stabilisation observed; safety profile requires "
                   "further characterisation.";

        case BiologicalState::ControlledSuppression:
            if (toxicityBeforeTherapy)
                return "Toxicity markers appeared before the therapeutic firing-rate "
                       "reduction window; the dose-response ordering is unsafe.";
            if (toxicityAfterTherapy)
                return "Therapeutic suppression was observed before toxicity, indicating "
                       "a therapeutic window exists, though it is narrow.";
            if (lowStability)
                return "A therapeutic window is present but inter-run variability reduces "
                       "dosing confidence.";
            return "Controlled reduction in firing activity was achieved without "
                   "accompanying instability markers, indicating an acceptable safety margin.";

        case BiologicalState::LimitedEffect:
        default:
            return "No meaningful biological response detected; safety cannot be "
                   "assessed from available data.";
    }
}

std::string generateReasonFromMetrics(
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
            return "Near-complete firing-rate collapse was observed; this level of "
                   "network suppression precludes therapeutic use.";

        case BiologicalState::Hyperexcitability:
            return "Observed increases in firing rate, synchronisation, NII, and/or "
                   "seizure-risk markers exceeded safe neural stability limits.";

        case BiologicalState::ToxicInstability:
            return "Extreme instability metrics (NII spike, seizure escalation) were "
                   "observed without a preceding therapeutic window; risk is unacceptable.";

        case BiologicalState::NetworkStabilization:
            return "Observed reductions in synchronisation, NII, and seizure-risk "
                   "markers—with preserved viable firing—constitute a stabilising "
                   "biological response.";

        case BiologicalState::ControlledSuppression:
            if (toxicityBeforeTherapy)
                return "Toxicity markers (seizure escalation, instability) appeared at "
                       "doses below the therapeutic suppression window; the safety "
                       "ordering is unacceptable.";
            if (toxicityAfterTherapy)
                return "Firing-rate reduction within therapeutic range preceded instability "
                       "markers; a narrow therapeutic window exists but requires careful dosing.";
            if (!therapeuticWindowExists)
                return "Some firing-rate reduction was observed but no continuous "
                       "therapeutic window was identified.";
            if (fragmentedWindow)
                return "Therapeutic firing-rate reduction was intermittent; the fragmented "
                       "dose-response pattern reduces dosing reliability.";
            if (lowStability)
                return "A therapeutic window exists but high inter-run variability in "
                       "firing rate and instability metrics reduces confidence.";
            return "A continuous therapeutic firing-rate reduction window was identified "
                   "with no accompanying instability, indicating a promising dose-response "
                   "profile.";

        case BiologicalState::LimitedEffect:
        default:
            return "No significant change in firing rate, synchronisation, NII, or "
                   "seizure-risk markers was observed across the tested dose range.";
    }
}

// ─── Global biological state (from final observation + scan) ─────────────────
//
// [V2][V3] CORRECTED: no channel block fields accessed.
// Hyperexcitability emerges from biological metric changes.
// NetworkStabilization emerges from metric reductions alone.

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
    const double currentRate  = safeNonNegativeD(finalObs.meanFiringRateHz);
    const double currentSync  = safeNonNegativeD(finalObs.synchronizationIndex);
    const double currentBurst = safeNonNegativeD(finalObs.burstIndex);
    const double currentNii   = safeNonNegativeD(finalObs.nii);
    const double currentSeiz  = safeNonNegativeD(finalObs.seizureProbabilityPct);

    const double rateFrac = (currentRate - baselineRate) / std::max(1.0, baselineRate);

    MetricDeltas d;
    d.rateChangeFrac    = rateFrac;
    d.syncDelta         = currentSync  - baselineSync;
    d.niiDelta          = currentNii   - baselineNii;
    d.seizureDelta      = currentSeiz / 100.0 - baselineSeizure / 100.0;
    d.syncReductPct     = reductionPercent(baselineSync,    currentSync);
    d.niiReductPct      = reductionPercent(baselineNii,     currentNii);
    d.seizureReductPct  = reductionPercent(baselineSeizure, currentSeiz);
    d.burstReductPct    = reductionPercent(baselineBurst,   currentBurst);
    d.seizureNorm       = clamp01d(currentSeiz / 100.0);
    d.syncNorm          = clamp01d(currentSync);
    d.burstNorm         = clamp01d(currentBurst / 0.20);
    d.niiNorm           = clamp01d(currentNii);

    // Accumulate per-dose evidence across the full curve.
    double peakSilencing      = 0.0;
    double peakToxicity       = 0.0;
    double peakExcitation     = 0.0;
    double peakStabilization  = 0.0;
    double peakSuppression    = 0.0;

    for (const auto& obs : sortedObs) {
        const double oRate  = safeNonNegativeD(obs.meanFiringRateHz);
        const double oSync  = safeNonNegativeD(obs.synchronizationIndex);
        const double oBurst = safeNonNegativeD(obs.burstIndex);
        const double oNii   = safeNonNegativeD(obs.nii);
        const double oSeiz  = safeNonNegativeD(obs.seizureProbabilityPct);

        MetricDeltas od;
        od.rateChangeFrac    = (oRate - baselineRate) / std::max(1.0, baselineRate);
        od.syncDelta         = oSync  - baselineSync;
        od.niiDelta          = oNii   - baselineNii;
        od.seizureDelta      = oSeiz / 100.0 - baselineSeizure / 100.0;
        od.syncReductPct     = reductionPercent(baselineSync,    oSync);
        od.niiReductPct      = reductionPercent(baselineNii,     oNii);
        od.seizureReductPct  = reductionPercent(baselineSeizure, oSeiz);
        od.burstReductPct    = reductionPercent(baselineBurst,   oBurst);
        od.seizureNorm       = clamp01d(oSeiz / 100.0);
        od.syncNorm          = clamp01d(oSync);
        od.burstNorm         = clamp01d(oBurst / 0.20);
        od.niiNorm           = clamp01d(oNii);

        const BioEvidence ev = computeEvidenceScores(od, od.rateChangeFrac);
        peakSilencing    = std::max(peakSilencing,    ev.silencing);
        peakToxicity     = std::max(peakToxicity,     ev.toxicity);
        peakExcitation   = std::max(peakExcitation,   ev.excitation);
        peakStabilization= std::max(peakStabilization,ev.stabilization);
        peakSuppression  = std::max(peakSuppression,  ev.suppression);
    }

    // Use the final-observation evidence as primary, boosted by curve peaks.
    const BioEvidence finalEv = computeEvidenceScores(d, rateFrac);

    // Blend: 60% final state, 40% peak seen anywhere on curve.
    BioEvidence blended;
    blended.silencing     = 0.60 * finalEv.silencing     + 0.40 * peakSilencing;
    blended.toxicity      = 0.60 * finalEv.toxicity      + 0.40 * peakToxicity;
    blended.excitation    = 0.60 * finalEv.excitation    + 0.40 * peakExcitation;
    blended.stabilization = 0.60 * finalEv.stabilization + 0.40 * peakStabilization;
    blended.suppression   = 0.60 * finalEv.suppression   + 0.40 * peakSuppression;

    return stateFromEvidence(blended);
}

// ─── Sigmoid R² fit ──────────────────────────────────────────────────────────

double computeBestSigmoidR2(const std::vector<double>& dose,
                             const std::vector<double>& effect01) {
    if (dose.size() != effect01.size() || dose.size() < 4U) return 0.0;

    const auto [dminIt, dmaxIt] = std::minmax_element(dose.begin(), dose.end());
    const double dMin = *dminIt, dMax = *dmaxIt;

    double meanY = 0.0;
    for (double y : effect01) meanY += y;
    meanY /= static_cast<double>(effect01.size());

    double sst = 0.0;
    for (double y : effect01) { double dy = y - meanY; sst += dy * dy; }
    if (sst <= 1.0e-12) return 1.0;

    double bestSse = std::numeric_limits<double>::infinity();
    const double dStep = std::max(0.25, (dMax - dMin) / 120.0);

    for (double k = 0.02; k <= 2.51; k += 0.02) {
        for (double d50 = dMin; d50 <= dMax + 1.0e-9; d50 += dStep) {
            double numer = 0.0, denom = 0.0;
            for (std::size_t i = 0; i < dose.size(); ++i) {
                const double z = std::clamp(-k * (dose[i] - d50), -60.0, 60.0);
                const double s = 1.0 / (1.0 + std::exp(z));
                numer += effect01[i] * s;
                denom += s * s;
            }
            if (denom <= 1.0e-12) continue;
            const double emax = std::clamp(numer / denom, 0.0, 1.0);
            double sse = 0.0;
            for (std::size_t i = 0; i < dose.size(); ++i) {
                const double z = std::clamp(-k * (dose[i] - d50), -60.0, 60.0);
                const double s = 1.0 / (1.0 + std::exp(z));
                const double err = effect01[i] - emax * s;
                sse += err * err;
            }
            bestSse = std::min(bestSse, sse);
        }
    }
    if (!std::isfinite(bestSse)) return 0.0;
    return std::clamp(1.0 - bestSse / (sst + 1.0e-12), -1.0, 1.0);
}

// ─── Contiguous range helper ─────────────────────────────────────────────────

struct Range { double lo = 0.0, hi = 0.0; std::size_t points = 0U; };

std::vector<Range> contiguousRanges(const std::vector<double>& doses, double step) {
    std::vector<Range> out;
    if (doses.empty()) return out;
    const double expected = std::max(1.0e-6, step);
    Range current{doses.front(), doses.front(), 1U};
    for (std::size_t i = 1; i < doses.size(); ++i) {
        if (doses[i] - doses[i - 1U] <= 2.0 * expected + 1.0e-6) {
            current.hi = doses[i]; ++current.points;
        } else {
            out.push_back(current);
            current = {doses[i], doses[i], 1U};
        }
    }
    out.push_back(current);
    return out;
}

// ─── Risk tier ───────────────────────────────────────────────────────────────

DrugRiskTier classifyTier(float seizureNorm, float suppressionNorm, float riskScore) {
    if (seizureNorm > 0.80f || suppressionNorm > 0.80f || riskScore >= 80.0f)
        return DrugRiskTier::Toxic;
    if (riskScore >= 65.0f) return DrugRiskTier::HighRisk;
    if (riskScore >= 35.0f) return DrugRiskTier::ModerateRisk;
    return DrugRiskTier::Safe;
}

// ─── Confidence ──────────────────────────────────────────────────────────────
//
// Confidence is a function of curve quality, mechanism consistency,
// biological coherence, and window continuity.  Run count is a minor
// secondary factor only.

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
    double score = 0.0;

    const bool isClearState  = (state != BiologicalState::LimitedEffect);
    const bool isTherapeutic = (state == BiologicalState::ControlledSuppression ||
                                 state == BiologicalState::NetworkStabilization);
    const bool isDanger      = (state == BiologicalState::NeuralSilencing ||
                                 state == BiologicalState::Hyperexcitability ||
                                 state == BiologicalState::ToxicInstability);

    // 1. State clarity (30 pts max)
    if (isClearState) {
        score += 30.0;
        if (isTherapeutic || isDanger) score += 10.0;
    } else {
        score += (maxRateChangePct > 10.0) ? 8.0 : 2.0;
    }

    // 2. Metric alignment (20 pts max)
    if (isTherapeutic) {
        int aligned = 0;
        if (syncReductionPct   >= 15.0) aligned++;
        if (niiReductionPct    >= 15.0) aligned++;
        if (seizureReductionPct >= 15.0) aligned++;
        if (burstReductionPct  >= 15.0) aligned++;
        if      (aligned >= 3)          score += 20.0;
        else if (aligned == 2)          score += 15.0;
        else if (aligned == 1)          score += 10.0;
        else if (maxRateChangePct > 30) score +=  8.0;
    } else if (isDanger) {
        if      (peakRiskScore >= 70.0 || peakSeizureProbabilityPct >= 70.0) score += 20.0;
        else if (peakRiskScore >= 50.0 || peakSeizureProbabilityPct >= 50.0) score += 15.0;
        else                                                                   score += 10.0;
    } else {
        score += (maxRateChangePct > 20.0) ? 10.0 : (maxRateChangePct > 10.0) ? 5.0 : 0.0;
    }

    // 3. Curve quality — sigmoid fit (20 pts max)
    if      (sigmoidR2 >= 0.95) score += 20.0;
    else if (sigmoidR2 >= 0.90) score += 17.0;
    else if (sigmoidR2 >= 0.85) score += 14.0;
    else if (sigmoidR2 >= 0.75) score +=  9.0;
    else if (sigmoidR2 >= 0.65) score +=  5.0;

    // 4. Window continuity (10 pts max) — penalise fragmentation
    if      (therapeuticWindowExists && hasContinuousEffectiveWindow && !fragmentedWindow) score += 10.0;
    else if (therapeuticWindowExists && hasContinuousEffectiveWindow)                       score +=  8.0;
    else if (therapeuticWindowExists)                                                        score +=  5.0;
    else if (isDanger)                                                                       score +=  5.0;

    // 5. Reproducibility / variability (10 pts max)
    if      (rateVariability < 1.0f  && toxicityVariability <  5.0f) score += 10.0;
    else if (rateVariability < 1.5f  && toxicityVariability <  7.5f) score +=  8.0;
    else if (rateVariability < 2.0f  && toxicityVariability < 10.0f) score +=  5.0;
    else                                                               score +=  2.0;

    const double clamped = std::clamp(score, 0.0, 100.0);

    // Run-count is a minor modifier only — it cannot promote LOW→HIGH alone.
    if (runCount < 2) {
        if (clamped >= 80.0) return "HIGH";
        if (clamped >= 70.0) return "MEDIUM";
        return "LOW";
    }
    if (clamped >= 80.0) return "HIGH";
    if (clamped >= 50.0) return "MEDIUM";
    return "LOW";
}

} // anonymous namespace

// ─── Risk level text ─────────────────────────────────────────────────────────

std::string riskLevelText(DrugRiskTier tier) {
    switch (tier) {
        case DrugRiskTier::Safe:         return "LOW";
        case DrugRiskTier::ModerateRisk: return "MODERATE";
        default:                          return "HIGH";
    }
}

// ════════════════════════════════════════════════════════════════════════════
// PharmaDecisionEngine::evaluate
// ════════════════════════════════════════════════════════════════════════════

PharmaDecisionReport PharmaDecisionEngine::evaluate(
    const std::vector<DoseObservation>& observations,
    const DecisionStabilityInput& stabilityInput
) {
    PharmaDecisionReport report;
    if (observations.empty()) return report;

    std::vector<DoseObservation> sorted = observations;
    std::sort(sorted.begin(), sorted.end(),
              [](const DoseObservation& a, const DoseObservation& b) {
                  return a.dose < b.dose;
              });

    report.points.reserve(sorted.size());
    report.features.reserve(sorted.size());

    report.minTestedDose = safeNonNegativeD(sorted.front().dose);
    report.maxTestedDose = safeNonNegativeD(sorted.back().dose);

    double inferredStep = 0.0;
    for (std::size_t i = 1; i < sorted.size(); ++i) {
        const double dx = safeNonNegativeD(sorted[i].dose) -
                          safeNonNegativeD(sorted[i-1].dose);
        if (dx > 1.0e-6)
            inferredStep = (inferredStep <= 0.0) ? dx : std::min(inferredStep, dx);
    }
    report.stepDose = inferredStep;
    report.rateVariability    = std::max(0.0f, stabilityInput.rateStd);
    report.toxicityVariability= std::max(0.0f, stabilityInput.toxicityStd);
    report.stabilityScore     = stabilityInput.stabilityScore.empty()
                                  ? "UNSPECIFIED" : stabilityInput.stabilityScore;

    const double baselineRate    = std::max(1.0e-6, safeNonNegativeD(sorted.front().meanFiringRateHz));
    const double baselineSync    = safeNonNegativeD(sorted.front().synchronizationIndex);
    const double baselineBurst   = safeNonNegativeD(sorted.front().burstIndex);
    const double baselineNii     = safeNonNegativeD(sorted.front().nii);
    const double baselineIsiCv   = safeNonNegativeD(sorted.front().isiCv);
    const double baselineSeizure = safeNonNegativeD(sorted.front().seizureProbabilityPct);

    // ── Curve-level accumulators ─────────────────────────────────────────────
    double maxRateChangePct = -std::numeric_limits<double>::infinity();
    double peakEffect = -std::numeric_limits<double>::infinity();

    // Cumulative evidence sums — used for dominant mechanism determination.
    // [FIX vs old code]: dominance is by CUMULATIVE EVIDENCE STRENGTH,
    // not by count of doses classified in each state.
    double cumulativeSuppressionEvidence    = 0.0;
    double cumulativeExcitationEvidence     = 0.0;
    double cumulativeStabilizationEvidence  = 0.0;
    double cumulativeSilencingEvidence      = 0.0;
    double cumulativeToxicityEvidence       = 0.0;

    bool sawHighDoseEffect     = false;
    bool hasOnsetDose          = false;
    double onsetDose           = 0.0;
    // Removed: sawMeaningfulCaBlock — [V7] ELIMINATED

    double peakCalciumEffectMagnitude = 0.0;  // retained for report field, computed from metrics
    double maxSyncDelta   = 0.0;
    double maxBurstDelta  = 0.0;
    double maxNiiDelta    = 0.0;
    double maxIsiCvDelta  = 0.0;
    double maxSeizureDelta= 0.0;

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

    // ── Per-dose loop ────────────────────────────────────────────────────────
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const DoseObservation& obs = sorted[i];

        const float dose         = safeNonNegative(obs.dose);
        const float meanRate     = safeNonNegative(obs.meanFiringRateHz);
        const float seizurePct   = safeNonNegative(obs.seizureProbabilityPct);
        const float suppressPct  = safeNonNegative(obs.suppressionPct);
        const float syncValue    = safeNonNegative(obs.synchronizationIndex);
        const float burstValue   = safeNonNegative(obs.burstIndex);
        const float niiValue     = safeNonNegative(obs.nii);
        const float isiCv        = safeNonNegative(obs.isiCv);

        const float seizureNorm    = clamp01(seizurePct   / 100.0f);
        const float suppressionNorm= clamp01(suppressPct  / 100.0f);
        const float syncNorm       = clamp01(syncValue);
        const float burstNorm      = clamp01(burstValue   / 0.20f);
        const float niiNorm        = clamp01(niiValue);

        const double rateChangeFrac    = (static_cast<double>(meanRate) - baselineRate)
                                         / std::max(1.0, baselineRate);
        const double syncDeltaLocal    = static_cast<double>(syncValue)  - baselineSync;
        const double niiDeltaLocal     = static_cast<double>(niiValue)   - baselineNii;
        const double seizureDeltaLocal = static_cast<double>(seizurePct) / 100.0
                                         - baselineSeizure / 100.0;
        const double syncReductionPct  = reductionPercent(baselineSync,    static_cast<double>(syncValue));
        const double niiReductionPct   = reductionPercent(baselineNii,     static_cast<double>(niiValue));
        const double burstReductionPct = reductionPercent(baselineBurst,   static_cast<double>(burstValue));
        const double seizureReductionPct = reductionPercent(baselineSeizure, static_cast<double>(seizurePct));

        // ── EVIDENCE COMPUTATION — pure biology, no channel reads ────────────
        MetricDeltas md;
        md.rateChangeFrac   = rateChangeFrac;
        md.syncDelta        = syncDeltaLocal;
        md.niiDelta         = niiDeltaLocal;
        md.seizureDelta     = seizureDeltaLocal;
        md.syncReductPct    = syncReductionPct;
        md.niiReductPct     = niiReductionPct;
        md.seizureReductPct = seizureReductionPct;
        md.burstReductPct   = burstReductionPct;
        md.seizureNorm      = static_cast<double>(seizureNorm);
        md.syncNorm         = static_cast<double>(syncNorm);
        md.burstNorm        = static_cast<double>(burstNorm);
        md.niiNorm          = static_cast<double>(niiNorm);

        const BioEvidence ev = computeEvidenceScores(md, rateChangeFrac);

        // Accumulate evidence strength across the curve.
        cumulativeSuppressionEvidence   += ev.suppression;
        cumulativeExcitationEvidence    += ev.excitation;
        cumulativeStabilizationEvidence += ev.stabilization;
        cumulativeSilencingEvidence     += ev.silencing;
        cumulativeToxicityEvidence      += ev.toxicity;

        // ── Per-dose state — from evidence, not channels ─────────────────────
        const BiologicalState perDoseState = stateFromEvidence(ev);

        // ── Toxicity score for feature record ────────────────────────────────
        const double toxicityScore = 100.0 * std::clamp(
            0.55 * static_cast<double>(seizureNorm) +
            0.30 * static_cast<double>(syncNorm) +
            0.15 * std::clamp(0.20 - static_cast<double>(isiCv), 0.0, 0.20) / 0.20,
            0.0, 1.0
        );

        const double signedRateChangePct   = rateChangeFrac * 100.0;
        const double suppressionEffectPct  = std::max(0.0, -signedRateChangePct);
        const double excitationEffectPct   = std::max(0.0,  signedRateChangePct);
        const double stabilizationEffectPct =
            (perDoseState == BiologicalState::NetworkStabilization)
                ? std::max({syncReductionPct, niiReductionPct,
                            seizureReductionPct, burstReductionPct})
                : 0.0;
        const double effectMagnitudePct = std::clamp(
            std::max({suppressionEffectPct, excitationEffectPct, stabilizationEffectPct}),
            0.0, 100.0
        );

        // Track peak stabilisation magnitude (from metrics, not Ca block).
        if (perDoseState == BiologicalState::NetworkStabilization) {
            peakCalciumEffectMagnitude = std::max(peakCalciumEffectMagnitude,
                                                   stabilizationEffectPct);
        }

        const bool isEffective = (perDoseState == BiologicalState::ControlledSuppression ||
                                   perDoseState == BiologicalState::NetworkStabilization);
        const bool isToxic     = (perDoseState == BiologicalState::ToxicInstability ||
                                   perDoseState == BiologicalState::NeuralSilencing  ||
                                   perDoseState == BiologicalState::Hyperexcitability);

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
        peakEffect = std::max(peakEffect, effectMagnitudePct);

        xDose.push_back(static_cast<double>(dose));
        suppressionCurveEffect.push_back(std::clamp(suppressionEffectPct  / 100.0, 0.0, 1.0));
        excitationCurveEffect.push_back( std::clamp(excitationEffectPct   / 100.0, 0.0, 1.0));
        stabilizationCurveEffect.push_back(std::clamp(stabilizationEffectPct / 100.0, 0.0, 1.0));
        noSignificantCurveEffect.push_back(std::clamp(effectMagnitudePct   / 100.0, 0.0, 1.0));

        const float instabilityMetricVal = clamp01(
            0.50f * syncNorm + 0.30f * burstNorm + 0.20f * niiNorm);
        const float riskNorm  = clamp01(
            0.50f * seizureNorm + 0.30f * suppressionNorm + 0.20f * instabilityMetricVal);
        const float riskScore = 100.0f * riskNorm;

        float seizureSlopePctPerDose = 0.0f;
        if (i > 0U) {
            const float deltaDose = std::max(1.0e-6f, dose - sorted[i-1U].dose);
            seizureSlopePctPerDose =
                (seizurePct - sorted[i-1U].seizureProbabilityPct) / deltaDose;
        }

        const float slopeNorm = clamp01(std::max(0.0f, seizureSlopePctPerDose) / 25.0f);
        const float earlyWarningIndex = 100.0f * clamp01(
            0.55f * seizureNorm + 0.30f * slopeNorm + 0.15f * instabilityMetricVal);

        const DrugRiskTier tier = classifyTier(seizureNorm, suppressionNorm, riskScore);
        report.points.push_back(DrugDecisionPoint{
            dose, seizurePct, suppressPct, riskScore,
            toString(tier), earlyWarningIndex, seizureSlopePctPerDose
        });

        report.peakRiskScore            = std::max(report.peakRiskScore,            riskScore);
        report.peakSeizureProbabilityPct= std::max(report.peakSeizureProbabilityPct,seizurePct);
        report.peakSuppressionPct       = std::max(report.peakSuppressionPct,       suppressPct);
        report.peakEarlyWarningIndex    = std::max(report.peakEarlyWarningIndex,    earlyWarningIndex);
        report.maxSeizureSlopePctPerDose= std::max(report.maxSeizureSlopePctPerDose,seizureSlopePctPerDose);

        maxSyncDelta   = std::max(maxSyncDelta,   syncDeltaLocal);
        maxBurstDelta  = std::max(maxBurstDelta,  static_cast<double>(burstValue) - baselineBurst);
        maxNiiDelta    = std::max(maxNiiDelta,    niiDeltaLocal);
        maxIsiCvDelta  = std::max(maxIsiCvDelta,  static_cast<double>(isiCv) - baselineIsiCv);
        maxSeizureDelta= std::max(maxSeizureDelta,static_cast<double>(seizurePct) - baselineSeizure);

        if (perDoseState == BiologicalState::LimitedEffect)
            safeDoses.push_back(static_cast<double>(dose));

        if (isEffective) {
            therapeuticDoses.push_back(static_cast<double>(dose));
            if (!hasOnsetDose) {
                hasOnsetDose = true;
                onsetDose    = static_cast<double>(dose);
                report.hasSuppressionThreshold  = true;
                report.suppressionThresholdDose  = dose;
                report.hasEffectiveDose          = true;
                report.effectiveMinDose          = dose;
            }
        }

        const bool excitatoryRiskHere =
            (perDoseState == BiologicalState::Hyperexcitability) ||
            (perDoseState == BiologicalState::ToxicInstability &&
             (rateChangeFrac > -0.10 || syncDeltaLocal > 0.10 ||
              niiDeltaLocal  >  0.10 || seizureDeltaLocal > 0.10)) ||
            (excitationEffectPct > 60.0 &&
             (rateChangeFrac > 0.0 || syncDeltaLocal > 0.0 ||
              niiDeltaLocal  > 0.0 || seizureDeltaLocal > 0.0)) ||
            (niiDeltaLocal   > 0.40 &&
             (rateChangeFrac > -0.10 || syncDeltaLocal > 0.10 ||
              seizureDeltaLocal > 0.10)) ||
            (seizureDeltaLocal > 0.40 &&
             (rateChangeFrac > -0.10 || syncDeltaLocal > 0.10 ||
              niiDeltaLocal   >  0.10));

        const bool overSuppressionHere =
            (suppressionEffectPct > 60.0) ||
            (perDoseState == BiologicalState::NeuralSilencing);

        const bool stabilizationSaturationHere =
            (perDoseState == BiologicalState::NetworkStabilization) &&
            (stabilizationEffectPct > 60.0);

        if (excitatoryRiskHere) excitatoryRiskDoses.push_back(static_cast<double>(dose));
        if (overSuppressionHere) overSuppressionDoses.push_back(static_cast<double>(dose));
        if (stabilizationSaturationHere) stabilizationSaturationDoses.push_back(static_cast<double>(dose));

        if (excitatoryRiskHere || overSuppressionHere) {
            if (!sawHighDoseEffect) {
                sawHighDoseEffect = true;
                report.hasToxicThreshold       = true;
                report.toxicMinDose            = dose;
                report.hasToxicThresholdExact  = true;
                report.toxicThresholdDoseEval  = static_cast<double>(dose);
                report.toxicThresholdText      = std::to_string(report.toxicThresholdDoseEval);
            }
        }
    }
    // ── end per-dose loop ────────────────────────────────────────────────────

    // ── Smoothing pass (unchanged logic) ────────────────────────────────────
    if (sorted.size() >= 3U) {
        std::vector<bool> isExcitatoryVec(sorted.size(), false);
        for (std::size_t i = 0; i < report.features.size(); ++i)
            isExcitatoryVec[i] = report.features[i].is_toxic;

        bool changed = true;
        while (changed) {
            changed = false;
            for (std::size_t i = 1; i + 1 < sorted.size(); ++i) {
                if (!isExcitatoryVec[i] &&
                    isExcitatoryVec[i-1] && isExcitatoryVec[i+1]) {
                    report.features[i].is_toxic     = true;
                    report.features[i].is_effective = false;
                    safeDoses.erase(
                        std::remove_if(safeDoses.begin(), safeDoses.end(),
                            [&](double d) { return std::fabs(d - report.features[i].dose) <= 1.0e-6; }),
                        safeDoses.end());
                    therapeuticDoses.erase(
                        std::remove_if(therapeuticDoses.begin(), therapeuticDoses.end(),
                            [&](double d) { return std::fabs(d - report.features[i].dose) <= 1.0e-6; }),
                        therapeuticDoses.end());
                    excitatoryRiskDoses.push_back(report.features[i].dose);
                    isExcitatoryVec[i] = true;
                    changed = true;
                }
            }
        }
    }

    report.maxRateChangePct = std::isfinite(maxRateChangePct) ? maxRateChangePct : 0.0;

    auto fitCurveForMode = [&](const std::string& mode) -> std::vector<double> {
        const std::vector<double>* source = &suppressionCurveEffect;
        if (mode == "NO_SIGNIFICANT_RESPONSE")  source = &noSignificantCurveEffect;
        else if (mode == "EXCITATORY_RESPONSE") source = &excitationCurveEffect;
        else if (mode == "STABILIZING_RESPONSE")source = &stabilizationCurveEffect;
        std::vector<double> curve = *source;
        for (std::size_t i = 1; i < curve.size(); ++i)
            if (curve[i] < curve[i-1]) curve[i] = curve[i-1];
        return curve;
    };

    // Sort and deduplicate dose lists.
    auto sortDedup = [](std::vector<double>& v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end(),
                    [](double a, double b) { return std::fabs(a - b) <= 1.0e-6; }),
                v.end());
    };
    sortDedup(safeDoses);
    sortDedup(therapeuticDoses);
    sortDedup(excitatoryRiskDoses);
    sortDedup(overSuppressionDoses);
    sortDedup(stabilizationSaturationDoses);

    // Gap-fill therapeutic and excitatory dose lists.
    auto gapFill = [&](std::vector<double>& v) {
        if (inferredStep <= 1.0e-6 || v.size() < 2U) return;
        std::vector<double> filled;
        filled.reserve(v.size() * 2);
        filled.push_back(v.front());
        for (std::size_t i = 1; i < v.size(); ++i) {
            const double gap = v[i] - v[i-1];
            if (gap > inferredStep + 1.0e-6 && gap <= 2.0 * inferredStep + 1.0e-6)
                filled.push_back(v[i-1] + inferredStep);
            filled.push_back(v[i]);
        }
        v = std::move(filled);
    };
    gapFill(therapeuticDoses);
    gapFill(excitatoryRiskDoses);

    const auto safeRanges              = contiguousRanges(safeDoses,                report.stepDose);
    const auto therapeuticRanges       = contiguousRanges(therapeuticDoses,          report.stepDose);
    const auto excitatoryRiskRanges    = contiguousRanges(excitatoryRiskDoses,       report.stepDose);
    const auto overSuppressionRanges   = contiguousRanges(overSuppressionDoses,      report.stepDose);
    const auto stabilizationRanges     = contiguousRanges(stabilizationSaturationDoses, report.stepDose);

    report.overSuppressionDoses         = overSuppressionDoses;
    report.excitatoryRiskDoses          = excitatoryRiskDoses;
    report.stabilizationSaturationDoses = stabilizationSaturationDoses;

    if (!safeRanges.empty()) {
        const auto& widest = *std::max_element(safeRanges.begin(), safeRanges.end(),
            [](const Range& a, const Range& b) { return (a.hi - a.lo) < (b.hi - b.lo); });
        report.hasSafeRange  = true;
        report.safeMinDose   = safeNonNegative(static_cast<float>(widest.lo));
        report.safeMaxDose   = safeNonNegative(static_cast<float>(widest.hi));
    }

    if (!therapeuticRanges.empty()) {
        const auto& widest = *std::max_element(therapeuticRanges.begin(), therapeuticRanges.end(),
            [](const Range& a, const Range& b) { return (a.hi - a.lo) < (b.hi - b.lo); });
        report.hasContinuousEffectiveWindow = true;
        report.effectiveRangeMin  = widest.lo;
        report.effectiveRangeMax  = widest.hi;
        report.windowQuality      = "Continuous";
        report.hasTherapeuticWindow = true;
        report.therapeuticWindow  = static_cast<float>(widest.hi - widest.lo);
        report.hasSuppressionThreshold  = true;
        report.suppressionThresholdDose = widest.lo;
        report.hasEffectiveDose         = true;
        report.effectiveMinDose         = static_cast<float>(widest.lo);
        onsetDose    = widest.lo;
        hasOnsetDose = true;
    } else {
        report.hasContinuousEffectiveWindow = false;
        report.windowQuality  = "Not well-defined";
        report.hasTherapeuticWindow = false;
        report.therapeuticWindow = 0.0f;
    }

    const bool therapeuticWindowExists = report.hasContinuousEffectiveWindow
                                          && !therapeuticRanges.empty();
    const bool lowStability = (report.stabilityScore == "LOW") ||
                               (report.rateVariability    >= 2.0f) ||
                               (report.toxicityVariability >= 10.0f);
    const bool fragmentedWindow = therapeuticRanges.size() > 1U;

    // ── Final observation metrics ─────────────────────────────────────────────
    const DoseObservation& finalObs = sorted.back();
    const double finalSyncReductionPct  = reductionPercent(baselineSync,    safeNonNegativeD(finalObs.synchronizationIndex));
    const double finalNiiReductionPct   = reductionPercent(baselineNii,     safeNonNegativeD(finalObs.nii));
    const double finalNiiIncreasePct    = increasePercent( baselineNii,     safeNonNegativeD(finalObs.nii));
    const double finalSeizureReductionPct = reductionPercent(baselineSeizure, safeNonNegativeD(finalObs.seizureProbabilityPct));
    const double finalBurstReductionPct = reductionPercent(baselineBurst,   safeNonNegativeD(finalObs.burstIndex));
    const double finalCalciumEffectMagnitude = std::max({finalSyncReductionPct, finalNiiReductionPct,
                                                         finalSeizureReductionPct, finalBurstReductionPct});

    report.syncReductionPct    = std::max(0.0, finalSyncReductionPct);
    report.niiReductionPct     = std::max(0.0, finalNiiReductionPct);
    report.niiIncreasePct      = std::max(0.0, finalNiiIncreasePct);
    report.seizureReductionPct = std::max(0.0, finalSeizureReductionPct);
    report.burstReductionPct   = std::max(0.0, finalBurstReductionPct);
    report.calciumEffectMagnitude = std::max(0.0, std::max(peakCalciumEffectMagnitude,
                                                            finalCalciumEffectMagnitude));

    // [V8] meaningfulCaBlock replaced: stabilizationConfirmedByMetrics
    // True when at least two instability metrics show ≥15% reduction.
    {
        int stabilMetrics = 0;
        if (finalSyncReductionPct   >= 15.0) stabilMetrics++;
        if (finalNiiReductionPct    >= 15.0) stabilMetrics++;
        if (finalSeizureReductionPct >= 15.0) stabilMetrics++;
        if (finalBurstReductionPct  >= 15.0) stabilMetrics++;
        report.meaningfulCaBlock = (stabilMetrics >= 2);  // field retained for API compatibility
    }

    // ── Global biological state ───────────────────────────────────────────────
    report.biologicalState = detectBiologicalState(
        baselineRate, baselineSync, baselineBurst,
        baselineNii, baselineSeizure, baselineIsiCv,
        finalObs, sorted,
        report.maxRateChangePct, therapeuticWindowExists,
        report.effectiveRangeMin, report.effectiveRangeMax
    );

    // ── Dominant mechanism — cumulative evidence strength ─────────────────────
    // [FIX V dominance]: A few strongly toxic doses outweigh many weak others.
    //
    // Toxicity and silencing carry a 1.5× weight penalty to ensure that even
    // a small number of high-toxicity doses can dominate a profile that is
    // otherwise mildly suppressive.  This replaces the old "most common
    // mechanism wins" approach.
    {
        const double weightedToxicity  = 1.5 * cumulativeToxicityEvidence;
        const double weightedSilencing = 1.5 * cumulativeSilencingEvidence;
        const double weightedExcit     = 1.2 * cumulativeExcitationEvidence;

        // Override global state if cumulative excitatory/toxic evidence dominates.
        if (weightedToxicity  > cumulativeSuppressionEvidence &&
            weightedToxicity  > cumulativeStabilizationEvidence &&
            weightedToxicity  > weightedExcit) {
            // Only override if global state doesn't already reflect toxicity.
            if (report.biologicalState != BiologicalState::ToxicInstability &&
                report.biologicalState != BiologicalState::NeuralSilencing) {
                report.biologicalState = BiologicalState::ToxicInstability;
            }
        } else if (weightedSilencing > cumulativeSuppressionEvidence * 1.5) {
            report.biologicalState = BiologicalState::NeuralSilencing;
        }
    }

    // ── Stabilising response detection — purely from observed metrics ─────────
    // [V7] CORRECTED: no channel check.  Stabilisation is confirmed when:
    //   (a) cumulative stabilisation evidence dominates, AND
    //   (b) no meaningful excitation evidence is present.
    const bool stabilizingResponseObserved =
        (report.biologicalState == BiologicalState::NetworkStabilization) &&
        (cumulativeStabilizationEvidence > cumulativeExcitationEvidence) &&
        (cumulativeStabilizationEvidence > cumulativeToxicityEvidence * 0.8) &&
        !sawHighDoseEffect;

    if (stabilizingResponseObserved)
        report.biologicalState = BiologicalState::NetworkStabilization;

    // ── Response mode ─────────────────────────────────────────────────────────
    if (stabilizingResponseObserved) {
        report.responseMode = "STABILIZING_RESPONSE";
    } else if (report.biologicalState == BiologicalState::Hyperexcitability ||
               cumulativeExcitationEvidence > cumulativeSuppressionEvidence * 1.2) {
        report.responseMode = "EXCITATORY_RESPONSE";
    } else if (report.biologicalState == BiologicalState::LimitedEffect &&
               !therapeuticWindowExists &&
               report.maxRateChangePct < 20.0 &&
               !report.hasToxicThreshold) {
        report.responseMode = "NO_SIGNIFICANT_RESPONSE";
    } else {
        report.responseMode = "SUPPRESSIVE_RESPONSE";
    }

    // ── Risk score second pass ────────────────────────────────────────────────
    report.peakRiskScore       = 0.0f;
    report.peakEarlyWarningIndex = 0.0f;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        const DoseObservation& obs = sorted[i];
        const float mR   = safeNonNegative(obs.meanFiringRateHz);
        const float sP   = safeNonNegative(obs.seizureProbabilityPct);
        const float suP  = safeNonNegative(obs.suppressionPct);
        const float sV   = safeNonNegative(obs.synchronizationIndex);
        const float bV   = safeNonNegative(obs.burstIndex);
        const float nV   = safeNonNegative(obs.nii);

        const float sN = clamp01(sP  / 100.0f);
        const float su = clamp01(suP / 100.0f);
        const float syncN = clamp01(sV);
        const float burstN= clamp01(bV / 0.20f);
        const float niiN  = clamp01(nV);

        const DoseRiskMetrics drm{
            (static_cast<double>(mR) - baselineRate) / std::max(1.0, baselineRate),
            static_cast<double>(sV)  - baselineSync,
            static_cast<double>(nV)  - baselineNii,
            static_cast<double>(sP)  / 100.0 - baselineSeizure / 100.0,
            reductionPercent(baselineSync,    static_cast<double>(sV)),
            reductionPercent(baselineNii,     static_cast<double>(nV)),
            reductionPercent(baselineSeizure, static_cast<double>(sP)),
            reductionPercent(baselineBurst,   static_cast<double>(bV)),
            static_cast<double>(sN),
            static_cast<double>(su),
            static_cast<double>(syncN),
            static_cast<double>(burstN),
            static_cast<double>(niiN)
        };

        const double seizureRiskPct = 100.0 *
            computeSeizureRiskForMode(report.responseMode, drm);
        const double ewi = computeEarlyWarningIndexForMode(
            report.responseMode, drm, seizureRiskPct / 100.0);
        const DrugRiskTier tier = classifyTier(sN, su, static_cast<float>(seizureRiskPct));

        report.points[i].riskScore          = static_cast<float>(seizureRiskPct);
        report.points[i].classification     = toString(tier);
        report.points[i].earlyWarningIndex  = static_cast<float>(ewi);
        report.peakRiskScore                = std::max(report.peakRiskScore,
                                                        static_cast<float>(seizureRiskPct));
        report.peakEarlyWarningIndex        = std::max(report.peakEarlyWarningIndex,
                                                        static_cast<float>(ewi));
    }

    // ── Sigmoid fit ───────────────────────────────────────────────────────────
    report.sigmoidR2 = computeBestSigmoidR2(xDose, fitCurveForMode(report.responseMode));
    if      (report.sigmoidR2 >= 0.95) report.curveType = "Sigmoidal";
    else if (report.sigmoidR2 >= 0.85) report.curveType = "Weak Sigmoidal";
    else                                report.curveType = "Non-sigmoidal";
    if (stabilizingResponseObserved)
        report.curveType = (report.sigmoidR2 >= 0.95)
                            ? "Sigmoidal Stabilization" : "Stabilizing Response";

    // ── Metric narrative ──────────────────────────────────────────────────────
    const double finalRate   = safeNonNegativeD(finalObs.meanFiringRateHz);
    const double finalRateChangePct = ((finalRate - baselineRate) / std::max(1.0, baselineRate)) * 100.0;
    const double finalSyncDelta   = safeNonNegativeD(finalObs.synchronizationIndex) - baselineSync;
    const double finalNiiDelta    = safeNonNegativeD(finalObs.nii) - baselineNii;
    const double finalSeizureDelta= safeNonNegativeD(finalObs.seizureProbabilityPct) - baselineSeizure;

    const MetricNarrative mn{
        finalRateChangePct,
        std::max(0.0, finalSyncReductionPct),
        std::max(0.0, finalNiiReductionPct),
        std::max(0.0, finalSeizureReductionPct),
        std::max(0.0, finalBurstReductionPct),
        std::max(0.0,  finalSyncDelta  * 100.0),  // increase %
        std::max(0.0, finalNiiIncreasePct),
        std::max(0.0, finalSeizureDelta)
    };

    report.biologicalStateText  = toString(report.biologicalState);
    report.primaryChangeText    = describeDominantChange(mn, report.biologicalState);

    const bool networkStabilizationObserved = stabilizingResponseObserved;
    const bool toxicityBeforeTherapy =
        report.hasToxicThreshold &&
        (!therapeuticWindowExists ||
         report.toxicThresholdDoseEval <= report.effectiveRangeMin);
    const bool toxicityAfterTherapy =
        report.hasToxicThreshold && therapeuticWindowExists &&
        report.toxicThresholdDoseEval > report.effectiveRangeMax;
    const bool stableTherapeuticWindow =
        therapeuticWindowExists && !lowStability &&
        !sawHighDoseEffect && !fragmentedWindow;

    report.safetyInterpretationText = safetyNarrativeFromMetrics(
        report.biologicalState, toxicityBeforeTherapy, toxicityAfterTherapy,
        lowStability, networkStabilizationObserved);

    report.seizureTrendText = (finalSeizureDelta > 0.0)
        ? "Seizure-risk markers increased with dose"
        : (finalSeizureDelta < 0.0)
          ? "Seizure-risk markers decreased with dose"
          : "Seizure-risk markers remained broadly stable";

    // ── Recommendation ────────────────────────────────────────────────────────
    const bool neuralSilencingDetected     = (report.biologicalState == BiologicalState::NeuralSilencing);
    const bool hyperexcitabilityDetected   = (report.biologicalState == BiologicalState::Hyperexcitability);
    const bool toxicInstabilityDetected    = (report.biologicalState == BiologicalState::ToxicInstability);
    const bool controlledSuppressionObserved = (report.biologicalState == BiologicalState::ControlledSuppression);

    if (neuralSilencingDetected || hyperexcitabilityDetected || toxicInstabilityDetected) {
        report.recommendation = "NOT RECOMMENDED";
        report.riskLevel      = "HIGH";
        report.overallTier    = DrugRiskTier::Toxic;
        report.reason = generateReasonFromMetrics(report.biologicalState,
            toxicityBeforeTherapy, toxicityAfterTherapy, lowStability,
            fragmentedWindow, therapeuticWindowExists, stabilityInput.runCount);

    } else if (toxicityBeforeTherapy) {
        report.recommendation = "NOT RECOMMENDED";
        report.riskLevel      = "HIGH";
        report.overallTier    = DrugRiskTier::Toxic;
        report.reason = "Instability markers (seizure escalation, NII rise) appear at "
                         "doses below the therapeutic firing-rate window; the dose-response "
                         "ordering is unsafe.";

    } else if (stabilizingResponseObserved) {
        if (stabilityScoreIsMediumOrHigh(report.stabilityScore)) {
            report.recommendation = "PROMISING";
            report.riskLevel      = "LOW";
            report.overallTier    = DrugRiskTier::Safe;
            report.reason = generateReasonFromMetrics(report.biologicalState,
                toxicityBeforeTherapy, toxicityAfterTherapy, lowStability,
                fragmentedWindow, therapeuticWindowExists, stabilityInput.runCount);
        } else {
            report.recommendation = "CAUTION";
            report.riskLevel      = "MODERATE";
            report.overallTier    = DrugRiskTier::ModerateRisk;
            report.reason = "Network stabilisation response observed in firing-rate and "
                             "instability metrics, but inter-run variability requires "
                             "further replication before clinical assessment.";
        }

    } else if (stableTherapeuticWindow ||
               (networkStabilizationObserved && !report.hasToxicThreshold && !lowStability)) {
        report.recommendation = "PROMISING";
        report.riskLevel      = "LOW";
        report.overallTier    = DrugRiskTier::Safe;
        report.reason = generateReasonFromMetrics(report.biologicalState,
            toxicityBeforeTherapy, toxicityAfterTherapy, lowStability,
            fragmentedWindow, therapeuticWindowExists, stabilityInput.runCount);

    } else if (therapeuticWindowExists && (toxicityAfterTherapy || report.hasToxicThreshold)) {
        report.recommendation = "CAUTION";
        report.riskLevel      = "MODERATE";
        report.overallTier    = DrugRiskTier::ModerateRisk;
        // [V13] REPLACED hardcoded channel narrative.
        report.reason = controlledSuppressionObserved
            ? "Controlled firing-rate suppression was observed, followed by "
              "over-suppression at higher doses; the therapeutic window is narrow."
            : "A therapeutic response was detected before instability markers appeared; "
              "the therapeutic window is narrow and requires careful dose management.";

    } else if (therapeuticWindowExists) {
        report.recommendation = lowStability ? "CAUTION"    : "PROMISING";
        report.riskLevel      = lowStability ? "MODERATE"   : "LOW";
        report.overallTier    = lowStability ? DrugRiskTier::ModerateRisk : DrugRiskTier::Safe;
        report.reason = lowStability
            ? "Therapeutic firing-rate response was detected but inter-run variability "
              "in instability metrics reduces dosing confidence."
            : "Therapeutic response was detected within the tested range with acceptable "
              "instability metrics and no toxicity markers.";

    } else {
        report.recommendation = "LIMITED EFFICACY";
        report.riskLevel      = "LOW";
        report.overallTier    = DrugRiskTier::Safe;
        report.reason = generateReasonFromMetrics(report.biologicalState,
            toxicityBeforeTherapy, toxicityAfterTherapy, lowStability,
            fragmentedWindow, therapeuticWindowExists, stabilityInput.runCount);
    }

    // ── Confidence ────────────────────────────────────────────────────────────
    report.confidence = computeConfidenceFromBiology(
        report.biologicalState, report.sigmoidR2,
        therapeuticWindowExists, report.hasContinuousEffectiveWindow,
        fragmentedWindow, report.rateVariability, report.toxicityVariability,
        report.syncReductionPct, report.niiReductionPct,
        report.seizureReductionPct, report.burstReductionPct,
        report.maxRateChangePct, report.peakRiskScore,
        report.peakSeizureProbabilityPct, stabilityInput.runCount);

    report.hasToxicThresholdExact = report.hasToxicThreshold;
    report.toxicThresholdText = report.hasToxicThreshold
        ? std::to_string(report.toxicThresholdDoseEval)
        : ">" + std::to_string(report.maxTestedDose);

    // ── Response strength ─────────────────────────────────────────────────────
    const double midDose = 0.5 * (report.minTestedDose + report.maxTestedDose);
    if (report.sigmoidR2 >= 0.95 && hasOnsetDose && onsetDose <= midDose)
        report.responseStrength = "Strong";
    else if (report.sigmoidR2 >= 0.85)
        report.responseStrength = "Moderate-to-Strong";
    else
        report.responseStrength = "Weak";

    return report;
}

// ─── toString helpers ────────────────────────────────────────────────────────

std::string PharmaDecisionEngine::toString(DrugRiskTier tier) {
    switch (tier) {
        case DrugRiskTier::Safe:         return "Safe";
        case DrugRiskTier::ModerateRisk: return "Moderate Risk";
        case DrugRiskTier::HighRisk:     return "High Risk";
        case DrugRiskTier::Toxic:        return "Toxic";
        default:                          return "Safe";
    }
}

std::string PharmaDecisionEngine::toString(BiologicalState state) {
    switch (state) {
        case BiologicalState::LimitedEffect:         return "Limited Effect";
        case BiologicalState::ControlledSuppression: return "Controlled Suppression";
        case BiologicalState::NeuralSilencing:       return "Neural Silencing";
        case BiologicalState::Hyperexcitability:     return "Hyperexcitability";
        case BiologicalState::NetworkStabilization:  return "Network Stabilization";
        case BiologicalState::ToxicInstability:      return "Toxic Instability";
        default:                                      return "Limited Effect";
    }
}

} // namespace spp::analyzer