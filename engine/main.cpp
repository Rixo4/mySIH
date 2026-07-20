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
    double external_current     = 2.5;
    double noise_level          = 0.35;
    double excitatory_weight_scale = 1.0;
    double inhibitory_weight_scale = 1.0;
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
    return simCfg;
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
    rates.reserve(results.size());      syncs.reserve(results.size());
    bursts.reserve(results.size());     isis.reserve(results.size());
    burstRates.reserve(results.size());
    peakSyncs.reserve(results.size());  burstingPcts.reserve(results.size());
    burstDurs.reserve(results.size());  rateStds.reserve(results.size());
    silentPcts.reserve(results.size()); earlyRates.reserve(results.size());
    lateRates.reserve(results.size());

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
    std::vector<spp::analyzer::NeuronMetrics>* outLastNeuronMetrics)
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
        blocks
    );

    std::vector<spp::simulation::SimulationResult> blockResults = batched.run();

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

static bool loadDrugConfigFromJsonFile(
    const std::string& path, RuntimeInput& out,
    std::optional<int>& outRuns, std::string& outMode)
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
    const auto drP=c.find("\"dose_range\"");
    if(drP!=c.npos){if(auto v=extractJsonNumber(c,"min",drP);v) out.config.dose=*v;}
    if(auto v=extractJsonNumber(c,"runs");v) outRuns=static_cast<int>(*v);
    if(auto v=extractJsonString(c,"mode");v) outMode=*v;
    return true;
}
// ─── Report text builder ──────────────────────────────────────────────────────
std::string buildDrugEvaluationReportText(
    const PharmaDecisionReport& report,
    const AggregatedStats& stabilityStats,
    int runCount,
    const RuntimeInput& evalInput,
    const std::string& engineInputMode)
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
                if (dose.suppressionScore > 0.20f || dose.stabilizationScore > 0.20f)
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
    aLine("Na IC50",           formatRuntimeNumber(evalInput.config.ic50_na));
    aLine("K IC50",            formatRuntimeNumber(evalInput.config.ic50_k));
    aLine("Ca IC50",           formatRuntimeNumber(evalInput.config.ic50_ca));
    aLine("Hill",              formatRuntimeNumber(evalInput.config.hill));
    aLine("Runs",              std::to_string(runCount));
    out << "\n--------------------------------------------------\n\n";

    out << "[Dose Range]\n";
    aLine("Tested Range", fRange(report.minTestedDose, report.maxTestedDose));
    aLine("Step Size",    formatRuntimeNumber(report.stepDose));
    out << "\n--------------------------------------------------\n\n";

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
        aLine("Rate Change",       formatRuntimeNumber(pk.rateChangePct, 1) + " %");
        aLine("Burst Rate Delta",  formatRuntimeNumber(pk.burstRateDelta, 2) + " Hz");
        aLine("Sync Delta",        formatRuntimeNumber(pk.syncDelta, 3));
        aLine("Irregularity Delta",formatRuntimeNumber(pk.irregularityDelta, 3));
        aLine("Silent Neuron Δ",   formatRuntimeNumber(pk.silentNeuronDelta, 1) + " %");
    }
    out << "\n--------------------------------------------------\n\n";

    out << "[Safety Analysis]\n";
    aLine("Toxic Threshold",
          toxDet ? formatRuntimeNumber(report.toxicMinDose)
                 : (">" + formatRuntimeNumber(report.maxTestedDose) + " (Not observed)"));
    aLine("Safety Observation",
          toxDet ? "Toxicity observed within tested range"
                 : "No toxicity within tested range");
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
    const RuntimeInput& input, const std::string& mode)
{
    std::ofstream out(path, std::ios::out|std::ios::trunc);
    if (!out.is_open()) throw std::runtime_error("Cannot open: " + path);
    out << buildDrugEvaluationReportText(report, stats, runs, input, mode);
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
    const std::optional<int>& userRuns = std::nullopt)
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
    }

    double minD=kDefaultMinDose, maxD=kDefaultMaxDose, stepD=kDefaultStepDose;
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
    const auto perDoseResults = runAllDosesBatched(evalInput, doses, runs, baseSeed, &finalNM);

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
        report, stabilityStats, runs, evalInput, engineInputMode);

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
            report, stabilityStats, runs, evalInput, engineInputMode);
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
            validateConfig(input.config);
            runSingleSimulationMode(input);
            return 0;
        }

        if(mode == "--dose-eval") {
            RuntimeInput input;
            input.run_dose_sweep       = false;
            input.config.output_folder = "output_pharma_decision";
#ifdef SPP_USE_CUDA
            input.config.neuron_count = 1420;
#else
            input.config.neuron_count = 1320;
#endif
            input.config.sim_time               = 500.0;
            input.config.dt                     = 0.04;
            input.config.connectivity           = 0.05;
            input.config.excitatory_ratio       = 0.80;
            input.config.external_current       = 1.05;
            input.config.noise_level            = 0.72;
            input.config.excitatory_weight_scale = 1.00;
            input.config.inhibitory_weight_scale = 1.00;
            input.config.ic50_na = 200.0;
            input.config.ic50_k  = 8.0;
            input.config.ic50_ca = 1000.0;
            input.config.hill    = 3.2;

            std::string engineMode = "Default Internal Engine Config";
            std::optional<int> userRuns;
            if(!drugConfigPath.empty()) {
                std::optional<int> ro; std::string mt;
                if(loadDrugConfigFromJsonFile(drugConfigPath, input, ro, mt)) {
                    engineMode = "User Drug Config";
                    if(ro) userRuns = *ro;
                }
            }
            validateConfig(input.config);
            runDoseEvaluationMode(input, engineMode, userRuns);
            return 0;
        }

        printHelp();
        return 0;

    } catch(const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}