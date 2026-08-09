#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <ctime>
#include <utility>
#include <vector>

#include "analyzer/Metrics.h"
#include "analyzer/NetworkAnalyzer.h"
#include "analyzer/PharmaDecisionEngine.h"
#include "analyzer/DoseObservation.h"
#include "analyzer/AnalyzedDose.h"
#include "drug/DrugModel.h"
#include "network/Network.h"
#include "output/CsvWriter.h"
#include "simulation/SimulationEngine.h"
#include "simulation/BatchedSimulationEngine.h"

#include "report/ReportTypes.h"
#include "report/ReportFormatting.h"
#include "report/LegacyLiabilityReport.h"
#include "report/LiabilityReport.h"

namespace {

using spp::analyzer::MetricsAnalyzer;
using spp::report::SimulationConfig;
using spp::report::RuntimeInput;
using spp::report::AggregatedStats;
using spp::report::kRuntimeOutputPrecision;
using spp::report::formatRuntimeNumber;
using spp::report::getStability;
// buildDrugEvaluationReportText/writeDrugEvaluationReport (legacy
// verdict-style report) are no longer called from the dose-eval path --
// see the Gap 10 retirement note near the print/export block below.
// LegacyLiabilityReport.h is still included above so the declarations
// remain available if a future side-by-side diff is ever needed again.
using spp::report::buildLiabilityReportText;
using spp::report::writeLiabilityReport;
using spp::analyzer::NetworkMetrics;
using spp::analyzer::NetworkState;
using spp::analyzer::MechanismSignature;
using spp::analyzer::NetworkAnalyzer;
using spp::analyzer::DoseObservation;
using spp::analyzer::AnalyzedDose;
using spp::analyzer::PharmaDecisionEngine;
using spp::analyzer::PharmaDecisionReport;
using spp::drug::ChannelDrugProfile;
using spp::drug::DrugModel;
using spp::drug::ReceptorAction;
using spp::drug::ReceptorDrugProfile;
using spp::drug::ReceptorMechanism;
using spp::network::NetworkConfig;
using spp::output::CsvWriter;
using spp::output::DoseResponsePoint;
using spp::output::NetworkMetricRecord;
using spp::simulation::SimulationEngine;
using spp::simulation::BatchedSimulationEngine;
using spp::simulation::BatchBlockSpec;

constexpr int kMinNeuronCount     = 1000;
constexpr int kMaxNeuronCount     = 100000;
constexpr double kMinConnectivity = 0.05;
constexpr double kMaxConnectivity = 0.20;

// SimulationSummary now carries only raw metrics — no NetworkState.
// Classification is done by NetworkAnalyzer after all doses are collected.
struct SimulationSummary {
    std::vector<spp::analyzer::NeuronMetrics> neuron_metrics;
    NetworkMetrics network_metrics;
};

struct SimulationTrace {
    SimulationSummary summary;
    spp::simulation::SimulationResult result;
};

struct ValidationCheck {
    std::string name;
    bool pass = false;
    std::string details;
};

struct MetricStats {
    double mean     = 0.0;
    double stddev   = 0.0;
    double ci95Low  = 0.0;
    double ci95High = 0.0;
    std::size_t n   = 0U;
};

// RunResult — pure measured quantities only, no decisions.
struct RunResult {
    float firingRate  = 0.0f;
    float sync        = 0.0f;
    float burst       = 0.0f;
    float burstRateHz = 0.0f;
    float isiCV       = 0.0f;
    float popVariance = 0.0f;
    float peakSync            = 0.0f;
    float burstingNeuronPct   = 0.0f;
    float meanBurstDurationMs = 0.0f;
    float firingRateStdHz     = 0.0f;
    float silentNeuronPct     = 0.0f;
    float lateWindowSilentNeuronPct = 0.0f;  // Gap 1.3 fix
    float earlyWindowRateHz   = 0.0f;
    float lateWindowRateHz    = 0.0f;
    float firstThirdRateHz    = 0.0f;
    float middleThirdRateHz   = 0.0f;
    float lastThirdRateHz     = 0.0f;

    // Diagnostic addition (2026-08-08, Tier 2.4 beta/alpha-1 ISN-hypothesis
    // investigation): per-run mean firing rate split by cell type, computed
    // directly from that run's NeuronMetrics (now that NeuronMetrics carries
    // neuronType -- see analyzer/Metrics.h). Added because there was
    // previously no reliable way to compare excitatory vs. inhibitory rate
    // AT A GIVEN DOSE across the whole sweep -- only exportSingleRunArtifacts'
    // finalNM snapshot (the LAST dose x LAST repeat) ever reached a CSV,
    // which caused real confusion when manually diffing separate re-runs.
    // This is computed for every (dose, repeat) pair, not just the last one.
    float excitatoryRateHz = 0.0f;
    float inhibitoryRateHz = 0.0f;

    // Diagnostic addition (2026-08-08, Tier 2.4 rhythm-hypothesis test):
    // per-run mean ISI and ISI variance split by cell type, same pattern
    // and same motivation as excitatoryRateHz/inhibitoryRateHz above --
    // tests whether the drug is making excitatory neurons fire MORE
    // REGULARLY (lower isi_variance_ms) rather than just more/less often,
    // since two direct hypotheses (I-neuron disinhibition, ISN network
    // effect) have already been tested and falsified with real celltype
    // rate data.
    float excitatoryIsiMeanMs = 0.0f;
    float inhibitoryIsiMeanMs = 0.0f;
    float excitatoryIsiVarMs  = 0.0f;
    float inhibitoryIsiVarMs  = 0.0f;
};

struct SigmoidFitResult {
    double k    = 0.0;
    double d50  = 0.0;
    double emax = 1.0;
    double r2   = 0.0;
    double sse  = std::numeric_limits<double>::infinity();
    std::vector<double> predicted;
};

// ─── String helpers ───────────────────────────────────────────────────────────
std::string toLower(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return v;
}

std::string toUpper(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(),
        [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return v;
}

std::string trim(std::string v) {
    const auto ns = [](unsigned char c){ return !std::isspace(c); };
    auto b = std::find_if(v.begin(), v.end(), ns);
    if (b == v.end()) return {};
    auto e = std::find_if(v.rbegin(), v.rend(), ns).base();
    return std::string(b, e);
}

std::optional<bool> parseBoolText(const std::string& text) {
    const std::string v = toLower(trim(text));
    if (v=="1"||v=="true" ||v=="yes"||v=="y"||v=="on")  return true;
    if (v=="0"||v=="false"||v=="no" ||v=="n"||v=="off") return false;
    return std::nullopt;
}

std::optional<std::string> readEnvVar(const char* name) {
#ifdef _MSC_VER
    char* buf = nullptr; std::size_t len = 0U;
    if (_dupenv_s(&buf, &len, name) != 0 || buf == nullptr) return std::nullopt;
    std::string val(buf); std::free(buf); return val;
#else
    const char* v = std::getenv(name);
    return v ? std::optional<std::string>(v) : std::nullopt;
#endif
}

// ─── Console helpers ──────────────────────────────────────────────────────────
// kRuntimeOutputPrecision / formatRuntimeNumber moved to report/ReportFormatting.h
// (see using-declarations at top of file) as part of the report-layer extraction.
constexpr int kRuntimeDividerWidth    = 50;
constexpr int kRuntimeLabelWidth      = 22;

void printDivider(char fill = '=') {
    std::cout << std::string(kRuntimeDividerWidth, fill) << "\n";
}

void printMetricLine(const std::string& metric, const std::string& value) {
    std::cout << std::left << std::setw(kRuntimeLabelWidth) << metric << " : " << value << "\n";
}

void printMetricLine(const std::string& metric, double value,
                     const std::string& suffix = std::string()) {
    printMetricLine(metric, formatRuntimeNumber(value) + suffix);
}

void printSection(const std::string& title) {
    printDivider('-');
    std::cout << '[' << title << "]\n";
    printDivider('-');
}

void printHelp() {
    std::cout << "Usage:\n"
              << "  silicon_patient.exe --simulate\n"
              << "  silicon_patient.exe --dose-eval\n";
}

// ─── Config validation ────────────────────────────────────────────────────────
void validateConfig(const SimulationConfig& cfg) {
    if (cfg.neuron_count < kMinNeuronCount || cfg.neuron_count > kMaxNeuronCount)
        throw std::runtime_error("Neuron count must be between 1000 and 100000.");
    if (!(cfg.sim_time > 0.0))   throw std::runtime_error("Simulation time must be > 0.");
    if (!(cfg.dt > 0.0))         throw std::runtime_error("dt must be > 0.");
    if (cfg.dt >= cfg.sim_time)  throw std::runtime_error("dt must be smaller than simulation time.");
    if (cfg.dose < 0.0)          throw std::runtime_error("Dose must be >= 0.");
    if (!(cfg.ic50_na>0.0)||!(cfg.ic50_k>0.0)||!(cfg.ic50_ca>0.0))
        throw std::runtime_error("IC50 values must be > 0.");
    if (!(cfg.hill>=1.0&&cfg.hill<=6.0))
        throw std::runtime_error("Hill coefficient must be in [1, 6].");
    if (cfg.connectivity<kMinConnectivity||cfg.connectivity>kMaxConnectivity)
        throw std::runtime_error("Connectivity must be in [0.05, 0.2].");
    if (!(cfg.excitatory_ratio>0.0&&cfg.excitatory_ratio<1.0))
        throw std::runtime_error("Excitatory ratio must be in (0, 1).");
    if (cfg.noise_level < 0.0)   throw std::runtime_error("Noise level must be >= 0.");
    if (trim(cfg.output_folder).empty())
        throw std::runtime_error("Output folder cannot be empty.");
    // Phase 3b: desensitization is now mirrored to the GPU batched kernel
    // (fusedBatchedStepKernel in NeuronUpdate.cu, same pattern as GAT1's
    // tau-extension GPU port) -- NOT yet verified on real GPU hardware by
    // this session, only compiled/reasoned through. The guard that used to
    // live here (reject desensitization_enabled && use_cuda) is removed now
    // that there's a real GPU implementation to fall through to, but treat
    // the GPU path as implemented-but-unverified until a real CUDA run
    // confirms it matches the CPU path's numbers, same caveat already
    // documented for the rest of the receptor-conductance GPU mirror.

    // Phase 3c: vesicle pools are now mirrored to the GPU batched kernel
    // (fusedBatchedStepKernel in NeuronUpdate.cu -- release-scale ring
    // buffer parallel to the spike delay buffer, persistent per-neuron
    // RRP/Reserve state, curand-based binomial release draw), same pattern
    // as GAT1's and desensitization's GPU ports. The hard reject that used
    // to live here (vesicle_pool_enabled && use_cuda) is removed now that
    // there's a real GPU implementation to fall through to -- but exactly
    // like desensitization's GPU mirror, treat this as implemented-but-
    // UNVERIFIED until a real CUDA run confirms it matches the CPU path's
    // numbers (SPP_FORCE_CPU=1 vs default, same drug config, compare
    // Early-Half/Late-Half rate and any deterministic occupancy/scale
    // values -- only genuinely stochastic firing-rate numbers should
    // differ, same parity discipline used for every other Phase 3 GPU
    // port this session).
}

std::uint32_t makeSeed() {
    std::random_device rd;
    return rd();
}

// ─── Network / engine config builders ────────────────────────────────────────
NetworkConfig buildNetworkConfig(const SimulationConfig& cfg, std::uint32_t seed) {
    NetworkConfig netCfg;
    const float excScale    = std::clamp(static_cast<float>(cfg.excitatory_weight_scale), 0.05f, 4.0f);
    const float inhScale    = std::clamp(static_cast<float>(cfg.inhibitory_weight_scale), 0.05f, 4.0f);
    const float neuronScale = std::clamp(
        std::sqrt(1000.0f / std::max(300.0f, static_cast<float>(cfg.neuron_count))),
        0.90f, 1.30f);
    netCfg.neuronCount           = static_cast<std::size_t>(cfg.neuron_count);
    netCfg.excitatoryFraction    = static_cast<float>(cfg.excitatory_ratio);
    netCfg.connectionProbability = std::clamp(static_cast<float>(cfg.connectivity)*1.40f, 0.02f, 0.10f);
    netCfg.minDelaySteps         = 1;
    netCfg.maxDelaySteps         = 24;
    netCfg.excitatoryWeightMean  = 1.25f * excScale * neuronScale;
    netCfg.inhibitoryWeightMean  = 2.20f * inhScale * neuronScale;
    netCfg.weightStdFraction     = 0.40f;
    netCfg.excitatoryWeightMin   = 0.45f * excScale * neuronScale;
    netCfg.excitatoryWeightMax   = 2.40f * excScale * neuronScale;
    netCfg.inhibitoryWeightMin   = 1.70f * inhScale * neuronScale;
    netCfg.inhibitoryWeightMax   = 5.80f * inhScale * neuronScale;
    netCfg.recurrentExcitatoryBias = 0.78f;
    netCfg.feedbackInhibitoryBias  = 0.82f;
    netCfg.maxSynapses = std::max<std::size_t>(500000,
        static_cast<std::size_t>(
            static_cast<double>(cfg.neuron_count) *
            static_cast<double>(cfg.neuron_count) *
            cfg.connectivity * 1.30));
    netCfg.randomSeed = seed;
    return netCfg;
}

spp::simulation::SimulationConfig buildEngineConfig(const SimulationConfig& cfg, std::uint32_t seed) {
    spp::simulation::SimulationConfig simCfg;
    simCfg.dtMs                = static_cast<float>(cfg.dt);
    simCfg.durationMs          = static_cast<float>(cfg.sim_time);
    simCfg.randomSeed          = seed + 17U;
    simCfg.baseExternalCurrent = static_cast<float>(cfg.external_current);
    simCfg.externalCurrentStd  = std::max(0.16f, static_cast<float>(std::fabs(cfg.external_current)*0.09));
    simCfg.baseNoiseStd        = std::max(0.14f, 0.56f*static_cast<float>(cfg.noise_level));
    simCfg.refractoryMs        = 1.8f;
    simCfg.synTauExcMs         = 7.0f;
    simCfg.synTauInhMs         = 14.0f;
    simCfg.maxSynCurrent       = 320.0f;
    simCfg.maxTotalCurrent     = 350.0f;
    simCfg.adaptationTauMs     = 220.0f;
    simCfg.adaptationIncrement = 0.016f + 0.010f*std::clamp(static_cast<float>(cfg.noise_level),0.0f,2.0f);
    simCfg.adaptationMaxCurrent      = 1.8f;
    simCfg.adaptationInhibitoryScale = 0.50f;
    simCfg.drugOnsetTauMs            = 140.0f;
    simCfg.useGpu                    = cfg.use_cuda;

    // Phase 3b: GABA-A desensitization pass-through, see SimulationConfig
    // comment above and synapse::DesensitizationConfig for the full design.
    simCfg.desensitizationEnabled          = cfg.desensitization_enabled;
    simCfg.desensitizationTauDesenseMs     = static_cast<float>(cfg.desensitization_tau_desense_ms);
    simCfg.desensitizationTauRecoveryMs    = static_cast<float>(cfg.desensitization_tau_recovery_ms);
    simCfg.desensitizationMaxAttenuation   = static_cast<float>(cfg.desensitization_max_attenuation);

    // Phase 3c: vesicle pool pass-through, see SimulationConfig comment
    // above and synapse::VesiclePoolConfig for the full design.
    simCfg.vesiclePoolEnabled              = cfg.vesicle_pool_enabled;
    simCfg.vesiclePoolRrpSize              = static_cast<float>(cfg.vesicle_pool_rrp_size);
    simCfg.vesiclePoolReserveSize          = static_cast<float>(cfg.vesicle_pool_reserve_size);
    simCfg.vesiclePoolRrpRefillTauMs       = static_cast<float>(cfg.vesicle_pool_rrp_refill_tau_ms);
    simCfg.vesiclePoolReserveRefillTauMs   = static_cast<float>(cfg.vesicle_pool_reserve_refill_tau_ms);
    simCfg.vesiclePoolCalciumFactor        = static_cast<float>(cfg.vesicle_pool_calcium_factor);

    return simCfg;
}

// PHASE2_PLAN.md step 4: builds the drug's receptor pharmacology profile
// from the generic ic50_ampa/hill_ampa/... fields above. Mechanism per
// receptor is fixed (see the SimulationConfig comment on those fields) --
// AMPA/NMDA Block, GABA-A Potentiate, GABA-B Agonist. With the struct's
// default (inert) values this produces a profile that is a no-op at any
// dose, same as an unconfigured ReceptorDrugProfile{}.
ReceptorDrugProfile buildReceptorProfile(const SimulationConfig& cfg) {
    ReceptorDrugProfile profile;

    profile.ampa.mechanism = ReceptorMechanism::Block;
    profile.ampa.ec50      = static_cast<float>(cfg.ic50_ampa);
    profile.ampa.hill      = static_cast<float>(cfg.hill_ampa);

    profile.nmda.mechanism = ReceptorMechanism::Block;
    profile.nmda.ec50      = static_cast<float>(cfg.ic50_nmda);
    profile.nmda.hill      = static_cast<float>(cfg.hill_nmda);

    profile.gabaA.mechanism             = ReceptorMechanism::Potentiate;
    profile.gabaA.ec50                  = static_cast<float>(cfg.ec50_gabaA);
    profile.gabaA.hill                  = static_cast<float>(cfg.hill_gabaA);
    profile.gabaA.maxPotentiationFactor = static_cast<float>(cfg.max_potentiation_gabaA);

    profile.gabaB.mechanism = ReceptorMechanism::Agonist;
    profile.gabaB.ec50      = static_cast<float>(cfg.ec50_gabaB);
    profile.gabaB.hill      = static_cast<float>(cfg.hill_gabaB);

    // Phase 3a: GAT1 reuptake block (tiagabine) -- extends GABA-A's and
    // GABA-B's decay time constant, see DrugModel::computeReceptorKineticsModifiers.
    // Unlike ampa/nmda/gabaA/gabaB above, mechanism stays None unless the
    // JSON actually configured a real Ki -- BatchedSimulationEngine gates
    // its (otherwise zero-cost) kinetics-override machinery on
    // mechanism != None, and that gate is only a meaningful safety
    // guarantee (guaranteed bit-identical nullptr passthrough for all
    // already-validated drugs) if an unconfigured drug genuinely leaves it
    // at None rather than "None in practice via a huge Ki".
    constexpr double kTransporterInertThreshold = 1.0e8;
    if (cfg.ki_gat1 < kTransporterInertThreshold) {
        profile.gat1.mechanism        = spp::synapse::TransporterBlockType::Competitive;
        profile.gat1.kiUm             = static_cast<float>(cfg.ki_gat1);
        profile.gat1.hill             = static_cast<float>(cfg.hill_gat1);
        profile.gat1.maxExtensionFold = static_cast<float>(cfg.max_extension_gat1);
    }

    // Phase 3a: SERT/DAT/NET -- report-only (see SimulationConfig comment).
    // Still wired into the profile with a real mechanism tag so
    // DrugModel::computeReceptorKineticsModifiers can compute correct
    // occupancy/fold numbers for the report; just never touches gMax or
    // gets threaded into any conductance path.
    if (cfg.ki_sert < kTransporterInertThreshold) {
        profile.sert.mechanism        = spp::synapse::TransporterBlockType::Competitive;
        profile.sert.kiUm             = static_cast<float>(cfg.ki_sert);
        profile.sert.hill             = static_cast<float>(cfg.hill_sert);
        profile.sert.maxExtensionFold = static_cast<float>(cfg.max_extension_sert);
    }
    if (cfg.ki_dat < kTransporterInertThreshold) {
        profile.dat.mechanism        = spp::synapse::TransporterBlockType::Competitive;
        profile.dat.kiUm             = static_cast<float>(cfg.ki_dat);
        profile.dat.hill             = static_cast<float>(cfg.hill_dat);
        profile.dat.maxExtensionFold = static_cast<float>(cfg.max_extension_dat);
    }
    if (cfg.ki_net < kTransporterInertThreshold) {
        profile.net.mechanism        = spp::synapse::TransporterBlockType::Competitive;
        profile.net.kiUm             = static_cast<float>(cfg.ki_net);
        profile.net.hill             = static_cast<float>(cfg.hill_net);
        profile.net.maxExtensionFold = static_cast<float>(cfg.max_extension_net);
    }

    // Phase 3c: neuromodulator gain (D1/D2/5-HT1A/5-HT2A). No mechanism
    // enum to gate on here (unlike Block/Potentiate/Agonist/Transporter) --
    // these are always a plain Hill-occupancy gain, so unconditional
    // assignment is safe: cfg's defaults (ec50=1e9, gain ceilings inert)
    // already produce a no-op profile with no special-casing needed.
    profile.neuromod.d1.ec50 = static_cast<float>(cfg.ec50_d1);
    profile.neuromod.d1.hill = static_cast<float>(cfg.hill_d1);
    profile.neuromod.d1.maxAdaptationReductionFrac = static_cast<float>(cfg.max_adaptation_reduction_d1);
    profile.neuromod.d1.maxNmdaGainFold = static_cast<float>(cfg.max_nmda_gain_d1);

    profile.neuromod.d2.ec50 = static_cast<float>(cfg.ec50_d2);
    profile.neuromod.d2.hill = static_cast<float>(cfg.hill_d2);
    profile.neuromod.d2.maxReleaseReductionFrac = static_cast<float>(cfg.max_release_reduction_d2);
    profile.neuromod.d2.postsynapticEc50 = static_cast<float>(cfg.postsynaptic_ec50_d2);
    profile.neuromod.d2.postsynapticHill = static_cast<float>(cfg.postsynaptic_hill_d2);
    profile.neuromod.d2.maxPostsynapticCaReductionFrac = static_cast<float>(cfg.max_postsynaptic_ca_reduction_d2);

    profile.neuromod.ht1a.ec50 = static_cast<float>(cfg.ec50_ht1a);
    profile.neuromod.ht1a.hill = static_cast<float>(cfg.hill_ht1a);
    profile.neuromod.ht1a.maxKGainFold = static_cast<float>(cfg.max_k_gain_ht1a);
    profile.neuromod.ht1a.autoreceptorEc50 = static_cast<float>(cfg.autoreceptor_ec50_ht1a);
    profile.neuromod.ht1a.autoreceptorHill = static_cast<float>(cfg.autoreceptor_hill_ht1a);
    profile.neuromod.ht1a.maxAutoreceptorSuppressionFrac = static_cast<float>(cfg.max_autoreceptor_suppression_ht1a);
    profile.neuromod.ht1a.autoreceptorTauDesenseMs = static_cast<float>(cfg.autoreceptor_tau_desense_ms_ht1a);
    profile.neuromod.ht1a.autoreceptorTauRecoveryMs = static_cast<float>(cfg.autoreceptor_tau_recovery_ms_ht1a);
    profile.neuromod.ht1a.autoreceptorExposureOffsetMs = static_cast<float>(cfg.autoreceptor_exposure_offset_ms_ht1a);

    profile.neuromod.ht2a.ec50 = static_cast<float>(cfg.ec50_ht2a);
    profile.neuromod.ht2a.hill = static_cast<float>(cfg.hill_ht2a);
    profile.neuromod.ht2a.maxKReductionFrac = static_cast<float>(cfg.max_k_reduction_ht2a);
    profile.neuromod.ht2a.maxAdaptationReductionFrac = static_cast<float>(cfg.max_adaptation_reduction_ht2a);

    profile.neuromod.alpha2.presynapticEc50 = static_cast<float>(cfg.presynaptic_ec50_alpha2);
    profile.neuromod.alpha2.presynapticHill = static_cast<float>(cfg.presynaptic_hill_alpha2);
    profile.neuromod.alpha2.maxPresynapticReleaseReductionFrac = static_cast<float>(cfg.max_presynaptic_release_reduction_alpha2);
    profile.neuromod.alpha2.postsynapticEc50 = static_cast<float>(cfg.postsynaptic_ec50_alpha2);
    profile.neuromod.alpha2.postsynapticHill = static_cast<float>(cfg.postsynaptic_hill_alpha2);
    profile.neuromod.alpha2.maxPostsynapticAdaptationReductionFrac = static_cast<float>(cfg.max_postsynaptic_adaptation_reduction_alpha2);

    profile.neuromod.beta.ec50 = static_cast<float>(cfg.ec50_beta);
    profile.neuromod.beta.hill = static_cast<float>(cfg.hill_beta);
    profile.neuromod.beta.maxAdaptationIncreaseFold = static_cast<float>(cfg.max_adaptation_increase_beta);

    profile.neuromod.alpha1.ec50 = static_cast<float>(cfg.ec50_alpha1);
    profile.neuromod.alpha1.hill = static_cast<float>(cfg.hill_alpha1);
    profile.neuromod.alpha1.maxAdaptationIncreaseFold = static_cast<float>(cfg.max_adaptation_increase_alpha1);

    return profile;
}

// ─── Core simulation runner ───────────────────────────────────────────────────
// Returns raw SimulationSummary — no classification, no decisions.
// Classification happens later in NetworkAnalyzer after all doses are collected.
SimulationSummary runSingleSimulationInternal(
    const RuntimeInput& input,
    std::uint32_t seed,
    spp::simulation::SimulationResult* outResult)
{
    const NetworkConfig networkCfg = buildNetworkConfig(input.config, seed);
    const spp::simulation::SimulationConfig engineCfg = buildEngineConfig(input.config, seed);

    SimulationEngine engine(static_cast<std::size_t>(input.config.neuron_count),
                            networkCfg, engineCfg);

    DrugModel drugModel;
    drugModel.setGlobalDose(static_cast<float>(input.config.dose));
    drugModel.setGlobalProfile(ChannelDrugProfile{
        static_cast<float>(input.config.ic50_na),
        static_cast<float>(input.config.ic50_k),
        static_cast<float>(input.config.ic50_ca),
        static_cast<float>(input.config.hill),
        static_cast<float>(input.config.hill),
        static_cast<float>(input.config.hill)
    });
    drugModel.setGlobalReceptorProfile(buildReceptorProfile(input.config));

    engine.setDrugModel(drugModel);
    engine.initialize();
    spp::simulation::SimulationResult simResult = engine.run();

    SimulationSummary summary;
    summary.neuron_metrics  = MetricsAnalyzer::computeNeuronMetrics(simResult);
    // Pure measurement — no baseline, no decisions
    summary.network_metrics = MetricsAnalyzer::computeNetworkMetrics(
        simResult, summary.neuron_metrics);

    if (outResult) *outResult = std::move(simResult);
    return summary;
}

SimulationSummary runSingleSimulation(const RuntimeInput& input, std::uint32_t seed) {
    return runSingleSimulationInternal(input, seed, nullptr);
}

SimulationTrace runSingleSimulationWithTrace(const RuntimeInput& input, std::uint32_t seed) {
    SimulationTrace trace;
    trace.summary = runSingleSimulationInternal(input, seed, &trace.result);
    return trace;
}

// ─── ISI-CV helper ────────────────────────────────────────────────────────────
float computeMeanIsiCv(const std::vector<spp::analyzer::NeuronMetrics>& neuronMetrics) {
    double pooledSum=0, pooledSumSq=0;
    std::size_t pooledN=0;
    double cvSum=0; std::size_t cvN=0;
    for (const auto& n : neuronMetrics) {
        if (n.spikeCount<3U||n.isiMeanMs<=1e-6f||n.isiVarianceMs<0.0f) continue;
        const std::size_t isiCount = n.spikeCount-1U;
        const double mean = static_cast<double>(n.isiMeanMs);
        const double var  = static_cast<double>(n.isiVarianceMs);
        pooledSum   += static_cast<double>(isiCount)*mean;
        pooledSumSq += static_cast<double>(isiCount)*(var+mean*mean);
        pooledN     += isiCount;
        const float cv = std::sqrt(std::max(0.0f,n.isiVarianceMs))/n.isiMeanMs;
        if (!std::isfinite(cv)||cv<=0.0f) continue;
        cvSum += cv; ++cvN;
    }
    if (pooledN<2U) return 0.0f;
    const double pm = pooledSum/static_cast<double>(pooledN);
    if (pm<=1e-12) return 0.0f;
    const double pv  = std::max(0.0, pooledSumSq/static_cast<double>(pooledN)-pm*pm);
    const double pcv = std::sqrt(pv)/pm;
    const double mcv = cvN ? cvSum/static_cast<double>(cvN) : pcv;
    return static_cast<float>(0.65*pcv+0.35*mcv);
}

// ─── Stat helpers ─────────────────────────────────────────────────────────────
MetricStats computeMetricStats(const std::vector<double>& values) {
    MetricStats s; s.n=values.size();
    if (values.empty()) return s;
    double mean=0; for (double v:values) mean+=v;
    mean/=static_cast<double>(values.size());
    double var=0;
    if (values.size()>1U){
        for(double v:values){double d=v-mean;var+=d*d;}
        var/=static_cast<double>(values.size()-1U);
    }
    const double sd=std::sqrt(std::max(0.0,var));
    const double hw=1.96*sd/std::sqrt(static_cast<double>(values.size()));
    s.mean=mean; s.stddev=sd; s.ci95Low=mean-hw; s.ci95High=mean+hw;
    return s;
}

float computeMean(const std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    double s=0; for (float x:v) s+=x;
    return static_cast<float>(s/static_cast<double>(v.size()));
}

float computeStd(const std::vector<float>& v, float mean) {
    if (v.empty()) return 0.0f;
    double s=0;
    for (float x:v){double d=static_cast<double>(x)-mean;s+=d*d;}
    return static_cast<float>(std::sqrt(s/static_cast<double>(v.size())));
}

AggregatedStats computeStats(const std::vector<RunResult>& results) {
    AggregatedStats stats;
    if (results.empty()) return stats;
    std::vector<float> rates,syncs,bursts,burstRates,isis;
    std::vector<float> peakSyncs, burstingPcts, burstDurs, rateStds, silentPcts, lateSilentPcts, earlyRates, lateRates;
    std::vector<float> firstThirdRates, middleThirdRates, lastThirdRates;
    std::vector<float> excRates, inhRates;
    std::vector<float> excIsiMean, inhIsiMean, excIsiVar, inhIsiVar;
    rates.reserve(results.size());      syncs.reserve(results.size());
    bursts.reserve(results.size());     isis.reserve(results.size());
    burstRates.reserve(results.size());
    peakSyncs.reserve(results.size());  burstingPcts.reserve(results.size());
    burstDurs.reserve(results.size());  rateStds.reserve(results.size());
    silentPcts.reserve(results.size()); lateSilentPcts.reserve(results.size());
    earlyRates.reserve(results.size());
    lateRates.reserve(results.size());
    firstThirdRates.reserve(results.size());
    middleThirdRates.reserve(results.size());
    lastThirdRates.reserve(results.size());
    excRates.reserve(results.size());
    inhRates.reserve(results.size());
    excIsiMean.reserve(results.size()); inhIsiMean.reserve(results.size());
    excIsiVar.reserve(results.size());  inhIsiVar.reserve(results.size());

    for (const auto& r : results) {
        rates.push_back(r.firingRate);
        syncs.push_back(r.sync);
        bursts.push_back(r.burst);
        burstRates.push_back(r.burstRateHz);
        isis.push_back(r.isiCV);
        peakSyncs.push_back(r.peakSync);
        burstingPcts.push_back(r.burstingNeuronPct);
        burstDurs.push_back(r.meanBurstDurationMs);
        rateStds.push_back(r.firingRateStdHz);
        silentPcts.push_back(r.silentNeuronPct);
        lateSilentPcts.push_back(r.lateWindowSilentNeuronPct);
        earlyRates.push_back(r.earlyWindowRateHz);
        lateRates.push_back(r.lateWindowRateHz);
        firstThirdRates.push_back(r.firstThirdRateHz);
        middleThirdRates.push_back(r.middleThirdRateHz);
        lastThirdRates.push_back(r.lastThirdRateHz);
        excRates.push_back(r.excitatoryRateHz);
        inhRates.push_back(r.inhibitoryRateHz);
        excIsiMean.push_back(r.excitatoryIsiMeanMs);
        inhIsiMean.push_back(r.inhibitoryIsiMeanMs);
        excIsiVar.push_back(r.excitatoryIsiVarMs);
        inhIsiVar.push_back(r.inhibitoryIsiVarMs);
    }

    stats.meanRate      = computeMean(rates);      stats.stdRate  = computeStd(rates,  stats.meanRate);
    stats.meanSync      = computeMean(syncs);      stats.stdSync  = computeStd(syncs,  stats.meanSync);
    stats.meanBurst     = computeMean(bursts);     stats.stdBurst = computeStd(bursts, stats.meanBurst);
    stats.meanBurstRate = computeMean(burstRates); // FIX: was never computed — always 0
    stats.meanISI       = computeMean(isis);       stats.stdISI   = computeStd(isis,   stats.meanISI);
    stats.stdRate2  = stats.stdRate; // used as stability proxy

    stats.meanPeakSync          = computeMean(peakSyncs);
    stats.meanBurstingNeuronPct = computeMean(burstingPcts);
    stats.meanBurstDurationMs   = computeMean(burstDurs);
    stats.meanFiringRateStdHz   = computeMean(rateStds);
    stats.meanSilentNeuronPct   = computeMean(silentPcts);
    stats.meanLateWindowSilentNeuronPct = computeMean(lateSilentPcts);
    stats.meanEarlyWindowRateHz = computeMean(earlyRates);
    stats.meanLateWindowRateHz  = computeMean(lateRates);
    stats.meanFirstThirdRateHz  = computeMean(firstThirdRates);
    stats.meanMiddleThirdRateHz = computeMean(middleThirdRates);
    stats.meanLastThirdRateHz   = computeMean(lastThirdRates);
    stats.meanExcitatoryRateHz  = computeMean(excRates);
    stats.meanInhibitoryRateHz  = computeMean(inhRates);
    stats.meanExcitatoryIsiMeanMs = computeMean(excIsiMean);
    stats.meanInhibitoryIsiMeanMs = computeMean(inhIsiMean);
    stats.meanExcitatoryIsiVarMs  = computeMean(excIsiVar);
    stats.meanInhibitoryIsiVarMs  = computeMean(inhIsiVar);

    return stats;
}


// ─── Multi-run simulation ─────────────────────────────────────────────────────
std::vector<RunResult> runMultipleSimulations(
    const RuntimeInput& baseInput,
    double dose,
    int numRuns,
    std::uint32_t doseSeedBase,
    std::vector<spp::analyzer::NeuronMetrics>* outLastNeuronMetrics)
{
    std::vector<RunResult> results;
    if (numRuns<=0) return results;
    results.reserve(static_cast<std::size_t>(numRuns));
    for (int run=0;run<numRuns;++run) {
        RuntimeInput ri = baseInput;
        ri.config.dose = dose;
        const std::uint32_t seed = doseSeedBase + static_cast<std::uint32_t>(run*9973+101);
        const SimulationSummary s = runSingleSimulation(ri, seed);
        if (outLastNeuronMetrics) *outLastNeuronMetrics = s.neuron_metrics;
        RunResult r;
        r.firingRate  = s.network_metrics.meanFiringRateHz;
        r.sync        = s.network_metrics.synchronizationIndex;
        r.burst       = s.network_metrics.burstIndex;
        r.burstRateHz = s.network_metrics.burstRateHz;
        r.isiCV       = computeMeanIsiCv(s.neuron_metrics);
        r.popVariance = s.network_metrics.populationVariance;
        // BUGFIX: same gap as the batched RunResult construction site --
        // see the comment there.
        r.peakSync            = s.network_metrics.peakSynchronizationIndex;
        r.burstingNeuronPct   = s.network_metrics.burstingNeuronPct;
        r.meanBurstDurationMs = s.network_metrics.meanBurstDurationMs;
        r.firingRateStdHz     = s.network_metrics.firingRateStdHz;
        r.silentNeuronPct     = s.network_metrics.silentNeuronPct;
        r.lateWindowSilentNeuronPct = s.network_metrics.lateWindowSilentNeuronPct;
        r.earlyWindowRateHz   = s.network_metrics.earlyWindowRateHz;
        r.lateWindowRateHz    = s.network_metrics.lateWindowRateHz;
        r.firstThirdRateHz    = s.network_metrics.firstThirdRateHz;
        r.middleThirdRateHz   = s.network_metrics.middleThirdRateHz;
        r.lastThirdRateHz     = s.network_metrics.lastThirdRateHz;
        results.push_back(r);
    }
    return results;
}

// ─── Batched multi-dose, multi-repeat sweep ──────────────────────────────────
// Runs EVERY (dose x repeat) combination as one BatchedSimulationEngine
// instead of looping runSingleSimulation once per combination. This is what
// stops CUDA call count (and CudaSimulator construction/teardown) from
// scaling with sweep size: one shared timestep loop, one set of GPU
// transfers per step, covering the whole sweep at once.
//
// Each REPEAT gets its own independently-generated network topology (a
// fresh Network::buildRandom() with its own seed) so that repeats are true
// biological replicates, not the same network measured three times with
// only the noise stream varying. As of the 2026-08-08 fix below, that
// network topology is now held FIXED across all doses within a given
// repeat -- i.e. dose d=0..N for repeat r=3 all use the same network,
// only the drug dose differs -- a proper paired/within-subject design.
// Previously each (dose, repeat) pair got its own fresh network, which
// confounded dose with network identity (see the seed-formula comment
// inside this function for the real-hardware evidence that surfaced this).
std::vector<std::vector<RunResult>> runAllDosesBatched(
    const RuntimeInput& evalInput,
    const std::vector<double>& doses,
    int runs,
    std::uint32_t baseSeed,
    std::vector<spp::analyzer::NeuronMetrics>* outLastNeuronMetrics,
    bool* outUsedGpu = nullptr)
{
    std::vector<std::vector<RunResult>> perDoseResults(doses.size());
    if (doses.empty() || runs <= 0) return perDoseResults;

    // Network/engine config only depend on population size and simulation
    // timing, not on dose — dose only affects the per-block DrugModel
    // dosing. One template network config is shared as the *statistical*
    // template; each block still gets its own random instantiation.
    const NetworkConfig networkCfgTemplate = buildNetworkConfig(evalInput.config, baseSeed);
    spp::simulation::SimulationConfig engineCfg = buildEngineConfig(evalInput.config, baseSeed);
    // Diagnostic override (2026-08-08, PRECISION_GAP_CLOSURE_PLAN.md Tier
    // 2.2/2.4 beta/alpha-1 investigation): lets a test config directly set
    // the network's baseline adaptationIncrement, bypassing the whole
    // drug/dose pathway entirely -- normally this is only reachable
    // indirectly via noise_level (buildEngineConfig above). Added
    // specifically to answer one question: does MORE spike-frequency
    // adaptation cause MORE or LESS population firing in this network, in
    // general -- independent of beta/alpha-1's drug-wiring code entirely.
    // If running at dose=0 (no drug) with adaptationIncrement swept
    // directly shows rate rising as increment rises, that's a genuine,
    // general property of this network's dynamics -- not something
    // specific to how beta/alpha-1 was built, and would mean every OTHER
    // drug using the adaptation lever (D1, 5-HT2A, alpha-2-postsynaptic)
    // needs the same scrutiny, not just beta/alpha-1.
    if (auto envVal = readEnvVar("SPP_DOSE_EVAL_ADAPTATION_INCREMENT")) {
        try {
            const double parsed = std::stod(trim(*envVal));
            if (parsed >= 0.0) engineCfg.adaptationIncrement = static_cast<float>(parsed);
        } catch (...) {}
    }

    const ChannelDrugProfile profile{
        static_cast<float>(evalInput.config.ic50_na),
        static_cast<float>(evalInput.config.ic50_k),
        static_cast<float>(evalInput.config.ic50_ca),
        static_cast<float>(evalInput.config.hill),
        static_cast<float>(evalInput.config.hill),
        static_cast<float>(evalInput.config.hill)
    };
    const ReceptorDrugProfile receptorProfile = buildReceptorProfile(evalInput.config);

    std::vector<BatchBlockSpec> blocks;
    blocks.reserve(doses.size() * static_cast<std::size_t>(runs));
    for (std::size_t d = 0; d < doses.size(); ++d) {
        for (int r = 0; r < runs; ++r) {
            // FIX (2026-08-08, PRECISION_GAP_CLOSURE_PLAN.md Tier 2.2/2.4
            // beta/alpha-1 investigation): this used to be
            // `baseSeed + d*7919U + r*9973+101` -- i.e. the network-topology
            // seed depended on the DOSE INDEX `d` as well as the repeat
            // index `r`. That meant every dose in a sweep got an entirely
            // fresh, independently-randomized network (different synaptic
            // wiring, different excitatory/inhibitory assignment) instead of
            // the SAME network being tested at increasing drug levels --
            // dose was confounded with network identity. Real-hardware data
            // showed the tell: excitatory firing rate clustered near-
            // perfectly by DOSE INDEX PARITY rather than dose magnitude
            // (odd-index doses ~25.57-25.60 Hz, even-index doses ~26.07-
            // 26.82 Hz, regardless of the actual dose value), which is the
            // signature of a seed artifact, not a real dose-response.
            //
            // Fix: drop the `d*7919U` term entirely. The network seed now
            // depends ONLY on the repeat index `r`, so for a given repeat,
            // the SAME network topology is used across every dose in the
            // sweep -- a proper paired/within-subject design (one simulated
            // network, tested at multiple drug levels), while different
            // repeats (r=0..runs-1) still sample genuinely different random
            // networks, preserving between-repeat biological-replicate
            // averaging. This changes dose-eval behavior for every drug
            // config that uses a multi-point sweep, not just beta/alpha-1 --
            // any previously-recorded dose-response shape should be treated
            // as validated under the OLD (confounded) methodology until
            // re-run under this fix.
            const std::uint32_t seed = baseSeed
                + static_cast<std::uint32_t>(r * 9973 + 101);
            blocks.push_back(BatchBlockSpec{static_cast<float>(doses[d]), seed});
        }
    }

    BatchedSimulationEngine batched(
        static_cast<std::size_t>(evalInput.config.neuron_count),
        networkCfgTemplate,
        engineCfg,
        profile,
        blocks,
        receptorProfile
    );

    std::vector<spp::simulation::SimulationResult> blockResults = batched.run();
    if (outUsedGpu) {
        *outUsedGpu = batched.lastRunUsedGpu();
    }

    std::size_t idx = 0;
    for (std::size_t d = 0; d < doses.size(); ++d) {
        perDoseResults[d].reserve(static_cast<std::size_t>(runs));
        for (int r = 0; r < runs; ++r, ++idx) {
            const std::vector<spp::analyzer::NeuronMetrics> nm = MetricsAnalyzer::computeNeuronMetrics(blockResults[idx]);
            const NetworkMetrics netm = MetricsAnalyzer::computeNetworkMetrics(blockResults[idx], nm);
            if (outLastNeuronMetrics) *outLastNeuronMetrics = nm;

            RunResult rr;
            rr.firingRate  = netm.meanFiringRateHz;
            rr.sync        = netm.synchronizationIndex;
            rr.burst       = netm.burstIndex;
            rr.burstRateHz = netm.burstRateHz;
            rr.isiCV       = computeMeanIsiCv(nm);
            rr.popVariance = netm.populationVariance;
            // BUGFIX: these 7 RunResult fields existed and were correctly
            // averaged by computeStats()/AggregatedStats, but were never
            // actually copied from netm here -- silently 0.0f in every
            // RunResult ever built via this (the actual --dose-eval) path.
            // Found while adding the Phase 3b early/late-window report
            // lines (the first code to ever print meanEarlyWindowRateHz/
            // meanLateWindowRateHz), but it affects more than those two --
            // "Silent Neuron Δ" has read 0.0% in every report all session
            // for the same reason, not because there were genuinely no
            // silent neurons.
            rr.peakSync            = netm.peakSynchronizationIndex;
            rr.burstingNeuronPct   = netm.burstingNeuronPct;
            rr.meanBurstDurationMs = netm.meanBurstDurationMs;
            rr.firingRateStdHz     = netm.firingRateStdHz;
            rr.silentNeuronPct     = netm.silentNeuronPct;
            rr.lateWindowSilentNeuronPct = netm.lateWindowSilentNeuronPct;
            rr.earlyWindowRateHz   = netm.earlyWindowRateHz;
            rr.lateWindowRateHz    = netm.lateWindowRateHz;
            rr.firstThirdRateHz    = netm.firstThirdRateHz;
            rr.middleThirdRateHz   = netm.middleThirdRateHz;
            rr.lastThirdRateHz     = netm.lastThirdRateHz;

            // Diagnostic addition (2026-08-08, Tier 2.4 investigation): split
            // this run's per-neuron rates by cell type. Computed for EVERY
            // (dose, repeat) pair here -- unlike finalNM above, which only
            // ever holds the very last block processed and caused real
            // confusion when manually comparing separate re-runs against
            // each other instead of doses within the SAME sweep.
            double excSum = 0.0, inhSum = 0.0;
            std::size_t excCnt = 0, inhCnt = 0;
            // Diagnostic addition (2026-08-08, Tier 2.4 rhythm-hypothesis
            // test): also accumulate ISI mean/variance by type, same loop,
            // same NeuronMetrics -- tests whether the drug makes excitatory
            // neurons fire more REGULARLY (lower isiVarianceMs) rather than
            // just more often, now that the two direct-mechanism hypotheses
            // (I-neuron disinhibition, ISN effect) are both ruled out.
            double excIsiMeanSum = 0.0, inhIsiMeanSum = 0.0;
            double excIsiVarSum = 0.0, inhIsiVarSum = 0.0;
            for (const auto& nmi : nm) {
                if (nmi.neuronType == 1U) {
                    excSum += nmi.firingRateHz; ++excCnt;
                    excIsiMeanSum += nmi.isiMeanMs;
                    excIsiVarSum  += nmi.isiVarianceMs;
                } else {
                    inhSum += nmi.firingRateHz; ++inhCnt;
                    inhIsiMeanSum += nmi.isiMeanMs;
                    inhIsiVarSum  += nmi.isiVarianceMs;
                }
            }
            rr.excitatoryRateHz = excCnt ? static_cast<float>(excSum / static_cast<double>(excCnt)) : 0.0f;
            rr.inhibitoryRateHz = inhCnt ? static_cast<float>(inhSum / static_cast<double>(inhCnt)) : 0.0f;
            rr.excitatoryIsiMeanMs = excCnt ? static_cast<float>(excIsiMeanSum / static_cast<double>(excCnt)) : 0.0f;
            rr.inhibitoryIsiMeanMs = inhCnt ? static_cast<float>(inhIsiMeanSum / static_cast<double>(inhCnt)) : 0.0f;
            rr.excitatoryIsiVarMs  = excCnt ? static_cast<float>(excIsiVarSum / static_cast<double>(excCnt)) : 0.0f;
            rr.inhibitoryIsiVarMs  = inhCnt ? static_cast<float>(inhIsiVarSum / static_cast<double>(inhCnt)) : 0.0f;

            perDoseResults[d].push_back(rr);
        }
    }

    return perDoseResults;
}

// ─── Build aggregated NetworkMetrics from multi-run stats ─────────────────────
// Pure raw metrics only — no NII, no seizure probability, no scores.
NetworkMetrics buildAggregatedNetworkMetrics(const AggregatedStats& stats) {
    NetworkMetrics m;
    m.meanFiringRateHz     = stats.meanRate;
    m.synchronizationIndex = stats.meanSync;
    m.burstIndex           = stats.meanBurst;
    m.burstRateHz          = stats.meanBurstRate;
    m.irregularityIndex    = stats.meanISI;
    m.populationVariance   = stats.meanPopVar;
    m.peakSynchronizationIndex = stats.meanPeakSync;
    m.burstingNeuronPct        = stats.meanBurstingNeuronPct;
    m.meanBurstDurationMs      = stats.meanBurstDurationMs;
    m.firingRateStdHz          = stats.meanFiringRateStdHz;
    m.silentNeuronPct          = stats.meanSilentNeuronPct;
    m.lateWindowSilentNeuronPct = stats.meanLateWindowSilentNeuronPct;
    m.earlyWindowRateHz        = stats.meanEarlyWindowRateHz;
    m.lateWindowRateHz         = stats.meanLateWindowRateHz;
    return m;
}

// ─── Build DoseObservation from SimulationSummary ────────────────────────────
// Pure collection — no analysis here.
DoseObservation buildDoseObservation(
    const RuntimeInput& input,
    const NetworkMetrics& metrics)
{
    const float dF = static_cast<float>(input.config.dose);
    DoseObservation o;
    o.dose    = dF;
    o.blockNa = static_cast<float>(spp::drug::DrugModel::hillBlock(
        dF, static_cast<float>(input.config.ic50_na), static_cast<float>(input.config.hill)));
    o.blockK  = static_cast<float>(spp::drug::DrugModel::hillBlock(
        dF, static_cast<float>(input.config.ic50_k),  static_cast<float>(input.config.hill)));
    o.blockCa = static_cast<float>(spp::drug::DrugModel::hillBlock(
        dF, static_cast<float>(input.config.ic50_ca), static_cast<float>(input.config.hill)));
    // PHASE2_PLAN.md step 5: receptor "strength" fractions for
    // NetworkAnalyzer::detectMechanism, all on the same 0..1 Hill-occupancy
    // scale as blockNa/K/Ca above -- reuses the plain hillBlock sigmoid for
    // all four, including GABA-A, where this is the underlying occupancy
    // driving the potentiation factor (see DrugModel::hillPotentiationFactor)
    // rather than the multiplier itself, so it stays comparable to the
    // other three fractions instead of living on a >=1.0 scale.
    o.blockAmpa = static_cast<float>(spp::drug::DrugModel::hillBlock(
        dF, static_cast<float>(input.config.ic50_ampa), static_cast<float>(input.config.hill_ampa)));
    o.blockNmda = static_cast<float>(spp::drug::DrugModel::hillBlock(
        dF, static_cast<float>(input.config.ic50_nmda), static_cast<float>(input.config.hill_nmda)));
    o.potentiateGabaA = static_cast<float>(spp::drug::DrugModel::hillBlock(
        dF, static_cast<float>(input.config.ec50_gabaA), static_cast<float>(input.config.hill_gabaA)));
    o.activateGabaB = static_cast<float>(spp::drug::DrugModel::hillBlock(
        dF, static_cast<float>(input.config.ec50_gabaB), static_cast<float>(input.config.hill_gabaB)));
    // Phase 3a: GAT1 reuptake-block occupancy, for NetworkAnalyzer::
    // detectMechanism -- see AnalyzedDose.h/DoseObservation.h. Uses the raw
    // Hill occupancy fraction (spp::synapse::transporterOccupancy), not the
    // resulting tau fold-change, so it stays on the same 0..1 scale as the
    // other seven candidates.
    {
        spp::synapse::TransporterDrugEffect gat1Effect;
        gat1Effect.mechanism = spp::synapse::TransporterBlockType::Competitive;
        gat1Effect.kiUm = static_cast<float>(input.config.ki_gat1);
        gat1Effect.hill = static_cast<float>(input.config.hill_gat1);
        o.gat1ReuptakeBlock = spp::synapse::transporterOccupancy(dF, gat1Effect);
    }
    // Phase 3c: neuromodulator receptor occupancy (0..1), for
    // NetworkAnalyzer::detectMechanism -- same Hill-occupancy scale as
    // gat1ReuptakeBlock above. Phase 3c retrofit: goes through
    // buildReceptorProfile() (the same profile the simulation itself uses)
    // and DrugModel's amplifiedDoseForDopamine/amplifiedDoseForSerotonin so
    // SERT/DAT reuptake-block amplification is reflected here too --
    // otherwise mechanism detection would be attributing doses that don't
    // match what the network actually simulated.
    {
        const ReceptorDrugProfile fullProfile = buildReceptorProfile(input.config);
        const float doseForDopamine  = spp::drug::DrugModel::amplifiedDoseForDopamine(fullProfile, dF);
        const float doseForSerotonin = spp::drug::DrugModel::amplifiedDoseForSerotonin(fullProfile, dF);
        o.d1Gain = spp::synapse::neuromodulatorOccupancy(
            doseForDopamine, static_cast<float>(input.config.ec50_d1), static_cast<float>(input.config.hill_d1));
        o.d2Gain = spp::synapse::neuromodulatorOccupancy(
            doseForDopamine, static_cast<float>(input.config.ec50_d2), static_cast<float>(input.config.hill_d2));
        o.ht1aGain = spp::synapse::neuromodulatorOccupancy(
            doseForSerotonin, static_cast<float>(input.config.ec50_ht1a), static_cast<float>(input.config.hill_ht1a));
        o.ht2aGain = spp::synapse::neuromodulatorOccupancy(
            doseForSerotonin, static_cast<float>(input.config.ec50_ht2a), static_cast<float>(input.config.hill_ht2a));
        // Tier 2.2: alpha-2 presynaptic occupancy, same
        // amplified-dose-through-NET treatment as D1/D2 (DAT) and 5-HT1A/
        // 5-HT2A (SERT) above -- otherwise mechanism detection would
        // attribute doses that don't match what the network actually
        // simulated, same bug class this section's header comment warns
        // about.
        const float doseForNorepinephrine =
            spp::drug::DrugModel::amplifiedDoseForNorepinephrine(fullProfile, dF);
        o.alpha2Gain = spp::synapse::neuromodulatorOccupancy(
            doseForNorepinephrine,
            static_cast<float>(input.config.presynaptic_ec50_alpha2),
            static_cast<float>(input.config.presynaptic_hill_alpha2));
        // Tier 2.2 completion: beta/alpha-1 occupancy, same
        // amplified-NE-dose treatment as alpha-2 above.
        o.betaGain = spp::synapse::neuromodulatorOccupancy(
            doseForNorepinephrine,
            static_cast<float>(input.config.ec50_beta),
            static_cast<float>(input.config.hill_beta));
        o.alpha1Gain = spp::synapse::neuromodulatorOccupancy(
            doseForNorepinephrine,
            static_cast<float>(input.config.ec50_alpha1),
            static_cast<float>(input.config.hill_alpha1));
    }
    o.metrics.meanFiringRateHz         = metrics.meanFiringRateHz;
    o.metrics.synchronizationIndex     = metrics.synchronizationIndex;
    o.metrics.peakSynchronizationIndex = metrics.peakSynchronizationIndex;
    o.metrics.burstRateHz              = metrics.burstRateHz;
    o.metrics.burstingNeuronPct        = metrics.burstingNeuronPct;
    o.metrics.meanBurstDurationMs      = metrics.meanBurstDurationMs;
    o.metrics.irregularityIndex        = metrics.irregularityIndex;
    o.metrics.populationVariance       = metrics.populationVariance;
    o.metrics.firingRateStdHz          = metrics.firingRateStdHz;
    o.metrics.silentNeuronPct          = metrics.silentNeuronPct;
    o.metrics.lateWindowSilentNeuronPct = metrics.lateWindowSilentNeuronPct;
    o.metrics.earlyWindowRateHz        = metrics.earlyWindowRateHz;
    o.metrics.lateWindowRateHz         = metrics.lateWindowRateHz;
    return o;
}

double computeWelchZScore(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size()<2U||b.size()<2U) return 0.0;
    const MetricStats sa=computeMetricStats(a), sb=computeMetricStats(b);
    const double vA=sa.stddev*sa.stddev/static_cast<double>(a.size());
    const double vB=sb.stddev*sb.stddev/static_cast<double>(b.size());
    return (sa.mean-sb.mean)/std::sqrt(std::max(1e-12,vA+vB));
}

SigmoidFitResult fitSigmoidCurve(
    const std::vector<double>& dose, const std::vector<double>& spf)
{
    SigmoidFitResult best;
    if (dose.size()!=spf.size()||dose.size()<4U) return best;
    const auto [dminIt,dmaxIt]=std::minmax_element(dose.begin(),dose.end());
    const double dMin=*dminIt, dMax=*dmaxIt;
    double meanY=0; for (double y:spf) meanY+=y;
    meanY/=static_cast<double>(spf.size());
    double sst=0; for(double y:spf){double d=y-meanY;sst+=d*d;}
    const double dStep=std::max(0.25,(dMax-dMin)/120.0);
    for (double k=0.02;k<=2.51;k+=0.02) {
        for (double d50=dMin;d50<=dMax+1e-9;d50+=dStep) {
            double en=0,ed=0;
            for (std::size_t i=0;i<dose.size();++i){
                const double z=std::clamp(-k*(dose[i]-d50),-60.0,60.0);
                const double s=1.0/(1.0+std::exp(z));
                en+=spf[i]*s; ed+=s*s;
            }
            if (ed<=1e-12) continue;
            const double em=std::clamp(en/ed,0.0,1.0);
            double sse=0;
            for (std::size_t i=0;i<dose.size();++i){
                const double z=std::clamp(-k*(dose[i]-d50),-60.0,60.0);
                const double e=spf[i]-em/(1.0+std::exp(z));
                sse+=e*e;
            }
            if (sse<best.sse){best.sse=sse;best.k=k;best.d50=d50;best.emax=em;}
        }
    }
    best.predicted.resize(dose.size(),0.0);
    for (std::size_t i=0;i<dose.size();++i){
        const double z=std::clamp(-best.k*(dose[i]-best.d50),-60.0,60.0);
        best.predicted[i]=best.emax/(1.0+std::exp(z));
    }
    best.r2 = (sst<=1e-12) ? 1.0 : 1.0-(best.sse/sst);
    return best;
}

std::vector<double> buildLinearDoseGrid(double start, double end, int pts) {
    std::vector<double> v; v.reserve(static_cast<std::size_t>(pts));
    if (pts<=1){v.push_back(start);return v;}
    const double step=(end-start)/static_cast<double>(pts-1);
    for (int i=0;i<pts;++i) v.push_back(start+static_cast<double>(i)*step);
    return v;
}

// ─── JSON helpers ─────────────────────────────────────────────────────────────
static std::string readFileToString(const std::string& path){
    std::ifstream in(path); if (!in.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
}

static std::optional<std::string> extractJsonString(
    const std::string& s, const std::string& key, std::size_t start=0)
{
    const std::string pat='"'+key+'"';
    auto p=s.find(pat,start); if(p==s.npos) return std::nullopt;
    auto colon=s.find(':',p+pat.size()); if(colon==s.npos) return std::nullopt;
    auto q1=s.find('"',colon); if(q1==s.npos) return std::nullopt;
    auto q2=s.find('"',q1+1); if(q2==s.npos) return std::nullopt;
    return s.substr(q1+1,q2-(q1+1));
}

static std::optional<double> extractJsonNumber(
    const std::string& s, const std::string& key, std::size_t start=0)
{
    const std::string pat='"'+key+'"';
    auto p=s.find(pat,start); if(p==s.npos) return std::nullopt;
    auto colon=s.find(':',p+pat.size()); if(colon==s.npos) return std::nullopt;
    auto ns=s.find_first_of("0123456789-+.",colon); if(ns==s.npos) return std::nullopt;
    std::size_t ne=ns;
    while(ne<s.size()&&(std::isdigit((unsigned char)s[ne])||
          s[ne]=='.'||s[ne]=='e'||s[ne]=='E'||s[ne]=='+'||s[ne]=='-'))++ne;
    try{return std::stod(s.substr(ns,ne-ns));}catch(...){return std::nullopt;}
}

// Phase 3b: minimal true/false extractor, same "find key, look nearby"
// pattern as extractJsonNumber/extractJsonString above -- this hand-rolled
// parser has no general boolean support, so this is a small dedicated
// helper rather than extending the number/string ones.
static std::optional<bool> extractJsonBool(
    const std::string& s, const std::string& key, std::size_t start=0)
{
    const std::string pat='"'+key+'"';
    auto p=s.find(pat,start); if(p==s.npos) return std::nullopt;
    auto colon=s.find(':',p+pat.size()); if(colon==s.npos) return std::nullopt;
    auto t=s.find("true",colon);
    auto f=s.find("false",colon);
    auto comma=s.find_first_of(",}",colon);
    if(f!=s.npos && (comma==s.npos || f<comma)) return false;
    if(t!=s.npos && (comma==s.npos || t<comma)) return true;
    return std::nullopt;
}

static bool loadDrugConfigFromJsonFile(
    const std::string& path, RuntimeInput& out,
    std::optional<int>& outRuns, std::string& outMode,
    std::optional<double>& outDoseMin, std::optional<double>& outDoseMax, std::optional<double>& outDoseStep)
{
    const std::string c=readFileToString(path); if(c.empty()) return false;
    if(auto v=extractJsonString(c,"drug_name");v) out.drug_name=*v;
    const auto chPos=c.find("\"channels\"");
    if(chPos!=c.npos){
        const auto naP=c.find("\"Na\"",chPos);
        if(naP!=c.npos){
            if(auto n=extractJsonNumber(c,"ic50",naP);n) out.config.ic50_na=*n;
            if(auto h=extractJsonNumber(c,"hill",naP);h) out.config.hill=*h;
        }
        const auto kP=c.find("\"K\"",chPos);
        if(kP!=c.npos){
            if(auto n=extractJsonNumber(c,"ic50",kP);n) out.config.ic50_k=*n;
            if(auto h=extractJsonNumber(c,"hill",kP);h) out.config.hill=*h;
        }
        const auto caP=c.find("\"Ca\"",chPos);
        if(caP!=c.npos){
            if(auto n=extractJsonNumber(c,"ic50",caP);n) out.config.ic50_ca=*n;
            if(auto h=extractJsonNumber(c,"hill",caP);h) out.config.hill=*h;
        }
    }
    // PHASE2_PLAN.md step 4: receptor pharmacology, same "block name, look
    // for ec50/hill nearby" pattern as "channels" above. Mechanism per
    // receptor is fixed by identity (see buildReceptorProfile), so the JSON
    // only needs to supply the numbers, not the mechanism tag.
    const auto rcPos=c.find("\"receptors\"");
    if(rcPos!=c.npos){
        const auto ampaP=c.find("\"AMPA\"",rcPos);
        if(ampaP!=c.npos){
            if(auto n=extractJsonNumber(c,"ec50",ampaP);n) out.config.ic50_ampa=*n;
            if(auto h=extractJsonNumber(c,"hill",ampaP);h) out.config.hill_ampa=*h;
        }
        const auto nmdaP=c.find("\"NMDA\"",rcPos);
        if(nmdaP!=c.npos){
            if(auto n=extractJsonNumber(c,"ec50",nmdaP);n) out.config.ic50_nmda=*n;
            if(auto h=extractJsonNumber(c,"hill",nmdaP);h) out.config.hill_nmda=*h;
        }
        const auto gabaAP=c.find("\"GABA_A\"",rcPos);
        if(gabaAP!=c.npos){
            if(auto n=extractJsonNumber(c,"ec50",gabaAP);n) out.config.ec50_gabaA=*n;
            if(auto h=extractJsonNumber(c,"hill",gabaAP);h) out.config.hill_gabaA=*h;
            if(auto m=extractJsonNumber(c,"max_potentiation",gabaAP);m) out.config.max_potentiation_gabaA=*m;
        }
        const auto gabaBP=c.find("\"GABA_B\"",rcPos);
        if(gabaBP!=c.npos){
            if(auto n=extractJsonNumber(c,"ec50",gabaBP);n) out.config.ec50_gabaB=*n;
            if(auto h=extractJsonNumber(c,"hill",gabaBP);h) out.config.hill_gabaB=*h;
        }
    }
    // Phase 3a, step 1 (GAT1/tiagabine only -- see ReuptakeTransporter.h):
    // top-level "transporters" section, same "block name, look for numbers
    // nearby" pattern as "receptors" above. "ki" (not "ec50") since this is
    // a transporter inhibition constant, not a receptor EC50 -- deliberately
    // different key name so a drug config can't accidentally mix the two
    // concepts up.
    const auto trPos=c.find("\"transporters\"");
    if(trPos!=c.npos){
        const auto gat1P=c.find("\"GAT1\"",trPos);
        if(gat1P!=c.npos){
            if(auto n=extractJsonNumber(c,"ki",gat1P);n) out.config.ki_gat1=*n;
            if(auto h=extractJsonNumber(c,"hill",gat1P);h) out.config.hill_gat1=*h;
            if(auto m=extractJsonNumber(c,"max_extension",gat1P);m) out.config.max_extension_gat1=*m;
        }
        // Phase 3a, remaining 3 drugs: SERT/DAT/NET (report-only, see
        // SimulationConfig comment) -- same "ki"/"hill"/"max_extension"
        // keys as GAT1.
        const auto sertP=c.find("\"SERT\"",trPos);
        if(sertP!=c.npos){
            if(auto n=extractJsonNumber(c,"ki",sertP);n) out.config.ki_sert=*n;
            if(auto h=extractJsonNumber(c,"hill",sertP);h) out.config.hill_sert=*h;
            if(auto m=extractJsonNumber(c,"max_extension",sertP);m) out.config.max_extension_sert=*m;
        }
        const auto datP=c.find("\"DAT\"",trPos);
        if(datP!=c.npos){
            if(auto n=extractJsonNumber(c,"ki",datP);n) out.config.ki_dat=*n;
            if(auto h=extractJsonNumber(c,"hill",datP);h) out.config.hill_dat=*h;
            if(auto m=extractJsonNumber(c,"max_extension",datP);m) out.config.max_extension_dat=*m;
        }
        const auto netP=c.find("\"NET\"",trPos);
        if(netP!=c.npos){
            if(auto n=extractJsonNumber(c,"ki",netP);n) out.config.ki_net=*n;
            if(auto h=extractJsonNumber(c,"hill",netP);h) out.config.hill_net=*h;
            if(auto m=extractJsonNumber(c,"max_extension",netP);m) out.config.max_extension_net=*m;
        }
    }
    // Phase 3c: top-level "neuromodulators" section -- same "block name,
    // look for numbers nearby" pattern as "transporters"/"receptors" above.
    // Key names match each struct's field names directly (see
    // NeuromodulatorSystem.h): "ec50"/"hill" on all four, plus each one's
    // own specific ceiling key(s).
    const auto nmPos=c.find("\"neuromodulators\"");
    if(nmPos!=c.npos){
        const auto d1P=c.find("\"D1\"",nmPos);
        if(d1P!=c.npos){
            if(auto n=extractJsonNumber(c,"ec50",d1P);n) out.config.ec50_d1=*n;
            if(auto h=extractJsonNumber(c,"hill",d1P);h) out.config.hill_d1=*h;
            if(auto m=extractJsonNumber(c,"max_adaptation_reduction",d1P);m) out.config.max_adaptation_reduction_d1=*m;
            if(auto m=extractJsonNumber(c,"max_nmda_gain",d1P);m) out.config.max_nmda_gain_d1=*m;
        }
        const auto d2P=c.find("\"D2\"",nmPos);
        if(d2P!=c.npos){
            if(auto n=extractJsonNumber(c,"ec50",d2P);n) out.config.ec50_d2=*n;
            if(auto h=extractJsonNumber(c,"hill",d2P);h) out.config.hill_d2=*h;
            if(auto m=extractJsonNumber(c,"max_release_reduction",d2P);m) out.config.max_release_reduction_d2=*m;
            // Tier 2.1: postsynaptic pathway, same nested-object pattern
            // as 5-HT1A's autoreceptor fields.
            if(auto n=extractJsonNumber(c,"postsynaptic_ec50",d2P);n) out.config.postsynaptic_ec50_d2=*n;
            if(auto h=extractJsonNumber(c,"postsynaptic_hill",d2P);h) out.config.postsynaptic_hill_d2=*h;
            if(auto m=extractJsonNumber(c,"max_postsynaptic_ca_reduction",d2P);m) out.config.max_postsynaptic_ca_reduction_d2=*m;
        }
        const auto ht1aP=c.find("\"5HT1A\"",nmPos);
        if(ht1aP!=c.npos){
            if(auto n=extractJsonNumber(c,"ec50",ht1aP);n) out.config.ec50_ht1a=*n;
            if(auto h=extractJsonNumber(c,"hill",ht1aP);h) out.config.hill_ht1a=*h;
            if(auto m=extractJsonNumber(c,"max_k_gain",ht1aP);m) out.config.max_k_gain_ht1a=*m;
            // Tier 2.1: presynaptic autoreceptor pathway, same nested-object
            // pattern as the postsynaptic fields above.
            if(auto n=extractJsonNumber(c,"autoreceptor_ec50",ht1aP);n) out.config.autoreceptor_ec50_ht1a=*n;
            if(auto h=extractJsonNumber(c,"autoreceptor_hill",ht1aP);h) out.config.autoreceptor_hill_ht1a=*h;
            if(auto m=extractJsonNumber(c,"max_autoreceptor_suppression",ht1aP);m) out.config.max_autoreceptor_suppression_ht1a=*m;
            if(auto t=extractJsonNumber(c,"autoreceptor_tau_desense_ms",ht1aP);t) out.config.autoreceptor_tau_desense_ms_ht1a=*t;
            if(auto t=extractJsonNumber(c,"autoreceptor_tau_recovery_ms",ht1aP);t) out.config.autoreceptor_tau_recovery_ms_ht1a=*t;
            if(auto o=extractJsonNumber(c,"autoreceptor_exposure_offset_ms",ht1aP);o) out.config.autoreceptor_exposure_offset_ms_ht1a=*o;
        }
        const auto ht2aP=c.find("\"5HT2A\"",nmPos);
        if(ht2aP!=c.npos){
            if(auto n=extractJsonNumber(c,"ec50",ht2aP);n) out.config.ec50_ht2a=*n;
            if(auto h=extractJsonNumber(c,"hill",ht2aP);h) out.config.hill_ht2a=*h;
            if(auto m=extractJsonNumber(c,"max_k_reduction",ht2aP);m) out.config.max_k_reduction_ht2a=*m;
            if(auto m=extractJsonNumber(c,"max_adaptation_reduction",ht2aP);m) out.config.max_adaptation_reduction_ht2a=*m;
        }
        // Tier 2.2: alpha-2, both presynaptic autoreceptor and postsynaptic
        // PFC pathways built in the same pass -- same nested-object pattern
        // as D2/5-HT1A above.
        const auto a2P=c.find("\"Alpha2\"",nmPos);
        if(a2P!=c.npos){
            if(auto n=extractJsonNumber(c,"presynaptic_ec50",a2P);n) out.config.presynaptic_ec50_alpha2=*n;
            if(auto h=extractJsonNumber(c,"presynaptic_hill",a2P);h) out.config.presynaptic_hill_alpha2=*h;
            if(auto m=extractJsonNumber(c,"max_presynaptic_release_reduction",a2P);m) out.config.max_presynaptic_release_reduction_alpha2=*m;
            if(auto n=extractJsonNumber(c,"postsynaptic_ec50",a2P);n) out.config.postsynaptic_ec50_alpha2=*n;
            if(auto h=extractJsonNumber(c,"postsynaptic_hill",a2P);h) out.config.postsynaptic_hill_alpha2=*h;
            if(auto m=extractJsonNumber(c,"max_postsynaptic_adaptation_reduction",a2P);m) out.config.max_postsynaptic_adaptation_reduction_alpha2=*m;
        }
        // Tier 2.2 completion: beta and alpha-1, single-pathway each -- same
        // nested-object pattern as above. "max_adaptation_increase" (not
        // "_reduction") since both INCREASE adaptationScale -- see
        // NeuromodulatorSystem.h's BetaAction/Alpha1Action comments.
        const auto betaP=c.find("\"Beta\"",nmPos);
        if(betaP!=c.npos){
            if(auto n=extractJsonNumber(c,"ec50",betaP);n) out.config.ec50_beta=*n;
            if(auto h=extractJsonNumber(c,"hill",betaP);h) out.config.hill_beta=*h;
            if(auto m=extractJsonNumber(c,"max_adaptation_increase",betaP);m) out.config.max_adaptation_increase_beta=*m;
        }
        const auto a1P=c.find("\"Alpha1\"",nmPos);
        if(a1P!=c.npos){
            if(auto n=extractJsonNumber(c,"ec50",a1P);n) out.config.ec50_alpha1=*n;
            if(auto h=extractJsonNumber(c,"hill",a1P);h) out.config.hill_alpha1=*h;
            if(auto m=extractJsonNumber(c,"max_adaptation_increase",a1P);m) out.config.max_adaptation_increase_alpha1=*m;
        }
    }
    const auto drP=c.find("\"dose_range\"");
    if(drP!=c.npos){
        if(auto v=extractJsonNumber(c,"min",drP);v)  { out.config.dose=*v; outDoseMin=*v; }
        if(auto v=extractJsonNumber(c,"max",drP);v)  outDoseMax=*v;
        if(auto v=extractJsonNumber(c,"step",drP);v) outDoseStep=*v;
    }
    if(auto v=extractJsonNumber(c,"runs");v) outRuns=static_cast<int>(*v);
    if(auto v=extractJsonString(c,"mode");v) outMode=*v;

    // Phase 3b: top-level "desensitization" section, plus an optional
    // "sim_time_ms" override for long-duration desensitization runs. Both
    // deliberately opt-in (out.config.desensitization_enabled defaults to
    // false and sim_time is already set to --dose-eval's normal 500ms
    // default before this function runs) -- a drug config that doesn't
    // mention either key changes nothing.
    const auto dsPos=c.find("\"desensitization\"");
    if(dsPos!=c.npos){
        if(auto b=extractJsonBool(c,"enabled",dsPos);b) out.config.desensitization_enabled=*b;
        if(auto n=extractJsonNumber(c,"tau_desense_ms",dsPos);n) out.config.desensitization_tau_desense_ms=*n;
        if(auto n=extractJsonNumber(c,"tau_recovery_ms",dsPos);n) out.config.desensitization_tau_recovery_ms=*n;
        if(auto n=extractJsonNumber(c,"max_attenuation",dsPos);n) out.config.desensitization_max_attenuation=*n;
    }

    // Phase 3c: top-level "vesicle_pool" section, same opt-in discipline as
    // "desensitization" above (out.config.vesicle_pool_enabled defaults to
    // false; a drug config that never mentions this key changes nothing).
    // Depletion is observable at the normal ~400-500ms sim_time; recovery is
    // not (see NeurotransmitterPool.h) -- pair with the same "sim_time_ms"
    // override below for a long-duration recovery test.
    const auto vpPos=c.find("\"vesicle_pool\"");
    if(vpPos!=c.npos){
        if(auto b=extractJsonBool(c,"enabled",vpPos);b) out.config.vesicle_pool_enabled=*b;
        if(auto n=extractJsonNumber(c,"rrp_size",vpPos);n) out.config.vesicle_pool_rrp_size=*n;
        if(auto n=extractJsonNumber(c,"reserve_size",vpPos);n) out.config.vesicle_pool_reserve_size=*n;
        if(auto n=extractJsonNumber(c,"rrp_refill_tau_ms",vpPos);n) out.config.vesicle_pool_rrp_refill_tau_ms=*n;
        if(auto n=extractJsonNumber(c,"reserve_refill_tau_ms",vpPos);n) out.config.vesicle_pool_reserve_refill_tau_ms=*n;
        if(auto n=extractJsonNumber(c,"calcium_factor",vpPos);n) out.config.vesicle_pool_calcium_factor=*n;
    }

    if(auto v=extractJsonNumber(c,"sim_time_ms");v) out.config.sim_time=*v;

    return true;
}
// ─── Report text builder ──────────────────────────────────────────────────────
// buildDrugEvaluationReportText/writeDrugEvaluationReport moved to
// engine/report/LegacyLiabilityReport.cpp (spp::report namespace) -- see the
// using-declarations near the top of this file.

// ─── Single run report ────────────────────────────────────────────────────────
void printSingleRunReport(
    const RuntimeInput& input,
    const SimulationSummary& summary,
    const AnalyzedDose& analyzed)
{
    std::ostringstream out;
    out << "==================================================\n";
    out << "SILICON PATIENT - SINGLE DOSE SIMULATION REPORT\n";
    out << "==================================================\n";

    const auto aLine=[&](const std::string& label, const std::string& value){
        out << std::left << std::setw(19) << label << " : " << value << "\n";
    };

    out << "[Simulation Configuration]\n";
    aLine("Neurons",   std::to_string(input.config.neuron_count));
    aLine("Duration",  formatRuntimeNumber(input.config.sim_time) + " ms");
    aLine("Drug Dose", formatRuntimeNumber(input.config.dose));
    aLine("Mode",      input.config.use_cuda ? "CUDA" : "CPU");
    out << "\n---\n\n";

    out << "[Raw Metrics]\n";
    aLine("Firing Rate",     formatRuntimeNumber(summary.network_metrics.meanFiringRateHz) + " Hz");
    aLine("Sync Index",      formatRuntimeNumber(summary.network_metrics.synchronizationIndex));
    // Gap 1.3: "Burst Rate" removed here too -- same dead metric as the
    // dose-eval report (see PRECISION_GAP_CLOSURE_PLAN.md 1.3).
    aLine("Irregularity",    formatRuntimeNumber(summary.network_metrics.irregularityIndex));
    aLine("Silent Neurons",  formatRuntimeNumber(summary.network_metrics.silentNeuronPct) + " %");
    out << "\n--------------------------------------------------\n";

    out << "[Network Analysis]\n";
    aLine("Network State",   PharmaDecisionEngine::toString(analyzed.networkState));
    aLine("Mechanism",       PharmaDecisionEngine::toString(analyzed.mechanismSignature));
    aLine("NII",             formatRuntimeNumber(analyzed.nii));
    aLine("Seizure Prob",    formatRuntimeNumber(analyzed.seizureProbability * 100.0f) + " %");
    aLine("Rate Change",     formatRuntimeNumber(analyzed.rateChangePct) + " %");
    // Gap 1.3: "Burst Rate Δ" removed -- same dead metric.
    aLine("Sync Δ",          formatRuntimeNumber(analyzed.syncDelta));
    out << "--------------------------------------------------\n";

    out << "[FINAL DECISION]\n";
    const bool safe = (analyzed.networkState == NetworkState::Stable ||
                       analyzed.networkState == NetworkState::MildInstability);
    aLine("Recommendation", safe ? "PROCEED" : "NOT RECOMMENDED");
    aLine("Risk Level",     safe ? "LOW" : "HIGH");
    aLine("Confidence",     "MEDIUM");
    out << "\n==================================================\n";

    std::cout << out.str();
}

void exportSingleRunArtifacts(
    const RuntimeInput& input, const SimulationSummary& summary)
{
    if (!input.config.export_csv) return;
    std::filesystem::create_directories(input.config.output_folder);
    CsvWriter::writeNeuronStats(
        input.config.output_folder + "/neuron_stats.csv", summary.neuron_metrics);
    CsvWriter::writeNetworkMetrics(
        input.config.output_folder + "/network_metrics.csv", {
            NetworkMetricRecord{"single_run",
                static_cast<float>(input.config.dose),
                summary.network_metrics, ""}});
}

// ─── Single simulation mode ───────────────────────────────────────────────────
void runSingleSimulationMode(const RuntimeInput& input) {
    const std::uint32_t seed = makeSeed();
    const SimulationSummary summary = runSingleSimulation(input, seed);

    // Build a two-point observation series (dose=0 baseline + current dose)
    // so NetworkAnalyzer can compute deltas properly.
    std::vector<DoseObservation> observations;

    // Baseline
    RuntimeInput baselineInput = input;
    baselineInput.config.dose = 0.0;
    const SimulationSummary baselineSummary = runSingleSimulation(baselineInput, seed + 1U);
    observations.push_back(buildDoseObservation(baselineInput, baselineSummary.network_metrics));

    // Current dose
    observations.push_back(buildDoseObservation(input, summary.network_metrics));

    // Analyze
    const std::vector<AnalyzedDose> analyzed = NetworkAnalyzer::analyze(observations);
    const AnalyzedDose& currentAnalyzed = analyzed.size() > 1 ? analyzed[1] : analyzed[0];

    printSingleRunReport(input, summary, currentAnalyzed);
    exportSingleRunArtifacts(input, summary);
}

// ─── Dose evaluation mode ─────────────────────────────────────────────────────
// New pipeline order:
// 1. Run all doses → collect DoseObservation[]
// 2. NetworkAnalyzer::analyze() → AnalyzedDose[]
// 3. PharmaDecisionEngine::evaluate() → PharmaDecisionReport
void runDoseEvaluationMode(
    const RuntimeInput& baseInput,
    const std::string& engineInputMode = "Default Internal Engine Config",
    const std::optional<int>& userRuns = std::nullopt,
    const std::optional<double>& jsonDoseMin = std::nullopt,
    const std::optional<double>& jsonDoseMax = std::nullopt,
    const std::optional<double>& jsonDoseStep = std::nullopt)
{
    constexpr double kDefaultMinDose  = 0.0;
    constexpr double kDefaultMaxDose  = 20.0;
    constexpr double kDefaultStepDose = 2.0;
    constexpr int    kDoseEvalRuns    = 10;

    auto tryEnvDouble=[](const char* n)->std::optional<double>{
        if(auto t=readEnvVar(n)){try{return std::stod(trim(*t));}catch(...){}return std::nullopt;}
        return std::nullopt;
    };

    RuntimeInput evalInput = baseInput;
    const bool skipEnv = (engineInputMode == "User Drug Config");
    if (!skipEnv) {
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_IC50_NA");v&&*v>0) evalInput.config.ic50_na=*v;
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_IC50_K"); v&&*v>0) evalInput.config.ic50_k=*v;
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_IC50_CA");v&&*v>0) evalInput.config.ic50_ca=*v;
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_HILL");   v&&*v>=1&&*v<=6) evalInput.config.hill=*v;

        // PHASE2_PLAN.md step 4: receptor pharmacology overrides, same
        // real-hardware-smoke-test purpose as the channel IC50 overrides
        // above -- lets a single drug's receptor action be tested via
        // SPP_FORCE_CPU=1 --dose-eval without needing a JSON config file.
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_EC50_AMPA");  v&&*v>0) evalInput.config.ic50_ampa=*v;
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_HILL_AMPA");  v&&*v>=1&&*v<=6) evalInput.config.hill_ampa=*v;
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_EC50_NMDA");  v&&*v>0) evalInput.config.ic50_nmda=*v;
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_HILL_NMDA");  v&&*v>=1&&*v<=6) evalInput.config.hill_nmda=*v;
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_EC50_GABAA"); v&&*v>0) evalInput.config.ec50_gabaA=*v;
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_HILL_GABAA"); v&&*v>=1&&*v<=6) evalInput.config.hill_gabaA=*v;
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_MAX_POTENTIATION_GABAA"); v&&*v>=1.0) evalInput.config.max_potentiation_gabaA=*v;
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_EC50_GABAB"); v&&*v>0) evalInput.config.ec50_gabaB=*v;
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_HILL_GABAB"); v&&*v>=1&&*v<=6) evalInput.config.hill_gabaB=*v;
    }
    // Diagnostic overrides for baseline-dynamics tuning -- unconditional
    // (unlike the drug-parameter overrides above) since dt/external_current
    // affect network dynamics regardless of which drug config is loaded.
    if(auto v=tryEnvDouble("SPP_DOSE_EVAL_DT");                v&&*v>0) evalInput.config.dt = *v;
    if(auto v=tryEnvDouble("SPP_DOSE_EVAL_EXTERNAL_CURRENT");  v)       evalInput.config.external_current = *v;

    double minD  = jsonDoseMin.value_or(kDefaultMinDose);
    double maxD  = jsonDoseMax.value_or(kDefaultMaxDose);
    double stepD = jsonDoseStep.value_or(kDefaultStepDose);
    if(auto v=tryEnvDouble("SPP_DOSE_EVAL_MIN"); v&&*v>=0)   minD=*v;
    if(auto v=tryEnvDouble("SPP_DOSE_EVAL_MAX"); v&&*v>minD) maxD=*v;
    if(auto v=tryEnvDouble("SPP_DOSE_EVAL_STEP");v&&*v>0)    stepD=*v;

    int runs = userRuns.value_or(kDoseEvalRuns);
    if(auto t=readEnvVar("SPP_DOSE_EVAL_RUNS")){
        try{const int p=std::stoi(trim(*t));if(p>0&&p<=200)runs=p;}catch(...){}
    }

    std::vector<double> doses;
    for(double d=minD; d<=maxD+1e-9; d+=stepD) doses.push_back(d);

    // ── STEP 1: Collect DoseObservation[] ────────────────────────────────────
    std::vector<DoseObservation> observations;
    std::vector<DoseResponsePoint> doseResponse;
    std::vector<NetworkMetricRecord> networkRecords;
    std::vector<RunResult> allRunResults;
    observations.reserve(doses.size());
    doseResponse.reserve(doses.size());
    networkRecords.reserve(doses.size());

    std::vector<spp::analyzer::NeuronMetrics> finalNM;
    const std::uint32_t baseSeed = 0xD05E0001U;

    // One batched call covers every (dose x repeat) combination in a single
    // shared timestep loop, instead of looping runMultipleSimulations per dose.
    bool usedGpu = false;
    const auto perDoseResults = runAllDosesBatched(evalInput, doses, runs, baseSeed, &finalNM, &usedGpu);

    // Diagnostic addition (2026-08-08, Tier 2.4 investigation): one row per
    // dose with the excitatory/inhibitory mean rate split, so this can be
    // read straight off a SINGLE sweep run instead of manually diffing
    // separate re-runs (which is what caused confusion earlier this session
    // -- see PRECISION_GAP_CLOSURE_PLAN.md 2.4's beta/alpha-1 write-up).
    // Diagnostic addition (2026-08-08, Tier 2.4 rhythm-hypothesis test):
    // ISI mean/variance split by type added alongside rate -- see
    // AggregatedStats::meanExcitatoryIsiMeanMs/etc.
    struct CellTypeRateRow {
        float dose;
        float excitatoryRateHz; float inhibitoryRateHz;
        float excitatoryIsiMeanMs; float inhibitoryIsiMeanMs;
        float excitatoryIsiVarMs; float inhibitoryIsiVarMs;
    };
    std::vector<CellTypeRateRow> cellTypeRates;
    cellTypeRates.reserve(doses.size());

    for(std::size_t i=0; i<doses.size(); ++i) {
        const auto& rr = perDoseResults[i];
        if(rr.empty()) continue;

        const AggregatedStats ds = computeStats(rr);
        allRunResults.insert(allRunResults.end(), rr.begin(), rr.end());
        cellTypeRates.push_back({
            static_cast<float>(doses[i]), ds.meanExcitatoryRateHz, ds.meanInhibitoryRateHz,
            ds.meanExcitatoryIsiMeanMs, ds.meanInhibitoryIsiMeanMs,
            ds.meanExcitatoryIsiVarMs, ds.meanInhibitoryIsiVarMs});

        // Build aggregated NetworkMetrics from raw multi-run means
        const NetworkMetrics am = buildAggregatedNetworkMetrics(ds);

        // Build DoseObservation — pure collection, no analysis
        RuntimeInput doseInput = evalInput;
        doseInput.config.dose  = doses[i];
        observations.push_back(buildDoseObservation(doseInput, am));

        const std::string lbl = getStability(ds.stdRate, ds.stdSync);
        doseResponse.push_back({
            static_cast<float>(doses[i]),
            ds.meanRate, ds.meanSync, ds.meanBurst,
            0.0f, 0.0f, 0.0f,
            am.stabilityScore, lbl});
        networkRecords.push_back({"dose_eval", static_cast<float>(doses[i]), am, lbl});
    }

    if(observations.empty())
        throw std::runtime_error("Dose evaluation failed: no observations produced");

    // ── STEP 2: NetworkAnalyzer::analyze() ───────────────────────────────────
    // This is the key new step. Takes DoseObservation[], returns AnalyzedDose[].
    // dose[0] is baseline automatically.
    const std::vector<AnalyzedDose> analyzedDoses = NetworkAnalyzer::analyze(observations);

    if(analyzedDoses.empty())
        throw std::runtime_error("NetworkAnalyzer produced no results");

    // ── STEP 3: PharmaDecisionEngine::evaluate() ─────────────────────────────
    const AggregatedStats stabilityStats = computeStats(allRunResults);
    const spp::analyzer::DecisionStabilityInput si{
        stabilityStats.stdRate,
        stabilityStats.stdSync,
        getStability(stabilityStats.stdRate, stabilityStats.stdSync),
        runs};

    const PharmaDecisionReport report =
        PharmaDecisionEngine::evaluate(analyzedDoses, si);

    // ── Print and export ──────────────────────────────────────────────────────
    // Gap 10 (PRECISION_GAP_CLOSURE_PLAN.md 1.4): the legacy verdict-style
    // report (buildDrugEvaluationReportText/writeDrugEvaluationReport, the
    // "SILICON PATIENT - DRUG EVALUATION REPORT" format) has been retired
    // from stdout and CSV export as of the Gap 1.1/1.2 audit completing
    // across Phases 1-3. It was kept side-by-side with the new evidence
    // format specifically so the two could be diffed during that audit;
    // that audit is now done and the new format is the only one printed.
    // LegacyLiabilityReport.cpp/.h are left in the tree (not deleted) in
    // case a future diff is ever needed again, but are no longer called
    // from the normal dose-eval path below.
    std::cout << buildLiabilityReportText(
        report, stabilityStats, runs, evalInput, engineInputMode, usedGpu);

    if(evalInput.config.export_csv) {
        std::filesystem::create_directories(evalInput.config.output_folder);
        CsvWriter::writeDoseResponse(
            evalInput.config.output_folder + "/dose_response.csv", doseResponse);
        CsvWriter::writeNetworkMetrics(
            evalInput.config.output_folder + "/network_metrics.csv", networkRecords);
        CsvWriter::writeNeuronStats(
            evalInput.config.output_folder + "/neuron_stats.csv", finalNM);

        // Diagnostic addition (2026-08-08, Tier 2.4 investigation): see
        // cellTypeRates construction above. Written directly here rather
        // than through CsvWriter since it's a standalone diagnostic export,
        // not part of the stable report schema other code depends on.
        {
            std::ofstream ctOut(evalInput.config.output_folder + "/celltype_rates.csv",
                                 std::ios::out | std::ios::trunc);
            if (ctOut.is_open()) {
                ctOut << "dose,excitatory_rate_hz,inhibitory_rate_hz,"
                         "excitatory_isi_mean_ms,inhibitory_isi_mean_ms,"
                         "excitatory_isi_var_ms,inhibitory_isi_var_ms\n";
                ctOut << std::fixed << std::setprecision(6);
                for (const auto& row : cellTypeRates) {
                    ctOut << row.dose << ',' << row.excitatoryRateHz << ',' << row.inhibitoryRateHz << ','
                          << row.excitatoryIsiMeanMs << ',' << row.inhibitoryIsiMeanMs << ','
                          << row.excitatoryIsiVarMs << ',' << row.inhibitoryIsiVarMs << '\n';
                }
            }
        }

        writeLiabilityReport(
            evalInput.config.output_folder + "/liability_screening_report.txt",
            report, stabilityStats, runs, evalInput, engineInputMode, usedGpu);
    }
}

void printValidationCheck(const ValidationCheck& check) {
    printSection(check.name);
    if (!check.details.empty()) {
        std::cout << check.details;
        if (check.details.back()!='\n') std::cout<<'\n';
    }
    printMetricLine("Status", check.pass ? "PASS" : "FAIL");
    std::cout<<'\n';
}

} // namespace

// ─── main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    try {
        if (argc < 2) { printHelp(); return 0; }

        const std::string mode = argv[1];
        std::string drugConfigPath;
        for(int i=2; i<argc; ++i) {
            const std::string arg = argv[i];
            if(arg == "--drug-config" && (i+1) < argc) {
                drugConfigPath = argv[i+1]; ++i;
            }
        }

        if(mode == "--simulate") {
            RuntimeInput input;
            input.run_dose_sweep = false;
            // GABA-A's true conductance term isn't visible to the CUDA
            // kernel yet (see SimulationEngine.cpp comment at the GPU call
            // site) -- SPP_FORCE_CPU=1 forces the CPU path so real-hardware
            // verification actually exercises the new receptor code.
            if (auto v = readEnvVar("SPP_FORCE_CPU")) {
                if (trim(*v) == "1") input.config.use_cuda = false;
            }
            validateConfig(input.config);
            runSingleSimulationMode(input);
            return 0;
        }

        if(mode == "--dose-eval") {
            RuntimeInput input;
            input.run_dose_sweep       = false;
            input.config.output_folder = "output_pharma_decision";
            // Same GABA-A/GPU gap as --simulate -- see comment there. This
            // matters even more here: --dose-eval's GPU path is a separate
            // fully GPU-resident kernel (stepBatched) that has never
            // reflected ANY receptor addition, lighter or true-conductance.
            if (auto v = readEnvVar("SPP_FORCE_CPU")) {
                if (trim(*v) == "1") input.config.use_cuda = false;
            }
#ifdef SPP_USE_CUDA
            input.config.neuron_count = 1420;
#else
            input.config.neuron_count = 1320;
#endif
            input.config.sim_time               = 500.0;
            input.config.dt                     = 0.04;
            input.config.connectivity           = 0.05;
            input.config.excitatory_ratio       = 0.80;
            // --dose-eval has its own dt/connectivity/noise regime and is
            // tuned separately from the SimulationConfig default above.
            //
            // STALE CALIBRATION FOUND AND FIXED (PHASE2_PLAN.md step 4
            // validation): the I=2.0 operating point below was measured
            // against the pre-Phase-2 flat synaptic model and never
            // re-verified after GABA-A/NMDA/GABA-B/AMPA were converted to
            // true conductances this session. Confirmed on real hardware
            // (SPP_FORCE_CPU=1 --dose-eval, dose=0 no-drug control,
            // neuron_stats.csv): at I=2.0 every single neuron fired exactly
            // once (spike_count=1, isi_variance=0 for all 1320 neurons) and
            // stayed silent for the rest of the run -- the same "fires once
            // then permanently silent" collapse chased down earlier in
            // NeuronModel.h/SimulationEngine.h, just in this engine's
            // separate baseline. I=6.6 (the value already calibrated for
            // --simulate's regime, see SimulationConfig::external_current
            // above) fixes it here too: dose=0 control now gives a sustained
            // 16 Hz with a smooth, non-degenerate dose-response curve above
            // it, confirmed on real hardware. The I=1/2/4/12 measurements
            // below are pre-Phase-2 history, kept only as a record of what
            // used to be true -- do not treat them as current operating
            // guidance. Recurrent synaptic drive amplifies well above the
            // single-neuron rate, which is why this is much smaller than it
            // looks like it should be from single-cell characterisation
            // alone -- that part of the reasoning still holds, just not the
            // specific I=2.0 number.
            //   [pre-Phase-2] I=1 -> 3.7 Hz (23% of neurons still sub-threshold)
            //   [pre-Phase-2] I=2 -> 15.6 Hz, 0% collapsed
            //   [pre-Phase-2] I=4 -> 19.9 Hz,  I=12 -> 26.6 Hz
            input.config.external_current       = 6.6;
            input.config.noise_level            = 0.72;
            input.config.excitatory_weight_scale = 1.00;
            input.config.inhibitory_weight_scale = 1.00;
            input.config.ic50_na = 200.0;
            input.config.ic50_k  = 8.0;
            input.config.ic50_ca = 1000.0;
            input.config.hill    = 3.2;

            std::string engineMode = "Default Internal Engine Config";
            std::optional<int> userRuns;
            std::optional<double> jsonDoseMin, jsonDoseMax, jsonDoseStep;
            if(!drugConfigPath.empty()) {
                // BUG FIX (found auditing test_amlodipine.json): input.config.hill
                // was left at 3.2 from the "Default Internal Engine Config" preset
                // above. loadDrugConfigFromJsonFile only overwrites config.hill if
                // a channel's JSON block explicitly includes its own "hill" key
                // (see the naP/kP/caP extraction inside that function) -- any user
                // drug config that omits "hill" entirely (legal, and the documented
                // default per ChannelDrugProfile::hillNa/hillK/hillCa is 1.0) was
                // silently getting hill=3.2 instead, with no warning and a report
                // that looked plausible rather than obviously broken. Every
                // channel-blocker config audited earlier this session happened to
                // set "hill":1.0 explicitly, which is why this was never caught
                // until a config that omits it was tested. Reset to the correct
                // documented default here, right before parsing, so an omitted
                // "hill" in the JSON falls back to 1.0 as intended instead of
                // inheriting an unrelated demo preset value.
                input.config.hill = 1.0;
                std::optional<int> ro; std::string mt;
                if(loadDrugConfigFromJsonFile(drugConfigPath, input, ro, mt, jsonDoseMin, jsonDoseMax, jsonDoseStep)) {
                    engineMode = "User Drug Config";
                    if(ro) userRuns = *ro;
                } else {
                    // BUG FIX: an explicitly-requested --drug-config file that
                    // doesn't exist (or can't be read) used to silently fall
                    // through to the hardcoded demo config below (engineMode
                    // stayed "Default Internal Engine Config", no warning at
                    // all) -- found when a typo'd filename
                    // (test_vesicle_control.json instead of
                    // test_vesicle_pool_control.json) silently ran an
                    // unrelated full 11-dose/10-run demo sweep for 13 minutes
                    // instead of erroring immediately. The user EXPLICITLY
                    // named a file; silently substituting a different drug
                    // is never the right behavior, so this is now a hard
                    // error instead.
                    throw std::runtime_error(
                        "Could not read drug config file: \"" + drugConfigPath +
                        "\" -- check the path/filename (this is a hard error, "
                        "not falling back to the default demo config).");
                }
            }
            validateConfig(input.config);
            runDoseEvaluationMode(input, engineMode, userRuns, jsonDoseMin, jsonDoseMax, jsonDoseStep);
            return 0;
        }

        printHelp();
        return 0;

    } catch(const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}