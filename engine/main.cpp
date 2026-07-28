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

namespace {

using spp::analyzer::MetricsAnalyzer;
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

struct SimulationConfig {
    int neuron_count    = 1500;
    double sim_time     = 400.0;
    double dt           = 0.01;
    double dose         = 10.0;
    double ic50_na      = 50.0;
    double ic50_k       = 50.0;
    double ic50_ca      = 120.0;
    double hill         = 3.0;
    double connectivity = 0.10;
    double excitatory_ratio     = 0.8;
    // NOTE (historical): the "fires once then goes silent" bug from an
    // earlier session was NOT a drive problem -- it was calcium-driven
    // depolarization block (see the gCa comment in neuron/NeuronModel.h),
    // fixed by dropping gCa from 8 to 0.5. That fix is still correct and
    // still in place.
    //
    // UPDATE (this session): a *different* collapse resurfaced at
    // external_current=2.5 after the Phase 2 receptor work landed -- every
    // neuron fired exactly once (from the shared initial-condition burst)
    // then went permanently silent for the rest of the run (early_rate_hz
    // 5.0, late_rate_hz 0.0 exactly, at neuron_count=1500/400ms). Bisected
    // by individually zeroing every Phase 2 addition (gMaxGABAa,
    // gabaBFraction, ampaFraction, nmdaFraction), the adaptation current,
    // and even tripling recurrent excitatory weight strength -- none of it
    // changed the outcome at all. Only external_current mattered:
    //   2.5  -> dead (0 Hz after the first volley)
    //   5.0  -> dead (still 0 Hz, no change at all)
    //   6.6  -> 12.5 Hz, healthy (early 10.1 -> late 15.3, ramping up, no
    //           collapse) -- lands right at the ~12 Hz this value always
    //           targeted, so this is the new default.
    //   7.0  -> 17.5 Hz, still healthy but past target
    //   10.0 -> 41.3 Hz, overshooting
    //   20.0 -> 87.8 Hz, badly overshooting
    // Root cause not fully identified (why 2.5 stopped being enough is
    // still open), but the fix is confirmed: 6.6 restores the documented
    // ~9.8-12 Hz baseline with 0% silent and no collapse across the full
    // 400ms run. If this needs re-tuning later, re-run this same sweep
    // rather than assuming 2.5 (or any old value) still works.
    double external_current     = 6.6;
    double noise_level          = 0.35;
    double excitatory_weight_scale = 1.0;
    double inhibitory_weight_scale = 1.0;

    // PHASE2_PLAN.md step 4: receptor pharmacology (block/potentiate/
    // agonist), one config surface per receptor, same generic IC50/Hill-
    // style pattern as ic50_na/k/ca above. Defaults are deliberately inert
    // (huge ec50 / no-op ceiling), matching ReceptorDrugProfile.h's own
    // stated default policy -- an unconfigured run has zero receptor drug
    // effect, same as before this step existed. Mechanism per receptor is
    // fixed by receptor identity (AMPA/NMDA always Block, GABA-A always
    // Potentiate, GABA-B always Agonist) rather than user-selectable, since
    // that's what the Phase 2 validation drug set (§5) actually needs --
    // see ReceptorDrugProfile.h for why these three mechanism families
    // exist and what a mismatched mechanism/receptor pairing would mean.
    double ic50_ampa              = 1.0e9;
    double hill_ampa              = 1.0;
    double ic50_nmda              = 1.0e9;
    double hill_nmda              = 1.0;
    double ec50_gabaA             = 1.0e9;
    double hill_gabaA             = 1.0;
    double max_potentiation_gabaA = 1.0;
    double ec50_gabaB             = 1.0e9;
    double hill_gabaB             = 1.0;

    // Phase 3a, step 1 of the drug set (GAT1/tiagabine only -- see
    // ReuptakeTransporter.h design note): a transporter-blocking drug
    // extends a receptor's decay time constant instead of touching its
    // gMax/occupancy. GAT1 block is fixed Competitive by transporter
    // identity (tiagabine's real mechanism), same "mechanism fixed by
    // identity, JSON only supplies numbers" pattern as buildReceptorProfile
    // above. Defaults are inert (huge Ki, no extension ceiling), so an
    // unconfigured run is a bit-identical no-op, same policy as every other
    // receptor field here.
    double ki_gat1            = 1.0e9;
    double hill_gat1          = 1.0;
    double max_extension_gat1 = 1.0;

    // Phase 3a, remaining 3 drugs (SSRI/cocaine/reboxetine): SERT/DAT/NET
    // reuptake block. Serotonin/dopamine/norepinephrine have NO receptor
    // current in this engine (that's Phase 3c's neuromodulator gain
    // system, not built yet), so these three are REPORT-ONLY -- they
    // produce real, literature-sourced occupancy/clearance-fold numbers in
    // the Neurotransmitter Profile section, but deliberately no network/
    // firing-rate effect, since there's no receptor for them to act on.
    // Not wired into DoseObservation/AnalyzedDose/detectMechanism at all
    // (unlike GAT1) -- they have no observable dynamics to attribute a
    // mechanism signature to.
    double ki_sert            = 1.0e9;
    double hill_sert          = 1.0;
    double max_extension_sert = 1.0;
    double ki_dat             = 1.0e9;
    double hill_dat           = 1.0;
    double max_extension_dat  = 1.0;
    double ki_net             = 1.0e9;
    double hill_net           = 1.0;
    double max_extension_net  = 1.0;

    // Phase 3b: GABA-A desensitization ("receptor tiredness"). OFF by
    // default -- see SimulationConfig::desensitizationEnabled in
    // simulation/SimulationEngine.h for why this is safe to leave off for
    // every existing (short-duration) drug config. sim_time above already
    // exists as the run-duration knob -- a 3b desensitization test simply
    // sets both desensitization_enabled and a much larger sim_time (tens of
    // seconds, in ms) in the same JSON file.
    bool desensitization_enabled          = false;
    double desensitization_tau_desense_ms  = 30000.0;
    double desensitization_tau_recovery_ms = 124000.0;
    double desensitization_max_attenuation = 0.9;

    // Phase 3c: neuromodulator gain system (D1/D2/5-HT1A/5-HT2A), see
    // engine/synapse/NeuromodulatorSystem.h for the full design/literature
    // basis. Defaults are inert (ec50 huge, gain ceilings inert), matching
    // every other Phase 1/2/3 mechanism's "unconfigured = bit-identical
    // no-op" policy.
    double ec50_d1                  = 1.0e9;
    double hill_d1                  = 1.0;
    double max_adaptation_reduction_d1 = 0.0; // 0..1 fraction
    double max_nmda_gain_d1         = 1.0;    // fold, >=1

    double ec50_d2                  = 1.0e9;
    double hill_d2                  = 1.0;
    double max_release_reduction_d2 = 0.0;    // 0..1 fraction

    double ec50_ht1a                = 1.0e9;
    double hill_ht1a                = 1.0;
    double max_k_gain_ht1a          = 1.0;    // fold, >=1

    double ec50_ht2a                = 1.0e9;
    double hill_ht2a                = 1.0;
    double max_k_reduction_ht2a          = 0.0; // 0..1 fraction
    double max_adaptation_reduction_ht2a = 0.0; // 0..1 fraction

    bool use_cuda    = true;
    bool export_csv  = true;
    std::string output_folder = "output_data";
};

struct RuntimeInput {
    SimulationConfig config;
    std::string drug_name = "GenericCompound";
    bool run_dose_sweep   = false;
    double sweep_start    = 0.0;
    double sweep_end      = 100.0;
    int sweep_points      = 10;
};

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
    float earlyWindowRateHz   = 0.0f;
    float lateWindowRateHz    = 0.0f;
    float firstThirdRateHz    = 0.0f;
    float middleThirdRateHz   = 0.0f;
    float lastThirdRateHz     = 0.0f;
};

struct AggregatedStats {
    float meanRate      = 0.0f; float stdRate      = 0.0f;
    float meanSync      = 0.0f; float stdSync      = 0.0f;
    float meanBurst     = 0.0f; float stdBurst     = 0.0f;
    float meanBurstRate = 0.0f;
    float meanISI       = 0.0f; float stdISI       = 0.0f;
    float meanPopVar    = 0.0f;
    float stdRate2      = 0.0f;
    float meanPeakSync            = 0.0f;
    float meanBurstingNeuronPct   = 0.0f;
    float meanBurstDurationMs     = 0.0f;
    float meanFiringRateStdHz     = 0.0f;
    float meanSilentNeuronPct     = 0.0f;
    float meanEarlyWindowRateHz   = 0.0f;
    float meanLateWindowRateHz    = 0.0f;
    float meanFirstThirdRateHz    = 0.0f;
    float meanMiddleThirdRateHz   = 0.0f;
    float meanLastThirdRateHz     = 0.0f;
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
constexpr int kRuntimeOutputPrecision = 2;
constexpr int kRuntimeDividerWidth    = 50;
constexpr int kRuntimeLabelWidth      = 22;

std::string formatRuntimeNumber(double value, int precision = kRuntimeOutputPrecision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

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

    profile.neuromod.ht1a.ec50 = static_cast<float>(cfg.ec50_ht1a);
    profile.neuromod.ht1a.hill = static_cast<float>(cfg.hill_ht1a);
    profile.neuromod.ht1a.maxKGainFold = static_cast<float>(cfg.max_k_gain_ht1a);

    profile.neuromod.ht2a.ec50 = static_cast<float>(cfg.ec50_ht2a);
    profile.neuromod.ht2a.hill = static_cast<float>(cfg.hill_ht2a);
    profile.neuromod.ht2a.maxKReductionFrac = static_cast<float>(cfg.max_k_reduction_ht2a);
    profile.neuromod.ht2a.maxAdaptationReductionFrac = static_cast<float>(cfg.max_adaptation_reduction_ht2a);

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
    std::vector<float> peakSyncs, burstingPcts, burstDurs, rateStds, silentPcts, earlyRates, lateRates;
    std::vector<float> firstThirdRates, middleThirdRates, lastThirdRates;
    rates.reserve(results.size());      syncs.reserve(results.size());
    bursts.reserve(results.size());     isis.reserve(results.size());
    burstRates.reserve(results.size());
    peakSyncs.reserve(results.size());  burstingPcts.reserve(results.size());
    burstDurs.reserve(results.size());  rateStds.reserve(results.size());
    silentPcts.reserve(results.size()); earlyRates.reserve(results.size());
    lateRates.reserve(results.size());
    firstThirdRates.reserve(results.size());
    middleThirdRates.reserve(results.size());
    lastThirdRates.reserve(results.size());

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
        earlyRates.push_back(r.earlyWindowRateHz);
        lateRates.push_back(r.lateWindowRateHz);
        firstThirdRates.push_back(r.firstThirdRateHz);
        middleThirdRates.push_back(r.middleThirdRateHz);
        lastThirdRates.push_back(r.lastThirdRateHz);
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
    stats.meanEarlyWindowRateHz = computeMean(earlyRates);
    stats.meanLateWindowRateHz  = computeMean(lateRates);
    stats.meanFirstThirdRateHz  = computeMean(firstThirdRates);
    stats.meanMiddleThirdRateHz = computeMean(middleThirdRates);
    stats.meanLastThirdRateHz   = computeMean(lastThirdRates);

    return stats;
}

std::string getStability(float stdRate, float stdSync) {
    if (stdRate<1.0f&&stdSync<0.05f) return "HIGH";
    if (stdRate<2.0f&&stdSync<0.10f) return "MEDIUM";
    return "LOW";
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
// Each block gets its own independently-generated network topology (a
// fresh Network::buildRandom() with its own seed) so that repeats are true
// biological replicates, not the same network measured three times with
// only the noise stream varying.
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
    const spp::simulation::SimulationConfig engineCfg = buildEngineConfig(evalInput.config, baseSeed);

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
            // Same seed formula as the old per-run loop (baseSeed + dose
            // offset + run offset), so a given (dose, repeat) pair gets the
            // same network seed it would have under the old sequential path.
            const std::uint32_t seed = baseSeed
                + static_cast<std::uint32_t>(d * 7919U)
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
            rr.earlyWindowRateHz   = netm.earlyWindowRateHz;
            rr.lateWindowRateHz    = netm.lateWindowRateHz;
            rr.firstThirdRateHz    = netm.firstThirdRateHz;
            rr.middleThirdRateHz   = netm.middleThirdRateHz;
            rr.lastThirdRateHz     = netm.lastThirdRateHz;
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
    // gat1ReuptakeBlock above. Uses dose directly against each receptor's
    // own EC50/hill (see spp::synapse::neuromodulatorOccupancy), mirroring
    // the calculation used for the [Neuromodulator Gain Profile] report
    // section.
    o.d1Gain = spp::synapse::neuromodulatorOccupancy(
        dF, static_cast<float>(input.config.ec50_d1), static_cast<float>(input.config.hill_d1));
    o.d2Gain = spp::synapse::neuromodulatorOccupancy(
        dF, static_cast<float>(input.config.ec50_d2), static_cast<float>(input.config.hill_d2));
    o.ht1aGain = spp::synapse::neuromodulatorOccupancy(
        dF, static_cast<float>(input.config.ec50_ht1a), static_cast<float>(input.config.hill_ht1a));
    o.ht2aGain = spp::synapse::neuromodulatorOccupancy(
        dF, static_cast<float>(input.config.ec50_ht2a), static_cast<float>(input.config.hill_ht2a));
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
        }
        const auto ht1aP=c.find("\"5HT1A\"",nmPos);
        if(ht1aP!=c.npos){
            if(auto n=extractJsonNumber(c,"ec50",ht1aP);n) out.config.ec50_ht1a=*n;
            if(auto h=extractJsonNumber(c,"hill",ht1aP);h) out.config.hill_ht1a=*h;
            if(auto m=extractJsonNumber(c,"max_k_gain",ht1aP);m) out.config.max_k_gain_ht1a=*m;
        }
        const auto ht2aP=c.find("\"5HT2A\"",nmPos);
        if(ht2aP!=c.npos){
            if(auto n=extractJsonNumber(c,"ec50",ht2aP);n) out.config.ec50_ht2a=*n;
            if(auto h=extractJsonNumber(c,"hill",ht2aP);h) out.config.hill_ht2a=*h;
            if(auto m=extractJsonNumber(c,"max_k_reduction",ht2aP);m) out.config.max_k_reduction_ht2a=*m;
            if(auto m=extractJsonNumber(c,"max_adaptation_reduction",ht2aP);m) out.config.max_adaptation_reduction_ht2a=*m;
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
    if(auto v=extractJsonNumber(c,"sim_time_ms");v) out.config.sim_time=*v;

    return true;
}
// ─── Report text builder ──────────────────────────────────────────────────────
std::string buildDrugEvaluationReportText(
    const PharmaDecisionReport& report,
    const AggregatedStats& stabilityStats,
    int runCount,
    const RuntimeInput& evalInput,
    const std::string& engineInputMode,
    std::optional<bool> usedGpu = std::nullopt)
{
    std::ostringstream out;
    const auto aLine=[&](const std::string& label, const std::string& value){
        out << std::left << std::setw(20) << label << " : " << value << "\n";
    };
    const auto fRange=[&](double a, double b){
        return formatRuntimeNumber(a) + " - " + formatRuntimeNumber(b);
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
                // BUG FIX (diazepam validation, Phase 2 receptor drugs):
                // this was suppressionScore>0.20f || stabilizationScore>0.20f
                // -- a SEPARATE, independently-hardcoded copy of the same
                // "is this dose therapeutically effective" threshold
                // PharmaDecisionEngine.cpp already computes (isEffective,
                // used for report.hasTherapeuticWindow/windowQuality/the
                // FINAL DECISION verdict), which uses suppressionScore>0.15f
                // || stabilizationScore>0.10f. The two thresholds had
                // silently drifted apart, so a dose landing between 0.15 and
                // 0.20 suppressionScore (diazepam's real peak, ~0.18, given
                // its literature EC50/ceiling) got counted as "therapeutic"
                // by PharmaDecisionEngine (Window Quality: Continuous,
                // Recommendation: PROMISING) but "ineffective" by this local
                // duplicate (Effective Range / Therapeutic Zone: Not
                // observed) -- the exact contradiction seen in the report.
                // Fixed by matching PharmaDecisionEngine.cpp's thresholds
                // exactly rather than keeping two independent copies that
                // can drift again; ideally this loop would read an
                // isEffective flag PharmaDecisionEngine already computed per
                // dose instead of re-deriving it, but that's a larger
                // refactor than this fix warrants.
                if (dose.suppressionScore > 0.15f || dose.stabilizationScore > 0.10f)
                    therapeuticDoses.push_back(static_cast<double>(dose.dose));
                else
                    ineffDoses.push_back(static_cast<double>(dose.dose));
                break;
            default:
                ineffDoses.push_back(static_cast<double>(dose.dose));
                break;
        }
    }

    auto formatDoses=[&](const std::vector<double>& doses) -> std::string {
        if (doses.empty()) return "Not observed";
        std::ostringstream t;
        for (std::size_t i=0;i<doses.size();++i){
            if (i) t << ", ";
            t << formatRuntimeNumber(doses[i]);
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
    const std::string peakDoseStr = report.analyzedDoses.empty()
        ? "Not observed"
        : "~" + formatRuntimeNumber(report.analyzedDoses[peakIdx].dose);
    const std::string onsetDoseStr = report.hasEffectiveDose
        ? "~" + formatRuntimeNumber(report.effectiveMinDose) : "Not observed";
    const std::string saturationStr = (peakIdx+1 >= report.analyzedDoses.size())
        ? "Not observed within tested range"
        : "Observed beyond " + formatRuntimeNumber(report.analyzedDoses[peakIdx].dose);
    const std::string effRange = noResp || therapeuticDoses.empty()
        ? "Not observed"
        : fRange(therapeuticDoses.front(), therapeuticDoses.back());

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
    if (evalInput.config.ic50_ampa < kReceptorInertThreshold) {
        aLine("AMPA EC50 (Block)",  formatRuntimeNumber(evalInput.config.ic50_ampa));
        aLine("AMPA Hill",          formatRuntimeNumber(evalInput.config.hill_ampa));
    }
    if (evalInput.config.ic50_nmda < kReceptorInertThreshold) {
        aLine("NMDA EC50 (Block)",  formatRuntimeNumber(evalInput.config.ic50_nmda));
        aLine("NMDA Hill",          formatRuntimeNumber(evalInput.config.hill_nmda));
    }
    if (evalInput.config.ec50_gabaA < kReceptorInertThreshold) {
        aLine("GABA-A EC50 (Potentiate)", formatRuntimeNumber(evalInput.config.ec50_gabaA));
        aLine("GABA-A Hill",              formatRuntimeNumber(evalInput.config.hill_gabaA));
        aLine("GABA-A Max Potentiation",  formatRuntimeNumber(evalInput.config.max_potentiation_gabaA));
    }
    if (evalInput.config.ec50_gabaB < kReceptorInertThreshold) {
        aLine("GABA-B EC50 (Agonist)", formatRuntimeNumber(evalInput.config.ec50_gabaB));
        aLine("GABA-B Hill",           formatRuntimeNumber(evalInput.config.hill_gabaB));
    }
    // Phase 3a: GAT1 reuptake block -- extends GABA-A/GABA-B decay tau
    // instead of touching their conductance/occupancy, so it's reported
    // separately from the receptor lines above rather than folded in.
    if (evalInput.config.ki_gat1 < kReceptorInertThreshold) {
        aLine("GAT1 Ki (Reuptake Block)", formatRuntimeNumber(evalInput.config.ki_gat1));
        aLine("GAT1 Hill",                formatRuntimeNumber(evalInput.config.hill_gat1));
        aLine("GAT1 Max Extension",       formatRuntimeNumber(evalInput.config.max_extension_gat1) + "x");
    }
    // Phase 3a: SERT/DAT/NET -- report-only, explicitly labeled as such so
    // it's never mistaken for a drug with a real network effect.
    if (evalInput.config.ki_sert < kReceptorInertThreshold) {
        aLine("SERT Ki (Reuptake Block)", formatRuntimeNumber(evalInput.config.ki_sert) + " [report-only]");
        aLine("SERT Hill",                formatRuntimeNumber(evalInput.config.hill_sert));
        aLine("SERT Max Extension",       formatRuntimeNumber(evalInput.config.max_extension_sert) + "x");
    }
    if (evalInput.config.ki_dat < kReceptorInertThreshold) {
        aLine("DAT Ki (Reuptake Block)",  formatRuntimeNumber(evalInput.config.ki_dat) + " [report-only]");
        aLine("DAT Hill",                 formatRuntimeNumber(evalInput.config.hill_dat));
        aLine("DAT Max Extension",        formatRuntimeNumber(evalInput.config.max_extension_dat) + "x");
    }
    if (evalInput.config.ki_net < kReceptorInertThreshold) {
        aLine("NET Ki (Reuptake Block)",  formatRuntimeNumber(evalInput.config.ki_net) + " [report-only]");
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
    // Phase 3c: neuromodulator gain config, only printed per-receptor when
    // that receptor is actually configured (kReceptorInertThreshold already
    // declared above in this function).
    if (evalInput.config.ec50_d1 < kReceptorInertThreshold) {
        aLine("D1 EC50 (Gain)",        formatRuntimeNumber(evalInput.config.ec50_d1));
        aLine("D1 Hill",               formatRuntimeNumber(evalInput.config.hill_d1));
        aLine("D1 Max Adapt Reduction", formatRuntimeNumber(evalInput.config.max_adaptation_reduction_d1 * 100.0) + "%");
        aLine("D1 Max NMDA Gain",      formatRuntimeNumber(evalInput.config.max_nmda_gain_d1) + "x");
    }
    if (evalInput.config.ec50_d2 < kReceptorInertThreshold) {
        aLine("D2 EC50 (Gain)",        formatRuntimeNumber(evalInput.config.ec50_d2));
        aLine("D2 Hill",               formatRuntimeNumber(evalInput.config.hill_d2));
        aLine("D2 Max Release Reduction", formatRuntimeNumber(evalInput.config.max_release_reduction_d2 * 100.0) + "%");
    }
    if (evalInput.config.ec50_ht1a < kReceptorInertThreshold) {
        aLine("5-HT1A EC50 (Gain)",    formatRuntimeNumber(evalInput.config.ec50_ht1a));
        aLine("5-HT1A Hill",           formatRuntimeNumber(evalInput.config.hill_ht1a));
        aLine("5-HT1A Max K+ Gain",    formatRuntimeNumber(evalInput.config.max_k_gain_ht1a) + "x");
    }
    if (evalInput.config.ec50_ht2a < kReceptorInertThreshold) {
        aLine("5-HT2A EC50 (Gain)",    formatRuntimeNumber(evalInput.config.ec50_ht2a));
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
    aLine("Step Size",    formatRuntimeNumber(report.stepDose));
    out << "\n--------------------------------------------------\n\n";

    // Phase 3a: report-only transporters (SERT/DAT/NET) have NO receptor/
    // channel pathway wired into the simulation at all -- confirmed by
    // grep, nothing in BatchedSimulationEngine.cpp reads profile.sert/dat/
    // net (see ReceptorDrugProfile.h's TransporterAction comment). So if
    // none of the channels/AMPA/NMDA/GABA-A/GABA-B/GAT1 are configured,
    // every "dose" in this run is mechanistically identical to a zero-drug
    // baseline -- the only thing differing across doses is which random
    // network seed got drawn (seed depends on dose index, see
    // BatchedSimulationEngine's fixed baseSeed formula), so any apparent
    // dose-response the classifier reports is pure sampling noise, not a
    // real pharmacological signal. Letting that noise flow into the normal
    // Response Characteristics/FINAL DECISION path produces a misleading
    // verdict -- caught directly during Phase 3a validation, where
    // reboxetine (zero simulated mechanism) randomly showed "NOT
    // RECOMMENDED, HIGH RISK" from nothing but run-to-run seed variance.
    // Fix: detect this case and print an honest, explicit notice instead.
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
    const bool hasAnyReportOnlyTransporter =
            evalInput.config.ki_sert < kReceptorInertThreshold ||
            evalInput.config.ki_dat  < kReceptorInertThreshold ||
            evalInput.config.ki_net  < kReceptorInertThreshold;

    if (!hasChannelEffect && !hasReceptorOrGat1Effect && hasAnyReportOnlyTransporter) {
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
            aLine(std::string(label) + " Clearance Fold",
                  formatRuntimeNumber(foldChange, 2) + "x baseline");
        };
        if (evalInput.config.ki_sert < kReceptorInertThreshold) {
            printReportOnlyTransporterEarly("SERT", evalInput.config.ki_sert, evalInput.config.hill_sert, evalInput.config.max_extension_sert);
        }
        if (evalInput.config.ki_dat < kReceptorInertThreshold) {
            printReportOnlyTransporterEarly("DAT", evalInput.config.ki_dat, evalInput.config.hill_dat, evalInput.config.max_extension_dat);
        }
        if (evalInput.config.ki_net < kReceptorInertThreshold) {
            printReportOnlyTransporterEarly("NET", evalInput.config.ki_net, evalInput.config.hill_net, evalInput.config.max_extension_net);
        }
        out << "\n--------------------------------------------------\n\n";

        out << "[FINAL DECISION]\n";
        aLine("Recommendation", "NOT APPLICABLE");
        aLine("Risk Level",     "N/A");
        aLine("Mechanism",      "No receptor/current pathway modeled for this transmitter yet");
        out << "Reason               : This drug's target transporter (serotonin/dopamine/\n"
            << "                       norepinephrine reuptake) has no receptor current\n"
            << "                       modeled in this engine yet -- that needs Phase 3c's\n"
            << "                       neuromodulator gain system (D1/D2/5-HT1A/5-HT2A\n"
            << "                       modulating gNa/K+/release probability), not built yet.\n"
            << "                       The transporter pharmacology above (occupancy, clearance\n"
            << "                       fold) is real and literature-sourced. Every other section\n"
            << "                       of this report is SUPPRESSED here because this drug\n"
            << "                       config has zero effect on the simulated network -- any\n"
            << "                       apparent dose-response would be random seed noise, not a\n"
            << "                       real pharmacological signal.\n";
        aLine("Confidence", "N/A");
        out << "==================================================\n";
        return out.str();
    }

    out << "[Response Characteristics]\n";
    aLine("Curve Type",       report.curveType);
    aLine("Response Mode",    report.responseMode);
    aLine("Mechanism",        report.mechanismText);
    aLine("Model Fit (R^2)", formatRuntimeNumber(report.sigmoidR2, 2));
    aLine("Max Effect",       formatRuntimeNumber(maxEffect*100.0, 0) + " %");
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
        aLine("Burst Rate Delta",  formatRuntimeNumber(pk.burstRateDelta, 2) + " Hz");
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
    const bool anyTransporterConfigured =
        evalInput.config.ki_gat1 < kReceptorInertThreshold ||
        evalInput.config.ki_sert < kReceptorInertThreshold ||
        evalInput.config.ki_dat  < kReceptorInertThreshold ||
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
        if (evalInput.config.ki_sert < kReceptorInertThreshold) {
            printReportOnlyTransporter("SERT", evalInput.config.ki_sert, evalInput.config.hill_sert, evalInput.config.max_extension_sert);
        }
        if (evalInput.config.ki_dat < kReceptorInertThreshold) {
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

        const float d1Occ   = spp::synapse::neuromodulatorOccupancy(peakDoseF, static_cast<float>(evalInput.config.ec50_d1),   static_cast<float>(evalInput.config.hill_d1));
        const float d2Occ   = spp::synapse::neuromodulatorOccupancy(peakDoseF, static_cast<float>(evalInput.config.ec50_d2),   static_cast<float>(evalInput.config.hill_d2));
        const float ht1aOcc = spp::synapse::neuromodulatorOccupancy(peakDoseF, static_cast<float>(evalInput.config.ec50_ht1a), static_cast<float>(evalInput.config.hill_ht1a));
        const float ht2aOcc = spp::synapse::neuromodulatorOccupancy(peakDoseF, static_cast<float>(evalInput.config.ec50_ht2a), static_cast<float>(evalInput.config.hill_ht2a));

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

        const spp::synapse::NeuromodulatorGainModifiers gainMods =
            spp::drug::DrugModel::computeNeuromodulatorGainModifiers(nmProfileForReport, peakDoseF);

        aLine("D1 Occupancy",    formatRuntimeNumber(d1Occ   * 100.0, 0) + " % (at peak dose)");
        aLine("D2 Occupancy",    formatRuntimeNumber(d2Occ   * 100.0, 0) + " % (at peak dose)");
        aLine("5-HT1A Occupancy", formatRuntimeNumber(ht1aOcc * 100.0, 0) + " % (at peak dose)");
        aLine("5-HT2A Occupancy", formatRuntimeNumber(ht2aOcc * 100.0, 0) + " % (at peak dose)");

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
    aLine("Toxic Threshold",
          toxDet ? formatRuntimeNumber(report.toxicMinDose)
                 : (">" + formatRuntimeNumber(report.maxTestedDose) + " (Not observed)"));
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
    out << "\n--------------------------------------------------\n\n";

    out << "[Dose Classification Summary]\n";
    aLine("Ineffective Zone", formatDoses(ineffDoses));
    aLine(zoneLabel,          formatDoses(therapeuticDoses));
    aLine(exciMode ? "Severe Excitability Zone"
                   : stabMode ? "Stabilization Saturation Zone"
                              : "Over-Suppression Zone",
          formatDoses(exciDoses));
    out << "\n--------------------------------------------------\n\n";

    if (stabMode) {
        out << "[Network Stabilization Metrics]\n";
        aLine("Sync Reduction",    formatRuntimeNumber(report.syncReductionPct, 1) + " %");
        aLine("Burst Reduction",   formatRuntimeNumber(report.burstReductionPct, 2) + " Hz");
        aLine("Effect Magnitude",  formatRuntimeNumber(report.calciumEffectMagnitude, 1) + " %");
        out << "\n--------------------------------------------------\n\n";
    }

    out << "[Per Dose Network State]\n";
    for (const auto& dose : report.analyzedDoses) {
        out << "  Dose " << std::setw(6) << formatRuntimeNumber(dose.dose)
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
    aLine("Confidence",     report.confidence);
    out << "\n==================================================\n";

    return out.str();
}

void writeDrugEvaluationReport(
    const std::string& path, const PharmaDecisionReport& report,
    const AggregatedStats& stats, int runs,
    const RuntimeInput& input, const std::string& mode,
    std::optional<bool> usedGpu = std::nullopt)
{
    std::ofstream out(path, std::ios::out|std::ios::trunc);
    if (!out.is_open()) throw std::runtime_error("Cannot open: " + path);
    out << buildDrugEvaluationReportText(report, stats, runs, input, mode, usedGpu);
}

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
    aLine("Burst Rate",      formatRuntimeNumber(summary.network_metrics.burstRateHz) + " Hz");
    aLine("Irregularity",    formatRuntimeNumber(summary.network_metrics.irregularityIndex));
    aLine("Silent Neurons",  formatRuntimeNumber(summary.network_metrics.silentNeuronPct) + " %");
    out << "\n--------------------------------------------------\n";

    out << "[Network Analysis]\n";
    aLine("Network State",   PharmaDecisionEngine::toString(analyzed.networkState));
    aLine("Mechanism",       PharmaDecisionEngine::toString(analyzed.mechanismSignature));
    aLine("NII",             formatRuntimeNumber(analyzed.nii));
    aLine("Seizure Prob",    formatRuntimeNumber(analyzed.seizureProbability * 100.0f) + " %");
    aLine("Rate Change",     formatRuntimeNumber(analyzed.rateChangePct) + " %");
    aLine("Burst Rate Δ",    formatRuntimeNumber(analyzed.burstRateDelta) + " Hz");
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

    for(std::size_t i=0; i<doses.size(); ++i) {
        const auto& rr = perDoseResults[i];
        if(rr.empty()) continue;

        const AggregatedStats ds = computeStats(rr);
        allRunResults.insert(allRunResults.end(), rr.begin(), rr.end());

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
    std::cout << buildDrugEvaluationReportText(
        report, stabilityStats, runs, evalInput, engineInputMode, usedGpu);

    if(evalInput.config.export_csv) {
        std::filesystem::create_directories(evalInput.config.output_folder);
        CsvWriter::writeDoseResponse(
            evalInput.config.output_folder + "/dose_response.csv", doseResponse);
        CsvWriter::writeNetworkMetrics(
            evalInput.config.output_folder + "/network_metrics.csv", networkRecords);
        CsvWriter::writeNeuronStats(
            evalInput.config.output_folder + "/neuron_stats.csv", finalNM);
        writeDrugEvaluationReport(
            evalInput.config.output_folder + "/drug_evaluation_report.txt",
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
                std::optional<int> ro; std::string mt;
                if(loadDrugConfigFromJsonFile(drugConfigPath, input, ro, mt, jsonDoseMin, jsonDoseMax, jsonDoseStep)) {
                    engineMode = "User Drug Config";
                    if(ro) userRuns = *ro;
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