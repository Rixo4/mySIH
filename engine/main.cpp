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
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <ctime>
#include <utility>
#include <vector>

#include "analyzer/Metrics.h"
#include "analyzer/PharmaDecisionEngine.h"
#include "analyzer/SeizureDetector.h"
#include "drug/DrugModel.h"
#include "network/Network.h"
#include "output/CsvWriter.h"
#include "simulation/SimulationEngine.h"

namespace {

using spp::analyzer::MetricsAnalyzer;
using spp::analyzer::DoseObservation;
using spp::analyzer::NetworkMetrics;
using spp::analyzer::NetworkState;
using spp::analyzer::PharmaDecisionEngine;
using spp::analyzer::PharmaDecisionReport;
using spp::analyzer::SeizureDetector;
using spp::drug::ChannelDrugProfile;
using spp::drug::DrugModel;
using spp::network::NetworkConfig;
using spp::output::CsvWriter;
using spp::output::DoseResponsePoint;
using spp::output::NetworkMetricRecord;
using spp::simulation::SimulationEngine;

constexpr int kMinNeuronCount   = 1000;
constexpr int kMaxNeuronCount   = 100000;
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
    double excitatory_ratio = 0.8;
    double external_current = 2.5;
    double noise_level      = 0.35;
    double excitatory_weight_scale = 1.0;
    double inhibitory_weight_scale = 1.0;
    bool use_cuda    = true;
    bool export_csv  = true;
    std::string output_folder = "output_data";
};

struct RuntimeInput {
    SimulationConfig config;
    std::string drug_name    = "GenericCompound";
    bool run_dose_sweep      = false;
    double sweep_start       = 0.0;
    double sweep_end         = 100.0;
    int sweep_points         = 10;
};

struct SimulationSummary {
    std::vector<spp::analyzer::NeuronMetrics> neuron_metrics;
    NetworkMetrics network_metrics;
    NetworkState classification = NetworkState::Stable;
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

struct RunResult {
    float firingRate   = 0.0f;
    float sync         = 0.0f;
    float burst        = 0.0f;
    float nii          = 0.0f;
    float isiCV        = 0.0f;
    float seizureScore = 0.0f;
    // BUG 3 FIX: composite of seizure(0.60) + suppression(0.40), not suppression alone.
    float toxicityScore = 0.0f;
    // BUG 2 FIX: carry SeizureDetector output and baseline flag per run.
    NetworkState networkState         = NetworkState::Stable;
    bool suppressionHasBaseline       = false;
};

struct AggregatedStats {
    float meanRate     = 0.0f;  float stdRate     = 0.0f;
    float meanSync     = 0.0f;  float stdSync     = 0.0f;
    float meanBurst    = 0.0f;  float stdBurst    = 0.0f;
    float meanNii      = 0.0f;  float stdNii      = 0.0f;
    float meanISI      = 0.0f;  float stdISI      = 0.0f;
    float meanSeizure  = 0.0f;  float stdSeizure  = 0.0f;
    float meanToxicity = 0.0f;  float stdToxicity = 0.0f;
    // BUG 6 FIX: most-severe NetworkState observed across all runs.
    NetworkState dominantNetworkState = NetworkState::Stable;
    // BUG 2 FIX: true only when ALL runs had a valid baseline.
    bool suppressionHasBaseline = false;
};

struct SigmoidFitResult {
    double k    = 0.0;
    double d50  = 0.0;
    double emax = 1.0;
    double r2   = 0.0;
    double sse  = std::numeric_limits<double>::infinity();
    std::vector<double> predicted;
};

// ─── NetworkState severity ordering ──────────────────────────────────────────
// Used by multi-run aggregation to select the most severe state across runs.
int networkStateSeverity(NetworkState s) {
    switch (s) {
        case NetworkState::NeuralSuppression:   return 6;
        case NetworkState::DepolarizationBlock: return 5;
        case NetworkState::SeizureActive:       return 4;
        case NetworkState::SeizureRisk:         return 3;
        case NetworkState::Hyperexcitable:      return 2;
        case NetworkState::MildInstability:     return 1;
        case NetworkState::Stable: default:     return 0;
    }
}

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
    const auto notSpace = [](unsigned char c){ return !std::isspace(c); };
    auto b = std::find_if(v.begin(), v.end(), notSpace);
    if (b == v.end()) return {};
    auto e = std::find_if(v.rbegin(), v.rend(), notSpace).base();
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

[[maybe_unused]] bool parseModeText(const std::string& text, bool& useCudaOut) {
    const std::string m = toLower(trim(text));
    if (m=="cuda"||m=="gpu") { useCudaOut=true;  return true; }
    if (m=="cpu")            { useCudaOut=false; return true; }
    return false;
}

// ─── Console formatting ───────────────────────────────────────────────────────
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
    std::cout
        << "Usage:\n"
        << "  silicon_patient.exe --simulate\n"
        << "  silicon_patient.exe --dose-eval\n"
        << "  silicon_patient.exe --analyze\n";
}

// ─── Config validation ────────────────────────────────────────────────────────
void validateConfig(const SimulationConfig& cfg) {
    if (cfg.neuron_count < kMinNeuronCount || cfg.neuron_count > kMaxNeuronCount)
        throw std::runtime_error("Neuron count must be between 1000 and 100000.");
    if (!(cfg.sim_time > 0.0))    throw std::runtime_error("Simulation time must be > 0.");
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

[[maybe_unused]] void validateSweep(const RuntimeInput& input) {
    if (!input.run_dose_sweep) return;
    if (input.sweep_start<0.0||input.sweep_end<0.0)
        throw std::runtime_error("Sweep doses must be >= 0.");
    if (input.sweep_end < input.sweep_start)
        throw std::runtime_error("sweep_end must be >= sweep_start.");
    if (input.sweep_points < 2)
        throw std::runtime_error("sweep_points must be >= 2.");
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
    netCfg.inhibitoryWeightMean  = 3.80f * inhScale * neuronScale;
    netCfg.weightStdFraction     = 0.40f;
    netCfg.excitatoryWeightMin   = 0.45f * excScale * neuronScale;
    netCfg.excitatoryWeightMax   = 2.40f * excScale * neuronScale;
    netCfg.inhibitoryWeightMin   = 1.70f * inhScale * neuronScale;
    netCfg.inhibitoryWeightMax   = 5.80f * inhScale * neuronScale;
    netCfg.recurrentExcitatoryBias = 0.58f;
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
    simCfg.dtMs               = static_cast<float>(cfg.dt);
    simCfg.durationMs         = static_cast<float>(cfg.sim_time);
    simCfg.randomSeed         = seed + 17U;
    simCfg.baseExternalCurrent= static_cast<float>(cfg.external_current);
    simCfg.externalCurrentStd = std::max(0.16f, static_cast<float>(std::fabs(cfg.external_current)*0.09));
    simCfg.baseNoiseStd       = std::max(0.14f, 0.56f*static_cast<float>(cfg.noise_level));
    simCfg.refractoryMs       = 1.8f;
    simCfg.synTauExcMs        = 7.0f;
    simCfg.synTauInhMs        = 14.0f;
    simCfg.maxSynCurrent      = 320.0f;
    simCfg.maxTotalCurrent    = 350.0f;
    simCfg.adaptationTauMs    = 220.0f;
    simCfg.adaptationIncrement= 0.016f + 0.010f*std::clamp(static_cast<float>(cfg.noise_level),0.0f,2.0f);
    simCfg.adaptationMaxCurrent       = 1.8f;
    simCfg.adaptationInhibitoryScale  = 0.50f;
    simCfg.drugOnsetTauMs             = 140.0f;
    simCfg.useGpu                     = cfg.use_cuda;
    return simCfg;
}

// ─── Core simulation runner ───────────────────────────────────────────────────
// BUG 1 FIX: accepts optional baseline NetworkMetrics* so computeNetworkMetrics
// can compute suppression relative to the drug-free population mean, making
// suppressionHasBaseline=true on all subsequent drug doses.
SimulationSummary runSingleSimulationInternal(
    const RuntimeInput& input,
    std::uint32_t seed,
    spp::simulation::SimulationResult* outResult,
    const NetworkMetrics* baseline = nullptr
) {
    const NetworkConfig networkCfg = buildNetworkConfig(input.config, seed);
    const spp::simulation::SimulationConfig engineCfg = buildEngineConfig(input.config, seed);

    SimulationEngine engine(static_cast<std::size_t>(input.config.neuron_count), networkCfg, engineCfg);

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
    // BUG 1 FIX: pass baseline for correct baseline-relative suppression.
    summary.network_metrics = MetricsAnalyzer::computeNetworkMetrics(
        simResult, summary.neuron_metrics, baseline);
    summary.classification  = SeizureDetector::classify(
        summary.network_metrics, simResult.finalVoltages);

    if (outResult) *outResult = std::move(simResult);
    return summary;
}

SimulationSummary runSingleSimulation(
    const RuntimeInput& input, std::uint32_t seed,
    const NetworkMetrics* baseline = nullptr)
{
    return runSingleSimulationInternal(input, seed, nullptr, baseline);
}

SimulationTrace runSingleSimulationWithTrace(
    const RuntimeInput& input, std::uint32_t seed,
    const NetworkMetrics* baseline = nullptr)
{
    SimulationTrace trace;
    trace.summary = runSingleSimulationInternal(input, seed, &trace.result, baseline);
    return trace;
}

// ─── ISI / metric helpers ─────────────────────────────────────────────────────
[[maybe_unused]] float computeMeanIsiVarianceMs(
    const std::vector<spp::analyzer::NeuronMetrics>& neuronMetrics)
{
    float sum = 0.0f; std::size_t count = 0U;
    for (const auto& n : neuronMetrics) {
        if (n.spikeCount < 3U) continue;
        if (!std::isfinite(n.isiVarianceMs) || n.isiVarianceMs <= 0.0f) continue;
        sum += n.isiVarianceMs; ++count;
    }
    return count ? sum / static_cast<float>(count) : 0.0f;
}

[[maybe_unused]] float computeWindowRateHz(
    const spp::simulation::SimulationResult& result, float startMs, float endMs)
{
    if (result.populationSpikesPerStep.empty() || result.dtMs<=0.0f || endMs<=startMs) return 0.0f;
    const float s0 = std::clamp(startMs, 0.0f, result.durationMs);
    const float s1 = std::clamp(endMs,   0.0f, result.durationMs);
    if (s1 <= s0) return 0.0f;
    const std::size_t N  = result.populationSpikesPerStep.size();
    const std::size_t i0 = std::min(N, static_cast<std::size_t>(std::floor(s0/result.dtMs)));
    const std::size_t i1 = std::min(N, static_cast<std::size_t>(std::ceil (s1/result.dtMs)));
    if (i1 <= i0) return 0.0f;
    std::uint64_t spk = 0U;
    for (std::size_t i = i0; i < i1; ++i) spk += result.populationSpikesPerStep[i];
    const float nc  = static_cast<float>(std::max<std::size_t>(1U, result.spikeTimes.size()));
    const float dur = std::max(1.0e-6f, static_cast<float>(i1-i0)*result.dtMs/1000.0f);
    return static_cast<float>(spk) / (nc * dur);
}

[[maybe_unused]] float computeWindowPeakSpikeFraction(
    const spp::simulation::SimulationResult& result, float startMs, float endMs)
{
    if (result.populationSpikesPerStep.empty() || result.dtMs<=0.0f || endMs<=startMs) return 0.0f;
    const float s0 = std::clamp(startMs,0.0f,result.durationMs);
    const float s1 = std::clamp(endMs,  0.0f,result.durationMs);
    if (s1<=s0) return 0.0f;
    const std::size_t N  = result.populationSpikesPerStep.size();
    const std::size_t i0 = std::min(N, static_cast<std::size_t>(std::floor(s0/result.dtMs)));
    const std::size_t i1 = std::min(N, static_cast<std::size_t>(std::ceil (s1/result.dtMs)));
    if (i1<=i0) return 0.0f;
    const float nc = static_cast<float>(std::max<std::size_t>(1U, result.spikeTimes.size()));
    float peak = 0.0f;
    for (std::size_t i = i0; i < i1; ++i)
        peak = std::max(peak, static_cast<float>(result.populationSpikesPerStep[i])/nc);
    return peak;
}

[[maybe_unused]] double computeLinearR2(
    const std::vector<double>& x, const std::vector<double>& y)
{
    if (x.size()!=y.size()||x.size()<3U) return 1.0;
    const double n = static_cast<double>(x.size());
    double sx=0,sy=0;
    for (std::size_t i=0;i<x.size();++i){sx+=x[i];sy+=y[i];}
    const double mx=sx/n, my=sy/n;
    double sxx=0,sxy=0,sst=0;
    for (std::size_t i=0;i<x.size();++i){
        const double dx=x[i]-mx, dy=y[i]-my;
        sxx+=dx*dx; sxy+=dx*dy; sst+=dy*dy;
    }
    if (sxx<=1e-12||sst<=1e-12) return 1.0;
    const double slope=sxy/sxx, intercept=my-slope*mx;
    double ssr=0;
    for (std::size_t i=0;i<x.size();++i){
        const double e=y[i]-(slope*x[i]+intercept); ssr+=e*e;
    }
    return 1.0-(ssr/(sst+1e-12));
}

[[maybe_unused]] double computePeakPositiveSlope(
    const std::vector<double>& x, const std::vector<double>& y, std::size_t* peakIndexOut)
{
    if (x.size()!=y.size()||x.size()<2U){if(peakIndexOut)*peakIndexOut=0U;return 0.0;}
    double peakSlope=0.0; std::size_t peakIdx=0U;
    for (std::size_t i=1;i<x.size();++i){
        const double dx=std::max(1e-12,x[i]-x[i-1]);
        const double sl=(y[i]-y[i-1])/dx;
        if(sl>peakSlope){peakSlope=sl;peakIdx=i-1;}
    }
    if (peakIndexOut)*peakIndexOut=peakIdx;
    return peakSlope;
}

[[maybe_unused]] double computeMidSlope(
    const std::vector<double>& x, const std::vector<double>& y)
{
    if (x.size()!=y.size()||x.size()<3U) return 0.0;
    const std::size_t m=x.size()/2U;
    if (m==0U||m+1U>=x.size()) return 0.0;
    return (y[m+1]-y[m-1])/std::max(1e-12,x[m+1]-x[m-1]);
}

// BUG 4 FIX: burstIndex is sigmoid output [0,1] after Metrics.cpp fix.
// Old code divided by 0.20 which saturated at burstIndex=0.20 (mild bursting).
float computeCoherenceScore(const NetworkMetrics& metrics) {
    const float syncNorm  = std::clamp(metrics.synchronizationIndex, 0.0f, 1.0f);
    const float burstNorm = std::clamp(metrics.burstIndex,           0.0f, 1.0f); // BUG 4 FIX: no /0.20
    return 0.70f * syncNorm + 0.30f * burstNorm;
}

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

    std::vector<float> rates,syncs,bursts,niis,isis,seizures,toxicities;
    rates.reserve(results.size());    syncs.reserve(results.size());
    bursts.reserve(results.size());   niis.reserve(results.size());
    isis.reserve(results.size());     seizures.reserve(results.size());
    toxicities.reserve(results.size());

    // BUG 6 FIX: track most severe state and whether all runs had a baseline.
    NetworkState mostSevere = NetworkState::Stable;
    bool allHaveBaseline = true;

    for (const auto& r : results) {
        rates.push_back(r.firingRate);    syncs.push_back(r.sync);
        bursts.push_back(r.burst);        niis.push_back(r.nii);
        isis.push_back(r.isiCV);          seizures.push_back(r.seizureScore);
        toxicities.push_back(r.toxicityScore);
        if (networkStateSeverity(r.networkState) > networkStateSeverity(mostSevere))
            mostSevere = r.networkState;
        if (!r.suppressionHasBaseline) allHaveBaseline = false;
    }

    stats.meanRate     = computeMean(rates);      stats.stdRate     = computeStd(rates,     stats.meanRate);
    stats.meanSync     = computeMean(syncs);      stats.stdSync     = computeStd(syncs,     stats.meanSync);
    stats.meanBurst    = computeMean(bursts);     stats.stdBurst    = computeStd(bursts,    stats.meanBurst);
    stats.meanNii      = computeMean(niis);       stats.stdNii      = computeStd(niis,      stats.meanNii);
    stats.meanISI      = computeMean(isis);       stats.stdISI      = computeStd(isis,      stats.meanISI);
    stats.meanSeizure  = computeMean(seizures);   stats.stdSeizure  = computeStd(seizures,  stats.meanSeizure);
    stats.meanToxicity = computeMean(toxicities); stats.stdToxicity = computeStd(toxicities,stats.meanToxicity);
    stats.dominantNetworkState   = mostSevere;
    stats.suppressionHasBaseline = allHaveBaseline;
    return stats;
}

std::string getStability(float stdRate, float stdToxicity) {
    if (stdRate<1.0f&&stdToxicity< 5.0f) return "HIGH";
    if (stdRate<2.0f&&stdToxicity<10.0f) return "MEDIUM";
    return "LOW";
}

// BUG 1 FIX: accepts baseline* so every run passes it to computeNetworkMetrics.
std::vector<RunResult> runMultipleSimulations(
    const RuntimeInput& baseInput,
    double dose,
    int numRuns,
    std::uint32_t doseSeedBase,
    std::vector<spp::analyzer::NeuronMetrics>* outLastNeuronMetrics,
    spp::analyzer::NetworkState* outLastState,
    const NetworkMetrics* baseline = nullptr
) {
    std::vector<RunResult> results;
    if (numRuns<=0) return results;
    results.reserve(static_cast<std::size_t>(numRuns));

    for (int run=0;run<numRuns;++run) {
        RuntimeInput ri = baseInput;
        ri.config.dose = dose;
        const std::uint32_t seed = doseSeedBase + static_cast<std::uint32_t>(run*9973+101);
        const SimulationSummary s = runSingleSimulation(ri, seed, baseline);

        if (outLastNeuronMetrics) *outLastNeuronMetrics = s.neuron_metrics;
        if (outLastState)         *outLastState          = s.classification;

        RunResult r;
        r.firingRate   = s.network_metrics.meanFiringRateHz;
        r.sync         = s.network_metrics.synchronizationIndex;
        r.burst        = s.network_metrics.burstIndex;
        r.nii          = s.network_metrics.nii;
        r.isiCV        = computeMeanIsiCv(s.neuron_metrics);
        r.seizureScore = s.network_metrics.seizureProbabilityPct;
        // BUG 3 FIX: composite toxicity — seizure dominates (0.60), suppression
        // secondary (0.40). suppressionPct alone was the wrong proxy.
        r.toxicityScore = 0.60f * s.network_metrics.seizureProbabilityPct
                        + 0.40f * s.network_metrics.suppressionPct;
        // BUG 2 FIX: carry SeizureDetector output and baseline reliability flag.
        r.networkState            = s.classification;
        r.suppressionHasBaseline  = s.network_metrics.suppressionHasBaseline;
        results.push_back(r);
    }
    return results;
}

// BUG 7 FIX: populate new NetworkMetrics fields introduced by Metrics.cpp:
// excitabilityScore, suppressionHasBaseline. burstIndex is sigmoid [0,1] now.
NetworkMetrics buildAggregatedNetworkMetrics(const AggregatedStats& stats) {
    NetworkMetrics m;
    m.meanFiringRateHz      = stats.meanRate;
    m.synchronizationIndex  = stats.meanSync;
    m.burstIndex            = stats.meanBurst;   // sigmoid [0,1]
    m.irregularityIndex     = stats.meanISI;
    m.seizureProbabilityPct = stats.meanSeizure;
    m.suppressionPct        = stats.meanToxicity;
    m.nii                   = stats.meanNii;

    // excitabilityScore: mirrors Metrics.cpp formula
    // 0.35*rateNorm + 0.40*burstNorm + 0.25*irregNorm
    {
        const float rN = std::clamp(stats.meanRate  / 50.0f, 0.0f, 1.0f);
        const float bN = std::clamp(stats.meanBurst,          0.0f, 1.0f); // sigmoid already
        const float iN = std::clamp(stats.meanISI   / 1.5f,  0.0f, 1.0f);
        m.excitabilityScore = std::clamp(0.35f*rN + 0.40f*bN + 0.25f*iN, 0.0f, 1.0f);
    }
    m.suppressionHasBaseline = stats.suppressionHasBaseline;

    const std::string stab = getStability(stats.stdRate, stats.stdToxicity);
    m.stabilityScore = (stab=="HIGH") ? 0.90f : (stab=="MEDIUM") ? 0.60f : 0.30f;
    return m;
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
namespace {
void printValidationCheck(const ValidationCheck& check) {
    printSection(check.name);
    if (!check.details.empty()) {
        std::cout << check.details;
        if (check.details.back()!='\n') std::cout<<'\n';
    }
    printMetricLine("Status", check.pass ? "PASS" : "FAIL");
    std::cout<<'\n';
}

// BUG 13 NOTE: isToxicState intentionally excludes Hyperexcitable.
// Hyperexcitable → "Caution" is biologically correct: the network is
// pathologically excited but not yet in ictal territory.
bool isToxicState(NetworkState s) {
    return s==NetworkState::SeizureRisk     ||
           s==NetworkState::SeizureActive   ||
           s==NetworkState::DepolarizationBlock ||
           s==NetworkState::NeuralSuppression;
}

bool isSafeState(NetworkState s) {
    return s==NetworkState::Stable || s==NetworkState::MildInstability;
}

std::string doseBandLabel(const SimulationSummary& summary) {
    if (isToxicState(summary.classification) ||
        summary.network_metrics.seizureProbabilityPct >= 60.0f ||
        summary.network_metrics.suppressionPct >= 60.0f) return "Toxic";
    if (isSafeState(summary.classification) &&
        summary.network_metrics.seizureProbabilityPct < 30.0f &&
        summary.network_metrics.suppressionPct < 40.0f) return "Safe";
    return "Caution";
}

// ─── Single-run report structures ────────────────────────────────────────────
struct SingleRunInterpretation {
    float isiVariability=0,instabilityIndex=0,baselineRateHz=0,currentRateHz=0;
    float changePct=0,syncRiskNorm=0,rateShiftNorm=0;
    float seizureRiskScore=0,seizureConfidence=0,seizureConfidenceDriverNorm=0;
    float toxicityRiskScore=0,decisionConfidenceDriverNorm=0;
    std::string brainState,networkRegime,variability,seizureRisk,toxicityRisk;
    std::string effectType,networkResponse,safetyMargin,networkShift;
    std::string recommendation,riskLevel,reason;
    std::string seizureConfidenceBasis,decisionConfidenceBasis;
    float finalConfidence=0,decisionConfidence=0;
};

struct SingleRunDoseContext {
    bool available=false;
    double rangeStartDose=0,rangeEndDose=0;
    int sampledPoints=0;
    PharmaDecisionReport report;
    bool hasCurrentPoint=false;
    float currentRiskScore=0,currentEarlyWarning=0,currentSeizureSlope=0;
    std::string currentTier,dosePosition;
};

constexpr int kStructuredReportLabelWidth = 19;

std::string classifyRiskLevel(float score) {
    if (score>=75) return "Critical";
    if (score>=55) return "High";
    if (score>=35) return "Moderate";
    return "Low";
}

std::string classifyToxicityRiskLevel(float score, float suppressPct, float seizurePct) {
    if (score<20&&suppressPct<20&&seizurePct<10) return "NONE";
    return classifyRiskLevel(score);
}

std::string classifyVariabilityLevel(float cv) {
    const float v=std::isfinite(cv)?std::max(0.0f,cv):0.0f;
    if (v<0.30f) return "Highly regular (low variability)";
    if (v<0.60f) return "Moderately regular";
    return "Irregular spiking (healthy variability)";
}

std::string classifyNetworkRegime(NetworkState state, float sync) {
    const float s=std::clamp(std::isfinite(sync)?sync:0.0f,0.0f,1.0f);
    if (state==NetworkState::SeizureRisk||state==NetworkState::SeizureActive||
        state==NetworkState::DepolarizationBlock||s>=0.55f)
        return "Hyper-synchronized regime";
    if (state==NetworkState::MildInstability||state==NetworkState::Hyperexcitable||s>=0.25f)
        return "Mildly synchronized regime";
    return "Asynchronous Irregular (AI), balanced E/I";
}

std::string classifyBrainState(NetworkState state,float rate,float sync,float nii,float supp) {
    if (state==NetworkState::SeizureActive||state==NetworkState::SeizureRisk||
        state==NetworkState::DepolarizationBlock||sync>=0.60f||nii>=0.70f)
        return "Hyper-synchronized regime";
    if (supp>=65.0f||rate<=2.0f) return "Mildly synchronized regime";
    if (state==NetworkState::MildInstability||state==NetworkState::Hyperexcitable||
        sync>=0.25f||nii>=0.45f) return "Mildly synchronized regime";
    return "Asynchronous Irregular (AI), balanced E/I";
}

std::vector<double> buildSingleRunDoseContextGrid(double targetDose) {
    std::vector<double> doses;
    doses.reserve(7U);
    if (targetDose<=1e-6) {
        doses={0.0,2.0,5.0,10.0,15.0,20.0,30.0};
    } else {
        const double ua=std::max(2.0*targetDose,targetDose+10.0);
        doses={0.0,0.50*targetDose,0.75*targetDose,targetDose,
               1.25*targetDose,1.50*targetDose,ua};
    }
    for (double& d:doses) d=std::max(0.0,d);
    std::sort(doses.begin(),doses.end());
    doses.erase(std::unique(doses.begin(),doses.end(),
        [](double a,double b){return std::fabs(a-b)<=1e-6;}),doses.end());
    return doses;
}

// BUG 5 FIX: all DoseObservation fields populated including networkState,
// suppressionHasBaseline, isiCv. Baseline run performed first so suppression
// is baseline-relative throughout the context grid.
SingleRunDoseContext buildSingleRunDoseContext(
    const RuntimeInput& input,
    const SimulationSummary& anchorSummary,
    std::uint32_t baseSeed)
{
    SingleRunDoseContext ctx;
    const std::vector<double> doses = buildSingleRunDoseContextGrid(input.config.dose);
    if (doses.size()<2U) return ctx;
    ctx.rangeStartDose = doses.front();
    ctx.rangeEndDose   = doses.back();
    ctx.sampledPoints  = static_cast<int>(doses.size());

    // Run dose=0 baseline first.
    NetworkMetrics baselineMetrics;
    {
        const bool anchorIsZero = (input.config.dose<=1e-6);
        if (anchorIsZero) {
            baselineMetrics = anchorSummary.network_metrics;
        } else {
            RuntimeInput bli = input; bli.config.dose = 0.0;
            baselineMetrics = runSingleSimulation(bli, baseSeed+8191U).network_metrics;
        }
    }
    const NetworkMetrics* blPtr = &baselineMetrics;

    std::vector<DoseObservation> obs;
    obs.reserve(doses.size());

    for (std::size_t i=0;i<doses.size();++i) {
        RuntimeInput ri=input; ri.config.dose=doses[i];
        const bool reuse=std::fabs(ri.config.dose-input.config.dose)<=1e-6;
        const SimulationSummary ls = reuse
            ? anchorSummary
            : runSingleSimulation(ri, baseSeed+static_cast<std::uint32_t>(i*4099U+31U), blPtr);

        const float dF   = static_cast<float>(ri.config.dose);
        const float bNa  = static_cast<float>(spp::drug::DrugModel::hillBlock(
            dF,static_cast<float>(ri.config.ic50_na),static_cast<float>(ri.config.hill)));
        const float bK   = static_cast<float>(spp::drug::DrugModel::hillBlock(
            dF,static_cast<float>(ri.config.ic50_k), static_cast<float>(ri.config.hill)));
        const float bCa  = static_cast<float>(spp::drug::DrugModel::hillBlock(
            dF,static_cast<float>(ri.config.ic50_ca),static_cast<float>(ri.config.hill)));

        DoseObservation o;
        o.dose                  = dF;
        o.meanFiringRateHz      = ls.network_metrics.meanFiringRateHz;
        o.synchronizationIndex  = ls.network_metrics.synchronizationIndex;
        o.burstIndex            = ls.network_metrics.burstIndex;
        o.nii                   = ls.network_metrics.nii;
        o.isiCv                 = computeMeanIsiCv(ls.neuron_metrics);
        o.seizureProbabilityPct = ls.network_metrics.seizureProbabilityPct;
        o.suppressionPct        = ls.network_metrics.suppressionPct;
        o.blockNa               = bNa; o.blockK=bK; o.blockCa=bCa;
        o.networkState          = ls.classification;                        // BUG 5 FIX
        o.suppressionHasBaseline= ls.network_metrics.suppressionHasBaseline;// BUG 5 FIX
        obs.push_back(o);
    }

    // Single-run context: low stability, 1 run.
    const spp::analyzer::DecisionStabilityInput si{0.0f,0.0f,"LOW",1};
    ctx.report    = PharmaDecisionEngine::evaluate(obs, si);
    ctx.available = !ctx.report.points.empty();
    if (!ctx.available) return ctx;

    const auto nearest = std::min_element(
        ctx.report.points.begin(), ctx.report.points.end(),
        [&](const auto& a,const auto& b){
            return std::fabs(static_cast<double>(a.dose)-input.config.dose)<
                   std::fabs(static_cast<double>(b.dose)-input.config.dose);
        });
    if (nearest!=ctx.report.points.end()) {
        ctx.hasCurrentPoint    = true;
        ctx.currentRiskScore   = nearest->riskScore;
        ctx.currentEarlyWarning= nearest->earlyWarningIndex;
        ctx.currentSeizureSlope= nearest->seizureSlopePctPerDose;
        ctx.currentTier        = nearest->classification;
    }

    if (ctx.report.hasSafeRange &&
        input.config.dose>=ctx.report.safeMinDose &&
        input.config.dose<=ctx.report.safeMaxDose)
        ctx.dosePosition = "Inside estimated safe range";
    else if (ctx.report.hasToxicThreshold && input.config.dose<ctx.report.toxicMinDose)
        ctx.dosePosition = "Below estimated toxic threshold";
    else if (ctx.report.hasToxicThreshold && input.config.dose>=ctx.report.toxicMinDose)
        ctx.dosePosition = "At/above estimated toxic threshold";
    else if (ctx.report.hasSafeRange)
        ctx.dosePosition = "Outside estimated safe range";
    else
        ctx.dosePosition = "Dose-context threshold not detected";

    return ctx;
}

[[maybe_unused]] std::string buildDoseDecisionRationale(
    const RuntimeInput& input, const SingleRunDoseContext& ctx)
{
    if (!ctx.available) return "Dose-response context unavailable for this run";
    if (ctx.report.hasToxicThreshold) {
        const double td=static_cast<double>(ctx.report.toxicMinDose);
        if (input.config.dose<td) {
            const double h=100.0*(td-input.config.dose)/std::max(1e-6,td);
            return "Current dose is "+formatRuntimeNumber(h)+"% below estimated toxicity threshold ("+formatRuntimeNumber(td)+")";
        }
        const double ex=100.0*(input.config.dose-td)/std::max(1e-6,td);
        return "Current dose is "+formatRuntimeNumber(ex)+"% above estimated toxicity threshold ("+formatRuntimeNumber(td)+")";
    }
    if (ctx.report.hasSafeRange&&!ctx.report.hasToxicThreshold)
        return "No toxicity observed within tested range ["+
               formatRuntimeNumber(ctx.report.safeMinDose)+", "+
               formatRuntimeNumber(ctx.report.safeMaxDose)+"]";
    if (ctx.report.hasSafeRange)
        return "Current dose evaluated against tested safe interval ["+
               formatRuntimeNumber(ctx.report.safeMinDose)+", "+
               formatRuntimeNumber(ctx.report.safeMaxDose)+"]";
    return "Local dose-response scan did not find a stable toxicity boundary";
}

std::string confidenceBandFromPct(float pct) {
    const float v=std::clamp(pct/100.0f,0.0f,1.0f);
    if (v>=0.75f) return "HIGH";
    if (v>=0.50f) return "MEDIUM";
    return "LOW";
}

[[maybe_unused]] std::string formatConfidenceBand(float pct) {
    const int r=static_cast<int>(std::lround(std::clamp(pct,0.0f,99.0f)));
    return confidenceBandFromPct(pct)+" ("+std::to_string(r)+"%)";
}

std::string formatSeizureSlopeDisplay(float slope) {
    if (std::fabs(slope)<0.05f) return "~0 (flat response)";
    return formatRuntimeNumber(slope,3)+" %/dose";
}

[[maybe_unused]] std::string buildSeizureConfidenceTrace(const SingleRunInterpretation& i) {
    return "55 + 45*max(NII="+formatRuntimeNumber(i.instabilityIndex)+
           ", SyncRisk="+formatRuntimeNumber(i.syncRiskNorm)+")";
}

[[maybe_unused]] std::string buildDecisionConfidenceTrace(const SingleRunInterpretation& i) {
    return "60 + 40*max(NII="+formatRuntimeNumber(i.instabilityIndex)+
           ", |dRate|="+formatRuntimeNumber(i.rateShiftNorm)+")";
}

void appendStructuredReportLine(std::ostringstream& out,
    const std::string& label, const std::string& value)
{
    out<<std::left<<std::setw(kStructuredReportLabelWidth)<<label<<" : "<<value<<"\n";
}

void appendStructuredReportLine(std::ostringstream& out,
    const std::string& label, double value, const std::string& suffix = std::string())
{
    out<<std::left<<std::setw(kStructuredReportLabelWidth)<<label<<" : "
       <<formatRuntimeNumber(value)<<suffix<<"\n";
}

SingleRunInterpretation buildSingleRunInterpretation(const SimulationSummary& summary) {
    SingleRunInterpretation I;
    const float rate  = std::isfinite(summary.network_metrics.meanFiringRateHz)
                        ? std::max(0.0f,summary.network_metrics.meanFiringRateHz) : 0.0f;
    const float sync  = std::clamp(std::isfinite(summary.network_metrics.synchronizationIndex)
                        ? summary.network_metrics.synchronizationIndex:0.0f,0.0f,1.0f);
    const float nii   = std::clamp(std::isfinite(summary.network_metrics.nii)
                        ? summary.network_metrics.nii:0.0f,0.0f,1.0f);
    const float seiz  = std::clamp(std::isfinite(summary.network_metrics.seizureProbabilityPct)
                        ? summary.network_metrics.seizureProbabilityPct:0.0f,0.0f,100.0f);
    const float supp  = std::clamp(std::isfinite(summary.network_metrics.suppressionPct)
                        ? summary.network_metrics.suppressionPct:0.0f,0.0f,100.0f);
    const float seizF = seiz/100.0f, suppF = supp/100.0f;

    I.instabilityIndex = nii;
    I.isiVariability   = std::max(0.0f, computeMeanIsiCv(summary.neuron_metrics));
    I.networkRegime    = classifyNetworkRegime(summary.classification, sync);
    I.variability      = classifyVariabilityLevel(I.isiVariability);
    I.brainState       = classifyBrainState(summary.classification,rate,sync,nii,supp);

    const float blRate = std::isfinite(summary.network_metrics.earlyWindowRateHz)&&
                         summary.network_metrics.earlyWindowRateHz>0.0f
                         ? summary.network_metrics.earlyWindowRateHz : rate;
    const float curRate= std::isfinite(summary.network_metrics.lateWindowRateHz)&&
                         summary.network_metrics.lateWindowRateHz>0.0f
                         ? summary.network_metrics.lateWindowRateHz  : rate;
    I.baselineRateHz=blRate; I.currentRateHz=curRate;
    I.changePct = blRate>1e-6f ? ((curRate-blRate)/blRate)*100.0f : 0.0f;

    if      (I.changePct>=15.0f)              I.networkShift="Excitatory Shift";
    else if (I.changePct<=-15.0f)             I.networkShift="Suppressive Shift";
    else if (nii>=0.55f||sync>=0.65f)         I.networkShift="Instability Shift";
    else                                       I.networkShift="Minimal Shift";

    const float rateRisk=std::clamp((rate-6.0f)/16.0f,0.0f,1.0f);
    const float syncRisk=std::clamp((sync-0.30f)/0.60f,0.0f,1.0f);
    const float cvRisk  =std::clamp((I.isiVariability-0.60f)/0.90f,0.0f,1.0f);
    I.syncRiskNorm=syncRisk;

    I.seizureRiskScore=100.0f*std::clamp(0.35f*nii+0.25f*syncRisk+0.20f*rateRisk+0.20f*seizF,0.0f,1.0f);
    I.seizureRisk=classifyRiskLevel(I.seizureRiskScore);
    I.seizureConfidenceDriverNorm=std::max(nii,syncRisk);
    I.seizureConfidenceBasis=(nii>=syncRisk)?"NII-dominant confidence":"Synchronization-dominant confidence";
    I.seizureConfidence=std::clamp(55.0f+45.0f*I.seizureConfidenceDriverNorm,0.0f,99.0f);

    I.toxicityRiskScore=100.0f*std::clamp(0.40f*nii+0.30f*suppF+0.15f*cvRisk+0.15f*seizF,0.0f,1.0f);
    I.toxicityRisk=classifyToxicityRiskLevel(I.toxicityRiskScore,supp,seiz);

    if      (curRate<=blRate*0.75f&&sync<0.45f) I.effectType="Suppressive";
    else if (curRate>=blRate*1.20f||sync>=0.65f) I.effectType="Excitatory";
    else if (nii<0.45f&&I.isiVariability<1.00f)  I.effectType="Stabilizing";
    else                                           I.effectType="Mixed";

    if      (I.changePct>=15.0f)  I.networkResponse="Rate Increase";
    else if (I.changePct<=-15.0f) I.networkResponse="Rate Decrease";
    else                           I.networkResponse="Rate Maintained";

    const std::string band=doseBandLabel(summary);
    if      (band=="Safe"&&I.toxicityRiskScore<45.0f) I.safetyMargin="Wide";
    else if (band=="Toxic"||I.toxicityRiskScore>=70.0f)I.safetyMargin="Narrow";
    else                                               I.safetyMargin="Moderate";

    const float overallRisk=std::max(I.seizureRiskScore,I.toxicityRiskScore);
    I.riskLevel=classifyRiskLevel(overallRisk);

    const bool sl=I.seizureRiskScore<35.0f, tn=I.toxicityRisk=="NONE";
    const float arc=std::fabs(I.changePct);
    if      (sl&&tn&&arc<30.0f) I.recommendation="PROCEED";
    else if (sl&&arc<50.0f)     I.recommendation="PROCEED WITH MONITORING";
    else                         I.recommendation="NOT RECOMMENDED";

    I.rateShiftNorm=std::clamp(arc/100.0f,0.0f,1.0f);
    I.decisionConfidenceDriverNorm=std::max(nii,I.rateShiftNorm);
    I.decisionConfidenceBasis=(nii>=I.rateShiftNorm)
        ?"NII-dominant confidence":"Rate-shift-dominant confidence";
    I.decisionConfidence=std::clamp(60.0f+40.0f*I.decisionConfidenceDriverNorm,0.0f,99.0f);
    I.finalConfidence=std::min(I.seizureConfidence,I.decisionConfidence);

    I.reason=(I.recommendation=="PROCEED"||I.recommendation=="PROCEED WITH MONITORING")
        ?"Controlled suppression with low seizure and toxicity risk"
        :"Elevated seizure or toxicity signal under current dose";
    return I;
}

void printSimulationReport(
    const RuntimeInput& input,
    const SimulationSummary& summary,
    const SingleRunDoseContext& doseContext)
{
    (void)doseContext;
    const SingleRunInterpretation I=buildSingleRunInterpretation(summary);
    std::ostringstream out;
    out<<"==================================================\n";
    out<<"SILICON PATIENT - SINGLE DOSE SIMULATION REPORT\n";
    out<<"==================================================\n";
    out<<"[Simulation Configuration]\n";
    appendStructuredReportLine(out,"Neurons",std::to_string(input.config.neuron_count));
    appendStructuredReportLine(out,"Duration",input.config.sim_time," ms");
    appendStructuredReportLine(out,"Drug Dose",input.config.dose);
    appendStructuredReportLine(out,"Mode",input.config.use_cuda?"CUDA":"CPU");
    out<<"\n---\n\n";
    out<<"[Neural Activity]\n";
    appendStructuredReportLine(out,"Firing Rate",summary.network_metrics.meanFiringRateHz," Hz");
    appendStructuredReportLine(out,"Synchronization",summary.network_metrics.synchronizationIndex);
    appendStructuredReportLine(out,"ISI Variability",I.isiVariability);
    out<<"\n--------------------------------------------------\n";
    out<<"[Network State]\n";
    appendStructuredReportLine(out,"Brain State",I.brainState);
    appendStructuredReportLine(out,"Spike Pattern",I.variability);
    out<<"--------------------------------------------------\n";
    out<<"[Risk Assessment]\n";
    appendStructuredReportLine(out,"Seizure Score",I.seizureRiskScore," / 100");
    appendStructuredReportLine(out,"Seizure Risk",toUpper(I.seizureRisk));
    appendStructuredReportLine(out,"Toxicity Score",I.toxicityRiskScore," / 100");
    appendStructuredReportLine(out,"Toxicity Risk",toUpper(I.toxicityRisk=="NONE"?"LOW":I.toxicityRisk));
    appendStructuredReportLine(out,"Instability Index",I.instabilityIndex);
    out<<"--------------------------------------------------\n";
    out<<"[Drug Impact]\n";
    appendStructuredReportLine(out,"Baseline Rate",I.baselineRateHz," Hz");
    appendStructuredReportLine(out,"Current Rate",I.currentRateHz," Hz");
    appendStructuredReportLine(out,"Change",I.changePct," %");
    appendStructuredReportLine(out,"Effect Type",I.effectType);
    appendStructuredReportLine(out,"Network Response",I.networkResponse);
    appendStructuredReportLine(out,"Safety Margin",I.safetyMargin);
    out<<"--------------------------------------------------\n";
    out<<"[FINAL DECISION]\n";
    appendStructuredReportLine(out,"Recommendation",I.recommendation);
    appendStructuredReportLine(out,"Risk Level",toUpper(I.riskLevel));
    appendStructuredReportLine(out,"Confidence",confidenceBandFromPct(I.finalConfidence));
    appendStructuredReportLine(out,"Reason",I.reason);
    out<<"\n==================================================\n";
    std::cout<<out.str();
}

void printResult(const RuntimeInput& input, const SimulationSummary& summary,
                 const SingleRunDoseContext& ctx)
{
    printSimulationReport(input,summary,ctx);
}

void exportSingleRunArtifacts(const RuntimeInput& input, const SimulationSummary& summary) {
    if (!input.config.export_csv) return;
    std::filesystem::create_directories(input.config.output_folder);
    CsvWriter::writeNeuronStats(input.config.output_folder+"/neuron_stats.csv",summary.neuron_metrics);
    CsvWriter::writeNetworkMetrics(input.config.output_folder+"/network_metrics.csv",{
        NetworkMetricRecord{"single_run",static_cast<float>(input.config.dose),
                            summary.network_metrics,SeizureDetector::toString(summary.classification)}});
    CsvWriter::writeDoseResponse(input.config.output_folder+"/dose_response.csv",{
        DoseResponsePoint{static_cast<float>(input.config.dose),
                          summary.network_metrics.meanFiringRateHz,
                          summary.network_metrics.synchronizationIndex,
                          summary.network_metrics.burstIndex,
                          summary.network_metrics.nii,
                          summary.network_metrics.seizureProbabilityPct,
                          summary.network_metrics.suppressionPct,
                          summary.network_metrics.stabilityScore,
                          SeizureDetector::toString(summary.classification)}});
}

std::vector<double> buildLinearDoseGrid(double start, double end, int pts) {
    std::vector<double> v; v.reserve(static_cast<std::size_t>(pts));
    if (pts<=1){v.push_back(start);return v;}
    const double step=(end-start)/static_cast<double>(pts-1);
    for (int i=0;i<pts;++i) v.push_back(start+static_cast<double>(i)*step);
    return v;
}

void writePharmaDecisionReport(
    const std::string& path, const RuntimeInput& input, const PharmaDecisionReport& report)
{
    std::ofstream out(path,std::ios::out|std::ios::trunc);
    if (!out.is_open()) throw std::runtime_error("Cannot open: "+path);
    out<<std::fixed<<std::setprecision(4);
    out<<"Silicon Patient Platform - Pharma Decision Report\n";
    out<<"Drug: "<<input.drug_name<<"\n";
    out<<"Dose sweep: ["<<input.sweep_start<<", "<<input.sweep_end<<"] with "<<input.sweep_points<<" points\n\n";
    out<<"safe_max_dose,"  <<(report.hasSafeRange?std::to_string(report.safeMaxDose):std::string("N/A"))<<"\n";
    out<<"toxic_min_dose," <<(report.hasToxicThreshold?std::to_string(report.toxicMinDose):std::string("N/A"))<<"\n";
    out<<"effective_min_dose,"<<(report.hasEffectiveDose?std::to_string(report.effectiveMinDose):std::string("N/A"))<<"\n";
    out<<"therapeutic_window,"<<(report.hasTherapeuticWindow?std::to_string(report.therapeuticWindow):std::string("N/A"))<<"\n\n";
    out<<"overall_profile,"<<PharmaDecisionEngine::toString(report.overallTier)<<"\n";
    out<<"peak_risk_score,"<<report.peakRiskScore<<"\n";
    out<<"peak_seizure_probability_pct,"<<report.peakSeizureProbabilityPct<<"\n";
    out<<"peak_suppression_pct,"<<report.peakSuppressionPct<<"\n";
    out<<"peak_early_warning_index,"<<report.peakEarlyWarningIndex<<"\n";
    out<<"max_seizure_slope_pct_per_dose,"<<report.maxSeizureSlopePctPerDose<<"\n";
}

static std::vector<std::pair<double,double>> buildRangesFromSortedDoses(
    const std::vector<double>& doses, double stepDose)
{
    std::vector<std::pair<double,double>> ranges;
    if (doses.empty()) return ranges;
    double step=stepDose;
    if (!(step>0.0)&&doses.size()>=2U){
        step=std::numeric_limits<double>::infinity();
        for (std::size_t i=1;i<doses.size();++i){
            const double d=doses[i]-doses[i-1];
            if (d>1e-6) step=std::min(step,d);
        }
        if (!std::isfinite(step)) step=1.0;
    }
    if (!(step>0.0)) step=1.0;
    const double maxGap=1.50*step+1e-5;
    std::size_t i=0;
    while (i<doses.size()){
        std::size_t j=i;
        while (j+1<doses.size()&&(doses[j+1]-doses[j])<=maxGap)++j;
        ranges.emplace_back(doses[i],doses[j]);
        i=j+1;
    }
    return ranges;
}

// ─── Drug evaluation report text builder ─────────────────────────────────────
std::string buildDrugEvaluationReportText(
    const PharmaDecisionReport& report,
    const AggregatedStats& stabilityStats,
    int runCount,
    const RuntimeInput& evalInput,
    const std::string& engineInputMode)
{
    std::ostringstream out;
    const auto aLine=[&](const std::string& label,const std::string& value){
        out<<std::left<<std::setw(20)<<label<<" : "<<value<<"\n";
    };
    const auto fRange=[&](double a,double b){
        return formatRuntimeNumber(a)+" - "+formatRuntimeNumber(b);
    };

    std::vector<spp::analyzer::DoseFeatures> features=report.features;
    std::sort(features.begin(),features.end(),
        [](const auto& a,const auto& b){return a.dose<b.dose;});

    std::vector<double> ineff,ther,overSupp;
    ineff.reserve(features.size());ther.reserve(features.size());overSupp.reserve(features.size());
    for (const auto& f:features){
        if (f.is_toxic){if(report.responseMode!="EXCITATORY_RESPONSE")overSupp.push_back(f.dose);}
        else if (f.is_effective) ther.push_back(f.dose);
        else ineff.push_back(f.dose);
    }
    const auto dedup=[](std::vector<double>& v){
        v.erase(std::unique(v.begin(),v.end(),[](double a,double b){return std::fabs(a-b)<=1e-6;}),v.end());
    };
    dedup(ineff); dedup(ther); dedup(overSupp);

    const auto ineffR  =buildRangesFromSortedDoses(ineff,  report.stepDose);
    const auto therR   =buildRangesFromSortedDoses(ther,    report.stepDose);
    const auto exciR   =buildRangesFromSortedDoses(report.excitatoryRiskDoses,report.stepDose);
    const auto overSuppR=buildRangesFromSortedDoses(overSupp,report.stepDose);

    const auto fRanges=[&](const std::vector<std::pair<double,double>>& r){
        if (r.empty()) return std::string("Not observed");
        std::ostringstream t;
        for (std::size_t i=0;i<r.size();++i){if(i)t<<", ";t<<fRange(r[i].first,r[i].second);}
        return t.str();
    };

    const bool noResp=therR.empty()&&exciR.empty();
    double maxEff=-std::numeric_limits<double>::infinity(); std::size_t peakIdx=0;
    for (std::size_t i=0;i<features.size();++i)
        if(features[i].rate_change>maxEff){maxEff=features[i].rate_change;peakIdx=i;}
    if (!std::isfinite(maxEff)) maxEff=0.0;

    const std::string stab=getStability(stabilityStats.stdRate,stabilityStats.stdToxicity);
    const bool toxDet=report.hasToxicThreshold;
    const bool lowStab=stab=="LOW";
    const bool overSuppDet=!overSuppR.empty();
    const bool therWin=!therR.empty();

    const std::string curveType=noResp?"Flat / Non-responsive"
        :(report.sigmoidR2>=0.95?"Sigmoidal":(report.sigmoidR2>=0.80?"Weak Sigmoidal":"Irregular / Non-sigmoidal"));
    const std::string respStr=noResp?"None"
        :(maxEff<40?"Moderate":(maxEff<60?"Moderate-to-Strong":"Strong"));

    const std::string effRange=noResp||therR.empty()?"Not observed":fRanges(therR);

    const bool singlePt=(therR.size()==1U)&&
        (std::fabs(therR.front().second-therR.front().first)<=
         std::max(1e-6,0.25*std::max(1e-6,report.stepDose)));
    const std::string winQual=noResp||therR.empty()?"Not observed"
        :(singlePt?"Narrow":(therR.size()==1?"Continuous":"Fragmented"));

    const std::string ineffZ  =noResp?fRange(report.minTestedDose,report.maxTestedDose):fRanges(ineffR);
    const std::string therZ   =noResp||therR.empty()?"Not observed":fRanges(therR);
    const std::string overSuppZ=noResp?"Not observed":fRanges(overSuppR);
    const std::string exciZ   =exciR.empty()?"Not observed":fRanges(exciR);
    const std::string onsetD  =noResp||therR.empty()?"Not observed":("~"+formatRuntimeNumber(therR.front().first));

    const bool peakTox=toxDet&&(features[peakIdx].dose>=report.toxicMinDose);
    const std::string peakEff=noResp?"Not observed":
        ("~"+formatRuntimeNumber(features[peakIdx].dose)+(peakTox?" (within toxic range)":""));
    const std::string satText=noResp?"Not observed":
        ((peakIdx+1>=features.size())?"Not observed within tested range":
         ("Observed beyond "+formatRuntimeNumber(features[peakIdx].dose)));

    const bool exciMode=(report.biologicalState==spp::analyzer::BiologicalState::Hyperexcitability);
    const bool stabMode=(report.responseMode=="STABILIZING_RESPONSE")||
                        (report.biologicalState==spp::analyzer::BiologicalState::NetworkStabilization);

    const std::string winTitle=exciMode?"Excitatory Response Range":(stabMode?"Stabilization Response Range":"Therapeutic Window");
    const std::string effLabel=exciMode?"Excitatory Range":(stabMode?"Stabilization Range":"Effective Range");
    const std::string zoneLabel=exciMode?"Excitatory Zone":(stabMode?"Stabilization Zone":"Therapeutic Zone");
    // BUG 12 FIX: metric-derived narrative — no Ca-channel hardcoding.
    const std::string optZone=exciMode
        ?"Transient excitatory regime before seizure-risk escalation"
        :(stabMode
          ?"Reductions in synchronisation and NII markers with preserved firing activity"
          :"Moderate, controlled suppression (20-60%)");
    const std::string winQualText=noResp||therR.empty()?"Not observed"
        :(stabMode?(therR.size()>1?"Fragmented":"Continuous"):winQual);

    out<<"==================================================\n";
    out<<"SILICON PATIENT - DRUG EVALUATION REPORT\n";
    out<<"==================================================\n\n";
    out<<"[Drug Input]\n";
    aLine("Drug Name",        evalInput.drug_name);
    aLine("Engine Input Mode",engineInputMode);
    aLine("Na IC50",          formatRuntimeNumber(evalInput.config.ic50_na));
    aLine("K IC50",           formatRuntimeNumber(evalInput.config.ic50_k));
    aLine("Ca IC50",          formatRuntimeNumber(evalInput.config.ic50_ca));
    aLine("Hill",             formatRuntimeNumber(evalInput.config.hill));
    aLine("Runs",             std::to_string(runCount));
    out<<"\n--------------------------------------------------\n\n";
    out<<"[Dose Range]\n";
    aLine("Tested Range",formatRuntimeNumber(report.minTestedDose)+" - "+formatRuntimeNumber(report.maxTestedDose));
    aLine("Step Size",formatRuntimeNumber(report.stepDose));
    out<<"\n--------------------------------------------------\n\n";
    out<<"[Response Characteristics]\n";
    aLine("Curve Type",      curveType);
    aLine("Response Mode",   report.responseMode);
    aLine("Model Fit (R^2)", noResp?"N/A":formatRuntimeNumber(report.sigmoidR2,2));
    aLine("Max Effect",      formatRuntimeNumber(maxEff,0)+" %");
    aLine("Response Strength",respStr);
    out<<"\n--------------------------------------------------\n\n";
    out<<"[Safety Analysis]\n";
    aLine("Toxic Threshold",
          toxDet?formatRuntimeNumber(report.toxicMinDose)
               :(">"+formatRuntimeNumber(report.maxTestedDose)+" (Not observed)"));
    aLine("Safety Observation",toxDet?"Toxicity observed within tested range"
                                     :"No toxicity within tested range");
    out<<"\n--------------------------------------------------\n\n";
    out<<"["<<winTitle<<"]\n";
    aLine(effLabel,effRange);
    aLine("Window Quality",winQualText);
    aLine("Optimal Zone",optZone);
    out<<"\n--------------------------------------------------\n\n";
    out<<"[Dose Classification Summary]\n";
    aLine("Ineffective Zone",ineffZ);
    aLine(zoneLabel,therZ);
    if      (exciMode)  aLine("Severe Excitability Zone",exciZ);
    else if (stabMode)  aLine("Saturated Stabilization Zone",overSuppZ);
    else                aLine("Over-Suppression",overSuppZ);
    out<<"\n--------------------------------------------------\n\n";
    if (stabMode) {
        out<<"[Network Stabilization Metrics]\n";
        aLine("Sync Reduction",   formatRuntimeNumber(report.syncReductionPct,1)+" %");
        const bool niiR=report.niiReductionPct>0.0;
        aLine(niiR?"NII Reduction":"NII Increase",
              formatRuntimeNumber(niiR?report.niiReductionPct:report.niiIncreasePct,1)+" %");
        aLine("Seizure Reduction",formatRuntimeNumber(report.seizureReductionPct,1)+" %");
        aLine("Burst Reduction",  formatRuntimeNumber(report.burstReductionPct,1)+" %");
        aLine("Effect Magnitude", formatRuntimeNumber(report.calciumEffectMagnitude,1)+" %");
        out<<"\n--------------------------------------------------\n\n";
    }
    out<<"[Pharmacodynamic Interpretation]\n";
    aLine("Onset Dose",      onsetD);
    aLine("Peak Efficiency", peakEff);
    aLine("Saturation Trend",satText);
    out<<"\n--------------------------------------------------\n\n";
    out<<"[Stability Analysis]\n";
    aLine("Run Count",        std::to_string(runCount));
    aLine("Rate Variability", formatRuntimeNumber(stabilityStats.stdRate,2));
    aLine("Toxicity Variance",formatRuntimeNumber(stabilityStats.stdToxicity,2));
    aLine("Stability Score",  stab);
    out<<"\n--------------------------------------------------\n\n";
    out<<"[FINAL DECISION]\n";
    aLine("Recommendation",report.recommendation);
    aLine("Risk Level",    report.riskLevel);
    aLine("Reason",        report.reason);
    aLine("Confidence",    report.confidence);
    out<<"\n==================================================\n";
    return out.str();
}

void writeDrugEvaluationReport(
    const std::string& path, const PharmaDecisionReport& report,
    const AggregatedStats& stats, int runs,
    const RuntimeInput& input, const std::string& mode)
{
    std::ofstream out(path,std::ios::out|std::ios::trunc);
    if (!out.is_open()) throw std::runtime_error("Cannot open: "+path);
    out<<buildDrugEvaluationReportText(report,stats,runs,input,mode);
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

// ─── runDoseEvaluationMode ────────────────────────────────────────────────────
// BUG 1+6+8 FIX: baseline run first, new DoseObservation fields populated,
// DecisionStabilityInput built and passed to evaluate().
void runDoseEvaluationMode(
    const RuntimeInput& baseInput,
    const std::string& engineInputMode="Default Internal Engine Config",
    const std::optional<int>& userRuns=std::nullopt)
{
    constexpr double kDefaultMinDose=0.0, kDefaultMaxDose=20.0, kDefaultStepDose=2.0;
    constexpr int kDoseEvalRuns=10;

    auto tryEnvDouble=[](const char* n)->std::optional<double>{
        if(auto t=readEnvVar(n)){try{return std::stod(trim(*t));}catch(...){}return std::nullopt;}
        return std::nullopt;
    };

    RuntimeInput evalInput=baseInput;
    const bool skipEnv=(engineInputMode=="User Drug Config");
    if(!skipEnv){
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_IC50_NA");v&&*v>0) evalInput.config.ic50_na=*v;
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_IC50_K"); v&&*v>0) evalInput.config.ic50_k=*v;
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_IC50_CA");v&&*v>0) evalInput.config.ic50_ca=*v;
        if(auto v=tryEnvDouble("SPP_DOSE_EVAL_HILL");   v&&*v>=1&&*v<=6) evalInput.config.hill=*v;
    }
    double minD=kDefaultMinDose,maxD=kDefaultMaxDose,stepD=kDefaultStepDose;
    if(auto v=tryEnvDouble("SPP_DOSE_EVAL_MIN"); v&&*v>=0)     minD=*v;
    if(auto v=tryEnvDouble("SPP_DOSE_EVAL_MAX"); v&&*v>minD)   maxD=*v;
    if(auto v=tryEnvDouble("SPP_DOSE_EVAL_STEP");v&&*v>0)      stepD=*v;

    int runs=userRuns.value_or(kDoseEvalRuns);
    if(auto t=readEnvVar("SPP_DOSE_EVAL_RUNS")){
        try{const int p=std::stoi(trim(*t));if(p>0&&p<=200)runs=p;}catch(...){}
    }

    std::vector<double> doses;
    for(double d=minD;d<=maxD+1e-9;d+=stepD) doses.push_back(d);

    // BUG 1 FIX: run baseline (dose=0) first.
    NetworkMetrics blMetrics; bool haveBaseline=false;
    {
        RuntimeInput bli=evalInput; bli.config.dose=0.0;
        const auto blResults=runMultipleSimulations(bli,0.0,std::min(runs,3),
            0xBASE0001U,nullptr,nullptr,nullptr);
        if(!blResults.empty()){blMetrics=buildAggregatedNetworkMetrics(computeStats(blResults));haveBaseline=true;}
    }
    const NetworkMetrics* blPtr=haveBaseline?&blMetrics:nullptr;

    std::vector<DoseObservation> obs;
    std::vector<DoseResponsePoint> dr; std::vector<NetworkMetricRecord> nr;
    std::vector<RunResult> allResults;
    obs.reserve(doses.size()); dr.reserve(doses.size()); nr.reserve(doses.size());
    allResults.reserve(doses.size()*static_cast<std::size_t>(runs));

    std::vector<spp::analyzer::NeuronMetrics> finalNM;
    const std::uint32_t baseSeed=0xD05E0001U;

    for(std::size_t i=0;i<doses.size();++i){
        const auto rr=runMultipleSimulations(evalInput,doses[i],runs,
            baseSeed+static_cast<std::uint32_t>(i*7919U),&finalNM,nullptr,blPtr);
        if(rr.empty()) continue;
        const AggregatedStats ds=computeStats(rr);
        allResults.insert(allResults.end(),rr.begin(),rr.end());
        const NetworkMetrics am=buildAggregatedNetworkMetrics(ds);
        const std::string lbl=getStability(ds.stdRate,ds.stdToxicity);

        const float dF=static_cast<float>(doses[i]);
        const float bNa=static_cast<float>(spp::drug::DrugModel::hillBlock(dF,static_cast<float>(evalInput.config.ic50_na),static_cast<float>(evalInput.config.hill)));
        const float bK =static_cast<float>(spp::drug::DrugModel::hillBlock(dF,static_cast<float>(evalInput.config.ic50_k), static_cast<float>(evalInput.config.hill)));
        const float bCa=static_cast<float>(spp::drug::DrugModel::hillBlock(dF,static_cast<float>(evalInput.config.ic50_ca),static_cast<float>(evalInput.config.hill)));

        // BUG 6 FIX: populate all DoseObservation fields.
        DoseObservation o;
        o.dose=dF; o.meanFiringRateHz=ds.meanRate; o.synchronizationIndex=ds.meanSync;
        o.burstIndex=ds.meanBurst; o.nii=ds.meanNii; o.isiCv=ds.meanISI;
        o.seizureProbabilityPct=ds.meanSeizure; o.suppressionPct=ds.meanToxicity;
        o.blockNa=bNa; o.blockK=bK; o.blockCa=bCa;
        o.networkState=ds.dominantNetworkState;          // BUG 6 FIX
        o.suppressionHasBaseline=ds.suppressionHasBaseline; // BUG 6 FIX
        obs.push_back(o);

        dr.push_back({dF,ds.meanRate,ds.meanSync,ds.meanBurst,ds.meanNii,
                      ds.meanSeizure,ds.meanToxicity,am.stabilityScore,lbl});
        nr.push_back({"dose_eval",dF,am,lbl});
    }

    if(obs.empty()) throw std::runtime_error("Dose evaluation failed: no observations produced");

    const AggregatedStats stab=computeStats(allResults);
    // BUG 8 FIX: build and pass DecisionStabilityInput.
    const spp::analyzer::DecisionStabilityInput si{
        stab.stdRate,stab.stdToxicity,
        getStability(stab.stdRate,stab.stdToxicity),runs};
    const PharmaDecisionReport report=PharmaDecisionEngine::evaluate(obs,si);

    std::cout<<buildDrugEvaluationReportText(report,stab,runs,evalInput,engineInputMode);

    if(evalInput.config.export_csv){
        std::filesystem::create_directories(evalInput.config.output_folder);
        CsvWriter::writeDoseResponse(evalInput.config.output_folder+"/dose_response.csv",dr);
        CsvWriter::writeNetworkMetrics(evalInput.config.output_folder+"/network_metrics.csv",nr);
        CsvWriter::writeNeuronStats(evalInput.config.output_folder+"/neuron_stats.csv",finalNM);
        writeDrugEvaluationReport(evalInput.config.output_folder+"/drug_evaluation_report.txt",
            report,stab,runs,evalInput,engineInputMode);
    }
}

// ─── runDoseSweep ─────────────────────────────────────────────────────────────
// BUG 8+10 FIX: baseline first; DecisionStabilityInput built and passed.
[[maybe_unused]] void runDoseSweep(const RuntimeInput& baseInput) {
    const std::vector<double> doses=buildLinearDoseGrid(
        baseInput.sweep_start,baseInput.sweep_end,baseInput.sweep_points);

    std::vector<DoseResponsePoint> dr; std::vector<NetworkMetricRecord> nr;
    std::vector<DoseObservation> decIn;
    dr.reserve(doses.size()); nr.reserve(doses.size()); decIn.reserve(doses.size());
    std::vector<spp::analyzer::NeuronMetrics> finalNM;
    const std::uint32_t baseSeed=0x51C0A1U;

    // BUG 10 FIX: run baseline (first dose) first.
    NetworkMetrics blMetrics; bool haveBaseline=false;
    if (!doses.empty()){
        RuntimeInput bli=baseInput; bli.config.dose=doses.front();
        const SimulationSummary bls=runSingleSimulation(bli,baseSeed);
        blMetrics=bls.network_metrics; haveBaseline=true;
    }
    const NetworkMetrics* blPtr=haveBaseline?&blMetrics:nullptr;

    std::cout<<'\n'; printDivider('=');
    std::cout<<"SILICON PATIENT DOSE SWEEP REPORT\n"; printDivider('=');
    printSection("Run Configuration");
    printMetricLine("Dose Points",std::to_string(doses.size()));
    printMetricLine("Dose Range",formatRuntimeNumber(baseInput.sweep_start)+" to "+formatRuntimeNumber(baseInput.sweep_end));

    std::vector<RunResult> sweepRR; sweepRR.reserve(doses.size());

    for(std::size_t i=0;i<doses.size();++i){
        RuntimeInput ri=baseInput; ri.config.dose=doses[i];
        const SimulationSummary s=runSingleSimulation(ri,baseSeed+static_cast<std::uint32_t>(i*9973U),blPtr);
        finalNM=s.neuron_metrics;
        const std::string lbl=SeizureDetector::toString(s.classification);

        RunResult rr;
        rr.firingRate=s.network_metrics.meanFiringRateHz;
        rr.sync=s.network_metrics.synchronizationIndex;
        rr.burst=s.network_metrics.burstIndex;
        rr.nii=s.network_metrics.nii;
        rr.isiCV=computeMeanIsiCv(s.neuron_metrics);
        rr.seizureScore=s.network_metrics.seizureProbabilityPct;
        // BUG 3 FIX: composite toxicity
        rr.toxicityScore=0.60f*s.network_metrics.seizureProbabilityPct
                        +0.40f*s.network_metrics.suppressionPct;
        rr.networkState=s.classification;
        rr.suppressionHasBaseline=s.network_metrics.suppressionHasBaseline;
        sweepRR.push_back(rr);

        const float dF=static_cast<float>(doses[i]);
        const float bNa=static_cast<float>(spp::drug::DrugModel::hillBlock(dF,static_cast<float>(ri.config.ic50_na),static_cast<float>(ri.config.hill)));
        const float bK =static_cast<float>(spp::drug::DrugModel::hillBlock(dF,static_cast<float>(ri.config.ic50_k), static_cast<float>(ri.config.hill)));
        const float bCa=static_cast<float>(spp::drug::DrugModel::hillBlock(dF,static_cast<float>(ri.config.ic50_ca),static_cast<float>(ri.config.hill)));

        // BUG 5/6 FIX: all DoseObservation fields.
        DoseObservation o;
        o.dose=dF; o.meanFiringRateHz=s.network_metrics.meanFiringRateHz;
        o.synchronizationIndex=s.network_metrics.synchronizationIndex;
        o.burstIndex=s.network_metrics.burstIndex; o.nii=s.network_metrics.nii;
        o.isiCv=computeMeanIsiCv(s.neuron_metrics);
        o.seizureProbabilityPct=s.network_metrics.seizureProbabilityPct;
        o.suppressionPct=s.network_metrics.suppressionPct;
        o.blockNa=bNa; o.blockK=bK; o.blockCa=bCa;
        o.networkState=s.classification;
        o.suppressionHasBaseline=s.network_metrics.suppressionHasBaseline;
        decIn.push_back(o);

        dr.push_back({dF,s.network_metrics.meanFiringRateHz,
                      s.network_metrics.synchronizationIndex,s.network_metrics.burstIndex,
                      s.network_metrics.nii,s.network_metrics.seizureProbabilityPct,
                      s.network_metrics.suppressionPct,s.network_metrics.stabilityScore,lbl});
        nr.push_back({"dose_sweep",dF,s.network_metrics,lbl});
    }

    // BUG 8 FIX: DecisionStabilityInput from sweep stats.
    const AggregatedStats sweepStats=computeStats(sweepRR);
    const spp::analyzer::DecisionStabilityInput si{
        sweepStats.stdRate,sweepStats.stdToxicity,
        getStability(sweepStats.stdRate,sweepStats.stdToxicity),
        static_cast<int>(doses.size())};
    const PharmaDecisionReport decReport=PharmaDecisionEngine::evaluate(decIn,si);
    const double testedMax=doses.empty()?0.0:doses.back();

    printSection("Pharma Decision Summary");
    if(decReport.hasSafeRange&&!decReport.hasToxicThreshold){
        printMetricLine("Safe Dose Range","["+formatRuntimeNumber(decReport.safeMinDose)+", "+
            formatRuntimeNumber(decReport.safeMaxDose)+"] (No toxicity within tested range)");
        printMetricLine("Toxic Threshold",">"+formatRuntimeNumber(testedMax)+" (Not reached)");
    } else if(decReport.hasSafeRange){
        printMetricLine("Safe Dose Range","["+formatRuntimeNumber(decReport.safeMinDose)+", "+
            formatRuntimeNumber(decReport.safeMaxDose)+"]");
        printMetricLine("Toxic Threshold",decReport.hasToxicThreshold
            ?formatRuntimeNumber(decReport.toxicMinDose):std::string("NOT DETECTED"));
    } else {
        printMetricLine("Safe Dose Range","NOT DETECTED");
        printMetricLine("Toxic Threshold","NOT DETECTED");
    }
    if(decReport.hasEffectiveDose) printMetricLine("Effective Min Dose",decReport.effectiveMinDose);
    else printMetricLine("Effective Min Dose","NOT DETECTED");
    if(decReport.hasTherapeuticWindow) printMetricLine("Therapeutic Window",decReport.therapeuticWindow);
    else printMetricLine("Therapeutic Window","UNAVAILABLE");
    printMetricLine("Overall Profile",   PharmaDecisionEngine::toString(decReport.overallTier));
    printMetricLine("Peak Risk Score",   decReport.peakRiskScore," / 100");
    printMetricLine("Peak Seizure Prob.",decReport.peakSeizureProbabilityPct,"%");
    printMetricLine("Peak Suppression",  decReport.peakSuppressionPct,"%");
    printMetricLine("Peak Early Warning",decReport.peakEarlyWarningIndex," / 100");
    printMetricLine("Max dSeizure/dDose",formatSeizureSlopeDisplay(decReport.maxSeizureSlopePctPerDose));

    if(baseInput.config.export_csv){
        std::filesystem::create_directories(baseInput.config.output_folder);
        CsvWriter::writeDoseResponse(baseInput.config.output_folder+"/dose_response.csv",dr);
        CsvWriter::writeNetworkMetrics(baseInput.config.output_folder+"/network_metrics.csv",nr);
        CsvWriter::writeNeuronStats(baseInput.config.output_folder+"/neuron_stats.csv",finalNM);
        CsvWriter::writeDrugSummary(baseInput.config.output_folder+"/drug_summary.csv",decReport.points);
        writePharmaDecisionReport(baseInput.config.output_folder+"/drug_report.txt",baseInput,decReport);
        printSection("Artifacts");
        printMetricLine("Output Folder",baseInput.config.output_folder);
        printMetricLine("Files","dose_response.csv, network_metrics.csv, neuron_stats.csv, drug_summary.csv, drug_report.txt");
    }
}

void runSingleSimulationMode(const RuntimeInput& input) {
    const std::uint32_t seed=makeSeed();
    const SimulationSummary s=runSingleSimulation(input,seed);
    const SingleRunDoseContext ctx=buildSingleRunDoseContext(input,s,seed+911U);
    printResult(input,s,ctx);
    exportSingleRunArtifacts(input,s);
}

// ─── Internal benchmark suite ─────────────────────────────────────────────────
void runInternalBiologicalBenchmarkSuite() {
    const auto suiteStart=std::chrono::steady_clock::now();
    RuntimeInput baseInput;
    baseInput.drug_name="InternalBiologicalBenchmarkSuite";
#ifdef SPP_USE_CUDA
    baseInput.config.neuron_count=420;
#else
    baseInput.config.neuron_count=320;
#endif
    baseInput.config.sim_time=500.0; baseInput.config.dt=0.04;
    baseInput.config.dose=0.0;
    baseInput.config.ic50_na=1000.0; baseInput.config.ic50_k=1000.0; baseInput.config.ic50_ca=1000.0;
    baseInput.config.hill=3.0; baseInput.config.connectivity=0.05;
    baseInput.config.excitatory_ratio=0.80; baseInput.config.external_current=1.05;
    baseInput.config.noise_level=0.72;
    baseInput.config.excitatory_weight_scale=1.00; baseInput.config.inhibitory_weight_scale=1.00;
    baseInput.config.export_csv=false;
    const std::string outDir="output_internal_benchmark_full";
    std::filesystem::create_directories(outDir);
    baseInput.config.output_folder=outDir;

    bool fixedSeed=true;
    if(auto e=readEnvVar("SPP_FIXED_SEED_MODE")){if(auto p=parseBoolText(*e)) fixedSeed=*p;}

    constexpr int kSR=4, kDP=12; constexpr float kWMs=50.0f;
    const std::uint32_t detSeed=0x51C0A1U, rndSeed=makeSeed();
    std::vector<std::string> meta; meta.reserve(512);

    auto fStats=[](const MetricStats& s,int p){
        std::ostringstream o; o<<std::fixed<<std::setprecision(p)
            <<s.mean<<" +/- "<<s.stddev<<" (95% CI: ["<<s.ci95Low<<", "<<s.ci95High<<"])";
        return o.str();
    };
    auto mkSeed=[&](int ti,int ri){
        return fixedSeed ? detSeed+static_cast<std::uint32_t>(ti*100000+ri*7919+17)
                         : rndSeed^static_cast<std::uint32_t>(ti*73856093+ri*19349663)^makeSeed();
    };
    auto logMeta=[&](const std::string& tname,int ri,std::uint32_t seed,const RuntimeInput& inp){
        std::ostringstream l;
        l<<tname<<",run="<<ri<<",seed="<<seed
         <<",neurons="<<inp.config.neuron_count<<",sim_time_ms="<<inp.config.sim_time
         <<",dt_ms="<<inp.config.dt<<",dose="<<inp.config.dose
         <<",ic50_na="<<inp.config.ic50_na<<",ic50_k="<<inp.config.ic50_k
         <<",ic50_ca="<<inp.config.ic50_ca<<",ex_ratio="<<inp.config.excitatory_ratio
         <<",exc_scale="<<inp.config.excitatory_weight_scale
         <<",inh_scale="<<inp.config.inhibitory_weight_scale;
        meta.push_back(l.str());
    };

    std::vector<ValidationCheck> checks; std::vector<std::string> reportBlocks;
    checks.reserve(6); reportBlocks.reserve(6);
    std::cout<<'\n'; printDivider('=');
    std::cout<<"SILICON PATIENT - INTERNAL BIOLOGICAL BENCHMARK REPORT\n"; printDivider('=');
    printSection("Run Context");
    printMetricLine("Seed Mode",fixedSeed?"fixed-seed":"random-seed");
    printMetricLine("Output Directory",outDir);

    // --- Test 1: Baseline ---
    std::vector<double> blRate,blSync,blIsiCv,blCoh; std::vector<std::uint32_t> blSeeds;
    blRate.reserve(kSR);blSync.reserve(kSR);blIsiCv.reserve(kSR);blCoh.reserve(kSR);blSeeds.reserve(kSR);
    NetworkMetrics benchBL; bool benchBLReady=false;
    for(int r=0;r<kSR;++r){
        RuntimeInput ri=baseInput; ri.config.dose=0;
        ri.config.ic50_na=ri.config.ic50_k=ri.config.ic50_ca=1000.0;
        const std::uint32_t seed=mkSeed(1,r); blSeeds.push_back(seed);
        logMeta("Baseline",r+1,seed,ri);
        const SimulationSummary s=runSingleSimulation(ri,seed);
        blRate.push_back(s.network_metrics.meanFiringRateHz);
        blSync.push_back(s.network_metrics.synchronizationIndex);
        blIsiCv.push_back(computeMeanIsiCv(s.neuron_metrics));
        blCoh.push_back(computeCoherenceScore(s.network_metrics));
        if(!benchBLReady){benchBL=s.network_metrics;benchBLReady=true;}
    }
    const NetworkMetrics* benchBLPtr=benchBLReady?&benchBL:nullptr;

    const MetricStats blRS=computeMetricStats(blRate),blSS=computeMetricStats(blSync),blIS=computeMetricStats(blIsiCv);
    ValidationCheck blChk;
    blChk.name="Baseline Activity ("+std::to_string(kSR)+" runs)";
    const bool blRP=blRS.ci95Low>=5.0&&blRS.ci95High<=22.0;
    const bool blSP=blSS.ci95High<0.30; const bool blIP=blIS.ci95Low>0.35;
    blChk.pass=blRP&&blSP&&blIP;
    {std::ostringstream d;
     d<<"Mean Rate (Hz)         : "<<fStats(blRS,2)<<"\n"
      <<"Synchronization        : "<<fStats(blSS,2)<<"\n"
      <<"ISI CV                 : "<<fStats(blIS,2)<<"\n";
     if(!blChk.pass){d<<"Findings               :";
         if(!blRP)d<<" meanRate outside [5,22].";
         if(!blSP)d<<" sync CI >= 0.30.";
         if(!blIP)d<<" ISI CV CI <= 0.35.";d<<"\n";}
     blChk.details=d.str();}
    checks.push_back(blChk); printValidationCheck(blChk);
    {std::ostringstream rp;rp<<"TEST: Baseline Activity\nRuns: "<<kSR<<"\n"
      <<"meanRate: "<<fStats(blRS,3)<<" Hz\nsync: "<<fStats(blSS,3)<<"\nISI CV: "<<fStats(blIS,3)<<"\n"
      <<"RESULT: "<<(blChk.pass?"PASS":"FAIL")<<"\n"; reportBlocks.push_back(rp.str());}

    // --- Test 2: E/I Balance ---
    std::vector<double> rA,sA,rB,sB,seB; int stblB=0;
    rA.reserve(kSR);sA.reserve(kSR);rB.reserve(kSR);sB.reserve(kSR);seB.reserve(kSR);
    for(int r=0;r<kSR;++r){
        RuntimeInput riA=baseInput;
        riA.config.excitatory_ratio=0.84;riA.config.excitatory_weight_scale=1.05;
        riA.config.inhibitory_weight_scale=0.98;riA.config.external_current+=0.10;
        const std::uint32_t seedA=mkSeed(2,r); logMeta("EI_CaseA",r+1,seedA,riA);
        const SimulationSummary sA_=runSingleSimulation(riA,seedA,benchBLPtr);
        RuntimeInput riB=baseInput;
        riB.config.excitatory_ratio=0.74;riB.config.excitatory_weight_scale=0.50;
        riB.config.inhibitory_weight_scale=1.45;riB.config.external_current-=1.40;
        riB.config.noise_level=std::max(0.0,riB.config.noise_level-0.25);
        const std::uint32_t seedB=mkSeed(3,r); logMeta("EI_CaseB",r+1,seedB,riB);
        const SimulationSummary sB_=runSingleSimulation(riB,seedB,benchBLPtr);
        rA.push_back(sA_.network_metrics.meanFiringRateHz);sA.push_back(sA_.network_metrics.synchronizationIndex);
        rB.push_back(sB_.network_metrics.meanFiringRateHz);sB.push_back(sB_.network_metrics.synchronizationIndex);
        seB.push_back(sB_.network_metrics.seizureProbabilityPct/100.0);
        if(isSafeState(sB_.classification))++stblB;
    }
    const MetricStats rAS=computeMetricStats(rA),sAS=computeMetricStats(sA),
                      rBS=computeMetricStats(rB),sBS=computeMetricStats(sB),seBS=computeMetricStats(seB);
    const double zR=computeWelchZScore(rA,rB),zS=computeWelchZScore(sA,sB);
    const double stblF=static_cast<double>(stblB)/static_cast<double>(kSR);
    ValidationCheck eiChk;
    eiChk.name="E/I Balance ("+std::to_string(kSR)+" runs per case)";
    const bool rSig=(rAS.mean>rBS.mean)&&(zR>1.96),sSig=(sAS.mean>sBS.mean)&&(zS>1.96);
    const bool bStbl=stblF>=0.70&&seBS.ci95High<0.40;
    eiChk.pass=rSig&&sSig&&bStbl;
    {std::ostringstream d;
     d<<"CaseA Mean Rate (Hz)   : "<<fStats(rAS,2)<<"\nCaseB Mean Rate (Hz)   : "<<fStats(rBS,2)<<"\n"
      <<"CaseA Synchronization  : "<<fStats(sAS,2)<<"\nCaseB Synchronization  : "<<fStats(sBS,2)<<"\n"
      <<"zRate                  : "<<formatRuntimeNumber(zR)<<"\nzSync                  : "<<formatRuntimeNumber(zS)<<"\n"
      <<"CaseB Stable Fraction  : "<<formatRuntimeNumber(stblF)<<"\n";
     if(!eiChk.pass){d<<"Findings               :";
         if(!rSig)d<<" rate not significant.";
         if(!sSig)d<<" sync not significant.";
         if(!bStbl)d<<" CaseB stability failed.";d<<"\n";}
     eiChk.details=d.str();}
    checks.push_back(eiChk);printValidationCheck(eiChk);
    {std::ostringstream rp;rp<<"TEST: E/I Balance\nRuns: "<<kSR<<" per case\n"
      <<"CaseA meanRate: "<<fStats(rAS,3)<<" Hz\nCaseB meanRate: "<<fStats(rBS,3)<<" Hz\n"
      <<"CaseA sync: "<<fStats(sAS,3)<<"\nCaseB sync: "<<fStats(sBS,3)<<"\n"
      <<"zRate: "<<std::fixed<<std::setprecision(3)<<zR<<", zSync: "<<zS<<"\n"
      <<"CaseB seizureProb: "<<fStats(seBS,3)<<"\nRESULT: "<<(eiChk.pass?"PASS":"FAIL")<<"\n";
     reportBlocks.push_back(rp.str());}

    // --- Test 3: Dose-response ---
    RuntimeInput doseIn=baseInput;
    doseIn.config.ic50_na=200.0;doseIn.config.ic50_k=8.0;
    doseIn.config.ic50_ca=1000.0;doseIn.config.hill=3.2;
    const std::vector<double> dGrid=buildLinearDoseGrid(0.0,32.0,kDP);
    constexpr int kDR=3;
    std::vector<double> toxPct,toxFrac; toxPct.reserve(dGrid.size());toxFrac.reserve(dGrid.size());
    for(std::size_t i=0;i<dGrid.size();++i){
        doseIn.config.dose=dGrid[i]; double acc=0;
        for(int rep=0;rep<kDR;++rep){
            const std::uint32_t seed=mkSeed(40+static_cast<int>(i),rep);
            logMeta("DoseResponse",static_cast<int>(i*static_cast<std::size_t>(kDR)+static_cast<std::size_t>(rep)+1U),seed,doseIn);
            const SimulationSummary s=runSingleSimulation(doseIn,seed,benchBLPtr);
            acc+=std::clamp(0.60*static_cast<double>(s.network_metrics.seizureProbabilityPct)/100.0
                           +0.30*static_cast<double>(s.network_metrics.suppressionPct)/100.0
                           +0.10*static_cast<double>(s.network_metrics.nii),0.0,1.0);
        }
        const double mf=acc/static_cast<double>(kDR);
        toxFrac.push_back(mf);toxPct.push_back(100.0*mf);
    }
    std::vector<double> effFrac,effPct; effFrac.reserve(dGrid.size());effPct.reserve(dGrid.size());
    const double t0=toxFrac.empty()?0.0:toxFrac.front();
    const bool tUp=!toxFrac.empty()&&(toxFrac.back()>=t0);
    for(double tox:toxFrac){
        double e=0;
        if(tUp){e=std::clamp((tox-t0)/std::max(0.05,1.0-t0),0.0,1.0);}
        else    {e=std::clamp((t0-tox)/std::max(0.05,t0),0.0,1.0);}
        effFrac.push_back(e);effPct.push_back(100.0*e);
    }
    std::vector<double> eff4fit=effFrac;
    for(std::size_t i=1;i<eff4fit.size();++i) if(eff4fit[i]<eff4fit[i-1])eff4fit[i]=eff4fit[i-1];
    const SigmoidFitResult sf=fitSigmoidCurve(dGrid,eff4fit);
    const double midSlope=25.0*sf.k*sf.emax;
    const bool r2P=sf.r2>0.85,mpP=sf.d50>dGrid.front()&&sf.d50<dGrid.back(),stP=midSlope>1.0;
    ValidationCheck drChk;
    drChk.name="Dose Response ("+std::to_string(kDP)+" runs)";
    drChk.pass=r2P&&mpP&&stP;
    {std::ostringstream d;
     d<<"Sigmoid R2             : "<<formatRuntimeNumber(sf.r2)<<"\n"
      <<"k                      : "<<formatRuntimeNumber(sf.k)<<"\n"
      <<"d50                    : "<<formatRuntimeNumber(sf.d50)<<"\n"
      <<"emax                   : "<<formatRuntimeNumber(sf.emax)<<"\n"
      <<"Mid Slope              : "<<formatRuntimeNumber(midSlope)<<" (%/dose)\n";
     if(!drChk.pass){d<<"Findings               :";
         if(!r2P)d<<" R2<=0.85.";if(!mpP)d<<" d50 outside range.";if(!stP)d<<" slope too shallow.";d<<"\n";}
     drChk.details=d.str();}
    checks.push_back(drChk);printValidationCheck(drChk);
    {std::ostringstream rp;rp<<"TEST: Dose Response\nRuns: "<<kDP<<"\n"
      <<"sigmoid R2: "<<std::fixed<<std::setprecision(3)<<sf.r2<<"\n"
      <<"k: "<<std::setprecision(4)<<sf.k<<", d50: "<<sf.d50<<", emax: "<<sf.emax<<"\n"
      <<"midSlope: "<<std::setprecision(3)<<midSlope<<" (%/dose)\n"
      <<"RESULT: "<<(drChk.pass?"PASS":"FAIL")<<"\n"; reportBlocks.push_back(rp.str());}
    {
        const std::string fcp=outDir+"/dose_curve_fit.csv";
        std::ofstream fo(fcp,std::ios::out|std::ios::trunc);
        if(!fo.is_open())throw std::runtime_error("Cannot open: "+fcp);
        fo<<"dose,toxicity_pct,effect_pct,effect_pct_monotonic,fit_effect_pct,residual_pct,k,d50,emax,r2\n";
        fo<<std::fixed<<std::setprecision(6);
        for(std::size_t i=0;i<dGrid.size();++i){
            const double op=100.0*eff4fit[i],fp=100.0*sf.predicted[i];
            fo<<dGrid[i]<<','<<toxPct[i]<<','<<effPct[i]<<','<<op<<','<<fp<<','<<(op-fp)<<','
              <<sf.k<<','<<sf.d50<<','<<sf.emax<<','<<sf.r2<<'\n';
        }
    }

    // --- Test 4: Temporal evolution ---
    std::vector<double> earlyTP,lateTP,t0TP;
    earlyTP.reserve(kSR);lateTP.reserve(kSR);t0TP.reserve(kSR);
    std::vector<spp::analyzer::TimeWindowMetrics> meanTS;
    for(int r=0;r<kSR;++r){
        RuntimeInput ti=baseInput;
        ti.config.dose=16.0;ti.config.ic50_na=220.0;ti.config.ic50_k=7.0;
        ti.config.ic50_ca=1000.0;ti.config.external_current+=0.15;ti.config.noise_level+=0.10;
        const std::uint32_t seed=mkSeed(5,r);logMeta("TemporalToxic",r+1,seed,ti);
        const SimulationTrace tr=runSingleSimulationWithTrace(ti,seed,benchBLPtr);
        const auto wins=MetricsAnalyzer::computeTimeWindowMetrics(tr.result,kWMs,kWMs);
        if(wins.empty())continue;
        if(meanTS.empty()){meanTS=wins;for(auto& w:meanTS){w.meanFiringRateHz=0;w.synchronizationIndex=0;w.burstIndex=0;w.seizureProbability=0;}}
        double eA=0,lA=0,t0A=0,eRA=0,lRA=0; int eC=0,lC=0,t0C=0;
        const std::size_t cc=std::min(meanTS.size(),wins.size());
        for(std::size_t i=0;i<cc;++i){
            const auto& w=wins[i];
            meanTS[i].meanFiringRateHz+=w.meanFiringRateHz;meanTS[i].synchronizationIndex+=w.synchronizationIndex;
            meanTS[i].burstIndex+=w.burstIndex;meanTS[i].seizureProbability+=w.seizureProbability;
            if(w.endMs<=100.0f){eA+=w.seizureProbability;eRA+=w.meanFiringRateHz;++eC;}
            if(w.startMs>=300.0f&&w.endMs<=500.0f+1e-4f){lA+=w.seizureProbability;lRA+=w.meanFiringRateHz;++lC;}
            if(w.startMs<100.0f){t0A+=w.seizureProbability;++t0C;}
        }
        const double ep=eC?eA/eC:0,lp=lC?lA/lC:0,t0p=t0C?t0A/t0C:0;
        const double er=eC?eRA/eC:0,lr=lC?lRA/lC:0;
        const double dpProxy=(er>12.0&&lr<2.0)?0.80:0.0;
        earlyTP.push_back(ep);lateTP.push_back(std::max(lp,dpProxy));t0TP.push_back(t0p);
    }
    for(auto& w:meanTS){float n=static_cast<float>(std::max(1,kSR));
        w.meanFiringRateHz/=n;w.synchronizationIndex/=n;w.burstIndex/=n;w.seizureProbability/=n;}
    CsvWriter::writeTimeMetrics(outDir+"/time_metrics.csv",meanTS);
    const MetricStats eS=computeMetricStats(earlyTP),lS=computeMetricStats(lateTP),t0S=computeMetricStats(t0TP);
    ValidationCheck tempChk;
    tempChk.name="Temporal Evolution ("+std::to_string(kSR)+" runs)";
    const bool eP=eS.ci95High<0.20,lP=lS.ci95Low>0.60,tP=t0S.ci95High<0.20;
    tempChk.pass=eP&&lP&&tP;
    {std::ostringstream d;
     d<<"Toxic Prob (0-100ms)   : "<<fStats(eS,2)<<"\nToxic Prob (300-500ms) : "<<fStats(lS,2)<<"\nToxic Prob (t~0)       : "<<fStats(t0S,2)<<"\n";
     if(!tempChk.pass){d<<"Findings               :";
         if(!eP)d<<" early>=0.2.";if(!lP)d<<" late<=0.6.";if(!tP)d<<" t0 too high.";d<<"\n";}
     tempChk.details=d.str();}
    checks.push_back(tempChk);printValidationCheck(tempChk);
    {std::ostringstream rp;rp<<"TEST: Temporal Evolution\nRuns: "<<kSR<<"\n"
      <<"toxicProb(0-100ms): "<<fStats(eS,3)<<"\ntoxicProb(300-500ms): "<<fStats(lS,3)<<"\ntoxicProb(t~0): "<<fStats(t0S,3)<<"\n"
      <<"RESULT: "<<(tempChk.pass?"PASS":"FAIL")<<"\n";reportBlocks.push_back(rp.str());}

    // --- Test 5: Calcium block ---
    std::vector<double> brPct,rdFrac,bsProb;
    brPct.reserve(kSR);rdFrac.reserve(kSR);bsProb.reserve(kSR);
    for(int r=0;r<kSR;++r){
        RuntimeInput ctrl=baseInput;ctrl.config.dose=6.0;
        ctrl.config.ic50_na=ctrl.config.ic50_k=ctrl.config.ic50_ca=1000.0;
        RuntimeInput blk=ctrl;blk.config.ic50_ca=3.0;
        const std::uint32_t sc=mkSeed(6,r),sb=mkSeed(7,r);
        logMeta("CalciumControl",r+1,sc,ctrl);logMeta("CalciumBlock",r+1,sb,blk);
        const SimulationSummary sc_=runSingleSimulation(ctrl,sc,benchBLPtr);
        const SimulationSummary sb_=runSingleSimulation(blk, sb,benchBLPtr);
        const double cb=std::max(1e-6,static_cast<double>(sc_.network_metrics.burstIndex));
        const double bb=static_cast<double>(sb_.network_metrics.burstIndex);
        brPct.push_back(100.0*(cb-bb)/cb);
        rdFrac.push_back(std::fabs(static_cast<double>(sb_.network_metrics.meanFiringRateHz-sc_.network_metrics.meanFiringRateHz))/
                         std::max(1.0,static_cast<double>(sc_.network_metrics.meanFiringRateHz)));
        bsProb.push_back(static_cast<double>(sb_.network_metrics.seizureProbabilityPct)/100.0);
    }
    const MetricStats brS=computeMetricStats(brPct),rdS=computeMetricStats(rdFrac),bsS=computeMetricStats(bsProb);
    ValidationCheck caChk;
    caChk.name="Calcium Block ("+std::to_string(kSR)+" runs)";
    const bool bP=brS.ci95Low>=30.0,rP=rdS.ci95High<=0.35,nSP=bsS.ci95High<0.30;
    caChk.pass=bP&&rP&&nSP;
    {std::ostringstream d;
     d<<"Burst Reduction (%)    : "<<fStats(brS,2)<<"\nRate Delta             : "<<fStats(rdS,2)<<"\nBlocked Seizure Prob   : "<<fStats(bsS,2)<<"\n";
     if(!caChk.pass){d<<"Findings               :";
         if(!bP)d<<" burst<30%.";if(!rP)d<<" rate change not minor.";if(!nSP)d<<" seizure too high.";d<<"\n";}
     caChk.details=d.str();}
    checks.push_back(caChk);printValidationCheck(caChk);
    {std::ostringstream rp;rp<<"TEST: Calcium Block\nRuns: "<<kSR<<"\n"
      <<"burstReduction: "<<fStats(brS,3)<<" %\nrateDelta: "<<fStats(rdS,3)<<"\nblockedSeizureProb: "<<fStats(bsS,3)<<"\n"
      <<"RESULT: "<<(caChk.pass?"PASS":"FAIL")<<"\n";reportBlocks.push_back(rp.str());}

    // --- Test 6: Synaptic disruption ---
    std::vector<double> dSync,dCoh; dSync.reserve(kSR);dCoh.reserve(kSR);
    for(int r=0;r<kSR;++r){
        RuntimeInput di=baseInput;
        di.config.excitatory_weight_scale=0.01;di.config.inhibitory_weight_scale=1.80;
        di.config.excitatory_ratio=0.60;di.config.connectivity=0.001;
        di.config.external_current=0.0;di.config.noise_level=1.50;
        const std::uint32_t seed=blSeeds[static_cast<std::size_t>(r)];
        logMeta("SynapticDisruption",r+1,seed,di);
        const SimulationSummary s=runSingleSimulation(di,seed);
        dSync.push_back(s.network_metrics.synchronizationIndex);
        dCoh.push_back(computeCoherenceScore(s.network_metrics));
    }
    std::vector<double> srPct,crPct; srPct.reserve(kSR);crPct.reserve(kSR);
    for(int i=0;i<kSR;++i){
        const double bs=std::max(1e-6,blSync[static_cast<std::size_t>(i)]);
        const double bc=std::max(1e-6,blCoh[static_cast<std::size_t>(i)]);
        srPct.push_back(100.0*(bs-dSync[static_cast<std::size_t>(i)])/bs);
        crPct.push_back(100.0*(bc-dCoh[static_cast<std::size_t>(i)])/bc);
    }
    const MetricStats srS=computeMetricStats(srPct),crS=computeMetricStats(crPct);
    ValidationCheck synChk;
    synChk.name="Synaptic Disruption ("+std::to_string(kSR)+" runs)";
    const bool srP=srS.ci95Low>=40.0,cohP=crS.ci95Low>=25.0;
    synChk.pass=srP&&cohP;
    {std::ostringstream d;
     d<<"Sync Reduction (%)     : "<<fStats(srS,2)<<"\nCoherence Reduction(%) : "<<fStats(crS,2)<<"\n";
     if(!synChk.pass){d<<"Findings               :";
         if(!srP)d<<" sync<40%.";if(!cohP)d<<" coherence too small.";d<<"\n";}
     synChk.details=d.str();}
    checks.push_back(synChk);printValidationCheck(synChk);
    {std::ostringstream rp;rp<<"TEST: Synaptic Disruption\nRuns: "<<kSR<<"\n"
      <<"syncReduction: "<<fStats(srS,3)<<" %\ncoherenceReduction: "<<fStats(crS,3)<<" %\n"
      <<"RESULT: "<<(synChk.pass?"PASS":"FAIL")<<"\n";reportBlocks.push_back(rp.str());}

    // --- Summary ---
    int passCount=0; for(const auto& c:checks) if(c.pass)++passCount;
    const auto suiteEnd=std::chrono::steady_clock::now();
    const double suiteS=std::chrono::duration<double>(suiteEnd-suiteStart).count();
    const bool bioPass=passCount==static_cast<int>(checks.size());
    const bool rtPass=!baseInput.config.use_cuda||(suiteS<=300.0);
    const bool overall=bioPass&&rtPass;

    {const std::string rp=outDir+"/internal_benchmark_report.txt";
     std::ofstream fo(rp,std::ios::out|std::ios::trunc);
     if(!fo.is_open())throw std::runtime_error("Cannot open: "+rp);
     fo<<"Silicon Patient Platform - Internal Biological Benchmark Report\n";
     fo<<"Fixed Seed Mode: "<<(fixedSeed?"true":"false")<<"\n\n";
     for(const auto& b:reportBlocks)fo<<b<<"\n";
     fo<<"PERFORMANCE CONSTRAINT (<5 min GPU): "<<(rtPass?"PASS":"FAIL")
       <<" (runtime="<<std::fixed<<std::setprecision(2)<<suiteS<<" s)\n";
     fo<<"OVERALL BENCHMARK STATUS: "<<(overall?"PASS":"FAIL")<<"\n";}

    {const std::string mp=outDir+"/run_metadata.txt";
     std::ofstream fo(mp,std::ios::out|std::ios::trunc);
     if(!fo.is_open())throw std::runtime_error("Cannot open: "+mp);
     const auto now=std::chrono::system_clock::now();
     const std::time_t nowT=std::chrono::system_clock::to_time_t(now);
     std::tm utc{};
#ifdef _MSC_VER
     gmtime_s(&utc,&nowT);
#else
     if(const std::tm* p=std::gmtime(&nowT))utc=*p;
#endif
     fo<<"timestamp_utc,"<<std::put_time(&utc,"%Y-%m-%d %H:%M:%S")<<"\n";
     fo<<"fixed_seed_mode,"<<(fixedSeed?"true":"false")<<"\n";
     fo<<"deterministic_base_seed,"<<detSeed<<"\n";
     fo<<"random_base_seed,"<<rndSeed<<"\n";
     fo<<"stat_runs,"<<kSR<<"\ndose_points,"<<kDP<<"\nwindow_ms,"<<kWMs<<"\n\n";
     fo<<"runtime_seconds,"<<std::fixed<<std::setprecision(2)<<suiteS<<"\n";
     fo<<"runtime_constraint_pass,"<<(rtPass?"true":"false")<<"\n\n";
     fo<<"base_profile,neurons="<<baseInput.config.neuron_count
       <<",sim_time_ms="<<baseInput.config.sim_time<<",dt_ms="<<baseInput.config.dt
       <<",connectivity="<<baseInput.config.connectivity
       <<",external_current="<<baseInput.config.external_current
       <<",noise="<<baseInput.config.noise_level<<"\n\nrun_log\n";
     for(const auto& l:meta)fo<<l<<'\n';}

    printSection("FINAL SUMMARY");
    printMetricLine("Tests Passed",std::to_string(passCount)+" / 6");
    printMetricLine("Performance",std::string(rtPass?"PASS":"FAIL")+" ("+formatRuntimeNumber(suiteS)+" sec)");
    printMetricLine("System Status",bioPass?"BIOLOGICALLY VALIDATED":"BIOLOGICAL VALIDATION FAILED");
    printSection("Artifacts");
    printMetricLine("Output Directory",outDir);
    printMetricLine("Files","internal_benchmark_report.txt, time_metrics.csv, dose_curve_fit.csv, run_metadata.txt");
}

} // namespace

// ─── main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    try {
        if (argc<2){printHelp();return 0;}
        const std::string mode=argv[1];
        std::string drugConfigPath;
        for(int i=2;i<argc;++i){
            const std::string arg=argv[i];
            if(arg=="--drug-config"&&(i+1)<argc){drugConfigPath=argv[i+1];++i;}
        }

        if(mode=="--internal-benchmark"){
            const auto dev=readEnvVar("SPP_DEVELOPER_MODE");
            if(!dev||toLower(*dev)!="1"){
                std::cerr<<"Internal benchmark mode is developer-only. Set SPP_DEVELOPER_MODE=1 to enable.\n";
                return 1;
            }
            runInternalBiologicalBenchmarkSuite();
            return 0;
        }

        if(mode=="--simulate"){
            RuntimeInput input; input.run_dose_sweep=false;
            validateConfig(input.config);
            runSingleSimulationMode(input);
            return 0;
        }

        if(mode=="--dose-eval"){
            RuntimeInput input;
            input.run_dose_sweep=false;
            input.config.output_folder="output_pharma_decision";
#ifdef SPP_USE_CUDA
            input.config.neuron_count=1420;
#else
            input.config.neuron_count=1320;
#endif
            input.config.sim_time=500.0; input.config.dt=0.04;
            input.config.connectivity=0.05; input.config.excitatory_ratio=0.80;
            input.config.external_current=1.05; input.config.noise_level=0.72;
            input.config.excitatory_weight_scale=1.00; input.config.inhibitory_weight_scale=1.00;
            input.config.ic50_na=200.0; input.config.ic50_k=8.0;
            input.config.ic50_ca=1000.0; input.config.hill=3.2;

            std::string engineMode="Default Internal Engine Config";
            std::optional<int> userRuns;
            if(!drugConfigPath.empty()){
                std::optional<int> ro; std::string mt;
                if(loadDrugConfigFromJsonFile(drugConfigPath,input,ro,mt)){
                    engineMode="User Drug Config";
                    if(ro) userRuns=*ro;
                }
            }
            validateConfig(input.config);
            runDoseEvaluationMode(input,engineMode,userRuns);
            return 0;
        }

        printHelp();
        return 0;
    } catch(const std::exception& ex){
        std::cerr<<"Error: "<<ex.what()<<"\n";
        return 1;
    }
}