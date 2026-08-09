#include "LiabilityReport.h"
#include "ReportFormatting.h"

#include "../drug/DrugModel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace spp::report {

using spp::analyzer::PharmaDecisionReport;
using spp::analyzer::AnalyzedDose;
using spp::analyzer::NetworkState;
using spp::analyzer::MechanismSignature;
using spp::analyzer::PharmaDecisionEngine;

namespace {

// Evidence quality: HIGH/MEDIUM/LOW, driven by (a) whether the mechanism
// label is a specific literature-matched signature vs. a generic Unknown/
// Mixed fallback, (b) run-to-run stability, (c) run count. This is a first-
// pass, documented-as-such heuristic (same honesty discipline as
// getStability()'s bands) -- not a calibrated statistical confidence
// interval, since this engine has no held-out validation data yet to
// calibrate one against (see PRECISION_GAP_CLOSURE_PLAN.md gap 6/7).
std::string evidenceQuality(
    MechanismSignature mechanism,
    const std::string& stabilityScore,
    int runCount,
    bool hasResponse)
{
    if (!hasResponse) return "LOW";
    const bool specificMechanism =
        mechanism != MechanismSignature::Unknown &&
        mechanism != MechanismSignature::Mixed;
    if (specificMechanism && stabilityScore == "HIGH" && runCount >= 5) return "HIGH";
    if (stabilityScore == "LOW" || runCount < 3) return "LOW";
    return "MEDIUM";
}

} // namespace

std::string buildLiabilityReportText(
    const PharmaDecisionReport& report,
    const AggregatedStats& stabilityStats,
    int runCount,
    const RuntimeInput& evalInput,
    const std::string& engineInputMode,
    std::optional<bool> usedGpu)
{
    std::ostringstream out;
    const auto aLine=[&](const std::string& label, const std::string& value){
        out << std::left << std::setw(22) << label << " : " << value << "\n";
    };
    constexpr int kDoseListPrecision = 4;
    const auto fRange=[&](double a, double b){
        return formatRuntimeNumber(a, kDoseListPrecision) + " - " + formatRuntimeNumber(b, kDoseListPrecision);
    };

    const std::string stab = getStability(stabilityStats.stdRate, stabilityStats.stdSync);
    const bool toxDet      = report.hasToxicThreshold;
    const bool noResp      = report.responseMode == "NO_SIGNIFICANT_RESPONSE";
    const bool exciMode    = (report.responseMode == "EXCITATORY_RESPONSE");
    const bool stabMode    = (report.responseMode == "STABILIZING_RESPONSE");

    // Same per-dose classification as the legacy report (reuses
    // AnalyzedDose::isEffective, the single source of truth -- see
    // AnalyzedDose.h comment).
    std::vector<double> therapeuticDoses, exciDoses, ineffDoses;
    for (const auto& dose : report.analyzedDoses) {
        switch (dose.networkState) {
            case NetworkState::SeizureActive:
            case NetworkState::SeizureRisk:
            case NetworkState::NeuralSuppression:
            case NetworkState::DepolarizationBlock:
            case NetworkState::Hyperexcitable:
                exciDoses.push_back(static_cast<double>(dose.dose));
                break;
            case NetworkState::MildInstability:
            case NetworkState::Stable:
                if (dose.isEffective) therapeuticDoses.push_back(static_cast<double>(dose.dose));
                else ineffDoses.push_back(static_cast<double>(dose.dose));
                break;
            default:
                ineffDoses.push_back(static_cast<double>(dose.dose));
                break;
        }
    }

    double maxEffect = 0.0; std::size_t peakIdx = 0;
    for (std::size_t i=0;i<report.analyzedDoses.size();++i){
        const auto& d = report.analyzedDoses[i];
        const float s = std::max({d.suppressionScore, d.excitabilityScore, d.stabilizationScore});
        if (s > static_cast<float>(maxEffect)){maxEffect=s; peakIdx=i;}
    }
    const std::string effRange = (noResp || !report.hasTherapeuticWindow)
        ? "Not observed"
        : fRange(report.effectiveRangeMin, report.effectiveRangeMax);
    const bool fragmentedWindow = report.windowQuality == "Fragmented";

    constexpr double kReceptorInertThreshold = 1.0e8;
    constexpr double kChannelInertThreshold  = 1.0e5;
    const bool hasChannelEffect =
        evalInput.config.ic50_na < kChannelInertThreshold ||
        evalInput.config.ic50_k  < kChannelInertThreshold ||
        evalInput.config.ic50_ca < kChannelInertThreshold;
    const bool hasReceptorOrGat1Effect =
        evalInput.config.ic50_ampa  < kReceptorInertThreshold ||
        evalInput.config.ic50_nmda  < kReceptorInertThreshold ||
        evalInput.config.ec50_gabaA < kReceptorInertThreshold ||
        evalInput.config.ec50_gabaB < kReceptorInertThreshold ||
        evalInput.config.ki_gat1    < kReceptorInertThreshold;
    const bool hasNeuromodEffect =
        evalInput.config.ec50_d1   < kReceptorInertThreshold ||
        evalInput.config.ec50_d2   < kReceptorInertThreshold ||
        evalInput.config.ec50_ht1a < kReceptorInertThreshold ||
        evalInput.config.ec50_ht2a < kReceptorInertThreshold ||
        // Tier 2.2: alpha-2 is a real receptor pathway now (both curves) --
        // must count toward "this run has a real network effect", same as
        // the other four neuromodulator receptors above.
        evalInput.config.presynaptic_ec50_alpha2  < kReceptorInertThreshold ||
        evalInput.config.postsynaptic_ec50_alpha2 < kReceptorInertThreshold ||
        // Tier 2.2 completion: beta and alpha-1, same reasoning.
        evalInput.config.ec50_beta   < kReceptorInertThreshold ||
        evalInput.config.ec50_alpha1 < kReceptorInertThreshold;
    const bool hasAnyReportOnlyTransporter =
        evalInput.config.ki_sert < kReceptorInertThreshold ||
        evalInput.config.ki_dat  < kReceptorInertThreshold ||
        evalInput.config.ki_net  < kReceptorInertThreshold;
    const bool noNetworkEffectConfigured =
        !hasChannelEffect && !hasReceptorOrGat1Effect && !hasNeuromodEffect && hasAnyReportOnlyTransporter;

    out << "==================================================\n";
    out << "SILICON PATIENT - LIABILITY SCREENING REPORT\n";
    out << "==================================================\n";

    // ── [Compound Identification] ──────────────────────────────────────────
    out << "[Compound Identification]\n";
    aLine("Compound",           evalInput.drug_name);
    aLine("Engine Input Mode",  engineInputMode);
    aLine("Compute Backend",    usedGpu.has_value() ? (*usedGpu ? "GPU" : "CPU") : "Unknown (not reported by this run path)");

    std::ostringstream targets;
    bool firstTarget = true;
    const auto addTarget=[&](const std::string& label, double value){
        if (!firstTarget) targets << ", ";
        targets << label << "=" << formatRuntimeNumber(value, kDoseListPrecision);
        firstTarget = false;
    };
    // BUG FIX (found on real hardware, TEA test with deliberately huge
    // 1,000,000 channel IC50s): channels have no real "unconfigured"
    // sentinel the way receptors/transporters do (those default to an inert
    // 1e9 EC50; channels always carry SOME numeric IC50 from config, even a
    // very large/weak one). Gating Na/K/Ca behind kChannelInertThreshold --
    // a threshold that means "weak enough to treat as practically inert for
    // NOT-APPLICABLE gating purposes" elsewhere in this file, not "was this
    // ever configured" -- printed "Configured Targets: None" for a drug that
    // very much had three real (if numerically weak) channel targets set.
    // Channels are printed unconditionally instead, matching the legacy
    // report's own behavior.
    addTarget("Na IC50", evalInput.config.ic50_na);
    addTarget("K IC50",  evalInput.config.ic50_k);
    addTarget("Ca IC50", evalInput.config.ic50_ca);
    if (evalInput.config.ic50_ampa  < kReceptorInertThreshold) addTarget("AMPA EC50 (Block)", evalInput.config.ic50_ampa);
    if (evalInput.config.ic50_nmda  < kReceptorInertThreshold) addTarget("NMDA EC50 (Block)", evalInput.config.ic50_nmda);
    if (evalInput.config.ec50_gabaA < kReceptorInertThreshold) addTarget("GABA-A EC50 (Potentiate)", evalInput.config.ec50_gabaA);
    if (evalInput.config.ec50_gabaB < kReceptorInertThreshold) addTarget("GABA-B EC50 (Agonist)", evalInput.config.ec50_gabaB);
    if (evalInput.config.ki_gat1 < kReceptorInertThreshold) addTarget("GAT1 Ki", evalInput.config.ki_gat1);
    if (evalInput.config.ki_sert < kReceptorInertThreshold) addTarget("SERT Ki", evalInput.config.ki_sert);
    if (evalInput.config.ki_dat  < kReceptorInertThreshold) addTarget("DAT Ki",  evalInput.config.ki_dat);
    if (evalInput.config.ki_net  < kReceptorInertThreshold) addTarget("NET Ki",  evalInput.config.ki_net);
    if (evalInput.config.ec50_d1   < kReceptorInertThreshold) addTarget("D1 EC50",     evalInput.config.ec50_d1);
    if (evalInput.config.ec50_d2   < kReceptorInertThreshold) addTarget("D2 EC50",     evalInput.config.ec50_d2);
    // Tier 2.1: D2 postsynaptic pathway -- same header-visibility fix as
    // 5-HT1A's autoreceptor addition above, applied proactively this time
    // rather than found via a first-run gap.
    if (evalInput.config.postsynaptic_ec50_d2 < kReceptorInertThreshold)
        addTarget("D2 Postsynaptic EC50", evalInput.config.postsynaptic_ec50_d2);
    if (evalInput.config.ec50_ht1a < kReceptorInertThreshold) addTarget("5-HT1A EC50", evalInput.config.ec50_ht1a);
    // Tier 2.1: presynaptic autoreceptor pathway -- was silently missing
    // from this header (found via test_5ht1a_autoreceptor.json's own first
    // run, which showed the postsynaptic EC50 but nothing about the new
    // autoreceptor fields even though they were configured and active).
    if (evalInput.config.autoreceptor_ec50_ht1a < kReceptorInertThreshold)
        addTarget("5-HT1A Autoreceptor EC50", evalInput.config.autoreceptor_ec50_ht1a);
    if (evalInput.config.ec50_ht2a < kReceptorInertThreshold) addTarget("5-HT2A EC50", evalInput.config.ec50_ht2a);
    // Tier 2.2: alpha-2, both curves -- same header-visibility fix applied
    // proactively (as D2's postsynaptic addition was), not found reactively
    // via a first run.
    if (evalInput.config.presynaptic_ec50_alpha2 < kReceptorInertThreshold)
        addTarget("Alpha-2 Presynaptic EC50", evalInput.config.presynaptic_ec50_alpha2);
    if (evalInput.config.postsynaptic_ec50_alpha2 < kReceptorInertThreshold)
        addTarget("Alpha-2 Postsynaptic EC50", evalInput.config.postsynaptic_ec50_alpha2);
    // Tier 2.2 completion: beta and alpha-1 -- same proactive header-
    // visibility fix as alpha-2's addition above.
    if (evalInput.config.ec50_beta < kReceptorInertThreshold)
        addTarget("Beta EC50", evalInput.config.ec50_beta);
    if (evalInput.config.ec50_alpha1 < kReceptorInertThreshold)
        addTarget("Alpha-1 EC50", evalInput.config.ec50_alpha1);
    aLine("Configured Targets", firstTarget ? "None (unconfigured/inert profile)" : targets.str());
    aLine("Hill Coefficient(s)", formatRuntimeNumber(evalInput.config.hill, 2));
    aLine("Dose Range Tested",  fRange(report.minTestedDose, report.maxTestedDose)
          + ", step " + formatRuntimeNumber(report.stepDose, kDoseListPrecision));
    aLine("Runs",                std::to_string(runCount));
    out << "\n--------------------------------------------------\n\n";

    // No-network-effect early exit -- same honest short-circuit as the
    // legacy report's NOT APPLICABLE case, reframed without verdict
    // language: state what was measured (nothing) and why, then stop.
    if (noNetworkEffectConfigured) {
        out << "[Mechanism Classification]\n";
        aLine("Mechanism observed", "None -- no simulated network pathway for this configuration");
        aLine("Evidence quality",   "N/A");
        out << "\n--------------------------------------------------\n\n";
        out << "[What This Does NOT Establish]\n";
        out << "- No claim of clinical safety, efficacy, or dosing guidance -- this is a\n"
               "  network-level electrophysiology simulation, not a PK/PD, toxicology, or\n"
               "  clinical study.\n";
        out << "- This configuration produces zero simulated network effect: the only\n"
               "  targets configured (transporter Ki values) feed serotonin/dopamine/\n"
               "  norepinephrine pathways that need a matching D1/D2/5-HT1A/5-HT2A/alpha-2/\n"
               "  beta/alpha-1 receptor configured alongside them to have anywhere to act --\n"
               "  none is set in this run. Any dose-response shape in a run like this would be\n"
               "  random seed noise, not a real pharmacological signal -- so the remaining\n"
               "  sections of this report format (Quantitative Findings, Benchmark Context)\n"
               "  are omitted rather than populated with noise.\n";
        out << "\n--------------------------------------------------\n\n";
        out << "[Suggested Next Step]\n";
        out << "Add a matching receptor configuration (D1/D2 for a DAT-blocking compound,\n"
               "5-HT1A/5-HT2A for a SERT-blocking compound, alpha-2/beta/alpha-1 for a\n"
               "NET-blocking compound) for the transporter pharmacology above to reach the\n"
               "simulated network, or treat this run as configuration-only documentation\n"
               "rather than a liability screen.\n";
        out << "==================================================\n";
        return out.str();
    }

    // ── [Mechanism Classification] ─────────────────────────────────────────
    out << "[Mechanism Classification]\n";
    aLine("Mechanism observed", report.mechanismText);
    const std::string evidence = evidenceQuality(report.dominantMechanism, stab, runCount, !noResp);
    aLine("Evidence quality", evidence);
    out << "\n--------------------------------------------------\n\n";

    // ── [Quantitative Findings] ────────────────────────────────────────────
    out << "[Quantitative Findings]\n";
    // Gap 1.2 audit fix (4-Aminopyridine): computeBestSigmoidR2 deliberately
    // clamps its return value to [-1.0, 1.0] (see PharmaDecisionEngine.cpp) --
    // a negative value is real, not an error/sentinel, meaning NO logistic
    // curve in the whole grid search fit this drug's dose-response better
    // than a flat mean line. Printed with no context this looks like a
    // broken value to anyone assuming R^2 lives in [0,1], so add a plain-
    // language note whenever it prints negative.
    aLine("Dose-response fit", report.curveType + ", R^2 = " + formatRuntimeNumber(report.sigmoidR2, 2)
          + (report.sigmoidR2 < 0.0 ? " (worse than a flat baseline -- no sigmoid shape fit this data)" : ""));
    if (!report.analyzedDoses.empty()) {
        // Gap 1.1 audit fix (cocaine): pair decisionMaxEffectPct with the
        // dose that actually produced it (decisionMaxEffectDoseIdx), NOT
        // this file's own peakIdx (highest composite suppression/
        // excitability/stabilization score, used below for Network state
        // context) -- the two can legitimately be different doses, and
        // pairing them incorrectly produced a false "54.2% change at dose
        // 2.7000" for cocaine when dose 2.7's own rate change was 49.0%.
        const auto& maxEffectDose = report.analyzedDoses[report.decisionMaxEffectDoseIdx];
        std::string maxEffectLine =
              formatRuntimeNumber(report.decisionMaxEffectPct, 1) + "% change at dose "
              + formatRuntimeNumber(maxEffectDose.dose, kDoseListPrecision);
        // Gap 1.2 audit fix (4-Aminopyridine): decisionMaxEffectPct is
        // clamped to 100% (see PharmaDecisionEngine.cpp's effMag clamp,
        // which exists because this same value also drives the excitatory-
        // verdict gate elsewhere, not just display) -- so a dose with a real
        // +184.1% rate change reports identically to one with +105%, losing
        // real magnitude information for strongly excitatory drugs where the
        // size of the effect is the whole point. Surface the true, unclamped
        // rateChangePct alongside it whenever the clamp is actually engaged,
        // without touching the clamped value itself (that one still needs to
        // stay clamped -- it's wired into decision logic elsewhere).
        if (std::fabs(maxEffectDose.rateChangePct) > 100.05f) {
            maxEffectLine += " (raw rate change " + formatRuntimeNumber(maxEffectDose.rateChangePct, 1)
                  + "% -- reported effect magnitude above is clamped at 100%)";
        }
        aLine("Max observed effect", maxEffectLine);
    } else {
        aLine("Max observed effect", "Not observed");
    }
    const std::string rangeLabel = exciMode ? "Excitatory range" : stabMode ? "Stabilization range" : "Effective range";
    aLine(rangeLabel, effRange + (effRange == "Not observed" ? "" :
          (std::string(" -- ") + (fragmentedWindow ? "FRAGMENTED" : "Continuous"))));
    if (fragmentedWindow && !therapeuticDoses.empty()) {
        aLine("  Note", "widest single contiguous block reported above, not the full "
              "front-to-back span of every effective dose -- see Per-Dose Network State "
              "below for doses outside this block that still showed a real effect");
    }
    out << "\n  Per-Dose Network State:\n";
    for (const auto& dose : report.analyzedDoses) {
        // Gap 1.1 audit fix (buspirone/bromocriptine): this table used to
        // print only NetworkState | MechanismSignature, so notes elsewhere
        // in this report that point here to verify a claim -- the
        // fragmented-window note ("see Per-Dose Network State below for
        // doses outside this block that still showed a real effect") and
        // the single-point-noise note above (buspirone's dose 0.0420) --
        // referenced information this table didn't actually contain. Every
        // dose printed identically regardless of whether it was classified
        // effective or not, since AnalyzedDose::isEffective (the single
        // source of truth per AnalyzedDose.h) was never surfaced here. The
        // legacy report used to expose this via a separate Dose
        // Classification Summary zone-list section, which this report
        // format intentionally doesn't have -- so it needs to be visible
        // per-dose instead.
        // Gap 1.2 audit fix (4-Aminopyridine): AnalyzedDose::isEffective is
        // ONLY ever true when networkState is Stable or MildInstability
        // (see PharmaDecisionEngine.cpp's isEffective computation) -- any
        // dose that crossed into a genuinely dangerous state (Hyperexcitable/
        // SeizureRisk/SeizureActive/DepolarizationBlock/NeuralSuppression) is
        // "Ineffective" by construction, regardless of how large its actual
        // effect was. For 4-AP this printed "Hyperexcitable | Ineffective"
        // side by side for doses 425-850 -- exactly the Severe Excitability
        // Zone doses, i.e. the MOST affected doses in the whole run, labeled
        // as if they showed no real effect. Distinguishing "no real
        // response" from "response so large it's structurally dangerous"
        // instead of collapsing both into one Ineffective label.
        const bool isDangerState =
            dose.networkState == NetworkState::Hyperexcitable ||
            dose.networkState == NetworkState::SeizureRisk ||
            dose.networkState == NetworkState::SeizureActive ||
            dose.networkState == NetworkState::DepolarizationBlock ||
            dose.networkState == NetworkState::NeuralSuppression;
        const std::string doseEffectLabel = isDangerState ? "Dangerous"
            : (dose.isEffective ? "Effective" : "Ineffective");
        out << "    Dose " << std::setw(8) << formatRuntimeNumber(dose.dose, kDoseListPrecision)
            << " : " << PharmaDecisionEngine::toString(dose.networkState)
            << " | " << PharmaDecisionEngine::toString(dose.mechanismSignature)
            << " | " << doseEffectLabel << "\n";
    }
    out << "\n";
    aLine("Inter-run variability",
          "Rate " + formatRuntimeNumber(stabilityStats.stdRate, 2)
          + ", Sync " + formatRuntimeNumber(stabilityStats.stdSync, 3)
          + ", Stability Score " + stab);
    // Gap 1.1 audit fix (diazepam_desensitization): the legacy report's
    // Phase 3b [Adaptation Profile] section (Short/Medium/Long-term firing
    // rate tiers + Tolerance Risk banding) was never ported to this report
    // format -- found via a real desensitization-config run where this
    // entire section, arguably the only informative content for a
    // single-dose desensitization test, was silently missing. Folded in
    // here as extra Quantitative Findings lines rather than as a new
    // top-level section, since the locked template doesn't define one and
    // this only applies to the subset of configs with desensitization
    // enabled. Same first/middle/last-third rate split and Tolerance Risk
    // banding logic as LegacyLiabilityReport.cpp -- see that file's comment
    // for why this is a first-pass illustrative scale, not a literature-
    // sourced threshold.
    if (evalInput.config.desensitization_enabled) {
        const double firstHz  = stabilityStats.meanFirstThirdRateHz;
        const double middleHz = stabilityStats.meanMiddleThirdRateHz;
        const double lastHz   = stabilityStats.meanLastThirdRateHz;
        const double midPct  = (firstHz > 1.0e-6) ? ((middleHz - firstHz) / firstHz * 100.0) : 0.0;
        const double lastPct = (firstHz > 1.0e-6) ? ((lastHz  - firstHz) / firstHz * 100.0) : 0.0;
        const double thirdMs = evalInput.config.sim_time / 3.0;
        aLine("Short-term rate",  formatRuntimeNumber(firstHz)  + " Hz (0-" + formatRuntimeNumber(thirdMs/1000.0) + "s)");
        aLine("Medium-term rate", formatRuntimeNumber(middleHz) + " Hz (" + formatRuntimeNumber(midPct) + "% vs short-term)");
        aLine("Long-term rate",   formatRuntimeNumber(lastHz)   + " Hz (" + formatRuntimeNumber(lastPct) + "% vs short-term)");
        const double absSwing = std::fabs(lastPct);
        const std::string toleranceRisk =
            (absSwing < 2.0) ? "LOW" : (absSwing < 5.0) ? "MODERATE" : "HIGH";
        aLine("Tolerance risk", toleranceRisk +
              (lastPct > 0.05 ? " (rate rising -- classic tolerance signature)" :
               lastPct < -0.05 ? " (rate falling -- not the expected tolerance direction)" :
               " (no measurable drift)"));
    }
    out << "\n--------------------------------------------------\n\n";

    // ── [Benchmark Context] ────────────────────────────────────────────────
    // Honest limitation, not a stub: this engine has no structured cross-
    // drug comparator dataset wired in at runtime -- each run is
    // independent and doesn't have access to other drugs' saved reports.
    // Per the locked template's own instruction ("only printed when a real
    // comparator exists; omitted rather than invented"), this section states
    // that limitation explicitly instead of fabricating a comparison.
    out << "[Benchmark Context]\n";
    aLine("Comparator compound(s)", "Not available -- no structured cross-drug comparator "
          "dataset is wired into this engine build");
    aLine("Relative position", "Manual comparison against other reports in this project's "
          "validation set required (see PRECISION_GAP_CLOSURE_PLAN.md)");
    out << "\n--------------------------------------------------\n\n";

    // ── [What This Does NOT Establish] ─────────────────────────────────────
    out << "[What This Does NOT Establish]\n";
    out << "- No claim of clinical safety, efficacy, or dosing guidance -- this is a\n"
           "  network-level electrophysiology simulation, not a PK/PD, toxicology, or\n"
           "  clinical study.\n";
    out << "- No pharmacokinetics modeled -- dose is treated as instantly and constantly\n"
           "  present, not absorbed/cleared over time (PRECISION_GAP_CLOSURE_PLAN.md gap 2).\n";
    out << "- No real experimental or clinical outcome data has been used to validate this\n"
           "  engine's predictions -- confidence language above reflects internal run-to-run\n"
           "  consistency only, not agreement with ground truth (PRECISION_GAP_CLOSURE_PLAN.md\n"
           "  gaps 6-7).\n";
    out << "- Burst-rate-based classification is structurally inert for this network's\n"
           "  firing regime (peak rates observed here fall far short of the burst-detector's\n"
           "  ~150+ Hz threshold) -- danger states are still caught by sync/irregularity/rate\n"
           "  branches, but any burst-specific signal cannot register\n"
           "  (PRECISION_GAP_CLOSURE_PLAN.md 1.3).\n";
    // Tier 2.1 update, second correction: this message went through two
    // wrong states before this one -- first a blanket "no split at all"
    // claim (wrong once the autoreceptor pathway was added), then a
    // "dose-only, not time-dependent" claim (wrong again once the
    // closed-form time-dependent desensitization was added, and found
    // stale by rereading THIS SAME report during that mechanism's own
    // early/late validation run). Now keyed off whether the tau fields are
    // actually configured (finite), not just whether the autoreceptor
    // EC50 is set, so an old dose-only config (if one still exists) gets
    // the accurate middle message instead of silently claiming time-
    // dependence it doesn't have.
    constexpr double kTauInertThreshold = 1.0e10;
    const bool ht1aAutoreceptorConfigured =
        evalInput.config.autoreceptor_ec50_ht1a < kReceptorInertThreshold;
    const bool ht1aTimeDependent =
        ht1aAutoreceptorConfigured &&
        evalInput.config.autoreceptor_tau_desense_ms_ht1a < kTauInertThreshold;
    if (ht1aTimeDependent) {
        out << "- 5-HT1A has a time-dependent presynaptic autoreceptor pathway configured\n"
               "  alongside its postsynaptic effect -- the autoreceptor desensitizes over\n"
               "  continued exposure at this fixed dose (same qualitative mechanism as real\n"
               "  SSRIs' delayed clinical onset), using a compressed/illustrative timescale\n"
               "  unless the config's tau values were sourced from real literature. Still a\n"
               "  first-pass model: no real experimental timecourse has been used to validate\n"
               "  the desensitization rate itself (PRECISION_GAP_CLOSURE_PLAN.md 2.1).\n";
    } else if (ht1aAutoreceptorConfigured) {
        out << "- 5-HT1A has a second (presynaptic autoreceptor) pathway configured\n"
               "  alongside its postsynaptic effect -- but this config leaves the\n"
               "  autoreceptor's desensitization tau at its inert default, so the split is\n"
               "  DOSE-dependent only (evaluated at a single instant), not the TIME-dependent\n"
               "  desensitization that produces real SSRIs' delayed clinical onset. Do not\n"
               "  read this run as reproducing that phenomenon (PRECISION_GAP_CLOSURE_PLAN.md\n"
               "  2.1).\n";
    }
    // Tier 2.1 D2 postsynaptic split: independent of the 5-HT1A branches
    // above (a config could have either, both, or neither configured), so
    // this is a separate check rather than another else-if link in that
    // chain -- otherwise a drug with D2's split but not 5-HT1A's would
    // silently fall through to the generic "no split" message below, wrong
    // in exactly the way the 5-HT1A disclaimer was wrong twice already.
    const bool d2PostsynapticConfigured =
        evalInput.config.postsynaptic_ec50_d2 < kReceptorInertThreshold;
    if (d2PostsynapticConfigured) {
        out << "- D2 has a second (postsynaptic) pathway configured alongside its\n"
               "  presynaptic release-probability effect -- these are two independent Hill\n"
               "  curves on the same dose (own EC50/Hill each), not a time-dependent\n"
               "  desensitization like 5-HT1A's autoreceptor -- both pathways respond\n"
               "  instantly to dose, with no chronic-exposure/onset-delay behavior modeled\n"
               "  for D2 (PRECISION_GAP_CLOSURE_PLAN.md 2.1).\n";
    }
    // Tier 2.2: alpha-2 now exists (both presynaptic autoreceptor and
    // postsynaptic PFC pathways) -- computed here (before the generic
    // fallback below) so both that check and the NET-specific message
    // further down can use it. Tier 2.2 completion added beta/alpha-1 --
    // computed alongside it for the same reason (this generic fallback
    // needs to exclude all three adrenergic receptors, not just alpha-2).
    const bool alpha2Configured =
        evalInput.config.presynaptic_ec50_alpha2  < kReceptorInertThreshold ||
        evalInput.config.postsynaptic_ec50_alpha2 < kReceptorInertThreshold;
    const bool betaConfigured   = evalInput.config.ec50_beta   < kReceptorInertThreshold;
    const bool alpha1Configured = evalInput.config.ec50_alpha1 < kReceptorInertThreshold;
    // alpha2/beta/alpha1Configured each get their own accurate message
    // below -- excluded here so a run configuring only one of them doesn't
    // ALSO get this "no split" claim, which is specific to D1/D2/5-HT1A/
    // 5-HT2A and wrong for all three adrenergic additions (alpha-2 always
    // ships both curves together with no split ambiguity; beta/alpha-1 are
    // single-pathway and were never split candidates in the first place).
    if (!ht1aTimeDependent && !ht1aAutoreceptorConfigured && !d2PostsynapticConfigured
        && !alpha2Configured && !betaConfigured && !alpha1Configured && hasNeuromodEffect) {
        out << "- D1/D2/5-HT1A/5-HT2A pathways configured in this run are single monotonic\n"
               "  curves with no presynaptic-autoreceptor-vs-postsynaptic-receptor split --\n"
               "  genuinely biphasic dopaminergic/serotonergic response (real for some\n"
               "  compounds at this mechanism) cannot be reproduced regardless of tuning\n"
               "  (PRECISION_GAP_CLOSURE_PLAN.md 2.1).\n";
    }
    // This branch used to unconditionally claim "no adrenergic gain system
    // exists yet", which is now WRONG whenever any of alpha-2/beta/alpha-1
    // is configured alongside NET. The message is split: NET-without-any-
    // adrenergic-receptor still gets the old "no receptor pathway reached"
    // note (still true for that case), each configured receptor gets its
    // own accurate description instead.
    if (evalInput.config.ki_net < kReceptorInertThreshold
        && !alpha2Configured && !betaConfigured && !alpha1Configured) {
        out << "- NET (norepinephrine transporter) is configured but no alpha-2/beta/alpha-1\n"
               "  receptor pathway is set alongside it, so the amplified norepinephrine dose\n"
               "  has no receptor to act on and produces no simulated network effect\n"
               "  (PRECISION_GAP_CLOSURE_PLAN.md 2.2).\n";
    }
    if (alpha2Configured) {
        out << "- Alpha-2 (norepinephrine) has two independent Hill curves configured on the\n"
               "  same dose -- a presynaptic Gi-autoreceptor pathway (release-probability\n"
               "  reduction, same lever as D2's presynaptic pathway) and a postsynaptic PFC\n"
               "  cAMP-HCN pathway (adaptation reduction, same lever as D1's). Both respond\n"
               "  instantly to dose, with no chronic-exposure/onset-delay behavior modeled\n"
               "  (unlike 5-HT1A's autoreceptor) (PRECISION_GAP_CLOSURE_PLAN.md 2.2).\n";
    }
    // Tier 2.2 completion: beta and alpha-1 both INCREASE adaptationScale
    // (more braking) -- the opposite direction from D1/alpha-2-postsynaptic/
    // 5-HT2A's decrease, a literature-grounded finding (not a naive same-
    // G-protein-family assumption) worth surfacing explicitly here so this
    // direction isn't mistaken for a bug when compared against those.
    if (betaConfigured) {
        out << "- Beta (norepinephrine) is configured -- single pathway, INCREASES\n"
               "  adaptationScale (more braking / suppressed persistent firing) rather than\n"
               "  decreasing it, despite sharing D1's Gs coupling -- literature-grounded\n"
               "  direction (beta1-AR opens the same HCN channels alpha-2 closes; high NE/\n"
               "  cAMP-PKA suppresses PFC persistent firing), not the naive same-family\n"
               "  assumption. No presynaptic/postsynaptic split modeled -- no comparably\n"
               "  strong dual-role literature found for beta this session\n"
               "  (PRECISION_GAP_CLOSURE_PLAN.md 2.2).\n";
    }
    if (alpha1Configured) {
        out << "- Alpha-1 (norepinephrine) is configured -- single pathway, INCREASES\n"
               "  adaptationScale (more braking / suppressed dlPFC firing) rather than\n"
               "  decreasing it, despite sharing 5-HT2A's Gq coupling -- literature-grounded\n"
               "  direction (Arnsten lab: PKC-mediated dlPFC firing suppression), not the\n"
               "  naive same-family assumption. No gKEff lever modeled (unlike 5-HT2A) --\n"
               "  no K+-conductance-specific citation found for alpha-1 this session\n"
               "  (PRECISION_GAP_CLOSURE_PLAN.md 2.2).\n";
    }
    // Tier 2.4 (2026-08-08): permanent, automatic warning -- NOT just a
    // plan-doc note -- because this report's own "Excitatory"/"Effective"
    // classification above is easy to misread as "this drug excites
    // neurons via a classical excitatory pathway", which is NOT what beta/
    // alpha-1 do. Real-hardware investigation (PRECISION_GAP_CLOSURE_PLAN.md
    // 2.4) found this network's population firing rate has a NON-MONOTONIC
    // ("dip-shaped") relationship to per-neuron spike-frequency adaptation
    // strength: rate is highest at zero adaptation, falls to a minimum
    // close to this network's own baseline adaptation level, then rises
    // again in EITHER direction away from that point. Beta/alpha-1 operate
    // on the rising-again side (increasing adaptation from baseline), which
    // is why the population-level classification reads "Excitatory" even
    // though the per-neuron mechanism is doing exactly what it's built to
    // do (increasing adaptation/braking, confirmed directly from this run's
    // own drug parameters, not inferred from the population reading below).
    // This is a confirmed, general property of this network's dynamics
    // (verified independent of any drug code via a direct adaptation-
    // increment sweep at dose=0), not a bug in beta/alpha-1's
    // implementation and not something tuning beta/alpha-1's parameters
    // can fix. D1/5-HT2A/alpha-2-postsynaptic, which also use the
    // adaptationScale lever, were separately checked and confirmed NOT
    // exposed to this same issue -- their reported directions are reliable.
    if (betaConfigured || alpha1Configured) {
        out << "- IMPORTANT: the \"Excitatory\"/\"Effective\" classification above for\n"
               "  Beta/Alpha-1 reflects a documented NON-MONOTONIC network response, not a\n"
               "  classical excitatory mechanism -- this network's population firing rate\n"
               "  dips to a minimum near baseline adaptation strength and rises again as\n"
               "  adaptation is pushed higher OR lower from that point. Beta/Alpha-1 are\n"
               "  correctly increasing per-neuron adaptation (braking) exactly as designed;\n"
               "  the population-level rate increase is an emergent network consequence,\n"
               "  not evidence the drug acts on an excitatory pathway. Confirmed on real\n"
               "  hardware, independent of any drug-specific code (PRECISION_GAP_CLOSURE_\n"
               "  PLAN.md Tier 2.4, 2026-08-08 dip characterization).\n";
    }
    // Tier 2.4 part 2: ketamine's activity-dependent NMDA trapping --
    // report-honesty note, same reasoning as the beta/alpha-1 block above
    // (the caveat needs to travel with the report output itself, not just
    // live in the plan doc). Only the *shape* of this mechanism (activity-
    // dependent trapping vs. a flat dose-only block) is literature-
    // grounded (Glasgow et al., J Neurosci 2017); the tauTrapMs/
    // tauUntrapMs rate constants are starting estimates pending real
    // domain-expert (electrophysiology) review.
    if (evalInput.config.ic50_nmda < kReceptorInertThreshold && evalInput.config.nmda_activity_dependent) {
        out << "- IMPORTANT: NMDA block in this run uses the activity-dependent trapping\n"
               "  model (ketamine's interneuron-selectivity mechanism), not a flat dose-only\n"
               "  block -- each neuron's own block strength depends on how often its NMDA\n"
               "  channels have been open recently, so higher-firing neurons end up more\n"
               "  blocked purely from their own activity, with no cell-type flag anywhere.\n"
               "  Only the SHAPE of this mechanism is literature-grounded (Glasgow et al.,\n"
               "  J Neurosci 2017); the trap/untrap time-constant VALUES are starting\n"
               "  estimates, not validated rate constants -- treat any liability read\n"
               "  driven primarily by this lever as provisional pending real\n"
               "  electrophysiology review (PRECISION_GAP_CLOSURE_PLAN.md Tier 2.4 part 2).\n";
    }
    if (fragmentedWindow) {
        out << "- The effective/excitatory window fragmented at one or more tested doses --\n"
               "  see Per-Dose Network State above for the raw per-dose classification behind\n"
               "  that call rather than treating the reported range as a smooth window.\n";
    }
    out << "\n--------------------------------------------------\n\n";

    // ── [Suggested Next Step] ──────────────────────────────────────────────
    out << "[Suggested Next Step]\n";
    if (evidence == "HIGH") {
        out << "Compare this dose-response shape against published electrophysiology for\n"
               "this compound's mechanism before treating any dose boundary here as meaningful.\n";
    } else if (evidence == "LOW") {
        out << "Evidence quality is LOW -- consider re-running with more repeats and/or a\n"
               "dose range better centered on the expected potency before drawing conclusions\n"
               "from this data.\n";
    } else {
        out << "Held-out validation against real patch-clamp or in vivo electrophysiology data\n"
               "is recommended before using this report beyond internal screening.\n";
    }
    out << "==================================================\n";

    return out.str();
}

void writeLiabilityReport(
    const std::string& path, const PharmaDecisionReport& report,
    const AggregatedStats& stats, int runs,
    const RuntimeInput& input, const std::string& mode,
    std::optional<bool> usedGpu)
{
    std::ofstream out(path, std::ios::out|std::ios::trunc);
    if (!out.is_open()) throw std::runtime_error("Cannot open: " + path);
    out << buildLiabilityReportText(report, stats, runs, input, mode, usedGpu);
}

} // namespace spp::report