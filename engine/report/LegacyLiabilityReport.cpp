#include "LegacyLiabilityReport.h"
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

std::string buildDrugEvaluationReportText(
    const PharmaDecisionReport& report,
    const AggregatedStats& stabilityStats,
    int runCount,
    const RuntimeInput& evalInput,
    const std::string& engineInputMode,
    std::optional<bool> usedGpu)
{
    std::ostringstream out;
    const auto aLine=[&](const std::string& label, const std::string& value){
        out << std::left << std::setw(20) << label << " : " << value << "\n";
    };
    // Display-precision fix: default kRuntimeOutputPrecision=2 silently
    // rounds small dose/EC50 values (anything below 0.005) down to "0.00",
    // and can also make two genuinely different doses on a fine step (e.g.
    // 0.048 and 0.054 both print "0.05") look identical. Moved above fRange
    // (was originally declared further down, just for the zone-list
    // formatter) so every dose/potency-scale number in this report -- Tested
    // Range, Step Size, EC50 echoes, per-dose table, onset/peak dose, zone
    // lists -- uses the same fixed 4-decimal precision instead of drifting
    // per call site. See individual BUG FIX comments below for the specific
    // drugs (fluoxetine, DOI) that surfaced each case.
    constexpr int kDoseListPrecision = 4;
    const auto fRange=[&](double a, double b){
        return formatRuntimeNumber(a, kDoseListPrecision) + " - " + formatRuntimeNumber(b, kDoseListPrecision);
    };

    const std::string stab = getStability(stabilityStats.stdRate, stabilityStats.stdSync);
    const bool toxDet      = report.hasToxicThreshold;
    const bool noResp      = report.responseMode == "NO_SIGNIFICANT_RESPONSE";

    // Determine zone labels from response mode
    const bool exciMode  = (report.responseMode == "EXCITATORY_RESPONSE");
    const bool stabMode  = (report.responseMode == "STABILIZING_RESPONSE");
    const std::string winTitle  = exciMode ? "Excitatory Response Range"
                                : stabMode ? "Stabilization Response Range"
                                           : "Therapeutic Window";
    const std::string effLabel  = exciMode ? "Excitatory Range"
                                : stabMode ? "Stabilization Range"
                                           : "Effective Range";
    const std::string zoneLabel = exciMode ? "Excitatory Zone"
                                : stabMode ? "Stabilization Zone"
                                           : "Therapeutic Zone";
    const std::string optZone   = exciMode
        ? "Network excitability exceeded safe stability limits"
        : stabMode
          ? "Reductions in synchronisation and burst activity with preserved firing"
          : "Moderate controlled suppression (20-60%)";

    // Per-dose classification summary from AnalyzedDose
    std::vector<double> therapeuticDoses, exciDoses, overSuppDoses, ineffDoses;
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
                // BUG FIX (diazepam validation, Phase 2 receptor drugs; then
                // AGAIN for cocaine/D1, Phase 3c): this used to re-derive its
                // own local copy of "is this dose effective" (first
                // suppressionScore/stabilizationScore thresholds that drifted
                // from PharmaDecisionEngine.cpp's real ones, then even after
                // matching those, still missing the rateChangePct>5.0f
                // excitatory branch -- so cocaine's real +49% rate-increase
                // MildInstability doses silently fell into "Ineffective Zone"
                // instead of "Excitatory Zone" even though FINAL DECISION
                // correctly flagged them NOT RECOMMENDED/HIGH RISK). Fixed
                // for good by reading AnalyzedDose::isEffective, the single
                // flag PharmaDecisionEngine::evaluate() already computes per
                // dose -- no local threshold copy left here to drift.
                if (dose.isEffective)
                    therapeuticDoses.push_back(static_cast<double>(dose.dose));
                else
                    ineffDoses.push_back(static_cast<double>(dose.dose));
                break;
            default:
                ineffDoses.push_back(static_cast<double>(dose.dose));
                break;
        }
    }

    // Zone-list formatter: reuses kDoseListPrecision declared above (near
    // fRange) -- a fine dose_range step (e.g. step=0.006) can put two
    // adjacent, genuinely different doses on the same 2-decimal rounding
    // boundary (0.048 and 0.054 both print "0.05" under
    // kRuntimeOutputPrecision=2), so one dose correctly classified
    // Ineffective and a DIFFERENT dose correctly classified Therapeutic
    // could print the identical string in both zone lists, looking like a
    // contradiction when it's a display collision only (found validating
    // fluoxetine).
    auto formatDoses=[&](const std::vector<double>& doses) -> std::string {
        if (doses.empty()) return "Not observed";
        std::ostringstream t;
        for (std::size_t i=0;i<doses.size();++i){
            if (i) t << ", ";
            t << formatRuntimeNumber(doses[i], kDoseListPrecision);
        }
        return t.str();
    };

    // Peak effect
    double maxEffect = 0.0; std::size_t peakIdx = 0;
    for (std::size_t i=0;i<report.analyzedDoses.size();++i){
        const auto& d = report.analyzedDoses[i];
        const float s = std::max({d.suppressionScore, d.excitabilityScore, d.stabilizationScore});
        if (s > static_cast<float>(maxEffect)){maxEffect=s; peakIdx=i;}
    }
    // BUG FIX (Phase 3 10-drug validation, DOI): Onset/Peak/Saturation dose
    // strings used default 2-decimal precision too -- DOI's real onset dose
    // (0.001, the first tested nonzero dose) printed as "~0.00", reading as
    // "onset at essentially zero dose" rather than the actual smallest
    // tested step. Same fix as everywhere else above -- kDoseListPrecision.
    const std::string peakDoseStr = report.analyzedDoses.empty()
        ? "Not observed"
        : "~" + formatRuntimeNumber(report.analyzedDoses[peakIdx].dose, kDoseListPrecision);
    const std::string onsetDoseStr = report.hasEffectiveDose
        ? "~" + formatRuntimeNumber(report.effectiveMinDose, kDoseListPrecision) : "Not observed";
    const std::string saturationStr = (peakIdx+1 >= report.analyzedDoses.size())
        ? "Not observed within tested range"
        : "Observed beyond " + formatRuntimeNumber(report.analyzedDoses[peakIdx].dose, kDoseListPrecision);
    // BUG FIX (Phase 3 10-drug held-out validation, pramipexole): this used
    // to independently recompute the range as therapeuticDoses.front()/
    // .back() -- the full span of EVERY therapeutic dose, ignoring gaps --
    // instead of using report.effectiveRangeMin/effectiveRangeMax, which
    // PharmaDecisionEngine.cpp already computes correctly as the WIDEST
    // SINGLE CONTIGUOUS sub-range (the same value windowQuality's Continuous/
    // Fragmented label and the Reason text are actually based on). For a
    // Continuous window these coincide (only one contiguous block exists),
    // which is why this was invisible until a Fragmented case with a big
    // enough gap came along: pramipexole's therapeutic doses were {0.0062,
    // 0.0078, 0.0140} -- the real widest contiguous range is {0.0062,
    // 0.0078} (width 0.0016), but front()/back() printed "0.0062-0.0140",
    // spanning straight across the 4-step gap to an isolated point that
    // isn't part of any contiguous window at all. (Bromocriptine hit this
    // too, but its buggy span happened to round to the same 2-decimal
    // string as the correct answer, so it went unnoticed.)
    const std::string effRange = (noResp || !report.hasTherapeuticWindow)
        ? "Not observed"
        : fRange(report.effectiveRangeMin, report.effectiveRangeMax);

    out << "==================================================\n";
    out << "SILICON PATIENT - DRUG EVALUATION REPORT\n";
    out << "==================================================\n\n";

    out << "[Drug Input]\n";
    aLine("Drug Name",         evalInput.drug_name);
    aLine("Engine Input Mode", engineInputMode);
    // Added after a user asked "did this actually run on GPU or CPU?" and
    // there was no honest way to answer from the report alone -- see
    // BatchedSimulationEngine::lastRunUsedGpu(). std::nullopt for any
    // caller that doesn't thread this through (yet -- currently just
    // --dose-eval's batched path).
    aLine("Compute Backend", usedGpu.has_value() ? (*usedGpu ? "GPU" : "CPU") : "Unknown (not reported by this run path)");
    aLine("Na IC50",           formatRuntimeNumber(evalInput.config.ic50_na));
    aLine("K IC50",            formatRuntimeNumber(evalInput.config.ic50_k));
    aLine("Ca IC50",           formatRuntimeNumber(evalInput.config.ic50_ca));
    aLine("Hill",              formatRuntimeNumber(evalInput.config.hill));
    // PHASE2_PLAN.md step 4: only print a receptor's line when it's actually
    // configured (ec50 well below the inert 1e9 default) -- most drugs in
    // this engine are still channel-only (Phase 1 style), so printing four
    // "1.00e+09" no-op lines on every report would just be noise.
    constexpr double kReceptorInertThreshold = 1.0e8;
    // BUG FIX (Phase 3 10-drug held-out validation, alprazolam): same
    // 2-decimal rounding-to-"0.00" issue as the D1/D2/5-HT1A/5-HT2A EC50 and
    // transporter Ki fixes above, just never applied to AMPA/NMDA/GABA_A/
    // GABA_B -- alprazolam's real GABA-A EC50 (0.0046) printed as "0.00",
    // looking like no potentiation was configured at all. kDoseListPrecision.
    if (evalInput.config.ic50_ampa < kReceptorInertThreshold) {
        aLine("AMPA EC50 (Block)",  formatRuntimeNumber(evalInput.config.ic50_ampa, kDoseListPrecision));
        aLine("AMPA Hill",          formatRuntimeNumber(evalInput.config.hill_ampa));
    }
    if (evalInput.config.ic50_nmda < kReceptorInertThreshold) {
        aLine("NMDA EC50 (Block)",  formatRuntimeNumber(evalInput.config.ic50_nmda, kDoseListPrecision));
        aLine("NMDA Hill",          formatRuntimeNumber(evalInput.config.hill_nmda));
    }
    if (evalInput.config.ec50_gabaA < kReceptorInertThreshold) {
        aLine("GABA-A EC50 (Potentiate)", formatRuntimeNumber(evalInput.config.ec50_gabaA, kDoseListPrecision));
        aLine("GABA-A Hill",              formatRuntimeNumber(evalInput.config.hill_gabaA));
        aLine("GABA-A Max Potentiation",  formatRuntimeNumber(evalInput.config.max_potentiation_gabaA));
    }
    if (evalInput.config.ec50_gabaB < kReceptorInertThreshold) {
        aLine("GABA-B EC50 (Agonist)", formatRuntimeNumber(evalInput.config.ec50_gabaB, kDoseListPrecision));
        aLine("GABA-B Hill",           formatRuntimeNumber(evalInput.config.hill_gabaB));
    }
    // Phase 3a: GAT1 reuptake block -- extends GABA-A/GABA-B decay tau
    // instead of touching their conductance/occupancy, so it's reported
    // separately from the receptor lines above rather than folded in.
    // BUG FIX (Phase 3 10-drug held-out validation, atomoxetine): these Ki
    // echo lines used default 2-decimal precision too -- atomoxetine's real
    // NET Ki (0.005) rounded to "0.01", making it look like a 2x weaker
    // (less potent) transporter block than what was actually configured.
    // Same fix as the EC50/Step Size/per-dose-table fixes above --
    // kDoseListPrecision.
    if (evalInput.config.ki_gat1 < kReceptorInertThreshold) {
        aLine("GAT1 Ki (Reuptake Block)", formatRuntimeNumber(evalInput.config.ki_gat1, kDoseListPrecision));
        aLine("GAT1 Hill",                formatRuntimeNumber(evalInput.config.hill_gat1));
        aLine("GAT1 Max Extension",       formatRuntimeNumber(evalInput.config.max_extension_gat1) + "x");
    }
    // Phase 3a / Phase 3c retrofit: SERT/DAT now have a real network effect
    // (dose-amplification into 5-HT1A/5-HT2A or D1/D2, see
    // ReuptakeTransporter.h's amplifiedDoseUm) WHEN the drug also configures
    // the matching receptor -- so the "[report-only]" tag is now
    // conditional, not automatic, for these two. NET is unconditionally
    // report-only (no adrenergic receptor system exists in this engine).
    const bool sertHasReceptor =
        evalInput.config.ec50_ht1a < kReceptorInertThreshold ||
        evalInput.config.ec50_ht2a < kReceptorInertThreshold;
    const bool datHasReceptor =
        evalInput.config.ec50_d1 < kReceptorInertThreshold ||
        evalInput.config.ec50_d2 < kReceptorInertThreshold;
    if (evalInput.config.ki_sert < kReceptorInertThreshold) {
        aLine("SERT Ki (Reuptake Block)", formatRuntimeNumber(evalInput.config.ki_sert, kDoseListPrecision)
              + (sertHasReceptor ? "" : " [report-only]"));
        aLine("SERT Hill",                formatRuntimeNumber(evalInput.config.hill_sert));
        aLine("SERT Max Extension",       formatRuntimeNumber(evalInput.config.max_extension_sert) + "x");
    }
    if (evalInput.config.ki_dat < kReceptorInertThreshold) {
        aLine("DAT Ki (Reuptake Block)",  formatRuntimeNumber(evalInput.config.ki_dat, kDoseListPrecision)
              + (datHasReceptor ? "" : " [report-only]"));
        aLine("DAT Hill",                 formatRuntimeNumber(evalInput.config.hill_dat));
        aLine("DAT Max Extension",        formatRuntimeNumber(evalInput.config.max_extension_dat) + "x");
    }
    if (evalInput.config.ki_net < kReceptorInertThreshold) {
        aLine("NET Ki (Reuptake Block)",  formatRuntimeNumber(evalInput.config.ki_net, kDoseListPrecision) + " [report-only]");
        aLine("NET Hill",                 formatRuntimeNumber(evalInput.config.hill_net));
        aLine("NET Max Extension",        formatRuntimeNumber(evalInput.config.max_extension_net) + "x");
    }
    // Phase 3b: GABA-A desensitization config, only printed when configured.
    if (evalInput.config.desensitization_enabled) {
        aLine("GABA-A Desensitization", "ENABLED");
        aLine("  Desensitize Tau",      formatRuntimeNumber(evalInput.config.desensitization_tau_desense_ms) + " ms");
        aLine("  Recovery Tau",         formatRuntimeNumber(evalInput.config.desensitization_tau_recovery_ms) + " ms");
        aLine("  Max Attenuation",      formatRuntimeNumber(evalInput.config.desensitization_max_attenuation * 100.0) + "%");
        aLine("  Run Duration",         formatRuntimeNumber(evalInput.config.sim_time) + " ms");
    }
    // Phase 3c: vesicle pool config, only printed when configured. Honest
    // scoping note printed alongside it every time (not just in comments) --
    // see NeurotransmitterPool.h for why depletion is observable at default
    // sim_time but recovery is not.
    if (evalInput.config.vesicle_pool_enabled) {
        // BUG FIX: this used to hardcode "(CPU only)" -- true when this
        // section was written (no GPU kernel existed yet), stale the moment
        // the GPU port landed. Reads the same usedGpu value "Compute
        // Backend" above already reports, so this line can never disagree
        // with what actually ran -- exactly the "displayed vs. decision-
        // driving number must be the same value" discipline applied
        // earlier this session to the Max Effect field.
        aLine("Vesicle Pool Dynamics", std::string("ENABLED (") +
              (usedGpu.has_value() ? (*usedGpu ? "GPU" : "CPU") : "Unknown") + ")");
        aLine("  RRP Size",            formatRuntimeNumber(evalInput.config.vesicle_pool_rrp_size) + " vesicles");
        aLine("  Reserve Size",        formatRuntimeNumber(evalInput.config.vesicle_pool_reserve_size) + " vesicles");
        aLine("  RRP Refill Tau",      formatRuntimeNumber(evalInput.config.vesicle_pool_rrp_refill_tau_ms) + " ms");
        aLine("  Reserve Refill Tau",  formatRuntimeNumber(evalInput.config.vesicle_pool_reserve_refill_tau_ms) + " ms");
        aLine("  Calcium Factor",      formatRuntimeNumber(evalInput.config.vesicle_pool_calcium_factor));
        aLine("  Run Duration",        formatRuntimeNumber(evalInput.config.sim_time) + " ms");
        const bool longEnoughForRefill =
            evalInput.config.sim_time > 3.0 * evalInput.config.vesicle_pool_rrp_refill_tau_ms;
        aLine("  Recovery Observable", longEnoughForRefill
            ? "Plausible (run duration exceeds ~3x RRP refill tau)"
            : "No (run duration is short relative to refill tau -- depletion "
              "may show, recovery will not; see NeurotransmitterPool.h)");
    }
    // Phase 3c: neuromodulator gain config, only printed per-receptor when
    // that receptor is actually configured (kReceptorInertThreshold already
    // declared above in this function).
    // BUG FIX (Phase 3 10-drug validation, DOI): EC50 (Gain) values were
    // printed with the default kRuntimeOutputPrecision=2, which silently
    // rounds any EC50 below 0.005 down to the string "0.00" -- looks like
    // "no threshold configured" when it's actually a real, tiny, correctly-
    // wired potency constant (DOI's 5-HT2A EC50 is 0.0026, driving a real
    // 75% occupancy at peak dose). Same rounding-collision class of bug as
    // the dose-zone list fix above; reusing kDoseListPrecision here too.
    if (evalInput.config.ec50_d1 < kReceptorInertThreshold) {
        aLine("D1 EC50 (Gain)",        formatRuntimeNumber(evalInput.config.ec50_d1, kDoseListPrecision));
        aLine("D1 Hill",               formatRuntimeNumber(evalInput.config.hill_d1));
        aLine("D1 Max Adapt Reduction", formatRuntimeNumber(evalInput.config.max_adaptation_reduction_d1 * 100.0) + "%");
        aLine("D1 Max NMDA Gain",      formatRuntimeNumber(evalInput.config.max_nmda_gain_d1) + "x");
    }
    if (evalInput.config.ec50_d2 < kReceptorInertThreshold) {
        aLine("D2 EC50 (Gain)",        formatRuntimeNumber(evalInput.config.ec50_d2, kDoseListPrecision));
        aLine("D2 Hill",               formatRuntimeNumber(evalInput.config.hill_d2));
        aLine("D2 Max Release Reduction", formatRuntimeNumber(evalInput.config.max_release_reduction_d2 * 100.0) + "%");
    }
    if (evalInput.config.ec50_ht1a < kReceptorInertThreshold) {
        aLine("5-HT1A EC50 (Gain)",    formatRuntimeNumber(evalInput.config.ec50_ht1a, kDoseListPrecision));
        aLine("5-HT1A Hill",           formatRuntimeNumber(evalInput.config.hill_ht1a));
        aLine("5-HT1A Max K+ Gain",    formatRuntimeNumber(evalInput.config.max_k_gain_ht1a) + "x");
    }
    if (evalInput.config.ec50_ht2a < kReceptorInertThreshold) {
        aLine("5-HT2A EC50 (Gain)",    formatRuntimeNumber(evalInput.config.ec50_ht2a, kDoseListPrecision));
        aLine("5-HT2A Hill",           formatRuntimeNumber(evalInput.config.hill_ht2a));
        aLine("5-HT2A Max K+ Reduction", formatRuntimeNumber(evalInput.config.max_k_reduction_ht2a * 100.0) + "%");
        aLine("5-HT2A Max Adapt Reduction", formatRuntimeNumber(evalInput.config.max_adaptation_reduction_ht2a * 100.0) + "%");
    }
    // Early/Late-half firing rate split -- printed UNCONDITIONALLY (not
    // gated on desensitization_enabled) so an ON run and an OFF run (or any
    // long-duration run at all) can be directly compared using the exact
    // same lines. The [Response Characteristics]/[Network Impact] sections
    // elsewhere in this report compare ACROSS doses (dose[0] is always
    // treated as baseline) -- meaningless for a single-dose test (dose[0]
    // IS the tested dose, so "change vs baseline" trivially reads 0%).
    // This is what actually shows a within-run effect: the run's own
    // firing rate in the first half of sim_time vs the second half. Only
    // worth reading for long runs (500ms default splits into two 250ms
    // halves, far too short for any real biological process to show up);
    // works for any drug, not just desensitization tests.
    {
        const double earlyHz = stabilityStats.meanEarlyWindowRateHz;
        const double lateHz  = stabilityStats.meanLateWindowRateHz;
        const double fadePct = (earlyHz > 1.0e-6) ? ((lateHz - earlyHz) / earlyHz * 100.0) : 0.0;
        aLine("Early-Half Rate",   formatRuntimeNumber(earlyHz) + " Hz (first " + formatRuntimeNumber(evalInput.config.sim_time/2000.0) + "s)");
        aLine("Late-Half Rate",    formatRuntimeNumber(lateHz)  + " Hz (last "  + formatRuntimeNumber(evalInput.config.sim_time/2000.0) + "s)");
        aLine("Early/Late Change", formatRuntimeNumber(fadePct) + "% (rising = suppression easing over the run)");
    }
    aLine("Runs",              std::to_string(runCount));
    out << "\n--------------------------------------------------\n\n";

    out << "[Dose Range]\n";
    aLine("Tested Range", fRange(report.minTestedDose, report.maxTestedDose));
    // BUG FIX (Phase 3 10-drug validation, DOI): same 2-decimal rounding-to-
    // "0.00" issue as the EC50 fix above -- a fine dose_range step (DOI:
    // 0.001) printed as "0.00", looking like a single-point sweep with no
    // step at all even though 10 distinct doses were actually tested.
    aLine("Step Size",    formatRuntimeNumber(report.stepDose, kDoseListPrecision));
    out << "\n--------------------------------------------------\n\n";

    // Phase 3a / Phase 3c retrofit: SERT and DAT now DO have a real receptor
    // pathway -- they amplify the effective dose seen by 5-HT1A/5-HT2A
    // (SERT) or D1/D2 (DAT), see ReuptakeTransporter.h's amplifiedDoseUm and
    // DrugModel::amplifiedDoseForDopamine/amplifiedDoseForSerotonin. But
    // that amplification is a no-op if the drug doesn't ALSO configure the
    // corresponding neuromodulator receptor (amplifying a dose that only
    // feeds an inert, huge-EC50 occupancy calculation still yields ~0
    // occupancy). NET still has no receptor pathway at all -- no adrenergic
    // gain system exists in this engine. So the "every dose in this run is
    // mechanistically identical to baseline" case still exists, just
    // narrower than before: SERT/DAT with no matching neuromod receptor, or
    // NET at all, with nothing else configured. Original bug this guard
    // fixes (Phase 3a validation): reboxetine (zero simulated mechanism)
    // randomly showed "NOT RECOMMENDED, HIGH RISK" from nothing but
    // run-to-run seed variance -- letting that noise flow into the normal
    // Response Characteristics/FINAL DECISION path produces a misleading
    // verdict. Fix: detect this case and print an honest, explicit notice
    // instead.
    constexpr double kChannelInertThreshold = 1.0e5;
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
    // Phase 3c: a neuromod receptor being configured is what makes SERT/DAT
    // amplification actually DO something -- see comment above.
    const bool hasNeuromodEffect =
            evalInput.config.ec50_d1   < kReceptorInertThreshold ||
            evalInput.config.ec50_d2   < kReceptorInertThreshold ||
            evalInput.config.ec50_ht1a < kReceptorInertThreshold ||
            evalInput.config.ec50_ht2a < kReceptorInertThreshold;
    const bool hasAnyReportOnlyTransporter =
            evalInput.config.ki_sert < kReceptorInertThreshold ||
            evalInput.config.ki_dat  < kReceptorInertThreshold ||
            evalInput.config.ki_net  < kReceptorInertThreshold;

    if (!hasChannelEffect && !hasReceptorOrGat1Effect && !hasNeuromodEffect && hasAnyReportOnlyTransporter) {
        out << "[Neurotransmitter Profile]\n";
        const float peakDoseF = static_cast<float>(report.maxTestedDose);
        const auto printReportOnlyTransporterEarly = [&](const char* label, double kiUm, double hill, double maxExtension) {
            spp::synapse::TransporterDrugEffect effect;
            effect.mechanism = spp::synapse::TransporterBlockType::Competitive;
            effect.kiUm = static_cast<float>(kiUm);
            effect.hill = static_cast<float>(hill);
            effect.maxExtensionFold = static_cast<float>(maxExtension);
            const float occupancy = spp::synapse::transporterOccupancy(peakDoseF, effect);
            const float foldChange = spp::synapse::effectiveTauDecayMs(1.0f, peakDoseF, effect);
            aLine(std::string(label) + " Reuptake Block",
                  formatRuntimeNumber(occupancy * 100.0, 0) + " % (at max tested dose)");
            aLine(std::string(label) + " Amplification Fold",
                  formatRuntimeNumber(foldChange, 2) + "x baseline");
        };
        const bool sertConfigured = evalInput.config.ki_sert < kReceptorInertThreshold;
        const bool datConfigured  = evalInput.config.ki_dat  < kReceptorInertThreshold;
        const bool netConfigured  = evalInput.config.ki_net  < kReceptorInertThreshold;
        if (sertConfigured) {
            printReportOnlyTransporterEarly("SERT", evalInput.config.ki_sert, evalInput.config.hill_sert, evalInput.config.max_extension_sert);
        }
        if (datConfigured) {
            printReportOnlyTransporterEarly("DAT", evalInput.config.ki_dat, evalInput.config.hill_dat, evalInput.config.max_extension_dat);
        }
        if (netConfigured) {
            printReportOnlyTransporterEarly("NET", evalInput.config.ki_net, evalInput.config.hill_net, evalInput.config.max_extension_net);
        }
        out << "\n--------------------------------------------------\n\n";

        out << "[FINAL DECISION]\n";
        aLine("Recommendation", "NOT APPLICABLE");
        aLine("Risk Level",     "N/A");
        aLine("Mechanism",      "No effect on simulated network for this config");
        if (netConfigured && !sertConfigured && !datConfigured) {
            out << "Reason               : NET (norepinephrine transporter) has no receptor\n"
                << "                       pathway modeled in this engine -- no adrenergic\n"
                << "                       (alpha/beta) gain system exists, unlike D1/D2/\n"
                << "                       5-HT1A/5-HT2A. The transporter pharmacology above\n"
                << "                       (occupancy, clearance fold) is real and literature-\n"
                << "                       sourced. Every other section of this report is\n"
                << "                       SUPPRESSED because this config has zero effect on\n"
                << "                       the simulated network -- any apparent dose-response\n"
                << "                       would be random seed noise, not a real\n"
                << "                       pharmacological signal.\n";
        } else {
            out << "Reason               : SERT/DAT reuptake block amplifies the dose the\n"
                << "                       corresponding neuromodulator receptor (5-HT1A/\n"
                << "                       5-HT2A for SERT, D1/D2 for DAT) sees -- but this\n"
                << "                       config doesn't ALSO set up that receptor, so the\n"
                << "                       amplification has nothing to act on (amplifying a\n"
                << "                       dose that only feeds an unconfigured, inert\n"
                << "                       occupancy calculation still yields ~0 effect). Add a\n"
                << "                       matching D1/D2/5-HT1A/5-HT2A block to this drug's\n"
                << "                       config for the transporter effect to reach the\n"
                << "                       network. The transporter pharmacology above\n"
                << "                       (occupancy, amplification fold) is real and\n"
                << "                       literature-sourced regardless. Every other section\n"
                << "                       of this report is SUPPRESSED here because this\n"
                << "                       specific config has zero effect on the simulated\n"
                << "                       network -- any apparent dose-response would be\n"
                << "                       random seed noise, not a real pharmacological\n"
                << "                       signal.\n";
        }
        aLine("Confidence", "N/A");
        out << "==================================================\n";
        return out.str();
    }

    out << "[Response Characteristics]\n";
    aLine("Curve Type",       report.curveType);
    aLine("Response Mode",    report.responseMode);
    aLine("Mechanism",        report.mechanismText);
    aLine("Model Fit (R^2)", formatRuntimeNumber(report.sigmoidR2, 2));
    // BUG FIX: this used to print `maxEffect*100.0` -- a locally-computed
    // weighted composite (rateDrive/burstDrive/irregDrive) that is a
    // DIFFERENT number from the one PharmaDecisionEngine actually gates the
    // NOT RECOMMENDED/excitatory verdict against. Found via a DOI/5-HT2A
    // test that printed "Max Effect: 8%, Weak" directly above "NOT
    // RECOMMENDED / HIGH RISK" -- the real decision-driving value was
    // 11.3% (raw rate change), which crossed the 10% threshold the
    // displayed 8% made it look like the drug was safely under. Now prints
    // the same value the decision was actually made from. See
    // PharmaDecisionEngine.h's decisionMaxEffectPct comment.
    aLine("Max Effect",       formatRuntimeNumber(report.decisionMaxEffectPct, 0) + " %");
    aLine("Response Strength",report.responseStrength);
    out << "\n--------------------------------------------------\n\n";

    out << "[Network Impact at Peak Dose]\n";
    if (!report.analyzedDoses.empty()) {
        const auto& pk = report.analyzedDoses[peakIdx];
        // Ground-truth absolute rate, unconditional (not gated behind the
        // desensitization block) -- "Rate Change" below is only meaningful
        // when dose[0] in the sweep is a true no-drug baseline; for a
        // single-dose test (dose[0] IS the tested dose) it always reads 0%
        // by construction, so this line is what actually tells you whether
        // the network is firing at all.
        aLine("Absolute Rate",     formatRuntimeNumber(stabilityStats.meanRate, 2) + " Hz");
        aLine("Rate Change",       formatRuntimeNumber(pk.rateChangePct, 1) + " %");
        // Gap 1.3 (PRECISION_GAP_CLOSURE_PLAN.md 1.3): "Burst Rate Delta" removed.
        // This network's burst detector requires ~150+ Hz local instantaneous
        // firing (see Metrics.cpp kBurstWindowMsDefault); this network's peak
        // mean rate under even a real convulsant (4-AP) is ~49 Hz, nowhere near
        // burst-level clustering, so the field printed 0.00 Hz in every report
        // ever generated -- not a real zero-effect measurement. Retired rather
        // than relabeled: no evidence yet that genuine spike-clustering exists
        // in this network's dynamics at any tested drive level (see
        // NetworkAnalyzer.cpp's classifyState comment for the same finding).
        aLine("Sync Delta",        formatRuntimeNumber(pk.syncDelta, 3));
        aLine("Irregularity Delta",formatRuntimeNumber(pk.irregularityDelta, 3));
        aLine("Silent Neuron Δ",   formatRuntimeNumber(pk.silentNeuronDelta, 1) + " %");
    }
    out << "\n--------------------------------------------------\n\n";

    // Phase 3a: neurotransmitter profile -- only printed when a
    // transporter-blocking mechanism is actually configured (same
    // "don't print no-op lines" discipline as the receptor lines in
    // [Drug Input]). GAT1 (tiagabine) has a real network pathway (GABA-A/
    // GABA-B), so it gets clearance-in-milliseconds lines. SERT/DAT/NET
    // (SSRI/cocaine/reboxetine) are report-only -- serotonin/dopamine/
    // norepinephrine have no receptor current in this engine yet (Phase
    // 3c), so there is no baseline tau to report a "clearance in ms"
    // against; only occupancy and the resulting clearance FOLD-CHANGE are
    // shown, each explicitly labeled [report-only, no network effect
    // modeled] so it can never be mistaken for an observed drug effect.
    // Values computed at the same peak dose used by [Network Impact at
    // Peak Dose] above.
    // Phase 3c retrofit: SERT/DAT only contribute a line to THIS section
    // when they DON'T have a matching receptor (see printReportOnlyTransporter
    // call sites below) -- when they do, they're reported in
    // [Neuromodulator Gain Profile] instead, so they shouldn't gate this
    // section open on their own in that case.
    const bool anyTransporterConfigured =
        evalInput.config.ki_gat1 < kReceptorInertThreshold ||
        (evalInput.config.ki_sert < kReceptorInertThreshold && !sertHasReceptor) ||
        (evalInput.config.ki_dat  < kReceptorInertThreshold && !datHasReceptor) ||
        evalInput.config.ki_net  < kReceptorInertThreshold;
    if (anyTransporterConfigured) {
        out << "[Neurotransmitter Profile]\n";
        const double peakDoseForNt = report.analyzedDoses.empty() ? 0.0 : report.analyzedDoses[peakIdx].dose;
        const float peakDoseF = static_cast<float>(peakDoseForNt);

        if (evalInput.config.ki_gat1 < kReceptorInertThreshold) {
            spp::synapse::TransporterDrugEffect gat1Effect;
            gat1Effect.mechanism = spp::synapse::TransporterBlockType::Competitive;
            gat1Effect.kiUm = static_cast<float>(evalInput.config.ki_gat1);
            gat1Effect.hill = static_cast<float>(evalInput.config.hill_gat1);
            gat1Effect.maxExtensionFold = static_cast<float>(evalInput.config.max_extension_gat1);

            const float gat1Occupancy = spp::synapse::transporterOccupancy(peakDoseF, gat1Effect);
            const float gabaAClearanceMs = spp::synapse::effectiveTauDecayMs(
                spp::neuron::ReceptorKinetics::kGabaATauDecayMs, peakDoseF, gat1Effect);
            const float gabaBClearanceMs = spp::synapse::effectiveTauDecayMs(
                spp::neuron::ReceptorKinetics::kGabaBTauDecayMs, peakDoseF, gat1Effect);

            aLine("GAT1 Reuptake Block", formatRuntimeNumber(gat1Occupancy * 100.0, 0) + " % (at peak dose)");
            aLine("GABA-A Clearance",    formatRuntimeNumber(gabaAClearanceMs, 1) + "ms (vs "
                  + formatRuntimeNumber(spp::neuron::ReceptorKinetics::kGabaATauDecayMs, 1) + "ms baseline)");
            aLine("GABA-B Clearance",    formatRuntimeNumber(gabaBClearanceMs, 1) + "ms (vs "
                  + formatRuntimeNumber(spp::neuron::ReceptorKinetics::kGabaBTauDecayMs, 1) + "ms baseline)");
        }

        const auto printReportOnlyTransporter = [&](const char* label, double kiUm, double hill, double maxExtension) {
            spp::synapse::TransporterDrugEffect effect;
            effect.mechanism = spp::synapse::TransporterBlockType::Competitive;
            effect.kiUm = static_cast<float>(kiUm);
            effect.hill = static_cast<float>(hill);
            effect.maxExtensionFold = static_cast<float>(maxExtension);

            const float occupancy = spp::synapse::transporterOccupancy(peakDoseF, effect);
            const float foldChange = spp::synapse::effectiveTauDecayMs(1.0f, peakDoseF, effect);

            aLine(std::string(label) + " Reuptake Block",
                  formatRuntimeNumber(occupancy * 100.0, 0) + " % [report-only, no network effect modeled]");
            aLine(std::string(label) + " Clearance Fold",
                  formatRuntimeNumber(foldChange, 2) + "x baseline [report-only]");
        };
        // Phase 3c retrofit: SERT/DAT are skipped HERE (not printed as
        // "report-only, no network effect") when they're paired with a
        // matching receptor (5-HT1A/5-HT2A for SERT, D1/D2 for DAT) --
        // that combination has a real effect now (dose amplification), and
        // is already reported accurately in [Neuromodulator Gain Profile]'s
        // "SERT/DAT Reuptake Block ... (amplifies ... receptor dose Xx)"
        // lines. Printing both here AND there, with contradictory labels,
        // would be exactly the kind of "report disagrees with itself" bug
        // fixed earlier this session for Max Effect. sertHasReceptor/
        // datHasReceptor computed in [Drug Input] above.
        if (evalInput.config.ki_sert < kReceptorInertThreshold && !sertHasReceptor) {
            printReportOnlyTransporter("SERT", evalInput.config.ki_sert, evalInput.config.hill_sert, evalInput.config.max_extension_sert);
        }
        if (evalInput.config.ki_dat < kReceptorInertThreshold && !datHasReceptor) {
            printReportOnlyTransporter("DAT", evalInput.config.ki_dat, evalInput.config.hill_dat, evalInput.config.max_extension_dat);
        }
        if (evalInput.config.ki_net < kReceptorInertThreshold) {
            printReportOnlyTransporter("NET", evalInput.config.ki_net, evalInput.config.hill_net, evalInput.config.max_extension_net);
        }
        out << "\n--------------------------------------------------\n\n";
    }

    // Phase 3b: [Adaptation Profile], per PHASE3_PLAN.md §7's sample report
    // (Short/Medium/Long-term tiers + a Tolerance Risk rating). Only
    // printed when desensitization is actually configured -- meaningless
    // otherwise (same reasoning as the Neurotransmitter Profile gate
    // above). Uses the first/middle/last-third firing rate split
    // (Metrics.cpp, added specifically for this section) rather than the
    // coarser two-way early/late split shown unconditionally elsewhere in
    // this report -- three tiers map directly onto the plan's Short/
    // Medium/Long-term language instead of forcing a two-bucket read into
    // a three-label report shape.
    if (evalInput.config.desensitization_enabled) {
        out << "[Adaptation Profile]\n";
        const double firstHz  = stabilityStats.meanFirstThirdRateHz;
        const double middleHz = stabilityStats.meanMiddleThirdRateHz;
        const double lastHz   = stabilityStats.meanLastThirdRateHz;
        const double midPct  = (firstHz > 1.0e-6) ? ((middleHz - firstHz) / firstHz * 100.0) : 0.0;
        const double lastPct = (firstHz > 1.0e-6) ? ((lastHz  - firstHz) / firstHz * 100.0) : 0.0;
        const double thirdMs = evalInput.config.sim_time / 3.0;
        aLine("Short Term",  formatRuntimeNumber(firstHz)  + " Hz (0-" + formatRuntimeNumber(thirdMs/1000.0) + "s)");
        aLine("Medium Term", formatRuntimeNumber(middleHz) + " Hz (" + formatRuntimeNumber(midPct) + "% vs short-term)");
        aLine("Long Term",   formatRuntimeNumber(lastHz)   + " Hz (" + formatRuntimeNumber(lastPct) + "% vs short-term)");
        // Tolerance-risk banding is a first-pass illustrative scale (not a
        // literature-sourced threshold, same honest caveat as
        // getStability()'s bands elsewhere in this file) -- based on the
        // magnitude of the short-to-long-term rate swing. A RISING rate is
        // the expected tolerance signature (GABA-A losing potency ->
        // less suppression -> more firing); the magnitude bands just say
        // how much of that signature showed up in this particular run.
        const double absSwing = std::fabs(lastPct);
        const std::string toleranceRisk =
            (absSwing < 2.0) ? "LOW" : (absSwing < 5.0) ? "MODERATE" : "HIGH";
        aLine("Tolerance Risk", toleranceRisk +
              (lastPct > 0.05 ? " (rate rising -- classic tolerance signature)" :
               lastPct < -0.05 ? " (rate falling -- not the expected tolerance direction)" :
               " (no measurable drift)"));
        out << "\n--------------------------------------------------\n\n";
    }

    // Phase 3c: [Neuromodulator Gain Profile], per PHASE3_PLAN.md §4's
    // report fields (Network gain score, Signal-to-noise ratio, Arousal
    // level prediction, Neuromodulator balance). Only printed when at
    // least one of D1/D2/5-HT1A/5-HT2A is actually configured -- same
    // reasoning as the Neurotransmitter Profile / Adaptation Profile gates
    // above. Network Gain Score and the Arousal/SNR readings are OUR
    // derived composite metrics (not literature-measured clinical
    // quantities -- there's no real "network gain score" you can look up),
    // clearly flagged as such, same honest-about-thresholds discipline as
    // Tolerance Risk in the Adaptation Profile section.
    constexpr double kNeuromodInertThreshold = 1.0e8;
    const bool anyNeuromodConfigured =
        evalInput.config.ec50_d1   < kNeuromodInertThreshold ||
        evalInput.config.ec50_d2   < kNeuromodInertThreshold ||
        evalInput.config.ec50_ht1a < kNeuromodInertThreshold ||
        evalInput.config.ec50_ht2a < kNeuromodInertThreshold;
    if (anyNeuromodConfigured) {
        out << "[Neuromodulator Gain Profile]\n";
        const double peakDoseForNm = report.analyzedDoses.empty() ? 0.0 : report.analyzedDoses[peakIdx].dose;
        const float peakDoseF = static_cast<float>(peakDoseForNm);

        spp::drug::ReceptorDrugProfile nmProfileForReport;
        nmProfileForReport.neuromod.d1.ec50 = static_cast<float>(evalInput.config.ec50_d1);
        nmProfileForReport.neuromod.d1.hill = static_cast<float>(evalInput.config.hill_d1);
        nmProfileForReport.neuromod.d1.maxAdaptationReductionFrac = static_cast<float>(evalInput.config.max_adaptation_reduction_d1);
        nmProfileForReport.neuromod.d1.maxNmdaGainFold = static_cast<float>(evalInput.config.max_nmda_gain_d1);
        nmProfileForReport.neuromod.d2.ec50 = static_cast<float>(evalInput.config.ec50_d2);
        nmProfileForReport.neuromod.d2.hill = static_cast<float>(evalInput.config.hill_d2);
        nmProfileForReport.neuromod.d2.maxReleaseReductionFrac = static_cast<float>(evalInput.config.max_release_reduction_d2);
        nmProfileForReport.neuromod.ht1a.ec50 = static_cast<float>(evalInput.config.ec50_ht1a);
        nmProfileForReport.neuromod.ht1a.hill = static_cast<float>(evalInput.config.hill_ht1a);
        nmProfileForReport.neuromod.ht1a.maxKGainFold = static_cast<float>(evalInput.config.max_k_gain_ht1a);
        nmProfileForReport.neuromod.ht2a.ec50 = static_cast<float>(evalInput.config.ec50_ht2a);
        nmProfileForReport.neuromod.ht2a.hill = static_cast<float>(evalInput.config.hill_ht2a);
        nmProfileForReport.neuromod.ht2a.maxKReductionFrac = static_cast<float>(evalInput.config.max_k_reduction_ht2a);
        nmProfileForReport.neuromod.ht2a.maxAdaptationReductionFrac = static_cast<float>(evalInput.config.max_adaptation_reduction_ht2a);
        // Phase 3c retrofit: SERT/DAT reuptake block must be on this SAME
        // profile object so the occupancy/gain numbers below reflect the
        // real amplified dose the simulation actually used -- without this,
        // these fields would silently disagree with the simulated network
        // the same way the report's old "Max Effect" field once did (bug
        // fixed earlier this session).
        if (evalInput.config.ki_sert < kReceptorInertThreshold) {
            nmProfileForReport.sert.mechanism        = spp::synapse::TransporterBlockType::Competitive;
            nmProfileForReport.sert.kiUm             = static_cast<float>(evalInput.config.ki_sert);
            nmProfileForReport.sert.hill             = static_cast<float>(evalInput.config.hill_sert);
            nmProfileForReport.sert.maxExtensionFold = static_cast<float>(evalInput.config.max_extension_sert);
        }
        if (evalInput.config.ki_dat < kReceptorInertThreshold) {
            nmProfileForReport.dat.mechanism        = spp::synapse::TransporterBlockType::Competitive;
            nmProfileForReport.dat.kiUm             = static_cast<float>(evalInput.config.ki_dat);
            nmProfileForReport.dat.hill             = static_cast<float>(evalInput.config.hill_dat);
            nmProfileForReport.dat.maxExtensionFold = static_cast<float>(evalInput.config.max_extension_dat);
        }

        const float doseForDopamine  = spp::drug::DrugModel::amplifiedDoseForDopamine(nmProfileForReport, peakDoseF);
        const float doseForSerotonin = spp::drug::DrugModel::amplifiedDoseForSerotonin(nmProfileForReport, peakDoseF);

        const float d1Occ   = spp::synapse::neuromodulatorOccupancy(doseForDopamine,  static_cast<float>(evalInput.config.ec50_d1),   static_cast<float>(evalInput.config.hill_d1));
        const float d2Occ   = spp::synapse::neuromodulatorOccupancy(doseForDopamine,  static_cast<float>(evalInput.config.ec50_d2),   static_cast<float>(evalInput.config.hill_d2));
        const float ht1aOcc = spp::synapse::neuromodulatorOccupancy(doseForSerotonin, static_cast<float>(evalInput.config.ec50_ht1a), static_cast<float>(evalInput.config.hill_ht1a));
        const float ht2aOcc = spp::synapse::neuromodulatorOccupancy(doseForSerotonin, static_cast<float>(evalInput.config.ec50_ht2a), static_cast<float>(evalInput.config.hill_ht2a));

        const spp::synapse::NeuromodulatorGainModifiers gainMods =
            spp::drug::DrugModel::computeNeuromodulatorGainModifiers(nmProfileForReport, peakDoseF);

        aLine("D1 Occupancy",    formatRuntimeNumber(d1Occ   * 100.0, 0) + " % (at peak dose)");
        aLine("D2 Occupancy",    formatRuntimeNumber(d2Occ   * 100.0, 0) + " % (at peak dose)");
        aLine("5-HT1A Occupancy", formatRuntimeNumber(ht1aOcc * 100.0, 0) + " % (at peak dose)");
        aLine("5-HT2A Occupancy", formatRuntimeNumber(ht2aOcc * 100.0, 0) + " % (at peak dose)");
        // Phase 3c retrofit: surface SERT/DAT's contribution here too, since
        // they now genuinely amplify the numbers directly above -- not just
        // reported for their own sake the way the [Neurotransmitter Profile]
        // early-exit block shows them for a fully-inert config.
        if (evalInput.config.ki_sert < kReceptorInertThreshold) {
            aLine("SERT Reuptake Block", formatRuntimeNumber(
                spp::synapse::transporterOccupancy(peakDoseF, spp::synapse::TransporterDrugEffect{
                    spp::synapse::TransporterBlockType::Competitive,
                    static_cast<float>(evalInput.config.ki_sert),
                    static_cast<float>(evalInput.config.hill_sert),
                    static_cast<float>(evalInput.config.max_extension_sert)}) * 100.0, 0)
                + " % (amplifies serotonin receptor dose " + formatRuntimeNumber(doseForSerotonin / std::max(peakDoseF, 1.0e-9f), 2) + "x)");
        }
        if (evalInput.config.ki_dat < kReceptorInertThreshold) {
            aLine("DAT Reuptake Block", formatRuntimeNumber(
                spp::synapse::transporterOccupancy(peakDoseF, spp::synapse::TransporterDrugEffect{
                    spp::synapse::TransporterBlockType::Competitive,
                    static_cast<float>(evalInput.config.ki_dat),
                    static_cast<float>(evalInput.config.hill_dat),
                    static_cast<float>(evalInput.config.max_extension_dat)}) * 100.0, 0)
                + " % (amplifies dopamine receptor dose " + formatRuntimeNumber(doseForDopamine / std::max(peakDoseF, 1.0e-9f), 2) + "x)");
        }

        // Network Gain Score: >1 = net excitatory/disinhibited (NMDA gain
        // and/or reduced adaptation outweighing any K+/release reduction),
        // <1 = net suppressive. Derived, not a measured quantity.
        const double gainScore =
            (gainMods.gKEffScale > 1.0e-6f && gainMods.adaptationScale > 1.0e-6f)
                ? static_cast<double>(gainMods.gMaxNmdaScale * gainMods.excitatoryWeightScale)
                    / static_cast<double>(gainMods.gKEffScale * gainMods.adaptationScale)
                : 0.0;
        aLine("Network Gain Score", formatRuntimeNumber(gainScore, 2) +
              " (>1 = net excitatory/disinhibited, <1 = net suppressive) [derived]");
        aLine("Signal-to-Noise Shift", formatRuntimeNumber((gainMods.gMaxNmdaScale - 1.0) * 100.0, 1) +
              "% (NMDA-gain proxy) [derived]");
        const std::string arousal =
            (gainScore > 1.05) ? "INCREASED (more alert/excitable)" :
            (gainScore < 0.95) ? "DECREASED (more sedated/suppressed)" : "UNCHANGED";
        aLine("Arousal Prediction", arousal + " [derived]");
        out << "\n--------------------------------------------------\n\n";
    }

    out << "[Safety Analysis]\n";
    // Same dose-scale precision fix as everywhere above.
    aLine("Toxic Threshold",
          toxDet ? formatRuntimeNumber(report.toxicMinDose, kDoseListPrecision)
                 : (">" + formatRuntimeNumber(report.maxTestedDose, kDoseListPrecision) + " (Not observed)"));
    aLine("Safety Observation",
          toxDet ? "Toxicity observed within tested range"
                 : "No toxicity within tested range");
    if (report.hasSafetyMarginRatio) {
        aLine("Safety Margin", formatRuntimeNumber(report.safetyMarginRatio, 1) + "x"
              + (report.narrowSafetyMargin ? " (narrow therapeutic index)" : " (wide margin)"));
    }
    out << "\n--------------------------------------------------\n\n";

    out << "[" << winTitle << "]\n";
    aLine(effLabel,       effRange);
    aLine("Window Quality",report.windowQuality);
    aLine("Optimal Zone",  optZone);
    if (!report.toleratedNoiseDoses.empty()) {
        std::ostringstream noteT;
        for (std::size_t i=0;i<report.toleratedNoiseDoses.size();++i){
            if (i) noteT << ", ";
            noteT << formatRuntimeNumber(report.toleratedNoiseDoses[i], kDoseListPrecision);
        }
        aLine("Note", noteT.str() + " read below threshold but sit inside this "
              "window -- treated as single-point noise, not a break in the "
              "response (see Dose Classification Summary below)");
    }
    out << "\n--------------------------------------------------\n\n";

    out << "[Dose Classification Summary]\n";
    aLine("Ineffective Zone", formatDoses(ineffDoses));
    aLine(zoneLabel,          formatDoses(therapeuticDoses));
    aLine(exciMode ? "Severe Excitability Zone"
                   : stabMode ? "Stabilization Saturation Zone"
                              : "Over-Suppression Zone",
          formatDoses(exciDoses));
    // Clarifying note (Phase 3 10-drug validation, DOI): "Excitatory Zone"
    // and "Severe Excitability Zone" measure two different things and can
    // legitimately disagree with Per Dose Network State's Stable/
    // MildInstability labels below -- "Excitatory Zone" only requires
    // rateChangePct>5% while the network stayed Stable/MildInstability (see
    // AnalyzedDose::isEffective); it is NOT a danger signal. The actual
    // danger bucket is "Severe Excitability Zone" (Hyperexcitable/
    // SeizureRisk/etc.). Label text itself is left unchanged --
    // report_parser.py matches on it exactly (see backend/app/report_parser.py).
    if (exciMode && !therapeuticDoses.empty() && exciDoses.empty()) {
        aLine("Note", "\"Excitatory Zone\" doses rose >5% above baseline but the "
              "network stayed Stable/MildInstability at every one of them (see "
              "Per Dose Network State below) -- \"Severe Excitability Zone\" "
              "being empty confirms none became structurally dangerous. See "
              "FINAL DECISION for how the aggregate magnitude still drives the "
              "safety verdict.");
    }
    out << "\n--------------------------------------------------\n\n";

    if (stabMode) {
        out << "[Network Stabilization Metrics]\n";
        aLine("Sync Reduction",    formatRuntimeNumber(report.syncReductionPct, 1) + " %");
        // Gap 1.3: "Burst Reduction" removed -- same dead-metric finding as
        // "Burst Rate Delta" above, this field is derived from the same
        // permanently-~0 burstRateHz.
        aLine("Effect Magnitude",  formatRuntimeNumber(report.calciumEffectMagnitude, 1) + " %");
        out << "\n--------------------------------------------------\n\n";
    }

    out << "[Per Dose Network State]\n";
    // BUG FIX (Phase 3 10-drug validation, DOI): this table used the default
    // 2-decimal precision, so a fine dose_range step (DOI: 0.001, 10 doses
    // from 0.000-0.010) collapsed into just two visually-identical printed
    // values ("0.00" x6, "0.01" x5) -- the table looked like it only had two
    // distinct dose rows when it actually had ten. Same fix as the dose-zone
    // lists and Step Size above -- kDoseListPrecision, not the setw(6) width
    // (bumped alongside it so 4-decimal values still line up in columns).
    for (const auto& dose : report.analyzedDoses) {
        out << "  Dose " << std::setw(8) << formatRuntimeNumber(dose.dose, kDoseListPrecision)
            << " : " << PharmaDecisionEngine::toString(dose.networkState)
            << " | " << PharmaDecisionEngine::toString(dose.mechanismSignature) << "\n";
    }
    out << "\n--------------------------------------------------\n\n";

    out << "[Pharmacodynamic Interpretation]\n";
    aLine("Onset Dose",      onsetDoseStr);
    aLine("Peak Efficiency", peakDoseStr);
    aLine("Saturation Trend",saturationStr);
    aLine("Seizure Trend",   report.seizureTrendText);
    out << "\n--------------------------------------------------\n\n";

    out << "[Stability Analysis]\n";
    aLine("Run Count",        std::to_string(runCount));
    aLine("Rate Variability", formatRuntimeNumber(stabilityStats.stdRate, 2));
    aLine("Sync Variability", formatRuntimeNumber(stabilityStats.stdSync, 3));
    aLine("Stability Score",  stab);
    out << "\n--------------------------------------------------\n\n";

    out << "[Primary Observation]\n";
    out << report.primaryChangeText << "\n\n";

    out << "[Safety Interpretation]\n";
    out << report.safetyInterpretationText << "\n\n";

    out << "--------------------------------------------------\n";
    out << "[FINAL DECISION]\n";
    aLine("Recommendation", report.recommendation);
    aLine("Risk Level",     report.riskLevel);
    aLine("Mechanism",      report.mechanismText);
    aLine("Reason",         report.reason);
    if (report.excitatoryVerdictViaMagnitudeFloor) {
        out << "Note                  : No individual dose crossed a structural instability\n"
               "                        threshold (Hyperexcitable/SeizureRisk/etc. -- see Per\n"
               "                        Dose Network State above, all Stable). This verdict is\n"
               "                        driven by the aggregate rate-change magnitude exceeding\n"
               "                        this engine's 10% safety floor for a categorical\n"
               "                        excitatory response, not a per-dose danger signal.\n";
    }
    aLine("Confidence",     report.confidence);
    out << "\n==================================================\n";

    return out.str();
}

void writeDrugEvaluationReport(
    const std::string& path, const PharmaDecisionReport& report,
    const AggregatedStats& stats, int runs,
    const RuntimeInput& input, const std::string& mode,
    std::optional<bool> usedGpu)
{
    std::ofstream out(path, std::ios::out|std::ios::trunc);
    if (!out.is_open()) throw std::runtime_error("Cannot open: " + path);
    out << buildDrugEvaluationReportText(report, stats, runs, input, mode, usedGpu);
}

} // namespace spp::report