// This file defines the MetricsAnalyzer class, which computes various metrics for individual neurons and the overall network based on the results of a neural simulation. The metrics include firing rates, inter-spike interval statistics, synchronization indices, burst indices, and other measures of neural activity and network dynamics. The class also provides functionality to compute metrics over specific time windows to analyze temporal changes in network behavior. These metrics can be used for further analysis, such as seizure detection or drug response evaluation.
#include "Metrics.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <numeric>
#include <random>

namespace spp::analyzer {

namespace {

constexpr float kSyncActiveBinThresholdMin = 0.002f;
constexpr float kSyncSustainedActiveFraction = 0.015f;
constexpr float kSyncSensitivityGain = 9.0f;

float computeSeizureProbabilityPctFromMarkers(float rateHz, float synchronization, float burstIndex) {
    const float rateNorm = std::clamp(rateHz / 50.0f, 0.0f, 1.0f);
    const float syncNorm = std::clamp(synchronization, 0.0f, 1.0f);
    const float burstNorm = std::clamp(burstIndex, 0.0f, 1.0f);

    constexpr float kRateWeight = 0.25f;
    constexpr float kSyncWeight = 0.85f;
    constexpr float kBurstWeight = 0.75f;
    constexpr float kSeizureThreshold = 0.61f;
    constexpr float kSigmoidGain = 12.0f;

    const float seizureScore =
        kRateWeight * rateNorm +
        kSyncWeight * syncNorm +
        kBurstWeight * burstNorm;

    const float logits = kSigmoidGain * (seizureScore - kSeizureThreshold);
    const float safeLogits = std::clamp(logits, -60.0f, 60.0f);
    return 100.0f / (1.0f + std::exp(-safeLogits));
}

} // namespace

std::vector<NeuronMetrics> MetricsAnalyzer::computeNeuronMetrics(const simulation::SimulationResult& result) {
    std::vector<NeuronMetrics> metrics(result.spikeTimes.size());

    const float durationSec = std::max(1.0e-6f, result.durationMs / 1000.0f);

    for (std::size_t i = 0; i < result.spikeTimes.size(); ++i) {
        const std::vector<float>& spikes = result.spikeTimes[i];

        NeuronMetrics m;
        m.spikeCount = spikes.size();
        m.firingRateHz = static_cast<float>(m.spikeCount) / durationSec;

        if (spikes.size() >= 2U) {
            std::vector<float> isi;
            isi.reserve(spikes.size() - 1U);

            for (std::size_t j = 1; j < spikes.size(); ++j) {
                isi.push_back(spikes[j] - spikes[j - 1]);
            }

            const float mean = std::accumulate(isi.begin(), isi.end(), 0.0f) / static_cast<float>(isi.size());
            float var = 0.0f;
            for (float x : isi) {
                const float d = x - mean;
                var += d * d;
            }
            var /= static_cast<float>(isi.size());

            m.isiMeanMs = mean;
            m.isiVarianceMs = var;
        }

        metrics[i] = m;
    }

    return metrics;
}

NetworkMetrics MetricsAnalyzer::computeNetworkMetrics(
    const simulation::SimulationResult& result,
    const std::vector<NeuronMetrics>& neuronMetrics
) {
    NetworkMetrics net;

    if (neuronMetrics.empty()) {
        return net;
    }

    const std::size_t neuronCountU = neuronMetrics.size();
    const float neuronCount = static_cast<float>(neuronCountU);

    std::vector<float> firingRates;
    firingRates.reserve(neuronCountU);

    std::size_t suppressedCount = 0U;
    std::size_t irregularCount = 0U;
    float irregularitySum = 0.0f;

    for (const NeuronMetrics& m : neuronMetrics) {
        const float rate = (std::isfinite(m.firingRateHz) && m.firingRateHz > 0.0f) ? m.firingRateHz : 0.0f;
        firingRates.push_back(rate);

        // Suppression = neurons below 0.5 Hz.
        if (rate < 0.5f) {
            ++suppressedCount;
        }

        if (m.isiMeanMs > 1.0e-3f && m.isiVarianceMs > 0.0f) {
            const float cv = std::sqrt(m.isiVarianceMs) / m.isiMeanMs;
            if (std::isfinite(cv)) {
                irregularitySum += std::clamp(cv, 0.0f, 5.0f);
                ++irregularCount;
            }
        }
    }

    if (!firingRates.empty()) {
        // Use trimmed mean to reduce outlier-driven inflation from a small subgroup.
        std::vector<float> sortedRates = firingRates;
        std::sort(sortedRates.begin(), sortedRates.end());

        const std::size_t trimCount = static_cast<std::size_t>(0.10f * static_cast<float>(sortedRates.size()));
        std::size_t begin = trimCount;
        std::size_t end = sortedRates.size() - trimCount;
        if (begin >= end) {
            begin = 0U;
            end = sortedRates.size();
        }

        const float trimmedSum = std::accumulate(sortedRates.begin() + static_cast<std::ptrdiff_t>(begin),
                                                 sortedRates.begin() + static_cast<std::ptrdiff_t>(end),
                                                 0.0f);
        net.meanFiringRateHz = trimmedSum / static_cast<float>(end - begin);
    }

    net.irregularityIndex = (irregularCount > 0U)
                               ? irregularitySum / static_cast<float>(irregularCount)
                               : 0.0f;

    float variance = 0.0f;
    for (float rate : firingRates) {
        const float d = rate - net.meanFiringRateHz;
        variance += d * d;
    }
    net.populationVariance = variance / neuronCount;

    if (!result.finalVoltages.empty()) {
        float vMean = 0.0f;
        float finiteCount = 0.0f;
        for (float v : result.finalVoltages) {
            if (std::isfinite(v)) {
                vMean += v;
                finiteCount += 1.0f;
            }
        }

        if (finiteCount > 0.0f) {
            vMean /= finiteCount;
            float vVar = 0.0f;
            for (float v : result.finalVoltages) {
                if (!std::isfinite(v)) {
                    continue;
                }
                const float dv = v - vMean;
                vVar += dv * dv;
            }
            net.voltageVariance = vVar / finiteCount;
        }
    }

    // Synchronization: pairwise coincidence above chance, gated by sustained activity.
    const std::size_t stepCount = result.populationSpikesPerStep.size();
    if (stepCount > 0U && result.dtMs > 0.0f) {
        double meanSpikeFraction = 0.0;
        double meanSquaredSpikeFraction = 0.0;
        std::size_t activeBins = 0U;
        const float activeBinThreshold = std::max(kSyncActiveBinThresholdMin, 1.0f / neuronCount);

        for (std::size_t step = 0; step < stepCount; ++step) {
            const float spikeFraction = std::clamp(
                static_cast<float>(result.populationSpikesPerStep[step]) / neuronCount,
                0.0f,
                1.0f
            );
            meanSpikeFraction += static_cast<double>(spikeFraction);
            meanSquaredSpikeFraction += static_cast<double>(spikeFraction) * static_cast<double>(spikeFraction);
            if (spikeFraction >= activeBinThreshold) {
                ++activeBins;
            }
        }

        const double binCount = static_cast<double>(stepCount);
        const double pMean = meanSpikeFraction / binCount;
        const double p2Mean = meanSquaredSpikeFraction / binCount;
        const double chanceCoincidence = pMean * pMean;
        const double excessCoincidence = std::max(0.0, p2Mean - chanceCoincidence);
        const double coincidenceDenom = std::max(1.0e-6, pMean * (1.0 - pMean));
        const double coincidenceIndex = std::clamp(excessCoincidence / coincidenceDenom, 0.0, 1.0);

        const float activeFraction = static_cast<float>(activeBins) / static_cast<float>(stepCount);
        const float sustainedFactor = std::clamp(activeFraction / kSyncSustainedActiveFraction, 0.0f, 1.0f);
        net.synchronizationIndex = std::clamp(
            static_cast<float>(coincidenceIndex) * sustainedFactor * kSyncSensitivityGain,
            0.0f,
            1.0f
        );
    }

    // Burst index: burst event if >=3 spikes in 10 ms; index = bursts / total spikes.
    std::uint64_t totalSpikes = 0U;
    std::uint64_t burstEvents = 0U;
    for (const auto& spikes : result.spikeTimes) {
        totalSpikes += static_cast<std::uint64_t>(spikes.size());

        if (spikes.size() < 3U) {
            continue;
        }

        std::size_t j = 0U;
        while (j + 2U < spikes.size()) {
            if ((spikes[j + 2U] - spikes[j]) <= 10.0f) {
                ++burstEvents;

                std::size_t end = j + 3U;
                while (end < spikes.size() && (spikes[end] - spikes[end - 1U]) <= 10.0f) {
                    ++end;
                }
                j = end;
            } else {
                ++j;
            }
        }
    }
    net.burstIndex = (totalSpikes > 0U)
                         ? static_cast<float>(burstEvents) / static_cast<float>(totalSpikes)
                         : 0.0f;

    const std::size_t mid = result.populationSpikesPerStep.size() / 2U;
    std::uint64_t earlySpikes = 0U;
    std::uint64_t lateSpikes = 0U;

    for (std::size_t i = 0; i < result.populationSpikesPerStep.size(); ++i) {
        if (i < mid) {
            earlySpikes += result.populationSpikesPerStep[i];
        } else {
            lateSpikes += result.populationSpikesPerStep[i];
        }
    }

    const float halfDurationSec = std::max(1.0e-6f, (result.durationMs * 0.5f) / 1000.0f);
    net.earlyWindowRateHz = static_cast<float>(earlySpikes) / (neuronCount * halfDurationSec);
    net.lateWindowRateHz = static_cast<float>(lateSpikes) / (neuronCount * halfDurationSec);

    net.suppressionPct = (100.0f * static_cast<float>(suppressedCount)) / neuronCount;

    net.nii = computeNII(
        net.populationVariance,
        net.voltageVariance,
        net.irregularityIndex,
        net.synchronizationIndex,
        net.burstIndex
    );
    net.stabilityScore = std::clamp(1.0f - net.nii, 0.0f, 1.0f);

    // Seizure probability: sigmoid(weighted normalized physiological markers).
    net.seizureProbabilityPct = computeSeizureProbabilityPctFromMarkers(
        net.meanFiringRateHz,
        net.synchronizationIndex,
        net.burstIndex
    );

    auto sanitizeMetric = [](float value, float fallback) {
        return std::isfinite(value) ? value : fallback;
    };

    net.meanFiringRateHz = sanitizeMetric(net.meanFiringRateHz, 0.0f);
    net.synchronizationIndex = std::clamp(sanitizeMetric(net.synchronizationIndex, 0.0f), 0.0f, 1.0f);
    net.burstIndex = std::clamp(sanitizeMetric(net.burstIndex, 0.0f), 0.0f, 1.0f);
    net.populationVariance = sanitizeMetric(net.populationVariance, 0.0f);
    net.voltageVariance = sanitizeMetric(net.voltageVariance, 0.0f);
    net.irregularityIndex = sanitizeMetric(net.irregularityIndex, 0.0f);
    net.earlyWindowRateHz = sanitizeMetric(net.earlyWindowRateHz, 0.0f);
    net.lateWindowRateHz = sanitizeMetric(net.lateWindowRateHz, 0.0f);
    net.suppressionPct = std::clamp(sanitizeMetric(net.suppressionPct, 0.0f), 0.0f, 100.0f);
    net.seizureProbabilityPct = std::clamp(sanitizeMetric(net.seizureProbabilityPct, 0.0f), 0.0f, 100.0f);
    net.stabilityScore = std::clamp(sanitizeMetric(net.stabilityScore, 1.0f), 0.0f, 1.0f);
    net.nii = std::clamp(sanitizeMetric(net.nii, 0.0f), 0.0f, 1.0f);

    return net;
}

std::vector<TimeWindowMetrics> MetricsAnalyzer::computeTimeWindowMetrics(
    const simulation::SimulationResult& result,
    float windowMs,
    float stepMs
) {
    std::vector<TimeWindowMetrics> windows;

    if (result.durationMs <= 0.0f || result.dtMs <= 0.0f) {
        return windows;
    }

    const float safeWindowMs = std::max(windowMs, result.dtMs);
    const float safeStepMs = std::max(stepMs, result.dtMs);
    const std::size_t neuronCount = std::max<std::size_t>(1U, result.spikeTimes.size());
    const std::size_t stepCount = result.populationSpikesPerStep.size();

    for (float startMs = 0.0f; startMs + safeWindowMs <= result.durationMs + 1.0e-4f; startMs += safeStepMs) {
        const float endMs = std::min(result.durationMs, startMs + safeWindowMs);
        if (endMs <= startMs) {
            continue;
        }

        const std::size_t startStep = std::min(
            stepCount,
            static_cast<std::size_t>(std::floor(startMs / result.dtMs))
        );
        const std::size_t endStep = std::min(
            stepCount,
            static_cast<std::size_t>(std::ceil(endMs / result.dtMs))
        );

        if (endStep <= startStep) {
            continue;
        }

        std::uint64_t spikeSum = 0U;
        std::vector<float> stepSpikeFractions;
        stepSpikeFractions.reserve(endStep - startStep);
        for (std::size_t step = startStep; step < endStep; ++step) {
            const std::uint64_t spikes = static_cast<std::uint64_t>(result.populationSpikesPerStep[step]);
            spikeSum += spikes;
            stepSpikeFractions.push_back(static_cast<float>(spikes) / static_cast<float>(neuronCount));
        }

        const float durationSec = std::max(1.0e-6f, static_cast<float>(endStep - startStep) * result.dtMs / 1000.0f);
        const float meanRateHz = static_cast<float>(spikeSum) / (static_cast<float>(neuronCount) * durationSec);

        float sync = 0.0f;
        if (!stepSpikeFractions.empty()) {
            double meanFrac = 0.0;
            double meanFracSq = 0.0;
            std::size_t activeBins = 0U;
            const float activeBinThreshold = std::max(
                kSyncActiveBinThresholdMin,
                1.0f / static_cast<float>(neuronCount)
            );

            for (float frac : stepSpikeFractions) {
                const float clamped = std::clamp(frac, 0.0f, 1.0f);
                meanFrac += clamped;
                meanFracSq += static_cast<double>(clamped) * static_cast<double>(clamped);
                if (clamped >= activeBinThreshold) {
                    ++activeBins;
                }
            }

            const double invN = 1.0 / static_cast<double>(stepSpikeFractions.size());
            const double pMean = meanFrac * invN;
            const double p2Mean = meanFracSq * invN;
            const double chanceCoincidence = pMean * pMean;
            const double excessCoincidence = std::max(0.0, p2Mean - chanceCoincidence);
            const double denom = std::max(1.0e-6, pMean * (1.0 - pMean));
            const float coincidenceIndex = static_cast<float>(std::clamp(excessCoincidence / denom, 0.0, 1.0));

            const float activeFraction = static_cast<float>(activeBins) / static_cast<float>(stepSpikeFractions.size());
            const float sustainedFactor = std::clamp(activeFraction / kSyncSustainedActiveFraction, 0.0f, 1.0f);
            sync = std::clamp(coincidenceIndex * sustainedFactor * kSyncSensitivityGain, 0.0f, 1.0f);
        }

        std::uint64_t totalWindowSpikes = 0U;
        std::uint64_t burstEvents = 0U;
        for (const auto& spikes : result.spikeTimes) {
            const auto begin = std::lower_bound(spikes.begin(), spikes.end(), startMs);
            const auto end = std::lower_bound(spikes.begin(), spikes.end(), endMs);
            const std::size_t count = static_cast<std::size_t>(std::distance(begin, end));

            totalWindowSpikes += static_cast<std::uint64_t>(count);
            if (count < 3U) {
                continue;
            }

            std::size_t j = 0U;
            while (j + 2U < count) {
                if ((*(begin + static_cast<std::ptrdiff_t>(j + 2U)) - *(begin + static_cast<std::ptrdiff_t>(j))) <= 10.0f) {
                    ++burstEvents;

                    std::size_t k = j + 3U;
                    while (k < count &&
                           (*(begin + static_cast<std::ptrdiff_t>(k)) -
                            *(begin + static_cast<std::ptrdiff_t>(k - 1U))) <= 10.0f) {
                        ++k;
                    }
                    j = k;
                } else {
                    ++j;
                }
            }
        }

        const float burst = (totalWindowSpikes > 0U)
                                ? static_cast<float>(burstEvents) / static_cast<float>(totalWindowSpikes)
                                : 0.0f;

        TimeWindowMetrics metrics;
        metrics.startMs = startMs;
        metrics.endMs = endMs;
        metrics.meanFiringRateHz = std::isfinite(meanRateHz) ? std::max(0.0f, meanRateHz) : 0.0f;
        metrics.synchronizationIndex = std::clamp(std::isfinite(sync) ? sync : 0.0f, 0.0f, 1.0f);
        metrics.burstIndex = std::clamp(std::isfinite(burst) ? burst : 0.0f, 0.0f, 1.0f);

        const float seizureProbPct = computeSeizureProbabilityPctFromMarkers(
            metrics.meanFiringRateHz,
            metrics.synchronizationIndex,
            metrics.burstIndex
        );
        metrics.seizureProbability = std::clamp(seizureProbPct / 100.0f, 0.0f, 1.0f);

        windows.push_back(metrics);
    }

    return windows;
}

float MetricsAnalyzer::computeNII(
    float populationVariance,
    float voltageVariance,
    float irregularityIndex,
    float synchronization,
    float burstIndex
) {
    const float spikeDeviationNorm = std::clamp(populationVariance / 300.0f, 0.0f, 1.0f);
    const float voltageVarNorm = std::clamp(voltageVariance / 250.0f, 0.0f, 1.0f);
    const float irregularityNorm = std::clamp(irregularityIndex / 1.5f, 0.0f, 1.0f);
    const float syncNorm = std::clamp(synchronization, 0.0f, 1.0f);
    const float burstNorm = std::clamp(burstIndex / 0.35f, 0.0f, 1.0f);

    const float nii =
        0.25f * spikeDeviationNorm +
        0.25f * voltageVarNorm +
        0.20f * irregularityNorm +
        0.20f * syncNorm +
        0.10f * burstNorm;
    return std::clamp(nii, 0.0f, 1.0f);
}

} // namespace spp::analyzer
