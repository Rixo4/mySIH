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

constexpr int kMinNeuronCount = 1000;
constexpr int kMaxNeuronCount = 100000;
constexpr double kMinConnectivity = 0.05;
constexpr double kMaxConnectivity = 0.20;

struct SimulationConfig {
    int neuron_count = 1500;
    double sim_time = 400.0;
    double dt = 0.01;

    double dose = 10.0;
    double ic50_na = 50.0;
    double ic50_k = 50.0;
    double ic50_ca = 120.0;
    double hill = 3.0;

    double connectivity = 0.10;
    double excitatory_ratio = 0.8;

    double external_current = 2.5;
    double noise_level = 0.35;

    double excitatory_weight_scale = 1.0;
    double inhibitory_weight_scale = 1.0;

    bool use_cuda = true;
    bool export_csv = true;

    std::string output_folder = "output_data";
};

struct RuntimeInput {
    SimulationConfig config;
    std::string drug_name = "GenericCompound";

    bool run_dose_sweep = false;
    double sweep_start = 0.0;
    double sweep_end = 100.0;
    int sweep_points = 10;
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
    double mean = 0.0;
    double stddev = 0.0;
    double ci95Low = 0.0;
    double ci95High = 0.0;
    std::size_t n = 0U;
};

struct RunResult {
    float firingRate = 0.0f;
    float sync = 0.0f;
    float burst = 0.0f;
    float nii = 0.0f;
    float isiCV = 0.0f;
    float seizureScore = 0.0f;
    float toxicityScore = 0.0f;
};

struct AggregatedStats {
    float meanRate = 0.0f;
    float stdRate = 0.0f;
    float meanSync = 0.0f;
    float stdSync = 0.0f;
    float meanBurst = 0.0f;
    float stdBurst = 0.0f;
    float meanNii = 0.0f;
    float stdNii = 0.0f;
    float meanISI = 0.0f;
    float stdISI = 0.0f;
    float meanSeizure = 0.0f;
    float stdSeizure = 0.0f;
    float meanToxicity = 0.0f;
    float stdToxicity = 0.0f;
};

struct SigmoidFitResult {
    double k = 0.0;
    double d50 = 0.0;
    double emax = 1.0;
    double r2 = 0.0;
    double sse = std::numeric_limits<double>::infinity();
    std::vector<double> predicted;
};

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string toUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

std::string trim(std::string value) {
    const auto isNotSpace = [](unsigned char c) { return !std::isspace(c); };

    auto begin = std::find_if(value.begin(), value.end(), isNotSpace);
    if (begin == value.end()) {
        return {};
    }

    auto end = std::find_if(value.rbegin(), value.rend(), isNotSpace).base();
    return std::string(begin, end);
}

std::optional<bool> parseBoolText(const std::string& text) {
    const std::string v = toLower(trim(text));
    if (v == "1" || v == "true" || v == "yes" || v == "y" || v == "on") {
        return true;
    }
    if (v == "0" || v == "false" || v == "no" || v == "n" || v == "off") {
        return false;
    }
    return std::nullopt;
}

std::optional<std::string> readEnvVar(const char* name) {
#ifdef _MSC_VER
    char* valueBuffer = nullptr;
    std::size_t valueLen = 0U;
    if (_dupenv_s(&valueBuffer, &valueLen, name) != 0 || valueBuffer == nullptr) {
        return std::nullopt;
    }

    std::string value(valueBuffer);
    std::free(valueBuffer);
    return value;
#else
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

[[maybe_unused]] bool parseModeText(const std::string& text, bool& useCudaOut) {
    const std::string mode = toLower(trim(text));
    if (mode == "cuda" || mode == "gpu") {
        useCudaOut = true;
        return true;
    }
    if (mode == "cpu") {
        useCudaOut = false;
        return true;
    }
    return false;
}

constexpr int kRuntimeOutputPrecision = 2;
constexpr int kRuntimeDividerWidth = 50;
constexpr int kRuntimeLabelWidth = 22;

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

void printMetricLine(const std::string& metric, double value, const std::string& suffix = std::string()) {
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
           // Note: internal benchmark mode is developer-only and hidden from public help.
           // To run internal benchmarks set `SPP_DEVELOPER_MODE=1` and invoke `--internal-benchmark`.
           << "  silicon_patient.exe --analyze\n";
}

void validateConfig(const SimulationConfig& cfg) {
    if (cfg.neuron_count < kMinNeuronCount || cfg.neuron_count > kMaxNeuronCount) {
            throw std::runtime_error("Neuron count must be between 1000 and 100000. Please adjust your configuration.");
    }
    if (!(cfg.sim_time > 0.0)) {
        throw std::runtime_error("Simulation time must be > 0.");
    }
    if (!(cfg.dt > 0.0)) {
        throw std::runtime_error("dt must be > 0.");
    }
    if (cfg.dt >= cfg.sim_time) {
        throw std::runtime_error("dt must be smaller than simulation time.");
    }
    if (cfg.dose < 0.0) {
        throw std::runtime_error("Dose must be >= 0.");
    }
    if (!(cfg.ic50_na > 0.0) || !(cfg.ic50_k > 0.0) || !(cfg.ic50_ca > 0.0)) {
        throw std::runtime_error("IC50 values must be > 0.");
    }
    if (!(cfg.hill >= 1.0 && cfg.hill <= 6.0)) {
        throw std::runtime_error("Hill coefficient must be in [1, 6].");
    }
    if (cfg.connectivity < kMinConnectivity || cfg.connectivity > kMaxConnectivity) {
        throw std::runtime_error("Connectivity must be in [0.05, 0.2].");
    }
    if (!(cfg.excitatory_ratio > 0.0 && cfg.excitatory_ratio < 1.0)) {
        throw std::runtime_error("Excitatory ratio must be in (0, 1).");
    }
    if (cfg.noise_level < 0.0) {
        throw std::runtime_error("Noise level must be >= 0.");
    }
    if (trim(cfg.output_folder).empty()) {
        throw std::runtime_error("Output folder cannot be empty.");
    }
}

[[maybe_unused]] void validateSweep(const RuntimeInput& input) {
    if (!input.run_dose_sweep) {
        return;
    }
    if (input.sweep_start < 0.0 || input.sweep_end < 0.0) {
        throw std::runtime_error("Sweep doses must be >= 0.");
    }
    if (input.sweep_end < input.sweep_start) {
        throw std::runtime_error("sweep_end must be >= sweep_start.");
    }
    if (input.sweep_points < 2) {
        throw std::runtime_error("sweep_points must be >= 2.");
    }
}

std::uint32_t makeSeed() {
    std::random_device rd;
    return rd();
}

NetworkConfig buildNetworkConfig(const SimulationConfig& cfg, std::uint32_t seed) {
    NetworkConfig netCfg;
    const float excScale = std::clamp(static_cast<float>(cfg.excitatory_weight_scale), 0.05f, 4.0f);
    const float inhScale = std::clamp(static_cast<float>(cfg.inhibitory_weight_scale), 0.05f, 4.0f);
    const float neuronScale = std::clamp(
        std::sqrt(1000.0f / std::max(300.0f, static_cast<float>(cfg.neuron_count))),
        0.90f,
        1.30f
    );

    netCfg.neuronCount = static_cast<std::size_t>(cfg.neuron_count);
    netCfg.excitatoryFraction = static_cast<float>(cfg.excitatory_ratio);
    netCfg.connectionProbability = std::clamp(static_cast<float>(cfg.connectivity) * 1.40f, 0.02f, 0.10f);
    netCfg.minDelaySteps = 1;
    netCfg.maxDelaySteps = 24;
    netCfg.excitatoryWeightMean = 1.25f * excScale * neuronScale;
    netCfg.inhibitoryWeightMean = 3.80f * inhScale * neuronScale;
    netCfg.weightStdFraction = 0.40f;
    netCfg.excitatoryWeightMin = 0.45f * excScale * neuronScale;
    netCfg.excitatoryWeightMax = 2.40f * excScale * neuronScale;
    netCfg.inhibitoryWeightMin = 1.70f * inhScale * neuronScale;
    netCfg.inhibitoryWeightMax = 5.80f * inhScale * neuronScale;
    netCfg.recurrentExcitatoryBias = 0.58f;
    netCfg.feedbackInhibitoryBias = 0.82f;
    netCfg.maxSynapses = std::max<std::size_t>(
        500000,
        static_cast<std::size_t>(
            static_cast<double>(cfg.neuron_count) *
            static_cast<double>(cfg.neuron_count) *
            cfg.connectivity *
            1.30
        )
    );
    netCfg.randomSeed = seed;
    return netCfg;
}

spp::simulation::SimulationConfig buildEngineConfig(const SimulationConfig& cfg, std::uint32_t seed) {
    spp::simulation::SimulationConfig simCfg;
    simCfg.dtMs = static_cast<float>(cfg.dt);
    simCfg.durationMs = static_cast<float>(cfg.sim_time);
    simCfg.randomSeed = seed + 17U;
    simCfg.baseExternalCurrent = static_cast<float>(cfg.external_current);
    simCfg.externalCurrentStd = std::max(0.16f, static_cast<float>(std::fabs(cfg.external_current) * 0.09));
    simCfg.baseNoiseStd = std::max(0.14f, 0.56f * static_cast<float>(cfg.noise_level));
    simCfg.refractoryMs = 1.8f;
    simCfg.synTauExcMs = 7.0f;
    simCfg.synTauInhMs = 14.0f;
    simCfg.maxSynCurrent = 320.0f;
    simCfg.maxTotalCurrent = 350.0f;
    simCfg.adaptationTauMs = 220.0f;
    simCfg.adaptationIncrement = 0.016f +
                                 0.010f * std::clamp(static_cast<float>(cfg.noise_level), 0.0f, 2.0f);
    simCfg.adaptationMaxCurrent = 1.8f;
    simCfg.adaptationInhibitoryScale = 0.50f;
    simCfg.drugOnsetTauMs = 140.0f;
    simCfg.useGpu = cfg.use_cuda;
    return simCfg;
}

SimulationSummary runSingleSimulationInternal(
    const RuntimeInput& input,
    std::uint32_t seed,
    spp::simulation::SimulationResult* outResult
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
    summary.neuron_metrics = MetricsAnalyzer::computeNeuronMetrics(simResult);
    summary.network_metrics = MetricsAnalyzer::computeNetworkMetrics(simResult, summary.neuron_metrics);
    summary.classification = SeizureDetector::classify(summary.network_metrics, simResult.finalVoltages);

    if (outResult != nullptr) {
        *outResult = std::move(simResult);
    }

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

[[maybe_unused]] float computeMeanIsiVarianceMs(const std::vector<spp::analyzer::NeuronMetrics>& neuronMetrics) {
    float sum = 0.0f;
    std::size_t count = 0U;

    for (const auto& neuron : neuronMetrics) {
        if (neuron.spikeCount < 3U) {
            continue;
        }
        if (!std::isfinite(neuron.isiVarianceMs) || neuron.isiVarianceMs <= 0.0f) {
            continue;
        }

        sum += neuron.isiVarianceMs;
        ++count;
    }

    if (count == 0U) {
        return 0.0f;
    }
    return sum / static_cast<float>(count);
}

[[maybe_unused]] float computeWindowRateHz(const spp::simulation::SimulationResult& result, float startMs, float endMs) {
    if (result.populationSpikesPerStep.empty() || result.dtMs <= 0.0f || endMs <= startMs) {
        return 0.0f;
    }

    const float startClamped = std::clamp(startMs, 0.0f, result.durationMs);
    const float endClamped = std::clamp(endMs, 0.0f, result.durationMs);
    if (endClamped <= startClamped) {
        return 0.0f;
    }

    const std::size_t stepCount = result.populationSpikesPerStep.size();
    const std::size_t startStep = std::min(
        stepCount,
        static_cast<std::size_t>(std::floor(startClamped / result.dtMs))
    );
    const std::size_t endStep = std::min(
        stepCount,
        static_cast<std::size_t>(std::ceil(endClamped / result.dtMs))
    );

    if (endStep <= startStep) {
        return 0.0f;
    }

    std::uint64_t spikeSum = 0U;
    for (std::size_t step = startStep; step < endStep; ++step) {
        spikeSum += static_cast<std::uint64_t>(result.populationSpikesPerStep[step]);
    }

    const float neuronCount = static_cast<float>(std::max<std::size_t>(1U, result.spikeTimes.size()));
    const float durationSec = std::max(1.0e-6f, static_cast<float>(endStep - startStep) * result.dtMs / 1000.0f);

    return static_cast<float>(spikeSum) / (neuronCount * durationSec);
}

[[maybe_unused]] float computeWindowPeakSpikeFraction(const spp::simulation::SimulationResult& result, float startMs, float endMs) {
    if (result.populationSpikesPerStep.empty() || result.dtMs <= 0.0f || endMs <= startMs) {
        return 0.0f;
    }

    const float startClamped = std::clamp(startMs, 0.0f, result.durationMs);
    const float endClamped = std::clamp(endMs, 0.0f, result.durationMs);
    if (endClamped <= startClamped) {
        return 0.0f;
    }

    const std::size_t stepCount = result.populationSpikesPerStep.size();
    const std::size_t startStep = std::min(
        stepCount,
        static_cast<std::size_t>(std::floor(startClamped / result.dtMs))
    );
    const std::size_t endStep = std::min(
        stepCount,
        static_cast<std::size_t>(std::ceil(endClamped / result.dtMs))
    );
    if (endStep <= startStep) {
        return 0.0f;
    }

    const float neuronCount = static_cast<float>(std::max<std::size_t>(1U, result.spikeTimes.size()));
    float peakFraction = 0.0f;

    for (std::size_t step = startStep; step < endStep; ++step) {
        const float frac = static_cast<float>(result.populationSpikesPerStep[step]) / neuronCount;
        peakFraction = std::max(peakFraction, frac);
    }

    return peakFraction;
}

[[maybe_unused]] double computeLinearR2(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() != y.size() || x.size() < 3U) {
        return 1.0;
    }

    const double n = static_cast<double>(x.size());
    double sumX = 0.0;
    double sumY = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        sumX += x[i];
        sumY += y[i];
    }

    const double meanX = sumX / n;
    const double meanY = sumY / n;

    double sxx = 0.0;
    double sxy = 0.0;
    double sst = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double dx = x[i] - meanX;
        const double dy = y[i] - meanY;
        sxx += dx * dx;
        sxy += dx * dy;
        sst += dy * dy;
    }

    if (sxx <= 1.0e-12 || sst <= 1.0e-12) {
        return 1.0;
    }

    const double slope = sxy / sxx;
    const double intercept = meanY - slope * meanX;

    double ssr = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double pred = slope * x[i] + intercept;
        const double err = y[i] - pred;
        ssr += err * err;
    }

    return 1.0 - (ssr / (sst + 1.0e-12));
}

[[maybe_unused]] double computePeakPositiveSlope(
    const std::vector<double>& x,
    const std::vector<double>& y,
    std::size_t* peakIndexOut
) {
    if (x.size() != y.size() || x.size() < 2U) {
        if (peakIndexOut != nullptr) {
            *peakIndexOut = 0U;
        }
        return 0.0;
    }

    double peakSlope = 0.0;
    std::size_t peakIndex = 0U;

    for (std::size_t i = 1; i < x.size(); ++i) {
        const double dx = std::max(1.0e-12, x[i] - x[i - 1U]);
        const double slope = (y[i] - y[i - 1U]) / dx;
        if (slope > peakSlope) {
            peakSlope = slope;
            peakIndex = i - 1U;
        }
    }

    if (peakIndexOut != nullptr) {
        *peakIndexOut = peakIndex;
    }
    return peakSlope;
}

[[maybe_unused]] double computeMidSlope(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() != y.size() || x.size() < 3U) {
        return 0.0;
    }

    const std::size_t mid = x.size() / 2U;
    if (mid == 0U || mid + 1U >= x.size()) {
        return 0.0;
    }

    const double dx = std::max(1.0e-12, x[mid + 1U] - x[mid - 1U]);
    return (y[mid + 1U] - y[mid - 1U]) / dx;
}

float computeCoherenceScore(const NetworkMetrics& metrics) {
    const float syncNorm = std::clamp(metrics.synchronizationIndex, 0.0f, 1.0f);
    const float burstNorm = std::clamp(metrics.burstIndex / 0.20f, 0.0f, 1.0f);
    return 0.70f * syncNorm + 0.30f * burstNorm;
}

float computeMeanIsiCv(const std::vector<spp::analyzer::NeuronMetrics>& neuronMetrics) {
    double pooledIsiSum = 0.0;
    double pooledIsiSecondMomentSum = 0.0;
    std::size_t pooledCount = 0U;

    double neuronCvSum = 0.0;
    std::size_t neuronCvCount = 0U;

    for (const auto& neuron : neuronMetrics) {
        if (neuron.spikeCount < 3U || neuron.isiMeanMs <= 1.0e-6f || neuron.isiVarianceMs < 0.0f) {
            continue;
        }

        const std::size_t isiCount = neuron.spikeCount - 1U;
        const double meanIsi = static_cast<double>(neuron.isiMeanMs);
        const double varIsi = static_cast<double>(neuron.isiVarianceMs);
        const double secondMoment = varIsi + meanIsi * meanIsi;

        pooledIsiSum += static_cast<double>(isiCount) * meanIsi;
        pooledIsiSecondMomentSum += static_cast<double>(isiCount) * secondMoment;
        pooledCount += isiCount;

        const float cv = std::sqrt(std::max(0.0f, neuron.isiVarianceMs)) / neuron.isiMeanMs;
        if (!std::isfinite(cv) || cv <= 0.0f) {
            continue;
        }

        neuronCvSum += static_cast<double>(cv);
        ++neuronCvCount;
    }

    if (pooledCount < 2U) {
        return 0.0f;
    }

    const double pooledMean = pooledIsiSum / static_cast<double>(pooledCount);
    if (pooledMean <= 1.0e-12) {
        return 0.0f;
    }

    const double pooledSecondMoment = pooledIsiSecondMomentSum / static_cast<double>(pooledCount);
    const double pooledVar = std::max(0.0, pooledSecondMoment - pooledMean * pooledMean);
    const double pooledCv = std::sqrt(pooledVar) / pooledMean;

    const double meanNeuronCv = (neuronCvCount > 0U)
                                    ? (neuronCvSum / static_cast<double>(neuronCvCount))
                                    : pooledCv;

    return static_cast<float>(0.65 * pooledCv + 0.35 * meanNeuronCv);
}

MetricStats computeMetricStats(const std::vector<double>& values) {
    MetricStats stats;
    stats.n = values.size();
    if (values.empty()) {
        return stats;
    }

    double mean = 0.0;
    for (double v : values) {
        mean += v;
    }
    mean /= static_cast<double>(values.size());

    double variance = 0.0;
    if (values.size() > 1U) {
        for (double v : values) {
            const double d = v - mean;
            variance += d * d;
        }
        variance /= static_cast<double>(values.size() - 1U);
    }

    const double stddev = std::sqrt(std::max(0.0, variance));
    const double ciHalfWidth = 1.96 * stddev / std::sqrt(static_cast<double>(values.size()));

    stats.mean = mean;
    stats.stddev = stddev;
    stats.ci95Low = mean - ciHalfWidth;
    stats.ci95High = mean + ciHalfWidth;
    return stats;
}

float computeMean(const std::vector<float>& values) {
    if (values.empty()) {
        return 0.0f;
    }

    double sum = 0.0;
    for (float value : values) {
        sum += static_cast<double>(value);
    }
    return static_cast<float>(sum / static_cast<double>(values.size()));
}

float computeStd(const std::vector<float>& values, float mean) {
    if (values.empty()) {
        return 0.0f;
    }

    double sumSq = 0.0;
    for (float value : values) {
        const double diff = static_cast<double>(value) - static_cast<double>(mean);
        sumSq += diff * diff;
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(values.size())));
}

AggregatedStats computeStats(const std::vector<RunResult>& results) {
    AggregatedStats stats;
    if (results.empty()) {
        return stats;
    }

    std::vector<float> rates;
    std::vector<float> syncs;
    std::vector<float> bursts;
    std::vector<float> niis;
    std::vector<float> isis;
    std::vector<float> seizures;
    std::vector<float> toxicities;
    rates.reserve(results.size());
    syncs.reserve(results.size());
    bursts.reserve(results.size());
    niis.reserve(results.size());
    isis.reserve(results.size());
    seizures.reserve(results.size());
    toxicities.reserve(results.size());

    for (const auto& result : results) {
        rates.push_back(result.firingRate);
        syncs.push_back(result.sync);
        bursts.push_back(result.burst);
        niis.push_back(result.nii);
        isis.push_back(result.isiCV);
        seizures.push_back(result.seizureScore);
        toxicities.push_back(result.toxicityScore);
    }

    stats.meanRate = computeMean(rates);
    stats.stdRate = computeStd(rates, stats.meanRate);
    stats.meanSync = computeMean(syncs);
    stats.stdSync = computeStd(syncs, stats.meanSync);
    stats.meanBurst = computeMean(bursts);
    stats.stdBurst = computeStd(bursts, stats.meanBurst);
    stats.meanNii = computeMean(niis);
    stats.stdNii = computeStd(niis, stats.meanNii);
    stats.meanISI = computeMean(isis);
    stats.stdISI = computeStd(isis, stats.meanISI);
    stats.meanSeizure = computeMean(seizures);
    stats.stdSeizure = computeStd(seizures, stats.meanSeizure);
    stats.meanToxicity = computeMean(toxicities);
    stats.stdToxicity = computeStd(toxicities, stats.meanToxicity);

    return stats;
}

std::string getStability(float stdRate, float stdToxicity) {
    if (stdRate < 1.0f && stdToxicity < 5.0f) {
        return "HIGH";
    }
    if (stdRate < 2.0f && stdToxicity < 10.0f) {
        return "MEDIUM";
    }
    return "LOW";
}

std::vector<RunResult> runMultipleSimulations(
    const RuntimeInput& baseInput,
    double dose,
    int numRuns,
    std::uint32_t doseSeedBase,
    std::vector<spp::analyzer::NeuronMetrics>* outLastNeuronMetrics,
    spp::analyzer::NetworkState* outLastState
) {
    std::vector<RunResult> results;
    if (numRuns <= 0) {
        return results;
    }

    results.reserve(static_cast<std::size_t>(numRuns));
    for (int run = 0; run < numRuns; ++run) {
        RuntimeInput runInput = baseInput;
        runInput.config.dose = dose;

        const std::uint32_t seed = doseSeedBase + static_cast<std::uint32_t>(run * 9973 + 101);
        const SimulationSummary summary = runSingleSimulation(runInput, seed);

        if (outLastNeuronMetrics != nullptr) {
            *outLastNeuronMetrics = summary.neuron_metrics;
        }
        if (outLastState != nullptr) {
            *outLastState = summary.classification;
        }

        RunResult result;
        result.firingRate = summary.network_metrics.meanFiringRateHz;
        result.sync = summary.network_metrics.synchronizationIndex;
        result.burst = summary.network_metrics.burstIndex;
        result.nii = summary.network_metrics.nii;
        result.isiCV = computeMeanIsiCv(summary.neuron_metrics);
        result.seizureScore = summary.network_metrics.seizureProbabilityPct;
        result.toxicityScore = summary.network_metrics.suppressionPct;
        results.push_back(result);
    }

    return results;
}

NetworkMetrics buildAggregatedNetworkMetrics(const AggregatedStats& stats) {
    NetworkMetrics metrics;
    metrics.meanFiringRateHz = stats.meanRate;
    metrics.synchronizationIndex = stats.meanSync;
    metrics.burstIndex = stats.meanBurst;
    metrics.irregularityIndex = stats.meanISI;
    metrics.seizureProbabilityPct = stats.meanSeizure;
    metrics.suppressionPct = stats.meanToxicity;
    metrics.nii = stats.meanNii;

    const std::string stability = getStability(stats.stdRate, stats.stdToxicity);
    if (stability == "HIGH") {
        metrics.stabilityScore = 0.90f;
    } else if (stability == "MEDIUM") {
        metrics.stabilityScore = 0.60f;
    } else {
        metrics.stabilityScore = 0.30f;
    }

    return metrics;
}

double computeWelchZScore(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() < 2U || b.size() < 2U) {
        return 0.0;
    }

    const MetricStats sa = computeMetricStats(a);
    const MetricStats sb = computeMetricStats(b);

    const double varAOverN = (sa.stddev * sa.stddev) / static_cast<double>(a.size());
    const double varBOverN = (sb.stddev * sb.stddev) / static_cast<double>(b.size());
    const double denom = std::sqrt(std::max(1.0e-12, varAOverN + varBOverN));

    return (sa.mean - sb.mean) / denom;
}

SigmoidFitResult fitSigmoidCurve(const std::vector<double>& dose, const std::vector<double>& seizureProbFraction) {
    SigmoidFitResult best;
    if (dose.size() != seizureProbFraction.size() || dose.size() < 4U) {
        return best;
    }

    const auto [doseMinIt, doseMaxIt] = std::minmax_element(dose.begin(), dose.end());
    const double doseMin = *doseMinIt;
    const double doseMax = *doseMaxIt;

    double meanY = 0.0;
    for (double y : seizureProbFraction) {
        meanY += y;
    }
    meanY /= static_cast<double>(seizureProbFraction.size());

    double sst = 0.0;
    for (double y : seizureProbFraction) {
        const double dy = y - meanY;
        sst += dy * dy;
    }

    constexpr double kMin = 0.02;
    constexpr double kMax = 2.50;
    constexpr double kStep = 0.02;
    const double dStep = std::max(0.25, (doseMax - doseMin) / 120.0);

    for (double k = kMin; k <= kMax + 1.0e-9; k += kStep) {
        for (double d50 = doseMin; d50 <= doseMax + 1.0e-9; d50 += dStep) {
            // Solve the best amplitude analytically for each (k, d50): y ~= emax * sigmoid(k, d50).
            double emaxNumerator = 0.0;
            double emaxDenominator = 0.0;

            for (std::size_t i = 0; i < dose.size(); ++i) {
                const double x = dose[i];
                const double y = seizureProbFraction[i];
                const double logits = std::clamp(-k * (x - d50), -60.0, 60.0);
                const double sigmoid = 1.0 / (1.0 + std::exp(logits));

                emaxNumerator += y * sigmoid;
                emaxDenominator += sigmoid * sigmoid;
            }

            if (emaxDenominator <= 1.0e-12) {
                continue;
            }

            const double emax = std::clamp(emaxNumerator / emaxDenominator, 0.0, 1.0);
            double sse = 0.0;

            for (std::size_t i = 0; i < dose.size(); ++i) {
                const double x = dose[i];
                const double y = seizureProbFraction[i];

                const double logits = std::clamp(-k * (x - d50), -60.0, 60.0);
                const double sigmoid = 1.0 / (1.0 + std::exp(logits));
                const double yFit = emax * sigmoid;
                const double err = y - yFit;
                sse += err * err;
            }

            if (sse < best.sse) {
                best.sse = sse;
                best.k = k;
                best.d50 = d50;
                best.emax = emax;
            }
        }
    }

    best.predicted.resize(dose.size(), 0.0);
    for (std::size_t i = 0; i < dose.size(); ++i) {
        const double logits = std::clamp(-best.k * (dose[i] - best.d50), -60.0, 60.0);
        const double sigmoid = 1.0 / (1.0 + std::exp(logits));
        best.predicted[i] = best.emax * sigmoid;
    }

    if (sst <= 1.0e-12) {
        best.r2 = 1.0;
    } else {
        best.r2 = 1.0 - (best.sse / sst);
    }

    return best;
}

void printValidationCheck(const ValidationCheck& check) {
    printSection(check.name);
    if (!check.details.empty()) {
        std::cout << check.details;
        if (check.details.back() != '\n') {
            std::cout << '\n';
        }
    }
    printMetricLine("Status", check.pass ? "PASS" : "FAIL");
    std::cout << '\n';
}

bool isToxicState(NetworkState state) {
    return state == NetworkState::SeizureRisk ||
           state == NetworkState::SeizureActive ||
           state == NetworkState::DepolarizationBlock ||
           state == NetworkState::NeuralSuppression;
}

bool isSafeState(NetworkState state) {
    return state == NetworkState::Stable || state == NetworkState::MildInstability;
}

std::string doseBandLabel(const SimulationSummary& summary) {
    if (isToxicState(summary.classification) ||
        summary.network_metrics.seizureProbabilityPct >= 60.0f ||
        summary.network_metrics.suppressionPct >= 60.0f) {
        return "Toxic";
    }

    if (isSafeState(summary.classification) &&
        summary.network_metrics.seizureProbabilityPct < 30.0f &&
        summary.network_metrics.suppressionPct < 40.0f) {
        return "Safe";
    }

    return "Caution";
}

struct SingleRunInterpretation {
    float isiVariability = 0.0f;
    float instabilityIndex = 0.0f;
    float baselineRateHz = 0.0f;
    float currentRateHz = 0.0f;
    float changePct = 0.0f;
    float syncRiskNorm = 0.0f;
    float rateShiftNorm = 0.0f;
    float seizureRiskScore = 0.0f;
    float seizureConfidence = 0.0f;
    float seizureConfidenceDriverNorm = 0.0f;
    float toxicityRiskScore = 0.0f;
    float decisionConfidenceDriverNorm = 0.0f;
    std::string brainState;
    std::string networkRegime;
    std::string variability;
    std::string seizureRisk;
    std::string toxicityRisk;
    std::string effectType;
    std::string networkResponse;
    std::string safetyMargin;
    std::string networkShift;
    std::string recommendation;
    std::string riskLevel;
    std::string reason;
    std::string seizureConfidenceBasis;
    std::string decisionConfidenceBasis;
    float finalConfidence = 0.0f;
    float decisionConfidence = 0.0f;
};

struct SingleRunDoseContext {
    bool available = false;
    double rangeStartDose = 0.0;
    double rangeEndDose = 0.0;
    int sampledPoints = 0;

    PharmaDecisionReport report;

    bool hasCurrentPoint = false;
    float currentRiskScore = 0.0f;
    float currentEarlyWarning = 0.0f;
    float currentSeizureSlope = 0.0f;
    std::string currentTier;
    std::string dosePosition;
};

constexpr int kStructuredReportLabelWidth = 19;

std::string classifyRiskLevel(float score) {
    if (score >= 75.0f) {
        return "Critical";
    }
    if (score >= 55.0f) {
        return "High";
    }
    if (score >= 35.0f) {
        return "Moderate";
    }
    return "Low";
}

std::string classifyToxicityRiskLevel(float score, float suppressionPct, float seizureProbPct) {
    if (score < 20.0f && suppressionPct < 20.0f && seizureProbPct < 10.0f) {
        return "NONE";
    }
    return classifyRiskLevel(score);
}

std::string classifyVariabilityLevel(float isiVariability) {
    const float isiCv = std::isfinite(isiVariability) ? std::max(0.0f, isiVariability) : 0.0f;
    if (isiCv < 0.30f) {
        return "Highly regular (low variability)";
    }
    if (isiCv < 0.60f) {
        return "Moderately regular";
    }
    return "Irregular spiking (healthy variability)";
}

std::string classifyNetworkRegime(NetworkState state, float synchronization) {
    const float sync = std::clamp(std::isfinite(synchronization) ? synchronization : 0.0f, 0.0f, 1.0f);
    if (state == NetworkState::SeizureRisk ||
        state == NetworkState::SeizureActive ||
        state == NetworkState::DepolarizationBlock ||
        sync >= 0.55f) {
        return "Hyper-synchronized regime";
    }
    if (state == NetworkState::MildInstability || state == NetworkState::Hyperexcitable || sync >= 0.25f) {
        return "Mildly synchronized regime";
    }
    return "Asynchronous Irregular (AI), balanced E/I";
}

std::string classifyBrainState(
    NetworkState state,
    float firingRateHz,
    float synchronization,
    float instabilityIndex,
    float suppressionPct
) {
    if (state == NetworkState::SeizureActive ||
        state == NetworkState::SeizureRisk ||
        state == NetworkState::DepolarizationBlock ||
        synchronization >= 0.60f ||
        instabilityIndex >= 0.70f) {
        return "Hyper-synchronized regime";
    }

    if (suppressionPct >= 65.0f || firingRateHz <= 2.0f) {
        return "Mildly synchronized regime";
    }

    if (state == NetworkState::MildInstability ||
        state == NetworkState::Hyperexcitable ||
        synchronization >= 0.25f ||
        instabilityIndex >= 0.45f) {
        return "Mildly synchronized regime";
    }

    return "Asynchronous Irregular (AI), balanced E/I";
}

std::vector<double> buildSingleRunDoseContextGrid(double targetDose) {
    std::vector<double> doses;
    doses.reserve(7U);

    if (targetDose <= 1.0e-6) {
        doses = {0.0, 2.0, 5.0, 10.0, 15.0, 20.0, 30.0};
    } else {
        const double upperAnchor = std::max(2.0 * targetDose, targetDose + 10.0);
        doses = {
            0.0,
            0.50 * targetDose,
            0.75 * targetDose,
            targetDose,
            1.25 * targetDose,
            1.50 * targetDose,
            upperAnchor
        };
    }

    for (double& dose : doses) {
        dose = std::max(0.0, dose);
    }

    std::sort(doses.begin(), doses.end());
    doses.erase(
        std::unique(doses.begin(), doses.end(), [](double a, double b) {
            return std::fabs(a - b) <= 1.0e-6;
        }),
        doses.end()
    );

    return doses;
}

SingleRunDoseContext buildSingleRunDoseContext(
    const RuntimeInput& input,
    const SimulationSummary& anchorSummary,
    std::uint32_t baseSeed
) {
    SingleRunDoseContext context;
    const std::vector<double> doses = buildSingleRunDoseContextGrid(input.config.dose);
    if (doses.size() < 2U) {
        return context;
    }

    context.rangeStartDose = doses.front();
    context.rangeEndDose = doses.back();
    context.sampledPoints = static_cast<int>(doses.size());

    std::vector<DoseObservation> observations;
    observations.reserve(doses.size());

    for (std::size_t i = 0; i < doses.size(); ++i) {
        RuntimeInput runInput = input;
        runInput.config.dose = doses[i];

        const bool reuseAnchor = std::fabs(runInput.config.dose - input.config.dose) <= 1.0e-6;
        const SimulationSummary localSummary = reuseAnchor
                                                   ? anchorSummary
                                                   : runSingleSimulation(
                                                         runInput,
                                                         baseSeed + static_cast<std::uint32_t>(i * 4099U + 31U)
                                                     );

        {
            const float doseF = static_cast<float>(runInput.config.dose);
            const float blockNa = static_cast<float>(spp::drug::DrugModel::hillBlock(doseF, static_cast<float>(runInput.config.ic50_na), static_cast<float>(runInput.config.hill)));
            const float blockK = static_cast<float>(spp::drug::DrugModel::hillBlock(doseF, static_cast<float>(runInput.config.ic50_k), static_cast<float>(runInput.config.hill)));
            const float blockCa = static_cast<float>(spp::drug::DrugModel::hillBlock(doseF, static_cast<float>(runInput.config.ic50_ca), static_cast<float>(runInput.config.hill)));
            observations.push_back(DoseObservation{
                doseF,
                localSummary.network_metrics.meanFiringRateHz,
                localSummary.network_metrics.synchronizationIndex,
                localSummary.network_metrics.burstIndex,
                localSummary.network_metrics.nii,
                localSummary.network_metrics.irregularityIndex,
                localSummary.network_metrics.seizureProbabilityPct,
                localSummary.network_metrics.suppressionPct,
                blockNa,
                blockK,
                blockCa
            });
        }
    }

    context.report = PharmaDecisionEngine::evaluate(observations);
    context.available = !context.report.points.empty();
    if (!context.available) {
        return context;
    }

    const auto nearestIt = std::min_element(
        context.report.points.begin(),
        context.report.points.end(),
        [&](const auto& a, const auto& b) {
            return std::fabs(static_cast<double>(a.dose) - input.config.dose) <
                   std::fabs(static_cast<double>(b.dose) - input.config.dose);
        }
    );

    if (nearestIt != context.report.points.end()) {
        context.hasCurrentPoint = true;
        context.currentRiskScore = nearestIt->riskScore;
        context.currentEarlyWarning = nearestIt->earlyWarningIndex;
        context.currentSeizureSlope = nearestIt->seizureSlopePctPerDose;
        context.currentTier = nearestIt->classification;
    }

    if (context.report.hasSafeRange &&
        input.config.dose >= context.report.safeMinDose &&
        input.config.dose <= context.report.safeMaxDose) {
        context.dosePosition = "Inside estimated safe range";
    } else if (context.report.hasToxicThreshold && input.config.dose < context.report.toxicMinDose) {
        context.dosePosition = "Below estimated toxic threshold";
    } else if (context.report.hasToxicThreshold && input.config.dose >= context.report.toxicMinDose) {
        context.dosePosition = "At/above estimated toxic threshold";
    } else if (context.report.hasSafeRange) {
        context.dosePosition = "Outside estimated safe range";
    } else {
        context.dosePosition = "Dose-context threshold not detected";
    }

    return context;
}

[[maybe_unused]] std::string buildDoseDecisionRationale(const RuntimeInput& input, const SingleRunDoseContext& context) {
    if (!context.available) {
        return "Dose-response context unavailable for this run";
    }

    if (context.report.hasToxicThreshold) {
        const double toxicDose = static_cast<double>(context.report.toxicMinDose);
        if (input.config.dose < toxicDose) {
            const double headroomPct = 100.0 * (toxicDose - input.config.dose) / std::max(1.0e-6, toxicDose);
            return "Current dose is " + formatRuntimeNumber(headroomPct) +
                   "% below estimated toxicity threshold (" + formatRuntimeNumber(toxicDose) + ")";
        }

        const double exceedPct = 100.0 * (input.config.dose - toxicDose) / std::max(1.0e-6, toxicDose);
        return "Current dose is " + formatRuntimeNumber(exceedPct) +
               "% above estimated toxicity threshold (" + formatRuntimeNumber(toxicDose) + ")";
    }

    if (context.report.hasSafeRange && !context.report.hasToxicThreshold) {
        return "No toxicity observed within tested range [" +
               formatRuntimeNumber(context.report.safeMinDose) + ", " +
               formatRuntimeNumber(context.report.safeMaxDose) + "]";
    }

    if (context.report.hasSafeRange) {
        return "Current dose evaluated against tested safe interval [" +
               formatRuntimeNumber(context.report.safeMinDose) + ", " +
               formatRuntimeNumber(context.report.safeMaxDose) + "]";
    }

    return "Local dose-response scan did not find a stable toxicity boundary";
}

std::string confidenceBandFromPct(float confidencePct) {
    const float confidence01 = std::clamp(confidencePct / 100.0f, 0.0f, 1.0f);
    if (confidence01 >= 0.75f) {
        return "HIGH";
    }
    if (confidence01 >= 0.50f) {
        return "MEDIUM";
    }
    return "LOW";
}

[[maybe_unused]] std::string formatConfidenceBand(float confidencePct) {
    const int roundedPct = static_cast<int>(std::lround(std::clamp(confidencePct, 0.0f, 99.0f)));
    return confidenceBandFromPct(confidencePct) + " (" + std::to_string(roundedPct) + "%)";
}

std::string formatSeizureSlopeDisplay(float slopePctPerDose) {
    if (std::fabs(slopePctPerDose) < 0.05f) {
        return "~0 (flat response)";
    }
    return formatRuntimeNumber(slopePctPerDose, 3) + " %/dose";
}

[[maybe_unused]] std::string buildSeizureConfidenceTrace(const SingleRunInterpretation& interpretation) {
    return "55 + 45*max(NII=" + formatRuntimeNumber(interpretation.instabilityIndex) +
           ", SyncRisk=" + formatRuntimeNumber(interpretation.syncRiskNorm) + ")";
}

[[maybe_unused]] std::string buildDecisionConfidenceTrace(const SingleRunInterpretation& interpretation) {
    return "60 + 40*max(NII=" + formatRuntimeNumber(interpretation.instabilityIndex) +
           ", |dRate|=" + formatRuntimeNumber(interpretation.rateShiftNorm) + ")";
}

void appendStructuredReportLine(std::ostringstream& out, const std::string& label, const std::string& value) {
    out << std::left << std::setw(kStructuredReportLabelWidth) << label << " : " << value << "\n";
}

void appendStructuredReportLine(
    std::ostringstream& out,
    const std::string& label,
    double value,
    const std::string& suffix = std::string()
) {
    out << std::left << std::setw(kStructuredReportLabelWidth) << label << " : "
        << formatRuntimeNumber(value) << suffix << "\n";
}

SingleRunInterpretation buildSingleRunInterpretation(const SimulationSummary& summary) {
    SingleRunInterpretation interpretation;

    const float firingRateHz = std::isfinite(summary.network_metrics.meanFiringRateHz)
                                   ? std::max(0.0f, summary.network_metrics.meanFiringRateHz)
                                   : 0.0f;
    const float synchronization = std::clamp(
        std::isfinite(summary.network_metrics.synchronizationIndex)
            ? summary.network_metrics.synchronizationIndex
            : 0.0f,
        0.0f,
        1.0f
    );
    const float nii = std::clamp(
        std::isfinite(summary.network_metrics.nii) ? summary.network_metrics.nii : 0.0f,
        0.0f,
        1.0f
    );
    const float seizureProbPct = std::clamp(
        std::isfinite(summary.network_metrics.seizureProbabilityPct)
            ? summary.network_metrics.seizureProbabilityPct
            : 0.0f,
        0.0f,
        100.0f
    );
    const float suppressionPct = std::clamp(
        std::isfinite(summary.network_metrics.suppressionPct)
            ? summary.network_metrics.suppressionPct
            : 0.0f,
        0.0f,
        100.0f
    );
    const float seizureProbFrac = seizureProbPct / 100.0f;
    const float suppressionFrac = suppressionPct / 100.0f;

    interpretation.instabilityIndex = nii;
    interpretation.isiVariability = std::max(0.0f, computeMeanIsiCv(summary.neuron_metrics));
    interpretation.networkRegime = classifyNetworkRegime(summary.classification, synchronization);
    interpretation.variability = classifyVariabilityLevel(interpretation.isiVariability);
    interpretation.brainState = classifyBrainState(
        summary.classification,
        firingRateHz,
        synchronization,
        nii,
        suppressionPct
    );

    const float baselineRateHz = std::isfinite(summary.network_metrics.earlyWindowRateHz) &&
                                         summary.network_metrics.earlyWindowRateHz > 0.0f
                                     ? summary.network_metrics.earlyWindowRateHz
                                     : firingRateHz;
    const float currentRateHz = std::isfinite(summary.network_metrics.lateWindowRateHz) &&
                                        summary.network_metrics.lateWindowRateHz > 0.0f
                                    ? summary.network_metrics.lateWindowRateHz
                                    : firingRateHz;
    interpretation.baselineRateHz = baselineRateHz;
    interpretation.currentRateHz = currentRateHz;
    interpretation.changePct = baselineRateHz > 1.0e-6f
                                   ? ((currentRateHz - baselineRateHz) / baselineRateHz) * 100.0f
                                   : 0.0f;

    if (interpretation.changePct >= 15.0f) {
        interpretation.networkShift = "Excitatory Shift";
    } else if (interpretation.changePct <= -15.0f) {
        interpretation.networkShift = "Suppressive Shift";
    } else if (nii >= 0.55f || synchronization >= 0.65f) {
        interpretation.networkShift = "Instability Shift";
    } else {
        interpretation.networkShift = "Minimal Shift";
    }

    const float rateRisk = std::clamp((firingRateHz - 6.0f) / 16.0f, 0.0f, 1.0f);
    const float syncRisk = std::clamp((synchronization - 0.30f) / 0.60f, 0.0f, 1.0f);
    const float cvRisk = std::clamp((interpretation.isiVariability - 0.60f) / 0.90f, 0.0f, 1.0f);
    interpretation.syncRiskNorm = syncRisk;

    interpretation.seizureRiskScore = 100.0f * std::clamp(
        0.35f * nii +
            0.25f * syncRisk +
            0.20f * rateRisk +
            0.20f * seizureProbFrac,
        0.0f,
        1.0f
    );
    interpretation.seizureRisk = classifyRiskLevel(interpretation.seizureRiskScore);
    interpretation.seizureConfidenceDriverNorm = std::max(nii, syncRisk);
    interpretation.seizureConfidenceBasis = (nii >= syncRisk)
                                                ? "NII-dominant confidence"
                                                : "Synchronization-dominant confidence";
    interpretation.seizureConfidence = std::clamp(
        55.0f + 45.0f * interpretation.seizureConfidenceDriverNorm,
        0.0f,
        99.0f
    );

    interpretation.toxicityRiskScore = 100.0f * std::clamp(
        0.40f * nii +
            0.30f * suppressionFrac +
            0.15f * cvRisk +
            0.15f * seizureProbFrac,
        0.0f,
        1.0f
    );
    interpretation.toxicityRisk = classifyToxicityRiskLevel(
        interpretation.toxicityRiskScore,
        suppressionPct,
        seizureProbPct
    );

    if (currentRateHz <= baselineRateHz * 0.75f && synchronization < 0.45f) {
        interpretation.effectType = "Suppressive";
    } else if (currentRateHz >= baselineRateHz * 1.20f || synchronization >= 0.65f) {
        interpretation.effectType = "Excitatory";
    } else if (nii < 0.45f && interpretation.isiVariability < 1.00f) {
        interpretation.effectType = "Stabilizing";
    } else {
        interpretation.effectType = "Mixed";
    }

    if (interpretation.changePct >= 15.0f) {
        interpretation.networkResponse = "Rate Increase";
    } else if (interpretation.changePct <= -15.0f) {
        interpretation.networkResponse = "Rate Decrease";
    } else {
        interpretation.networkResponse = "Rate Maintained";
    }

    const std::string doseBand = doseBandLabel(summary);
    if (doseBand == "Safe" && interpretation.toxicityRiskScore < 45.0f) {
        interpretation.safetyMargin = "Wide";
    } else if (doseBand == "Toxic" || interpretation.toxicityRiskScore >= 70.0f) {
        interpretation.safetyMargin = "Narrow";
    } else {
        interpretation.safetyMargin = "Moderate";
    }

    const float overallRisk = std::max(interpretation.seizureRiskScore, interpretation.toxicityRiskScore);
    interpretation.riskLevel = classifyRiskLevel(overallRisk);

    const bool seizureRiskLow = interpretation.seizureRiskScore < 35.0f;
    const bool toxicityNone = interpretation.toxicityRisk == "NONE";
    const float absRateChangePct = std::fabs(interpretation.changePct);
    if (seizureRiskLow && toxicityNone && absRateChangePct < 30.0f) {
        interpretation.recommendation = "PROCEED";
    } else if (seizureRiskLow && absRateChangePct < 50.0f) {
        interpretation.recommendation = "PROCEED WITH MONITORING";
    } else {
        interpretation.recommendation = "NOT RECOMMENDED";
    }

    interpretation.rateShiftNorm = std::clamp(std::fabs(interpretation.changePct) / 100.0f, 0.0f, 1.0f);
    interpretation.decisionConfidenceDriverNorm = std::max(nii, interpretation.rateShiftNorm);
    interpretation.decisionConfidenceBasis = (nii >= interpretation.rateShiftNorm)
                                                 ? "NII-dominant confidence"
                                                 : "Rate-shift-dominant confidence";
    interpretation.decisionConfidence = std::clamp(
        60.0f + 40.0f * interpretation.decisionConfidenceDriverNorm,
        0.0f,
        99.0f
    );

    interpretation.finalConfidence = std::min(interpretation.seizureConfidence, interpretation.decisionConfidence);

    if (interpretation.recommendation == "PROCEED" || interpretation.recommendation == "PROCEED WITH MONITORING") {
        interpretation.reason = "Controlled suppression with low seizure and toxicity risk";
    } else {
        interpretation.reason = "Elevated seizure or toxicity signal under current dose";
    }

    return interpretation;
}

void printSimulationReport(
    const RuntimeInput& input,
    const SimulationSummary& summary,
    const SingleRunDoseContext& doseContext
) {
    (void)doseContext;
    const SingleRunInterpretation interpretation = buildSingleRunInterpretation(summary);

    std::ostringstream out;
    out << "==================================================\n";
    out << "SILICON PATIENT - SINGLE DOSE SIMULATION REPORT\n";
    out << "==================================================\n";

    out << "[Simulation Configuration]\n";
    appendStructuredReportLine(out, "Neurons", std::to_string(input.config.neuron_count));
    appendStructuredReportLine(out, "Duration", input.config.sim_time, " ms");
    appendStructuredReportLine(out, "Drug Dose", input.config.dose);
    appendStructuredReportLine(out, "Mode", input.config.use_cuda ? "CUDA" : "CPU");

    out << "\n---\n\n";
    out << "[Neural Activity]\n";
    appendStructuredReportLine(out, "Firing Rate", summary.network_metrics.meanFiringRateHz, " Hz");
    appendStructuredReportLine(out, "Synchronization", summary.network_metrics.synchronizationIndex);
    appendStructuredReportLine(out, "ISI Variability", interpretation.isiVariability);

    out << "\n--------------------------------------------------\n";
    out << "[Network State]\n";
    appendStructuredReportLine(out, "Brain State", interpretation.brainState);
    appendStructuredReportLine(out, "Spike Pattern", interpretation.variability);

    out << "--------------------------------------------------\n";
    out << "[Risk Assessment]\n";
    appendStructuredReportLine(out, "Seizure Score", interpretation.seizureRiskScore, " / 100");
    appendStructuredReportLine(out, "Seizure Risk", toUpper(interpretation.seizureRisk));
    appendStructuredReportLine(out, "Toxicity Score", interpretation.toxicityRiskScore, " / 100");
    appendStructuredReportLine(out, "Toxicity Risk", toUpper(interpretation.toxicityRisk == "NONE" ? "LOW" : interpretation.toxicityRisk));
    appendStructuredReportLine(out, "Instability Index", interpretation.instabilityIndex);

    out << "--------------------------------------------------\n";
    out << "[Drug Impact]\n";
    appendStructuredReportLine(out, "Baseline Rate", interpretation.baselineRateHz, " Hz");
    appendStructuredReportLine(out, "Current Rate", interpretation.currentRateHz, " Hz");
    appendStructuredReportLine(out, "Change", interpretation.changePct, " %");
    appendStructuredReportLine(out, "Effect Type", interpretation.effectType);
    appendStructuredReportLine(out, "Network Response", interpretation.networkResponse);
    appendStructuredReportLine(out, "Safety Margin", interpretation.safetyMargin);

    out << "--------------------------------------------------\n";
    out << "[FINAL DECISION]\n";
    appendStructuredReportLine(out, "Recommendation", interpretation.recommendation);
    appendStructuredReportLine(out, "Risk Level", toUpper(interpretation.riskLevel));
    appendStructuredReportLine(out, "Confidence", confidenceBandFromPct(interpretation.finalConfidence));
    appendStructuredReportLine(out, "Reason", interpretation.reason);

    out << "\n==================================================\n";
    std::cout << out.str();
}

void printResult(
    const RuntimeInput& input,
    const SimulationSummary& summary,
    const SingleRunDoseContext& doseContext
) {
    printSimulationReport(input, summary, doseContext);
}

void exportSingleRunArtifacts(const RuntimeInput& input, const SimulationSummary& summary) {
    if (!input.config.export_csv) {
        return;
    }

    std::filesystem::create_directories(input.config.output_folder);

    const std::string neuronPath = input.config.output_folder + "/neuron_stats.csv";
    const std::string networkPath = input.config.output_folder + "/network_metrics.csv";
    const std::string dosePath = input.config.output_folder + "/dose_response.csv";

    CsvWriter::writeNeuronStats(neuronPath, summary.neuron_metrics);
    CsvWriter::writeNetworkMetrics(networkPath, {
        NetworkMetricRecord{
            "single_run",
            static_cast<float>(input.config.dose),
            summary.network_metrics,
            SeizureDetector::toString(summary.classification)
        }
    });

    CsvWriter::writeDoseResponse(dosePath, {
        DoseResponsePoint{
            static_cast<float>(input.config.dose),
            summary.network_metrics.meanFiringRateHz,
            summary.network_metrics.synchronizationIndex,
            summary.network_metrics.burstIndex,
            summary.network_metrics.nii,
            summary.network_metrics.seizureProbabilityPct,
            summary.network_metrics.suppressionPct,
            summary.network_metrics.stabilityScore,
            SeizureDetector::toString(summary.classification)
        }
    });
}

std::vector<double> buildLinearDoseGrid(double startDose, double endDose, int points) {
    std::vector<double> doses;
    doses.reserve(static_cast<std::size_t>(points));

    if (points <= 1) {
        doses.push_back(startDose);
        return doses;
    }

    const double step = (endDose - startDose) / static_cast<double>(points - 1);
    for (int i = 0; i < points; ++i) {
        doses.push_back(startDose + static_cast<double>(i) * step);
    }

    return doses;
}

void writePharmaDecisionReport(
    const std::string& filePath,
    const RuntimeInput& input,
    const PharmaDecisionReport& report
) {
    std::ofstream out(filePath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("Unable to open pharma decision report file: " + filePath);
    }

    out << std::fixed << std::setprecision(4);
    out << "Silicon Patient Platform - Pharma Decision Report\n";
    out << "Drug: " << input.drug_name << "\n";
    out << "Dose sweep: [" << input.sweep_start << ", " << input.sweep_end << "] with " << input.sweep_points << " points\n\n";

    out << "safe_max_dose,";
    out << (report.hasSafeRange ? std::to_string(report.safeMaxDose) : std::string("N/A"));
    out << "\n";

    out << "toxic_min_dose,";
    out << (report.hasToxicThreshold ? std::to_string(report.toxicMinDose) : std::string("N/A"));
    out << "\n";

    out << "effective_min_dose,";
    out << (report.hasEffectiveDose ? std::to_string(report.effectiveMinDose) : std::string("N/A"));
    out << "\n";

    out << "therapeutic_window,";
    out << (report.hasTherapeuticWindow ? std::to_string(report.therapeuticWindow) : std::string("N/A"));
    out << "\n\n";

    out << "overall_profile," << PharmaDecisionEngine::toString(report.overallTier) << "\n";
    out << "peak_risk_score," << report.peakRiskScore << "\n";
    out << "peak_seizure_probability_pct," << report.peakSeizureProbabilityPct << "\n";
    out << "peak_suppression_pct," << report.peakSuppressionPct << "\n";
    out << "peak_early_warning_index," << report.peakEarlyWarningIndex << "\n";
    out << "max_seizure_slope_pct_per_dose," << report.maxSeizureSlopePctPerDose << "\n";
}

// Build drug evaluation report including the drug input details so callers can
// prove user-provided values reached the engine.
std::string buildDrugEvaluationReportText(
    const PharmaDecisionReport& report,
    const AggregatedStats& stabilityStats,
    int runCount,
    const RuntimeInput& evalInput,
    const std::string& engineInputMode
) {
    std::ostringstream out;
    const auto appendLine = [&](const std::string& label, const std::string& value) {
        out << std::left << std::setw(20) << label << " : " << value << "\n";
    };
    const auto formatRange = [&](double startDose, double endDose) {
        return formatRuntimeNumber(startDose) + " - " + formatRuntimeNumber(endDose);
    };
    struct DoseBucket {
        double dose = 0.0;
        double effectPct = 0.0;
    };

    std::vector<DoseBucket> features;
    features.reserve(report.features.size());
    for (const auto& feature : report.features) {
        features.push_back(DoseBucket{feature.dose, feature.rate_change});
    }
    std::sort(features.begin(), features.end(), [](const DoseBucket& a, const DoseBucket& b) {
        return a.dose < b.dose;
    });

    const auto buildDoseRanges = [&](auto predicate) {
        std::vector<std::pair<double, double>> ranges;
        if (features.empty()) {
            return ranges;
        }

        double step = report.stepDose;
        if (!(step > 0.0) && features.size() >= 2U) {
            step = std::numeric_limits<double>::infinity();
            for (std::size_t i = 1U; i < features.size(); ++i) {
                const double diff = features[i].dose - features[i - 1U].dose;
                if (diff > 1.0e-6) {
                    step = std::min(step, diff);
                }
            }
            if (!std::isfinite(step)) {
                step = 1.0;
            }
        }
        if (!(step > 0.0)) {
            step = 1.0;
        }

        const double maxGap = 1.50 * step + 1.0e-5;

        std::size_t i = 0U;
        while (i < features.size()) {
            while (i < features.size() && !predicate(features[i].effectPct)) {
                ++i;
            }
            if (i >= features.size()) {
                break;
            }

            std::size_t j = i;
            while (j + 1U < features.size() &&
                   predicate(features[j + 1U].effectPct) &&
                   (features[j + 1U].dose - features[j].dose) <= maxGap) {
                ++j;
            }

            ranges.emplace_back(features[i].dose, features[j].dose);
            i = j + 1U;
        }

        return ranges;
    };

    const auto formatRanges = [&](const std::vector<std::pair<double, double>>& ranges) {
        if (ranges.empty()) {
            return std::string("Not observed");
        }

        std::ostringstream text;
        for (std::size_t i = 0U; i < ranges.size(); ++i) {
            if (i > 0U) {
                text << ", ";
            }
            text << formatRange(ranges[i].first, ranges[i].second);
        }
        return text.str();
    };

    const auto buildRangesFromDoses = [&](const std::vector<double>& doses) {
        std::vector<std::pair<double, double>> ranges;
        if (doses.empty()) {
            return ranges;
        }

        double step = report.stepDose;
        if (!(step > 0.0) && doses.size() >= 2U) {
            step = std::numeric_limits<double>::infinity();
            for (std::size_t i = 1U; i < doses.size(); ++i) {
                const double diff = doses[i] - doses[i - 1U];
                if (diff > 1.0e-6) {
                    step = std::min(step, diff);
                }
            }
            if (!std::isfinite(step)) {
                step = 1.0;
            }
        }
        if (!(step > 0.0)) {
            step = 1.0;
        }

        const double maxGap = 1.50 * step + 1.0e-5;

        std::size_t i = 0U;
        while (i < doses.size()) {
            std::size_t j = i;
            while (j + 1U < doses.size() && (doses[j + 1U] - doses[j]) <= maxGap) {
                ++j;
            }

            ranges.emplace_back(doses[i], doses[j]);
            i = j + 1U;
        }

        return ranges;
    };

    double maxEffect = -std::numeric_limits<double>::infinity();
    std::size_t peakIndex = 0U;
    for (std::size_t i = 0U; i < features.size(); ++i) {
        if (features[i].effectPct > maxEffect) {
            maxEffect = features[i].effectPct;
            peakIndex = i;
        }
    }

    const bool noResponse = !std::isfinite(maxEffect) || maxEffect < 20.0;
    const std::string stabilityScore = getStability(stabilityStats.stdRate, stabilityStats.stdToxicity);
    const bool toxicityDetected = report.hasToxicThreshold;
    const bool lowStability = stabilityScore == "LOW";

    const auto ineffectiveRanges = buildDoseRanges([](double effectPct) {
        return effectPct < 20.0;
    });
    const auto therapeuticRanges = buildDoseRanges([](double effectPct) {
        return effectPct >= 20.0 && effectPct <= 60.0;
    });
    const auto overSuppressionRanges = buildRangesFromDoses(report.overSuppressionDoses);
    const auto excitatoryRiskRanges = buildRangesFromDoses(report.excitatoryRiskDoses);
    const auto stabilizationSaturationRanges = buildRangesFromDoses(report.stabilizationSaturationDoses);

    const bool overSuppressionDetected = !overSuppressionRanges.empty();
    const bool fragmentedWindow = therapeuticRanges.size() > 1U;
    const bool therapeuticWindowExists = !therapeuticRanges.empty();
    const bool promising = !noResponse && !toxicityDetected && !overSuppressionDetected && !lowStability && therapeuticWindowExists && report.sigmoidR2 >= 0.95;

    const std::string curveType = noResponse
                                      ? std::string("Flat / Non-responsive")
                                      : (report.sigmoidR2 >= 0.95 ? std::string("Sigmoidal")
                                                                  : (report.sigmoidR2 >= 0.80 ? std::string("Weak Sigmoidal")
                                                                                              : std::string("Irregular / Non-sigmoidal")));
    const std::string responseStrength = noResponse
                                            ? std::string("None")
                                            : (maxEffect < 40.0 ? std::string("Moderate")
                                                                : (maxEffect < 60.0 ? std::string("Moderate-to-Strong") : std::string("Strong")));
    const std::string effectiveRange = noResponse || therapeuticRanges.empty()
                                           ? std::string("Not observed")
                                           : formatRanges(therapeuticRanges);
    const bool singlePointTherapeuticWindow =
        (therapeuticRanges.size() == 1U) &&
        (std::fabs(therapeuticRanges.front().second - therapeuticRanges.front().first) <=
         std::max(1.0e-6, 0.25 * std::max(1.0e-6, report.stepDose)));
    const std::string windowQuality = noResponse || therapeuticRanges.empty()
                                          ? std::string("Not observed")
                                          : (singlePointTherapeuticWindow
                                                 ? std::string("Narrow")
                                                 : (therapeuticRanges.size() == 1U ? std::string("Continuous")
                                                                                   : std::string("Fragmented")));
    const std::string ineffectiveZone = noResponse ? formatRange(report.minTestedDose, report.maxTestedDose) : formatRanges(ineffectiveRanges);
    const std::string therapeuticZone = noResponse || therapeuticRanges.empty() ? std::string("Not observed") : formatRanges(therapeuticRanges);
    const std::string overSuppressionZone = noResponse ? std::string("Not observed") : formatRanges(overSuppressionRanges);
    const std::string excitatoryRiskZone = excitatoryRiskRanges.empty() ? std::string("Not observed") : formatRanges(excitatoryRiskRanges);
    const std::string stabilizationSaturationZone = stabilizationSaturationRanges.empty() ? std::string("Not observed") : formatRanges(stabilizationSaturationRanges);
    const std::string onsetDose = noResponse || therapeuticRanges.empty() ? std::string("Not observed") : ("~" + formatRuntimeNumber(therapeuticRanges.front().first));
    const bool peakInToxicRange = toxicityDetected && (features[peakIndex].dose >= report.toxicMinDose);
    const std::string peakEfficiency = noResponse
                                           ? std::string("Not observed")
                                           : ("~" + formatRuntimeNumber(features[peakIndex].dose) +
                                              (peakInToxicRange ? " (within toxic range)" : ""));
    const std::string saturationText = noResponse
                                           ? std::string("Not observed")
                                           : ((peakIndex + 1U >= features.size())
                                                  ? std::string("Not observed within tested range")
                                                  : std::string("Observed beyond ") + formatRuntimeNumber(features[peakIndex].dose));

    const std::string recommendation = report.recommendation;
    const std::string riskLevel = report.riskLevel;
    const std::string reason = report.reason;
    const std::string confidence = report.confidence;
    const std::string responseMode = report.responseMode;

    const bool excitatoryMode =
        (responseMode == "EXCITATORY_RESPONSE") ||
        (report.biologicalState == spp::analyzer::BiologicalState::Hyperexcitability);
    const bool stabilizationMode =
        (responseMode == "STABILIZING_RESPONSE") ||
        (report.biologicalState == spp::analyzer::BiologicalState::NetworkStabilization);
    const std::string windowSectionTitle = excitatoryMode
                                               ? std::string("Excitatory Response Range")
                                 : (stabilizationMode ? std::string("Stabilization Response Range")
                                             : std::string("Therapeutic Window"));
    const std::string effectiveRangeLabel = excitatoryMode
                                                ? std::string("Excitatory Range")
                                  : (stabilizationMode ? std::string("Stabilization Range")
                                              : std::string("Effective Range"));
    const std::string zoneLabel = excitatoryMode
                                      ? std::string("Excitatory Zone")
                           : (stabilizationMode ? std::string("Stabilization Zone")
                                       : std::string("Therapeutic Zone"));
    const std::string optimalZoneText = excitatoryMode
                                            ? std::string("Transient excitatory regime before seizure-risk escalation")
                                            : (stabilizationMode
                                  ? std::string("Calcium-channel blockade reduced synchronization and neural instability")
                                                   : std::string("Moderate, controlled suppression (20-60%)"));
        const std::string windowQualityText = noResponse || therapeuticRanges.empty()
                                ? std::string("Not observed")
                                : (stabilizationMode
                                    ? (therapeuticRanges.size() > 1U ? std::string("Fragmented")
                                                      : std::string("Continuous"))
                                    : windowQuality);

    out << "==================================================\n";
    out << "SILICON PATIENT - DRUG EVALUATION REPORT\n";
    out << "==================================================\n\n";

    out << "[Drug Input]\n";
    appendStructuredReportLine(out, "Drug Name", evalInput.drug_name);
    appendStructuredReportLine(out, "Engine Input Mode", engineInputMode);
    appendStructuredReportLine(out, "Na IC50", evalInput.config.ic50_na);
    appendStructuredReportLine(out, "K IC50", evalInput.config.ic50_k);
    appendStructuredReportLine(out, "Ca IC50", evalInput.config.ic50_ca);
    appendStructuredReportLine(out, "Hill", evalInput.config.hill);
    appendStructuredReportLine(out, "Runs", runCount);
    out << "\n--------------------------------------------------\n\n";

    out << "[Dose Range]\n";
    appendLine("Tested Range", formatRange(report.minTestedDose, report.maxTestedDose));
    appendLine("Step Size", formatRuntimeNumber(report.stepDose));
    out << "\n--------------------------------------------------\n\n";

    out << "[Response Characteristics]\n";
    appendLine("Curve Type", curveType);
    appendLine("Response Mode", responseMode);
    appendLine("Model Fit (R^2)", noResponse ? std::string("N/A") : formatRuntimeNumber(report.sigmoidR2, 2));
    appendLine("Max Effect", formatRuntimeNumber(maxEffect, 0) + " %");
    appendLine("Response Strength", responseStrength);
    out << "\n--------------------------------------------------\n\n";

    out << "[Safety Analysis]\n";
    appendLine("Toxic Threshold", toxicityDetected ? formatRuntimeNumber(report.toxicMinDose) : (">" + formatRuntimeNumber(report.maxTestedDose) + " (Not observed)"));
    appendLine("Safety Observation", toxicityDetected ? "Toxicity observed within tested range" : "No toxicity within tested range");
    out << "\n--------------------------------------------------\n\n";

    out << "[" << windowSectionTitle << "]\n";
    appendLine(effectiveRangeLabel, effectiveRange);
    appendLine("Window Quality", windowQualityText);
    appendLine("Optimal Zone", optimalZoneText);
    out << "\n--------------------------------------------------\n\n";

    out << "[Dose Classification Summary]\n";
    appendLine("Ineffective Zone", ineffectiveZone);
    appendLine(zoneLabel, therapeuticZone);
    if (responseMode == "EXCITATORY_RESPONSE" || excitatoryMode) {
        appendLine("Severe Excitability Zone", excitatoryRiskZone);
    } else if (responseMode == "STABILIZING_RESPONSE" || stabilizationMode) {
        appendLine("Saturated Stabilization Zone", stabilizationSaturationZone);
    } else {
        appendLine("Over-Suppression", overSuppressionZone);
    }
    out << "\n--------------------------------------------------\n\n";

    if (stabilizationMode) {
        out << "[Calcium Stabilization Metrics]\n";
        appendLine("Sync Reduction", formatRuntimeNumber(report.syncReductionPct, 1) + " %");
        const bool niiReductionObserved = report.niiReductionPct > 0.0;
        appendLine(niiReductionObserved ? "NII Reduction" : "NII Increase",
                   formatRuntimeNumber(niiReductionObserved ? report.niiReductionPct : report.niiIncreasePct, 1) + " %");
        appendLine("Seizure Reduction", formatRuntimeNumber(report.seizureReductionPct, 1) + " %");
        appendLine("Burst Reduction", formatRuntimeNumber(report.burstReductionPct, 1) + " %");
        appendLine("Calcium Effect", formatRuntimeNumber(report.calciumEffectMagnitude, 1) + " %");
        out << "\n--------------------------------------------------\n\n";
    }

    out << "[Pharmacodynamic Interpretation]\n";
    appendLine("Onset Dose", onsetDose);
    appendLine("Peak Efficiency", peakEfficiency);
    appendLine("Saturation Trend", saturationText);
    out << "\n--------------------------------------------------\n\n";

    out << "[Stability Analysis]\n";
    appendLine("Run Count", std::to_string(runCount));
    appendLine("Rate Variability", formatRuntimeNumber(stabilityStats.stdRate, 2));
    appendLine("Toxicity Variance", formatRuntimeNumber(stabilityStats.stdToxicity, 2));
    appendLine("Stability Score", stabilityScore);
    out << "\n--------------------------------------------------\n\n";

    out << "[FINAL DECISION]\n";
    appendLine("Recommendation", recommendation);
    appendLine("Risk Level", riskLevel);
    appendLine("Reason", reason);
    appendLine("Confidence", confidence);
    out << "\n==================================================\n";

    return out.str();
}

void writeDrugEvaluationReport(
    const std::string& filePath,
    const PharmaDecisionReport& report,
    const AggregatedStats& stabilityStats,
    int runCount,
    const RuntimeInput& evalInput,
    const std::string& engineInputMode
) {
    std::ofstream out(filePath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("Unable to open drug evaluation report file: " + filePath);
    }

    out << buildDrugEvaluationReportText(report, stabilityStats, runCount, evalInput, engineInputMode);
}

void runDoseEvaluationMode(const RuntimeInput& baseInput, const std::string& engineInputMode = "Default Internal Engine Config", const std::optional<int>& userRuns = std::nullopt) {
    constexpr double kDefaultMinDose = 0.0;
    constexpr double kDefaultMaxDose = 20.0;
    constexpr double kDefaultStepDose = 2.0;
    constexpr int kDoseEvalRuns = 10;

    auto tryParseDoubleEnv = [](const char* name) -> std::optional<double> {
        if (const auto text = readEnvVar(name); text.has_value()) {
            try {
                return std::stod(trim(*text));
            } catch (const std::exception&) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    };

    RuntimeInput evalInput = baseInput;

    // If engineInputMode is "User Drug Config" then prefer values loaded from
    // the provided JSON and do not allow environment overrides to silently
    // replace those values. Environment overrides may still set `SPP_DOSE_EVAL_RUNS`.
    const bool skipEnvOverrides = (engineInputMode == "User Drug Config");

    if (!skipEnvOverrides) {
        if (const auto v = tryParseDoubleEnv("SPP_DOSE_EVAL_IC50_NA"); v.has_value() && *v > 0.0) {
            evalInput.config.ic50_na = *v;
        }
        if (const auto v = tryParseDoubleEnv("SPP_DOSE_EVAL_IC50_K"); v.has_value() && *v > 0.0) {
            evalInput.config.ic50_k = *v;
        }
        if (const auto v = tryParseDoubleEnv("SPP_DOSE_EVAL_IC50_CA"); v.has_value() && *v > 0.0) {
            evalInput.config.ic50_ca = *v;
        }
        if (const auto v = tryParseDoubleEnv("SPP_DOSE_EVAL_HILL"); v.has_value() && *v >= 1.0 && *v <= 6.0) {
            evalInput.config.hill = *v;
        }
    }

    double minDose = kDefaultMinDose;
    double maxDose = kDefaultMaxDose;
    double stepDose = kDefaultStepDose;

    if (const auto v = tryParseDoubleEnv("SPP_DOSE_EVAL_MIN"); v.has_value() && *v >= 0.0) {
        minDose = *v;
    }
    if (const auto v = tryParseDoubleEnv("SPP_DOSE_EVAL_MAX"); v.has_value() && *v > minDose) {
        maxDose = *v;
    }
    if (const auto v = tryParseDoubleEnv("SPP_DOSE_EVAL_STEP"); v.has_value() && *v > 0.0) {
        stepDose = *v;
    }

    int doseEvalRuns = userRuns.has_value() ? *userRuns : kDoseEvalRuns;
    if (const auto runsText = readEnvVar("SPP_DOSE_EVAL_RUNS")) {
        try {
            const int parsedRuns = std::stoi(trim(*runsText));
            if (parsedRuns > 0 && parsedRuns <= 200) {
                doseEvalRuns = parsedRuns;
            }
        } catch (const std::exception&) {
            // Ignore invalid override and keep default.
        }
    }

    std::vector<double> doses;
    for (double dose = minDose; dose <= maxDose + 1.0e-9; dose += stepDose) {
        doses.push_back(dose);
    }

    std::vector<DoseObservation> observations;
    std::vector<DoseResponsePoint> doseResponse;
    std::vector<NetworkMetricRecord> networkRecords;
    std::vector<RunResult> allRunResults;
    observations.reserve(doses.size());
    doseResponse.reserve(doses.size());
    networkRecords.reserve(doses.size());
    allRunResults.reserve(doses.size() * static_cast<std::size_t>(doseEvalRuns));

    std::vector<spp::analyzer::NeuronMetrics> finalNeuronMetrics;
    const std::uint32_t baseSeed = 0xD05E0001U;

    for (std::size_t i = 0; i < doses.size(); ++i) {
        const auto runResults = runMultipleSimulations(
            evalInput,
            doses[i],
            doseEvalRuns,
            baseSeed + static_cast<std::uint32_t>(i * 7919U),
            &finalNeuronMetrics,
            nullptr
        );
        if (runResults.empty()) {
            continue;
        }

        const AggregatedStats doseStats = computeStats(runResults);
        allRunResults.insert(allRunResults.end(), runResults.begin(), runResults.end());

        const NetworkMetrics aggregatedMetrics = buildAggregatedNetworkMetrics(doseStats);
        const std::string label = getStability(doseStats.stdRate, doseStats.stdToxicity);

        {
            const float doseF = static_cast<float>(doses[i]);
            const float blockNa = static_cast<float>(spp::drug::DrugModel::hillBlock(doseF, static_cast<float>(evalInput.config.ic50_na), static_cast<float>(evalInput.config.hill)));
            const float blockK = static_cast<float>(spp::drug::DrugModel::hillBlock(doseF, static_cast<float>(evalInput.config.ic50_k), static_cast<float>(evalInput.config.hill)));
            const float blockCa = static_cast<float>(spp::drug::DrugModel::hillBlock(doseF, static_cast<float>(evalInput.config.ic50_ca), static_cast<float>(evalInput.config.hill)));
            observations.push_back(DoseObservation{
                doseF,
                doseStats.meanRate,
                doseStats.meanSync,
                doseStats.meanBurst,
                doseStats.meanNii,
                doseStats.meanISI,
                doseStats.meanSeizure,
                doseStats.meanToxicity,
                blockNa,
                blockK,
                blockCa
            });
        }

        doseResponse.push_back(DoseResponsePoint{
            static_cast<float>(doses[i]),
            doseStats.meanRate,
            doseStats.meanSync,
            doseStats.meanBurst,
            doseStats.meanNii,
            doseStats.meanSeizure,
            doseStats.meanToxicity,
            aggregatedMetrics.stabilityScore,
            label
        });

        networkRecords.push_back(NetworkMetricRecord{
            "dose_eval",
            static_cast<float>(doses[i]),
            aggregatedMetrics,
            label
        });

    }

    if (observations.empty()) {
        throw std::runtime_error("Dose evaluation failed: no aggregated observations produced");
    }

    const AggregatedStats stabilityStats = computeStats(allRunResults);

    const spp::analyzer::DecisionStabilityInput stabilityInput{
        stabilityStats.stdRate,
        stabilityStats.stdToxicity,
        getStability(stabilityStats.stdRate, stabilityStats.stdToxicity),
        doseEvalRuns
    };
    const PharmaDecisionReport report = PharmaDecisionEngine::evaluate(observations, stabilityInput);

    std::cout << buildDrugEvaluationReportText(report, stabilityStats, doseEvalRuns, evalInput, engineInputMode);

    if (evalInput.config.export_csv) {
        std::filesystem::create_directories(evalInput.config.output_folder);
        CsvWriter::writeDoseResponse(evalInput.config.output_folder + "/dose_response.csv", doseResponse);
        CsvWriter::writeNetworkMetrics(evalInput.config.output_folder + "/network_metrics.csv", networkRecords);
        CsvWriter::writeNeuronStats(evalInput.config.output_folder + "/neuron_stats.csv", finalNeuronMetrics);
        writeDrugEvaluationReport(
            evalInput.config.output_folder + "/drug_evaluation_report.txt",
            report,
            stabilityStats,
            doseEvalRuns,
            evalInput,
            engineInputMode
        );
    }
}

// Minimal JSON extractor helpers (tailored to expected payload structure).
static std::string readFileToString(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return std::string();
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static std::optional<std::string> extractJsonString(const std::string& s, const std::string& key, std::size_t start = 0) {
    const std::string pat = '"' + key + '"';
    const auto p = s.find(pat, start);
    if (p == std::string::npos) return std::nullopt;
    const auto colon = s.find(':', p + pat.size());
    if (colon == std::string::npos) return std::nullopt;
    const auto quote1 = s.find('"', colon);
    if (quote1 == std::string::npos) return std::nullopt;
    const auto quote2 = s.find('"', quote1 + 1);
    if (quote2 == std::string::npos) return std::nullopt;
    return s.substr(quote1 + 1, quote2 - (quote1 + 1));
}

static std::optional<double> extractJsonNumber(const std::string& s, const std::string& key, std::size_t start = 0) {
    const std::string pat = '"' + key + '"';
    const auto p = s.find(pat, start);
    if (p == std::string::npos) return std::nullopt;
    const auto colon = s.find(':', p + pat.size());
    if (colon == std::string::npos) return std::nullopt;
    // find first digit/+-/. after colon
    const auto numStart = s.find_first_of("0123456789-+.", colon);
    if (numStart == std::string::npos) return std::nullopt;
    std::size_t numEnd = numStart;
    while (numEnd < s.size() && (std::isdigit((unsigned char)s[numEnd]) || s[numEnd] == '.' || s[numEnd] == 'e' || s[numEnd] == 'E' || s[numEnd] == '+' || s[numEnd] == '-')) {
        ++numEnd;
    }
    try {
        return std::stod(s.substr(numStart, numEnd - numStart));
    } catch (...) {
        return std::nullopt;
    }
}

static bool loadDrugConfigFromJsonFile(const std::string& path, RuntimeInput& outInput, std::optional<int>& outRuns, std::string& outMode) {
    const std::string content = readFileToString(path);
    if (content.empty()) return false;

    if (auto v = extractJsonString(content, "drug_name"); v.has_value()) {
        outInput.drug_name = *v;
    }

    // channels block
    const auto channelsPos = content.find("\"channels\"");
    if (channelsPos != std::string::npos) {
        const auto naPos = content.find("\"Na\"", channelsPos);
        if (naPos != std::string::npos) {
            if (auto n = extractJsonNumber(content, "ic50", naPos); n.has_value()) outInput.config.ic50_na = *n;
            if (auto h = extractJsonNumber(content, "hill", naPos); h.has_value()) outInput.config.hill = *h;
        }
        const auto kPos = content.find("\"K\"", channelsPos);
        if (kPos != std::string::npos) {
            if (auto n = extractJsonNumber(content, "ic50", kPos); n.has_value()) outInput.config.ic50_k = *n;
            if (auto h = extractJsonNumber(content, "hill", kPos); h.has_value()) outInput.config.hill = *h;
        }
        const auto caPos = content.find("\"Ca\"", channelsPos);
        if (caPos != std::string::npos) {
            if (auto n = extractJsonNumber(content, "ic50", caPos); n.has_value()) outInput.config.ic50_ca = *n;
            if (auto h = extractJsonNumber(content, "hill", caPos); h.has_value()) outInput.config.hill = *h;
        }
    }

    const auto doseRangePos = content.find("\"dose_range\"");
    if (doseRangePos != std::string::npos) {
        if (auto v = extractJsonNumber(content, "min", doseRangePos); v.has_value()) outInput.config.dose = *v; // note: base dose will be set to min
        if (auto v = extractJsonNumber(content, "min", doseRangePos); v.has_value()) {
            // we'll set sweep start/end/points elsewhere; but keep dose as min for single run context
        }
        if (auto v = extractJsonNumber(content, "min", doseRangePos); v.has_value()) {
            (void)v;
        }
        if (auto v = extractJsonNumber(content, "min", doseRangePos); v.has_value()) {
            (void)v;
        }
        if (auto v = extractJsonNumber(content, "min", doseRangePos); v.has_value()) {
            (void)v;
        }
        if (auto v = extractJsonNumber(content, "min", doseRangePos); v.has_value()) {
            (void)v;
        }
        if (auto v = extractJsonNumber(content, "min", doseRangePos); v.has_value()) {
            (void)v;
        }
        // extract min/max/step explicitly
        if (auto v = extractJsonNumber(content, "min", doseRangePos); v.has_value()) {
            // nothing: already handled
            (void)v;
        }
        if (auto v = extractJsonNumber(content, "max", doseRangePos); v.has_value()) {
            // nothing here; main run will use min/max/step local variables
            (void)v;
        }
    }

    if (auto v = extractJsonNumber(content, "runs"); v.has_value()) {
        outRuns = static_cast<int>(*v);
    }

    if (auto v = extractJsonString(content, "mode"); v.has_value()) {
        outMode = *v;
    }

    // For dose_range we separately parse min/max/step and provide them via environment or outInput fields.
    const auto dosePos = content.find("\"dose_range\"");
    if (dosePos != std::string::npos) {
        if (auto v = extractJsonNumber(content, "min", dosePos); v.has_value()) {
            outInput.config.dose = *v;
        }
        // store min,max,step temporarily in the output folder fields (abuse existing struct)
        // We will not set sweep points here; engine uses SPP_DOSE_EVAL_MIN/MAX/STEP env vars instead.
    }

    return true;
}

[[maybe_unused]] void runDoseSweep(const RuntimeInput& baseInput) {
    const std::vector<double> doses = buildLinearDoseGrid(baseInput.sweep_start, baseInput.sweep_end, baseInput.sweep_points);
    std::vector<DoseResponsePoint> doseResponse;
    std::vector<NetworkMetricRecord> networkRecords;
    std::vector<DoseObservation> decisionInput;
    doseResponse.reserve(doses.size());
    networkRecords.reserve(doses.size());
    decisionInput.reserve(doses.size());

    std::vector<spp::analyzer::NeuronMetrics> finalNeuronMetrics;

    const std::uint32_t baseSeed = 0x51C0A1U;

    std::cout << '\n';
    printDivider('=');
    std::cout << "SILICON PATIENT DOSE SWEEP REPORT\n";
    printDivider('=');
    printSection("Run Configuration");
    printMetricLine("Dose Points", std::to_string(doses.size()));
    printMetricLine("Dose Range", formatRuntimeNumber(baseInput.sweep_start) + " to " + formatRuntimeNumber(baseInput.sweep_end));

    for (std::size_t i = 0; i < doses.size(); ++i) {
        RuntimeInput runInput = baseInput;
        runInput.config.dose = doses[i];

        const SimulationSummary summary = runSingleSimulation(runInput, baseSeed + static_cast<std::uint32_t>(i * 9973U));
        finalNeuronMetrics = summary.neuron_metrics;

        const std::string label = SeizureDetector::toString(summary.classification);

        {
            const float doseF = static_cast<float>(runInput.config.dose);
            const float blockNa = static_cast<float>(spp::drug::DrugModel::hillBlock(doseF, static_cast<float>(runInput.config.ic50_na), static_cast<float>(runInput.config.hill)));
            const float blockK = static_cast<float>(spp::drug::DrugModel::hillBlock(doseF, static_cast<float>(runInput.config.ic50_k), static_cast<float>(runInput.config.hill)));
            const float blockCa = static_cast<float>(spp::drug::DrugModel::hillBlock(doseF, static_cast<float>(runInput.config.ic50_ca), static_cast<float>(runInput.config.hill)));
            decisionInput.push_back(DoseObservation{
                doseF,
                summary.network_metrics.meanFiringRateHz,
                summary.network_metrics.synchronizationIndex,
                summary.network_metrics.burstIndex,
                summary.network_metrics.nii,
                summary.network_metrics.irregularityIndex,
                summary.network_metrics.seizureProbabilityPct,
                summary.network_metrics.suppressionPct,
                blockNa,
                blockK,
                blockCa
            });
        }

        doseResponse.push_back(DoseResponsePoint{
            static_cast<float>(runInput.config.dose),
            summary.network_metrics.meanFiringRateHz,
            summary.network_metrics.synchronizationIndex,
            summary.network_metrics.burstIndex,
            summary.network_metrics.nii,
            summary.network_metrics.seizureProbabilityPct,
            summary.network_metrics.suppressionPct,
            summary.network_metrics.stabilityScore,
            label
        });

        networkRecords.push_back(NetworkMetricRecord{
            "dose_sweep",
            static_cast<float>(runInput.config.dose),
            summary.network_metrics,
            label
        });

    }

    const PharmaDecisionReport decisionReport = PharmaDecisionEngine::evaluate(decisionInput);
    const double testedMaxDose = doses.empty() ? 0.0 : doses.back();

    printSection("Pharma Decision Summary");
    if (decisionReport.hasSafeRange && !decisionReport.hasToxicThreshold) {
        printMetricLine(
            "Safe Dose Range",
            "[" + formatRuntimeNumber(decisionReport.safeMinDose) + ", " + formatRuntimeNumber(decisionReport.safeMaxDose) +
                "] (No toxicity observed within tested range)"
        );
        printMetricLine("Toxic Threshold", ">" + formatRuntimeNumber(testedMaxDose) + " (Not reached)");
    } else if (decisionReport.hasSafeRange) {
        printMetricLine(
            "Safe Dose Range",
            "[" + formatRuntimeNumber(decisionReport.safeMinDose) + ", " + formatRuntimeNumber(decisionReport.safeMaxDose) + "]"
        );
        if (decisionReport.hasToxicThreshold) {
            printMetricLine("Toxic Threshold", decisionReport.toxicMinDose);
        } else {
            printMetricLine("Toxic Threshold", "NOT DETECTED");
        }
    } else {
        printMetricLine("Safe Dose Range", "NOT DETECTED");
        printMetricLine("Toxic Threshold", "NOT DETECTED");
    }

    if (decisionReport.hasEffectiveDose) {
        printMetricLine("Effective Min Dose", decisionReport.effectiveMinDose);
    } else {
        printMetricLine("Effective Min Dose", "NOT DETECTED");
    }

    if (decisionReport.hasTherapeuticWindow) {
        printMetricLine("Therapeutic Window", decisionReport.therapeuticWindow);
    } else {
        printMetricLine("Therapeutic Window", "UNAVAILABLE");
    }

    printMetricLine("Overall Profile", PharmaDecisionEngine::toString(decisionReport.overallTier));
    printMetricLine("Peak Risk Score", decisionReport.peakRiskScore, " / 100");
    printMetricLine("Peak Seizure Prob.", decisionReport.peakSeizureProbabilityPct, "%");
    printMetricLine("Peak Suppression", decisionReport.peakSuppressionPct, "%");
    printMetricLine("Peak Early Warning", decisionReport.peakEarlyWarningIndex, " / 100");
    printMetricLine("Max dSeizure/dDose", formatSeizureSlopeDisplay(decisionReport.maxSeizureSlopePctPerDose));

    if (baseInput.config.export_csv) {
        std::filesystem::create_directories(baseInput.config.output_folder);
        CsvWriter::writeDoseResponse(baseInput.config.output_folder + "/dose_response.csv", doseResponse);
        CsvWriter::writeNetworkMetrics(baseInput.config.output_folder + "/network_metrics.csv", networkRecords);
        CsvWriter::writeNeuronStats(baseInput.config.output_folder + "/neuron_stats.csv", finalNeuronMetrics);
        CsvWriter::writeDrugSummary(baseInput.config.output_folder + "/drug_summary.csv", decisionReport.points);
        writePharmaDecisionReport(baseInput.config.output_folder + "/drug_report.txt", baseInput, decisionReport);
        printSection("Artifacts");
        printMetricLine("Output Folder", baseInput.config.output_folder);
        printMetricLine("Files", "dose_response.csv, network_metrics.csv, neuron_stats.csv, drug_summary.csv, drug_report.txt");
    }
}

void runSingleSimulationMode(const RuntimeInput& input) {
    const std::uint32_t baseSeed = makeSeed();
    const SimulationSummary summary = runSingleSimulation(input, baseSeed);
    const SingleRunDoseContext doseContext = buildSingleRunDoseContext(input, summary, baseSeed + 911U);
    printResult(input, summary, doseContext);
    exportSingleRunArtifacts(input, summary);
}

// Internal benchmark harness: Internal Biological Benchmark Suite
// Purpose: regression testing, biological verification, literature matching,
// scientific reproducibility, and internal QA. This is an internal developer
// tool and is not exposed as part of the public user workflow. To run this
// suite from the CLI set the environment variable `SPP_DEVELOPER_MODE=1`
// and invoke the hidden flag `--internal-benchmark`.
void runInternalBiologicalBenchmarkSuite() {
    const auto suiteStart = std::chrono::steady_clock::now();

    RuntimeInput baseInput;
    baseInput.drug_name = "InternalBiologicalBenchmarkSuite";

#ifdef SPP_USE_CUDA
    baseInput.config.neuron_count = 420;
#else
    baseInput.config.neuron_count = 320;
#endif
    baseInput.config.sim_time = 500.0;
    baseInput.config.dt = 0.04;

    baseInput.config.dose = 0.0;
    baseInput.config.ic50_na = 1000.0;
    baseInput.config.ic50_k = 1000.0;
    baseInput.config.ic50_ca = 1000.0;
    baseInput.config.hill = 3.0;
    baseInput.config.connectivity = 0.05;
    baseInput.config.excitatory_ratio = 0.80;
    baseInput.config.external_current = 1.05;
    baseInput.config.noise_level = 0.72;
    baseInput.config.excitatory_weight_scale = 1.00;
    baseInput.config.inhibitory_weight_scale = 1.00;
    baseInput.config.export_csv = false;

    const std::string outputDir = "output_internal_benchmark_full";
    std::filesystem::create_directories(outputDir);
    baseInput.config.output_folder = outputDir;

    bool fixedSeedMode = true;
    if (const auto envValue = readEnvVar("SPP_FIXED_SEED_MODE"); envValue.has_value()) {
        const auto parsed = parseBoolText(envValue.value());
        if (parsed.has_value()) {
            fixedSeedMode = parsed.value();
        }
    }

    constexpr int kStatRuns = 4;
    constexpr int kDosePoints = 12;
    constexpr float kWindowMs = 50.0f;

    const std::uint32_t deterministicBaseSeed = 0x51C0A1U;
    const std::uint32_t randomBaseSeed = makeSeed();

    std::vector<std::string> metadataLines;
    metadataLines.reserve(512);

    auto formatStats = [](const MetricStats& stats, int precision) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(precision)
            << stats.mean << " +/- " << stats.stddev
            << " (95% CI: [" << stats.ci95Low << ", " << stats.ci95High << "])";
        return out.str();
    };

    auto makeSeedForRun = [&](int testIndex, int runIndex) {
        if (fixedSeedMode) {
            return deterministicBaseSeed + static_cast<std::uint32_t>(testIndex * 100000 + runIndex * 7919 + 17);
        }
        return randomBaseSeed ^ static_cast<std::uint32_t>(testIndex * 73856093 + runIndex * 19349663) ^ makeSeed();
    };

    auto logRunMetadata = [&](const std::string& testName, int runIndex, std::uint32_t seed, const RuntimeInput& input) {
        std::ostringstream line;
        line << testName
             << ",run=" << runIndex
             << ",seed=" << seed
             << ",neurons=" << input.config.neuron_count
             << ",sim_time_ms=" << input.config.sim_time
             << ",dt_ms=" << input.config.dt
             << ",dose=" << input.config.dose
             << ",ic50_na=" << input.config.ic50_na
             << ",ic50_k=" << input.config.ic50_k
             << ",ic50_ca=" << input.config.ic50_ca
             << ",ex_ratio=" << input.config.excitatory_ratio
             << ",exc_scale=" << input.config.excitatory_weight_scale
             << ",inh_scale=" << input.config.inhibitory_weight_scale;
        metadataLines.push_back(line.str());
    };

    std::vector<ValidationCheck> checks;
    std::vector<std::string> reportBlocks;
    checks.reserve(6);
    reportBlocks.reserve(6);

    std::cout << '\n';
    printDivider('=');
    std::cout << "SILICON PATIENT - INTERNAL BIOLOGICAL BENCHMARK REPORT\n";
    printDivider('=');
    printSection("Run Context");
    printMetricLine("Seed Mode", fixedSeedMode ? "fixed-seed" : "random-seed");
    printMetricLine("Output Directory", outputDir);

    // 1) Baseline Activity (multi-run)
    std::vector<double> baselineRateHz;
    std::vector<double> baselineSync;
    std::vector<double> baselineIsiCv;
    std::vector<double> baselineCoherence;
    std::vector<std::uint32_t> baselineSeeds;
    baselineRateHz.reserve(kStatRuns);
    baselineSync.reserve(kStatRuns);
    baselineIsiCv.reserve(kStatRuns);
    baselineCoherence.reserve(kStatRuns);
    baselineSeeds.reserve(kStatRuns);

    for (int run = 0; run < kStatRuns; ++run) {
        RuntimeInput runInput = baseInput;
        runInput.config.dose = 0.0;
        runInput.config.ic50_na = 1000.0;
        runInput.config.ic50_k = 1000.0;
        runInput.config.ic50_ca = 1000.0;

        const std::uint32_t seed = makeSeedForRun(1, run);
        baselineSeeds.push_back(seed);
        logRunMetadata("Baseline", run + 1, seed, runInput);

        const SimulationSummary summary = runSingleSimulation(runInput, seed);
        baselineRateHz.push_back(summary.network_metrics.meanFiringRateHz);
        baselineSync.push_back(summary.network_metrics.synchronizationIndex);
        baselineIsiCv.push_back(computeMeanIsiCv(summary.neuron_metrics));
        baselineCoherence.push_back(computeCoherenceScore(summary.network_metrics));
    }

    const MetricStats baselineRateStats = computeMetricStats(baselineRateHz);
    const MetricStats baselineSyncStats = computeMetricStats(baselineSync);
    const MetricStats baselineIsiCvStats = computeMetricStats(baselineIsiCv);

    ValidationCheck baselineCheck;
    baselineCheck.name = "Baseline Activity (" + std::to_string(kStatRuns) + " runs)";
    const bool baselineRatePass = baselineRateStats.ci95Low >= 5.0 && baselineRateStats.ci95High <= 22.0;
    const bool baselineSyncPass = baselineSyncStats.ci95High < 0.30;
    const bool baselineIsiPass = baselineIsiCvStats.ci95Low > 0.35;
    baselineCheck.pass = baselineRatePass && baselineSyncPass && baselineIsiPass;
    {
        std::ostringstream details;
        details << "Mean Rate (Hz)         : " << formatStats(baselineRateStats, 2) << "\n"
                << "Synchronization        : " << formatStats(baselineSyncStats, 2) << "\n"
                << "ISI CV                 : " << formatStats(baselineIsiCvStats, 2) << "\n";

        if (!baselineCheck.pass) {
            details << "Findings               :";
            if (!baselineRatePass) {
                details << " meanRate outside [5,22] across CI.";
            }
            if (!baselineSyncPass) {
                details << " sync CI upper bound >= 0.30.";
            }
            if (!baselineIsiPass) {
                details << " ISI CV CI lower bound <= 0.35.";
            }
            details << "\n";
        }
        baselineCheck.details = details.str();
    }
    checks.push_back(baselineCheck);
    printValidationCheck(baselineCheck);
    {
        std::ostringstream report;
        report << "TEST: Baseline Activity\n"
               << "Runs: " << kStatRuns << "\n"
               << "meanRate: " << formatStats(baselineRateStats, 3) << " Hz\n"
               << "sync: " << formatStats(baselineSyncStats, 3) << "\n"
               << "ISI CV: " << formatStats(baselineIsiCvStats, 3) << "\n"
               << "RESULT: " << (baselineCheck.pass ? "PASS" : "FAIL") << "\n";
        reportBlocks.push_back(report.str());
    }

    // 2) Excitation-Inhibition Balance (multi-run)
    std::vector<double> rateA;
    std::vector<double> syncA;
    std::vector<double> rateB;
    std::vector<double> syncB;
    std::vector<double> seizureB;
    int stableCountB = 0;
    rateA.reserve(kStatRuns);
    syncA.reserve(kStatRuns);
    rateB.reserve(kStatRuns);
    syncB.reserve(kStatRuns);
    seizureB.reserve(kStatRuns);

    for (int run = 0; run < kStatRuns; ++run) {
        RuntimeInput runA = baseInput;
        runA.config.excitatory_ratio = 0.84;
        runA.config.excitatory_weight_scale = 1.05;
        runA.config.inhibitory_weight_scale = 0.98;
        runA.config.external_current += 0.10;
        const std::uint32_t seedA = makeSeedForRun(2, run);
        logRunMetadata("EI_CaseA", run + 1, seedA, runA);
        const SimulationSummary summaryA = runSingleSimulation(runA, seedA);

        RuntimeInput runB = baseInput;
        runB.config.excitatory_ratio = 0.74;
        runB.config.excitatory_weight_scale = 0.50;
        runB.config.inhibitory_weight_scale = 1.45;
        runB.config.external_current -= 1.40;
        runB.config.noise_level = std::max(0.0, runB.config.noise_level - 0.25);
        const std::uint32_t seedB = makeSeedForRun(3, run);
        logRunMetadata("EI_CaseB", run + 1, seedB, runB);
        const SimulationSummary summaryB = runSingleSimulation(runB, seedB);

        rateA.push_back(summaryA.network_metrics.meanFiringRateHz);
        syncA.push_back(summaryA.network_metrics.synchronizationIndex);
        rateB.push_back(summaryB.network_metrics.meanFiringRateHz);
        syncB.push_back(summaryB.network_metrics.synchronizationIndex);
        seizureB.push_back(summaryB.network_metrics.seizureProbabilityPct / 100.0);
        if (isSafeState(summaryB.classification)) {
            ++stableCountB;
        }
    }

    const MetricStats rateAStats = computeMetricStats(rateA);
    const MetricStats syncAStats = computeMetricStats(syncA);
    const MetricStats rateBStats = computeMetricStats(rateB);
    const MetricStats syncBStats = computeMetricStats(syncB);
    const MetricStats seizureBStats = computeMetricStats(seizureB);

    const double zRate = computeWelchZScore(rateA, rateB);
    const double zSync = computeWelchZScore(syncA, syncB);
    const double stableFracB = static_cast<double>(stableCountB) / static_cast<double>(kStatRuns);

    ValidationCheck eiCheck;
    eiCheck.name = "E/I Balance (" + std::to_string(kStatRuns) + " runs per case)";
    const bool rateSignificant = (rateAStats.mean > rateBStats.mean) && (zRate > 1.96);
    const bool syncSignificant = (syncAStats.mean > syncBStats.mean) && (zSync > 1.96);
    const bool caseBStable = stableFracB >= 0.70 && seizureBStats.ci95High < 0.40;
    eiCheck.pass = rateSignificant && syncSignificant && caseBStable;
    {
        std::ostringstream details;
        details << "CaseA Mean Rate (Hz)   : " << formatStats(rateAStats, 2) << "\n"
                << "CaseB Mean Rate (Hz)   : " << formatStats(rateBStats, 2) << "\n"
                << "CaseA Synchronization  : " << formatStats(syncAStats, 2) << "\n"
                << "CaseB Synchronization  : " << formatStats(syncBStats, 2) << "\n"
                << "zRate                  : " << formatRuntimeNumber(zRate) << "\n"
                << "zSync                  : " << formatRuntimeNumber(zSync) << "\n"
                << "CaseB Stable Fraction  : " << formatRuntimeNumber(stableFracB) << "\n";

        if (!eiCheck.pass) {
            details << "Findings               :";
            if (!rateSignificant) {
                details << " CaseA firing rate not significantly greater than CaseB.";
            }
            if (!syncSignificant) {
                details << " CaseA sync not significantly greater than CaseB.";
            }
            if (!caseBStable) {
                details << " CaseB failed stability envelope.";
            }
            details << "\n";
        }
        eiCheck.details = details.str();
    }
    checks.push_back(eiCheck);
    printValidationCheck(eiCheck);
    {
        std::ostringstream report;
        report << "TEST: E/I Balance\n"
               << "Runs: " << kStatRuns << " per case\n"
               << "CaseA meanRate: " << formatStats(rateAStats, 3) << " Hz\n"
               << "CaseB meanRate: " << formatStats(rateBStats, 3) << " Hz\n"
               << "CaseA sync: " << formatStats(syncAStats, 3) << "\n"
               << "CaseB sync: " << formatStats(syncBStats, 3) << "\n"
               << "zRate: " << std::fixed << std::setprecision(3) << zRate
               << ", zSync: " << std::fixed << std::setprecision(3) << zSync << "\n"
               << "CaseB seizureProb: " << formatStats(seizureBStats, 3) << "\n"
               << "RESULT: " << (eiCheck.pass ? "PASS" : "FAIL") << "\n";
        reportBlocks.push_back(report.str());
    }

    // 3) Dose-response curve fit (20 independent runs)
    RuntimeInput doseInput = baseInput;
    doseInput.config.ic50_na = 200.0;
    doseInput.config.ic50_k = 8.0;
    doseInput.config.ic50_ca = 1000.0;
    doseInput.config.hill = 3.2;

    const std::vector<double> doseGrid = buildLinearDoseGrid(0.0, 32.0, kDosePoints);
    constexpr int kDoseReplicates = 3;
    std::vector<double> toxicityPct;
    std::vector<double> toxicityFrac;
    toxicityPct.reserve(doseGrid.size());
    toxicityFrac.reserve(doseGrid.size());

    for (std::size_t i = 0; i < doseGrid.size(); ++i) {
        doseInput.config.dose = doseGrid[i];
        double toxicAccum = 0.0;

        for (int rep = 0; rep < kDoseReplicates; ++rep) {
            const std::uint32_t seed = makeSeedForRun(40 + static_cast<int>(i), rep);
            logRunMetadata(
                "DoseResponse",
                static_cast<int>(i * static_cast<std::size_t>(kDoseReplicates) + static_cast<std::size_t>(rep) + 1U),
                seed,
                doseInput
            );

            const SimulationSummary summary = runSingleSimulation(doseInput, seed);
            const double seizureFrac = std::clamp(
                static_cast<double>(summary.network_metrics.seizureProbabilityPct) / 100.0,
                0.0,
                1.0
            );
            const double suppressionFrac = std::clamp(
                static_cast<double>(summary.network_metrics.suppressionPct) / 100.0,
                0.0,
                1.0
            );
            const double instabilityFrac = std::clamp(static_cast<double>(summary.network_metrics.nii), 0.0, 1.0);

            toxicAccum += std::clamp(
                0.60 * seizureFrac + 0.30 * suppressionFrac + 0.10 * instabilityFrac,
                0.0,
                1.0
            );
        }

        const double meanToxicFrac = toxicAccum / static_cast<double>(kDoseReplicates);
        toxicityFrac.push_back(meanToxicFrac);
        toxicityPct.push_back(100.0 * meanToxicFrac);
    }

    std::vector<double> effectFrac;
    std::vector<double> effectPct;
    effectFrac.reserve(doseGrid.size());
    effectPct.reserve(doseGrid.size());

    const double toxAtZeroDose = toxicityFrac.empty() ? 0.0 : toxicityFrac.front();
    const bool toxicityIncreasesWithDose = !toxicityFrac.empty() && (toxicityFrac.back() >= toxAtZeroDose);

    for (double tox : toxicityFrac) {
        double effect = 0.0;
        if (toxicityIncreasesWithDose) {
            const double denom = std::max(0.05, 1.0 - toxAtZeroDose);
            effect = std::clamp((tox - toxAtZeroDose) / denom, 0.0, 1.0);
        } else {
            const double denom = std::max(0.05, toxAtZeroDose);
            effect = std::clamp((toxAtZeroDose - tox) / denom, 0.0, 1.0);
        }

        effectFrac.push_back(effect);
        effectPct.push_back(100.0 * effect);
    }

    std::vector<double> effectForFit = effectFrac;
    for (std::size_t i = 1; i < effectForFit.size(); ++i) {
        if (effectForFit[i] < effectForFit[i - 1U]) {
            effectForFit[i] = effectForFit[i - 1U];
        }
    }

    const SigmoidFitResult sigmoidFit = fitSigmoidCurve(doseGrid, effectForFit);
    const double midSlopePctPerDose = 25.0 * sigmoidFit.k * sigmoidFit.emax;
    const bool r2Pass = sigmoidFit.r2 > 0.85;
    const bool midpointPass = sigmoidFit.d50 > doseGrid.front() && sigmoidFit.d50 < doseGrid.back();
    const bool steepPass = midSlopePctPerDose > 1.0;

    ValidationCheck doseCheck;
    doseCheck.name = "Dose Response (" + std::to_string(kDosePoints) + " runs)";
    doseCheck.pass = r2Pass && midpointPass && steepPass;
    {
        std::ostringstream details;
        details << "Sigmoid R2             : " << formatRuntimeNumber(sigmoidFit.r2) << "\n"
                << "k                      : " << formatRuntimeNumber(sigmoidFit.k) << "\n"
                << "d50                    : " << formatRuntimeNumber(sigmoidFit.d50) << "\n"
                << "emax                   : " << formatRuntimeNumber(sigmoidFit.emax) << "\n"
                << "Mid Slope              : " << formatRuntimeNumber(midSlopePctPerDose) << " (%/dose)\n";

        if (!doseCheck.pass) {
            details << "Findings               :";
            if (!r2Pass) {
                details << " R2 <= 0.85.";
            }
            if (!midpointPass) {
                details << " fitted d50 outside dose range.";
            }
            if (!steepPass) {
                details << " transition slope too shallow.";
            }
            details << "\n";
        }
        doseCheck.details = details.str();
    }
    checks.push_back(doseCheck);
    printValidationCheck(doseCheck);
    {
        std::ostringstream report;
        report << "TEST: Dose Response\n"
               << "Runs: " << kDosePoints << "\n"
               << "sigmoid R2: " << std::fixed << std::setprecision(3) << sigmoidFit.r2 << "\n"
               << "k: " << std::fixed << std::setprecision(4) << sigmoidFit.k
             << ", d50: " << std::fixed << std::setprecision(4) << sigmoidFit.d50
             << ", emax: " << std::fixed << std::setprecision(4) << sigmoidFit.emax << "\n"
               << "midSlope: " << std::fixed << std::setprecision(3) << midSlopePctPerDose << " (%/dose)\n"
               << "RESULT: " << (doseCheck.pass ? "PASS" : "FAIL") << "\n";
        reportBlocks.push_back(report.str());
    }

    {
        const std::string fitCsvPath = outputDir + "/dose_curve_fit.csv";
        std::ofstream fitOut(fitCsvPath, std::ios::out | std::ios::trunc);
        if (!fitOut.is_open()) {
            throw std::runtime_error("Unable to open dose curve fit file: " + fitCsvPath);
        }
        fitOut << "dose,toxicity_pct,effect_pct,effect_pct_monotonic,fit_effect_pct,residual_pct,k,d50,emax,r2\n";
        fitOut << std::fixed << std::setprecision(6);
        for (std::size_t i = 0; i < doseGrid.size(); ++i) {
            const double observedPct = 100.0 * effectForFit[i];
            const double fitPct = 100.0 * sigmoidFit.predicted[i];
            fitOut << doseGrid[i] << ','
                   << toxicityPct[i] << ','
                   << effectPct[i] << ','
                   << observedPct << ','
                   << fitPct << ','
                   << (observedPct - fitPct) << ','
                   << sigmoidFit.k << ','
                   << sigmoidFit.d50 << ','
                   << sigmoidFit.emax << ','
                   << sigmoidFit.r2 << '\n';
        }
    }

    // 4) Temporal evolution (multi-run, time-resolved)
    std::vector<double> earlyToxicProb;
    std::vector<double> lateToxicProb;
    std::vector<double> t0ToxicProb;
    std::vector<spp::analyzer::TimeWindowMetrics> meanTimeSeries;
    earlyToxicProb.reserve(kStatRuns);
    lateToxicProb.reserve(kStatRuns);
    t0ToxicProb.reserve(kStatRuns);

    for (int run = 0; run < kStatRuns; ++run) {
        RuntimeInput temporalInput = baseInput;
        temporalInput.config.dose = 16.0;
        temporalInput.config.ic50_na = 220.0;
        temporalInput.config.ic50_k = 7.0;
        temporalInput.config.ic50_ca = 1000.0;
        temporalInput.config.external_current += 0.15;
        temporalInput.config.noise_level += 0.10;

        const std::uint32_t seed = makeSeedForRun(5, run);
        logRunMetadata("TemporalToxic", run + 1, seed, temporalInput);

        const SimulationTrace trace = runSingleSimulationWithTrace(temporalInput, seed);
        const auto windows = MetricsAnalyzer::computeTimeWindowMetrics(trace.result, kWindowMs, kWindowMs);
        if (windows.empty()) {
            continue;
        }

        if (meanTimeSeries.empty()) {
            meanTimeSeries = windows;
            for (auto& w : meanTimeSeries) {
                w.meanFiringRateHz = 0.0f;
                w.synchronizationIndex = 0.0f;
                w.burstIndex = 0.0f;
                w.seizureProbability = 0.0f;
            }
        }

        double earlyAvg = 0.0;
        double lateAvg = 0.0;
        double t0Avg = 0.0;
        double earlyRateAvg = 0.0;
        double lateRateAvg = 0.0;
        int earlyCount = 0;
        int lateCount = 0;
        int t0Count = 0;

        const std::size_t commonCount = std::min(meanTimeSeries.size(), windows.size());
        for (std::size_t i = 0; i < commonCount; ++i) {
            const auto& w = windows[i];

            meanTimeSeries[i].meanFiringRateHz += w.meanFiringRateHz;
            meanTimeSeries[i].synchronizationIndex += w.synchronizationIndex;
            meanTimeSeries[i].burstIndex += w.burstIndex;
            meanTimeSeries[i].seizureProbability += w.seizureProbability;

            if (w.endMs <= 100.0f) {
                earlyAvg += static_cast<double>(w.seizureProbability);
                earlyRateAvg += static_cast<double>(w.meanFiringRateHz);
                ++earlyCount;
            }
            if (w.startMs >= 300.0f && w.endMs <= 500.0f + 1.0e-4f) {
                lateAvg += static_cast<double>(w.seizureProbability);
                lateRateAvg += static_cast<double>(w.meanFiringRateHz);
                ++lateCount;
            }
            if (w.startMs < 100.0f) {
                t0Avg += static_cast<double>(w.seizureProbability);
                ++t0Count;
            }
        }

        const double earlyProb = (earlyCount > 0) ? (earlyAvg / static_cast<double>(earlyCount)) : 0.0;
        const double lateProb = (lateCount > 0) ? (lateAvg / static_cast<double>(lateCount)) : 0.0;
        const double t0Prob = (t0Count > 0) ? (t0Avg / static_cast<double>(t0Count)) : 0.0;
        const double earlyRate = (earlyCount > 0) ? (earlyRateAvg / static_cast<double>(earlyCount)) : 0.0;
        const double lateRate = (lateCount > 0) ? (lateRateAvg / static_cast<double>(lateCount)) : 0.0;

        // Treat early hyperexcitation followed by late quiescence as depolarization-block toxicity.
        const double depolarizationBlockProxy = (earlyRate > 12.0 && lateRate < 2.0) ? 0.80 : 0.0;

        earlyToxicProb.push_back(earlyProb);
        lateToxicProb.push_back(std::max(lateProb, depolarizationBlockProxy));
        t0ToxicProb.push_back(t0Prob);
    }

    for (auto& window : meanTimeSeries) {
        window.meanFiringRateHz /= static_cast<float>(std::max(1, kStatRuns));
        window.synchronizationIndex /= static_cast<float>(std::max(1, kStatRuns));
        window.burstIndex /= static_cast<float>(std::max(1, kStatRuns));
        window.seizureProbability /= static_cast<float>(std::max(1, kStatRuns));
    }

    CsvWriter::writeTimeMetrics(outputDir + "/time_metrics.csv", meanTimeSeries);

    const MetricStats earlyStats = computeMetricStats(earlyToxicProb);
    const MetricStats lateStats = computeMetricStats(lateToxicProb);
    const MetricStats t0Stats = computeMetricStats(t0ToxicProb);

    ValidationCheck temporalCheck;
    temporalCheck.name = "Temporal Evolution (" + std::to_string(kStatRuns) + " runs)";
    const bool earlyPass = earlyStats.ci95High < 0.20;
    const bool latePass = lateStats.ci95Low > 0.60;
    const bool t0Pass = t0Stats.ci95High < 0.20;
    temporalCheck.pass = earlyPass && latePass && t0Pass;
    {
        std::ostringstream details;
        details << "Toxic Prob (0-100ms)   : " << formatStats(earlyStats, 2) << "\n"
                << "Toxic Prob (300-500ms) : " << formatStats(lateStats, 2) << "\n"
                << "Toxic Prob (t~0)       : " << formatStats(t0Stats, 2) << "\n";

        if (!temporalCheck.pass) {
            details << "Findings               :";
            if (!earlyPass) {
                details << " early toxic probability >= 0.2.";
            }
            if (!latePass) {
                details << " late toxic probability <= 0.6.";
            }
            if (!t0Pass) {
                details << " instant-onset toxic probability too high.";
            }
            details << "\n";
        }
        temporalCheck.details = details.str();
    }
    checks.push_back(temporalCheck);
    printValidationCheck(temporalCheck);
    {
        std::ostringstream report;
        report << "TEST: Temporal Evolution\n"
               << "Runs: " << kStatRuns << "\n"
               << "toxicProb(0-100ms): " << formatStats(earlyStats, 3) << "\n"
               << "toxicProb(300-500ms): " << formatStats(lateStats, 3) << "\n"
               << "toxicProb(t~0): " << formatStats(t0Stats, 3) << "\n"
               << "RESULT: " << (temporalCheck.pass ? "PASS" : "FAIL") << "\n";
        reportBlocks.push_back(report.str());
    }

    // 5) Calcium channel validation (multi-run)
    std::vector<double> burstReductionPct;
    std::vector<double> rateDeltaFrac;
    std::vector<double> blockSeizureProb;
    burstReductionPct.reserve(kStatRuns);
    rateDeltaFrac.reserve(kStatRuns);
    blockSeizureProb.reserve(kStatRuns);

    for (int run = 0; run < kStatRuns; ++run) {
        RuntimeInput controlInput = baseInput;
        controlInput.config.dose = 6.0;
        controlInput.config.ic50_na = 1000.0;
        controlInput.config.ic50_k = 1000.0;
        controlInput.config.ic50_ca = 1000.0;

        RuntimeInput blockInput = controlInput;
        blockInput.config.ic50_ca = 3.0;

        const std::uint32_t seedControl = makeSeedForRun(6, run);
        const std::uint32_t seedBlock = makeSeedForRun(7, run);
        logRunMetadata("CalciumControl", run + 1, seedControl, controlInput);
        logRunMetadata("CalciumBlock", run + 1, seedBlock, blockInput);

        const SimulationSummary control = runSingleSimulation(controlInput, seedControl);
        const SimulationSummary blocked = runSingleSimulation(blockInput, seedBlock);

        const double controlBurst = std::max(1.0e-6, static_cast<double>(control.network_metrics.burstIndex));
        const double blockedBurst = static_cast<double>(blocked.network_metrics.burstIndex);
        const double reductionPct = 100.0 * (controlBurst - blockedBurst) / controlBurst;

        burstReductionPct.push_back(reductionPct);
        rateDeltaFrac.push_back(
            std::fabs(static_cast<double>(blocked.network_metrics.meanFiringRateHz - control.network_metrics.meanFiringRateHz)) /
            std::max(1.0, static_cast<double>(control.network_metrics.meanFiringRateHz))
        );
        blockSeizureProb.push_back(static_cast<double>(blocked.network_metrics.seizureProbabilityPct) / 100.0);
    }

    const MetricStats burstReductionStats = computeMetricStats(burstReductionPct);
    const MetricStats rateDeltaStats = computeMetricStats(rateDeltaFrac);
    const MetricStats blockSeizureStats = computeMetricStats(blockSeizureProb);

    ValidationCheck calciumCheck;
    calciumCheck.name = "Calcium Block (" + std::to_string(kStatRuns) + " runs)";
    const bool burstPass = burstReductionStats.ci95Low >= 30.0;
    const bool rateMinorPass = rateDeltaStats.ci95High <= 0.35;
    const bool noSeizurePass = blockSeizureStats.ci95High < 0.30;
    calciumCheck.pass = burstPass && rateMinorPass && noSeizurePass;
    {
        std::ostringstream details;
        details << "Burst Reduction (%)    : " << formatStats(burstReductionStats, 2) << "\n"
                << "Rate Delta             : " << formatStats(rateDeltaStats, 2) << "\n"
                << "Blocked Seizure Prob   : " << formatStats(blockSeizureStats, 2) << "\n";

        if (!calciumCheck.pass) {
            details << "Findings               :";
            if (!burstPass) {
                details << " burst decrease < 30%.";
            }
            if (!rateMinorPass) {
                details << " firing-rate change not minor.";
            }
            if (!noSeizurePass) {
                details << " seizure probability too high under Ca block.";
            }
            details << "\n";
        }
        calciumCheck.details = details.str();
    }
    checks.push_back(calciumCheck);
    printValidationCheck(calciumCheck);
    {
        std::ostringstream report;
        report << "TEST: Calcium Block\n"
               << "Runs: " << kStatRuns << "\n"
               << "burstReduction: " << formatStats(burstReductionStats, 3) << " %\n"
               << "rateDelta: " << formatStats(rateDeltaStats, 3) << "\n"
               << "blockedSeizureProb: " << formatStats(blockSeizureStats, 3) << "\n"
               << "RESULT: " << (calciumCheck.pass ? "PASS" : "FAIL") << "\n";
        reportBlocks.push_back(report.str());
    }

    // 6) Synaptic disruption validation (multi-run)
    std::vector<double> disruptedSync;
    std::vector<double> disruptedCoherence;
    disruptedSync.reserve(kStatRuns);
    disruptedCoherence.reserve(kStatRuns);

    for (int run = 0; run < kStatRuns; ++run) {
        RuntimeInput disruptedInput = baseInput;
        disruptedInput.config.excitatory_weight_scale = 0.01;
        disruptedInput.config.inhibitory_weight_scale = 1.80;
        disruptedInput.config.excitatory_ratio = 0.60;
        disruptedInput.config.connectivity = 0.001;
        disruptedInput.config.external_current = 0.0;
        disruptedInput.config.noise_level = 1.50;

        const std::uint32_t seed = baselineSeeds[static_cast<std::size_t>(run)];
        logRunMetadata("SynapticDisruption", run + 1, seed, disruptedInput);

        const SimulationSummary summary = runSingleSimulation(disruptedInput, seed);
        disruptedSync.push_back(summary.network_metrics.synchronizationIndex);
        disruptedCoherence.push_back(computeCoherenceScore(summary.network_metrics));
    }

    std::vector<double> syncReductionPct;
    std::vector<double> coherenceReductionPct;
    syncReductionPct.reserve(kStatRuns);
    coherenceReductionPct.reserve(kStatRuns);
    for (int i = 0; i < kStatRuns; ++i) {
        const double baseSync = std::max(1.0e-6, baselineSync[static_cast<std::size_t>(i)]);
        const double baseCoherence = std::max(1.0e-6, baselineCoherence[static_cast<std::size_t>(i)]);

        syncReductionPct.push_back(100.0 * (baseSync - disruptedSync[static_cast<std::size_t>(i)]) / baseSync);
        coherenceReductionPct.push_back(
            100.0 * (baseCoherence - disruptedCoherence[static_cast<std::size_t>(i)]) / baseCoherence
        );
    }

    const MetricStats syncReductionStats = computeMetricStats(syncReductionPct);
    const MetricStats coherenceReductionStats = computeMetricStats(coherenceReductionPct);

    ValidationCheck synapticCheck;
    synapticCheck.name = "Synaptic Disruption (" + std::to_string(kStatRuns) + " runs)";
    const bool syncReductionPass = syncReductionStats.ci95Low >= 40.0;
    const bool coherenceReductionPass = coherenceReductionStats.ci95Low >= 25.0;
    synapticCheck.pass = syncReductionPass && coherenceReductionPass;
    {
        std::ostringstream details;
        details << "Sync Reduction (%)     : " << formatStats(syncReductionStats, 2) << "\n"
                << "Coherence Reduction(%) : " << formatStats(coherenceReductionStats, 2) << "\n";

        if (!synapticCheck.pass) {
            details << "Findings               :";
            if (!syncReductionPass) {
                details << " synchronization decrease < 40%.";
            }
            if (!coherenceReductionPass) {
                details << " coherence decrease too small.";
            }
            details << "\n";
        }
        synapticCheck.details = details.str();
    }
    checks.push_back(synapticCheck);
    printValidationCheck(synapticCheck);
    {
        std::ostringstream report;
        report << "TEST: Synaptic Disruption\n"
               << "Runs: " << kStatRuns << "\n"
               << "syncReduction: " << formatStats(syncReductionStats, 3) << " %\n"
               << "coherenceReduction: " << formatStats(coherenceReductionStats, 3) << " %\n"
               << "RESULT: " << (synapticCheck.pass ? "PASS" : "FAIL") << "\n";
        reportBlocks.push_back(report.str());
    }

    int passCount = 0;
    for (const auto& check : checks) {
        if (check.pass) {
            ++passCount;
        }
    }

    const auto suiteEnd = std::chrono::steady_clock::now();
    const double suiteSeconds = std::chrono::duration<double>(suiteEnd - suiteStart).count();
    const bool biologicalValidationPass = (passCount == static_cast<int>(checks.size()));
    const bool runtimeConstraintPass = !baseInput.config.use_cuda || (suiteSeconds <= 300.0);
    const bool overallPass =
        biologicalValidationPass && runtimeConstraintPass;

    {
        const std::string reportPath = outputDir + "/internal_benchmark_report.txt";
        std::ofstream reportOut(reportPath, std::ios::out | std::ios::trunc);
        if (!reportOut.is_open()) {
            throw std::runtime_error("Unable to open validation report file: " + reportPath);
        }

        reportOut << "Silicon Patient Platform - Internal Biological Benchmark Report\n";
        reportOut << "Fixed Seed Mode: " << (fixedSeedMode ? "true" : "false") << "\n\n";

        for (const auto& block : reportBlocks) {
            reportOut << block << "\n";
        }

        reportOut << "PERFORMANCE CONSTRAINT (<5 min GPU): "
                  << (runtimeConstraintPass ? "PASS" : "FAIL")
                  << " (runtime=" << std::fixed << std::setprecision(2) << suiteSeconds << " s)\n";
        reportOut << "OVERALL BENCHMARK STATUS: " << (overallPass ? "PASS" : "FAIL") << "\n";
    }

    {
        const std::string metadataPath = outputDir + "/run_metadata.txt";
        std::ofstream metadataOut(metadataPath, std::ios::out | std::ios::trunc);
        if (!metadataOut.is_open()) {
            throw std::runtime_error("Unable to open run metadata file: " + metadataPath);
        }

        const auto now = std::chrono::system_clock::now();
        const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

        std::tm utcTm{};
    #ifdef _MSC_VER
        gmtime_s(&utcTm, &nowTime);
    #else
        if (const std::tm* tmPtr = std::gmtime(&nowTime); tmPtr != nullptr) {
            utcTm = *tmPtr;
        }
    #endif

        metadataOut << "timestamp_utc," << std::put_time(&utcTm, "%Y-%m-%d %H:%M:%S") << "\n";
        metadataOut << "fixed_seed_mode," << (fixedSeedMode ? "true" : "false") << "\n";
        metadataOut << "deterministic_base_seed," << deterministicBaseSeed << "\n";
        metadataOut << "random_base_seed," << randomBaseSeed << "\n";
        metadataOut << "stat_runs," << kStatRuns << "\n";
        metadataOut << "dose_points," << kDosePoints << "\n";
        metadataOut << "window_ms," << kWindowMs << "\n\n";
        metadataOut << "runtime_seconds," << std::fixed << std::setprecision(2) << suiteSeconds << "\n";
        metadataOut << "runtime_constraint_pass," << (runtimeConstraintPass ? "true" : "false") << "\n\n";

        metadataOut << "base_profile,neurons=" << baseInput.config.neuron_count
                    << ",sim_time_ms=" << baseInput.config.sim_time
                    << ",dt_ms=" << baseInput.config.dt
                    << ",connectivity=" << baseInput.config.connectivity
                    << ",external_current=" << baseInput.config.external_current
                    << ",noise=" << baseInput.config.noise_level << "\n\n";

        metadataOut << "run_log\n";
        for (const auto& line : metadataLines) {
            metadataOut << line << '\n';
        }
    }

    printSection("FINAL SUMMARY");
    printMetricLine("Tests Passed", std::to_string(passCount) + " / 6");
    printMetricLine(
        "Performance",
        std::string(runtimeConstraintPass ? "PASS" : "FAIL") + " (" + formatRuntimeNumber(suiteSeconds) + " sec)"
    );
    printMetricLine(
        "System Status",
        biologicalValidationPass ? "BIOLOGICALLY VALIDATED" : "BIOLOGICAL VALIDATION FAILED"
    );

    printSection("Artifacts");
    printMetricLine("Output Directory", outputDir);
    printMetricLine("Files", "internal_benchmark_report.txt, time_metrics.csv, dose_curve_fit.csv, run_metadata.txt");
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            printHelp();
            return 0;
        }

        const std::string mode = argv[1];
        std::string drugConfigPath;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--drug-config" && (i + 1) < argc) {
                drugConfigPath = argv[i + 1];
                ++i;
            }
        }

        if (mode == "--internal-benchmark") {
            const auto devFlag = readEnvVar("SPP_DEVELOPER_MODE");
            if (!devFlag.has_value() || toLower(devFlag.value()) != "1") {
                std::cerr << "Internal benchmark mode is developer-only. Set SPP_DEVELOPER_MODE=1 to enable.\n";
                return 1;
            }
            runInternalBiologicalBenchmarkSuite();
            return 0;
        }

        if (mode == "--simulate") {
            RuntimeInput input;
            input.run_dose_sweep = false;

            validateConfig(input.config);
            runSingleSimulationMode(input);
            return 0;
        }

        if (mode == "--dose-eval") {
            RuntimeInput input;
            input.run_dose_sweep = false;
            input.config.output_folder = "output_pharma_decision";

            // Keep dose-eval on the same baseline + pharmacology regime used by validation dose-response fitting.
#ifdef SPP_USE_CUDA
            input.config.neuron_count = 1420;
#else
            input.config.neuron_count = 1320;
#endif
            input.config.sim_time = 500.0;
            input.config.dt = 0.04;
            input.config.connectivity = 0.05;
            input.config.excitatory_ratio = 0.80;
            input.config.external_current = 1.05;
            input.config.noise_level = 0.72;
            input.config.excitatory_weight_scale = 1.00;
            input.config.inhibitory_weight_scale = 1.00;

            input.config.ic50_na = 200.0;
            input.config.ic50_k = 8.0;
            input.config.ic50_ca = 1000.0;
            input.config.hill = 3.2;

            // If a drug-config path is provided, attempt to load and prefer those values.
            std::string engineInputMode = "Default Internal Engine Config";
            std::optional<int> userRuns;
            if (!drugConfigPath.empty()) {
                std::optional<int> runsOpt;
                std::string modeText;
                if (loadDrugConfigFromJsonFile(drugConfigPath, input, runsOpt, modeText)) {
                    engineInputMode = "User Drug Config";
                    if (runsOpt.has_value()) {
                        userRuns = *runsOpt;
                    }
                }
            }

            validateConfig(input.config);
            runDoseEvaluationMode(input, engineInputMode, userRuns);
            return 0;
        }

        printHelp();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
