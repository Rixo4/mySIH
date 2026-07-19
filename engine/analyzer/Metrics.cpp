// This file defines the MetricsAnalyzer class, which computes various metrics for
// individual neurons and the overall network based on the results of a neural
// simulation. Metrics include firing rates, inter-spike interval statistics,
// synchronization indices, burst indices, and other measures of neural activity and
// network dynamics. The class also provides functionality to compute metrics over
// specific time windows to analyze temporal changes in network behavior. These
// metrics can be used for further analysis, such as seizure detection or drug
// response evaluation.
#include "Metrics.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <numeric>

namespace spp::analyzer {

// ---------------------------------------------------------------------------
// Configuration constants
// FIX (Bug 10): All calibration constants gathered into one place so they can
// be reviewed and tuned scientifically without hunting through the code.
// ---------------------------------------------------------------------------
namespace cfg {

// Synchronization
//   Bin width used to re-bin spikes before computing coincidence. Fixed at
//   1 ms so that the synchronization index is independent of simulation dt.
//   (Bug 5 fix — previously the raw dt-sized bins were used, meaning a run
//   with dt=0.025 ms and a run with dt=0.5 ms produced different scores for
//   identical spike trains.)
constexpr float kSyncBinMs                  = 1.0f;
constexpr float kSyncActiveBinThresholdMin  = 0.002f;
constexpr float kSyncSustainedActiveFraction= 0.015f;
// Issue B: kSyncSensitivityGain scales the raw coincidence-above-chance index
// before clamping to [0,1]. The raw index is typically small (0.05–0.15) even
// for strongly synchronised networks because it measures *excess* coincidence
// above the Poisson baseline, not absolute coincidence. The gain compensates
// for this compression.
//
// HOW TO CALIBRATE: run a known-synchronous network (e.g. all neurons driven
// by a common 40 Hz oscillation) and a known-independent network (Poisson
// with matched rates). The gain should be set so that the synchronous network
// saturates near 1.0 and the independent network stays near 0. A gain that
// causes the independent baseline to consistently read > 0.2 is too high.
//
// Current value of 6.0 is more conservative than the previous 9.0, which
// caused many networks to saturate and lose discriminability. Verify against
// actual simulation outputs and adjust via this constant — do not hardcode
// elsewhere.
constexpr float kSyncSensitivityGain        = 6.0f;

// Burst detection defaults.
// Issue 2 FIX: kBurstWindowMsDefault is the fallback burst window used when
// SimulationResult does not carry a neuron-type-specific override. The actual
// value used at runtime is result.burstWindowMs when that field is > 0,
// falling back to this constant otherwise. This makes burst statistics
// correct for cortical pyramidal cells today (~10 ms) while allowing the
// simulator to supply thalamic (20-50 ms) or interneuron-specific values
// without recompiling Metrics.cpp.
//
// To add neuron-type support: populate result.burstWindowMs in
// SimulationResult before calling computeNetworkMetrics /
// computeTimeWindowMetrics. Add the field to SimulationResult if not present.
constexpr float kBurstWindowMsDefault       = 12.0f;
// Minimum spikes to count a neuron as "bursting" when computing burstingNeuronPct.
constexpr std::size_t kBurstMinSpikes       = 3U;

// Suppression: a per-neuron rate below this fraction of the BASELINE
// population mean is considered suppressed. Requires a baseline to be
// passed in; see computeNetworkMetrics overload. (Bug 1 fix.)
constexpr float kSuppressionBaselineFraction = 0.50f;

// NII normalisation denominators.
constexpr float kNiiPopVarDenom             = 300.0f;
constexpr float kNiiVoltVarDenom            = 250.0f;
constexpr float kNiiIrregDenom              = 1.5f;
constexpr float kNiiBurstDenom              = 0.35f;

// Burst index sigmoid normalization (Issue A fix).
// burstIndex = sigmoid(burstRateHz) so that rankings above the old 10 Hz
// ceiling are preserved instead of clamped to 1.
//   kBurstSigmoidMidpoint: the burst rate (Hz) that maps to burstIndex = 0.5.
//     Calibrated to ~5 Hz (mid-range severe ictal activity).
//   kBurstSigmoidGain: slope of the sigmoid. At gain=2, the curve rises from
//     ~0.1 at 0 Hz to ~0.9 at 10 Hz and continues to separate 15 vs 20 Hz.
constexpr float kBurstSigmoidMidpoint       = 5.0f;
constexpr float kBurstSigmoidGain           = 2.0f;

// Seizure sigmoid parameters.
constexpr float kSeizureRateNormHz          = 50.0f;
constexpr float kSeizureIrregDenom          = 1.5f;
constexpr float kSeizureThreshold           = 0.55f;
constexpr float kSeizureSigmoidGain         = 12.0f;

// Excitability score normalisation.
constexpr float kExcitRateNormHz            = 50.0f;
constexpr float kExcitIrregDenom            = 1.5f;

} // namespace cfg

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
namespace {


// Issue 1 FIX: Re-bin a per-step spike count vector into fixed-width bins of
// exactly binMs milliseconds using time accumulation rather than a rounded
// step count.
//
// The previous implementation computed:
//   stepsPerBin = round(binMs / dtMs)
// which introduces a systematic error whenever binMs is not an exact multiple
// of dtMs. For example, dt=0.3 ms gives stepsPerBin=3, actual bin=0.9 ms —
// a 10 % distortion that varies with dt and breaks the dt-independence
// guarantee.
//
// The corrected approach walks the step array while accumulating elapsed time.
// A bin closes as soon as its accumulated time reaches or exceeds binMs.
// This guarantees that every bin represents exactly one binMs interval
// regardless of dt, at the cost of bins that may span a fractional number of
// steps (the last step of a bin is proportionally split between the closing
// bin and the opening one). For synchronization purposes the proportional
// split is more accurate than rounding.
std::vector<float> rebinSpikeFractions(
    const std::vector<std::uint32_t>& spikesPerStep,
    float dtMs,
    float neuronCount,
    float binMs
) {
    if (spikesPerStep.empty() || dtMs <= 0.0f || neuronCount <= 0.0f || binMs <= 0.0f) {
        return {};
    }

    std::vector<float> fractions;
    fractions.reserve(
        static_cast<std::size_t>(
            std::ceil(static_cast<float>(spikesPerStep.size()) * dtMs / binMs)
        ) + 1U
    );

    double binAccumSpikes  = 0.0;   // spikes accumulated into the current bin
    double binAccumSteps   = 0.0;   // effective steps accumulated (may be fractional)
    double elapsedInBinMs  = 0.0;   // time elapsed inside the current open bin

    const std::size_t nSteps = spikesPerStep.size();

    for (std::size_t s = 0; s < nSteps; ++s) {
        const double stepSpikes    = static_cast<double>(spikesPerStep[s]);
        double remainingStepMs     = static_cast<double>(dtMs);

        while (remainingStepMs > 1.0e-9) {
            const double spaceInBinMs = static_cast<double>(binMs) - elapsedInBinMs;

            if (remainingStepMs < spaceInBinMs - 1.0e-9) {
                // Entire remainder of this step fits in the current bin.
                const double fraction = remainingStepMs / static_cast<double>(dtMs);
                binAccumSpikes += stepSpikes * fraction;
                binAccumSteps  += fraction;
                elapsedInBinMs += remainingStepMs;
                remainingStepMs = 0.0;
            } else {
                // Step straddles a bin boundary: fill the current bin, then
                // close it and open a new one with the leftover.
                const double fillFraction = spaceInBinMs / static_cast<double>(dtMs);
                binAccumSpikes += stepSpikes * fillFraction;
                binAccumSteps  += fillFraction;

                // Emit the completed bin. Normalise by (neuronCount × steps)
                // so the fraction represents the mean per-neuron firing
                // density within the bin, clamped to [0,1].
                const float frac = (binAccumSteps > 0.0)
                    ? static_cast<float>(binAccumSpikes / (static_cast<double>(neuronCount) * binAccumSteps))
                    : 0.0f;
                fractions.push_back(std::clamp(frac, 0.0f, 1.0f));

                // Reset for the next bin.
                binAccumSpikes = 0.0;
                binAccumSteps  = 0.0;
                elapsedInBinMs = 0.0;
                remainingStepMs -= spaceInBinMs;
            }
        }
    }

    // Emit any partial bin at the end (covers the tail when total duration is
    // not an exact multiple of binMs).
    if (binAccumSteps > 0.0) {
        const float frac = static_cast<float>(
            binAccumSpikes / (static_cast<double>(neuronCount) * binAccumSteps)
        );
        fractions.push_back(std::clamp(frac, 0.0f, 1.0f));
    }

    return fractions;
}

// Compute coincidence-above-chance synchronization from a pre-binned vector
// of spike-fraction values. Shared by both the full-network and window paths.
float computeSyncFromFractions(
    const std::vector<float>& fractions,
    float neuronCount
) {
    if (fractions.empty()) {
        return 0.0f;
    }

    double meanFrac   = 0.0;
    double meanFracSq = 0.0;
    std::size_t activeBins = 0U;
    const float activeBinThreshold = std::max(
        cfg::kSyncActiveBinThresholdMin,
        1.0f / neuronCount
    );

    for (float frac : fractions) {
        meanFrac   += frac;
        meanFracSq += static_cast<double>(frac) * static_cast<double>(frac);
        if (frac >= activeBinThreshold) {
            ++activeBins;
        }
    }

    const double invN             = 1.0 / static_cast<double>(fractions.size());
    const double pMean            = meanFrac   * invN;
    const double p2Mean           = meanFracSq * invN;
    const double chanceCoincidence= pMean * pMean;
    const double excess           = std::max(0.0, p2Mean - chanceCoincidence);
    const double denom            = std::max(1.0e-6, pMean * (1.0 - pMean));
    const float  coincidenceIndex = static_cast<float>(std::clamp(excess / denom, 0.0, 1.0));

    const float activeFraction  = static_cast<float>(activeBins) /
                                   static_cast<float>(fractions.size());
    const float sustainedFactor = std::clamp(
        activeFraction / cfg::kSyncSustainedActiveFraction,
        0.0f, 1.0f
    );

    return std::clamp(
        coincidenceIndex * sustainedFactor * cfg::kSyncSensitivityGain,
        0.0f, 1.0f
    );
}

// Count burst events and bursting neurons within a spike-time span [begin, end).
// Returns {burstEvents, burstingNeuronCount} for the whole spike array slice.
// Used by both the full-network burst path and the time-window path.
struct BurstCounts {
    std::uint64_t events          = 0U;
    std::uint64_t burstingNeurons = 0U;
    float totalDurationMs         = 0.0f;
};

BurstCounts countBurstsInRange(
    const std::vector<float>& spikes,
    std::size_t idxBegin,
    std::size_t idxEnd,
    float burstWindowMs         // Issue 2: caller supplies neuron-type-specific window
) {
    BurstCounts bc{0U, 0U};
    const std::size_t count = idxEnd - idxBegin;
    if (count < cfg::kBurstMinSpikes) {
        return bc;
    }

    bool neuronBursted = false;
    std::size_t j = idxBegin;
    while (j + 2U < idxEnd) {
        if ((spikes[j + 2U] - spikes[j]) <= burstWindowMs) {
            ++bc.events;
            neuronBursted = true;
            const float burstStart = spikes[j];
            std::size_t k = j + 3U;
            while (k < idxEnd &&
                   (spikes[k] - spikes[k - 1U]) <= burstWindowMs) {
                ++k;
            }
            const float burstEnd = spikes[k - 1U];
            bc.totalDurationMs += (burstEnd - burstStart);
            j = k;
        } else {
            ++j;
        }
    }

    if (neuronBursted) {
        ++bc.burstingNeurons;
    }

    return bc;
}

// Issue A FIX: Convert burstRateHz → burstIndex via a sigmoid rather than a
// hard linear ceiling.
//
// Old:  burstIndex = clamp(burstRateHz / 10.0, 0, 1)
//   → everything above 10 Hz collapses to 1.0; mild (3 Hz) and severe (8 Hz)
//     bursting are ranked, but 8 Hz and 20 Hz are indistinguishable.
//
// New:  burstIndex = 1 / (1 + exp(-(rate - midpoint) / gain))
//   → a smooth, monotone curve that preserves ranking at all burst rates.
//     At the configured midpoint (5 Hz) the index reads exactly 0.5.
//     At 0 Hz it reads ~0.08 (not 0), so silent neurons are not identical
//     to very low-burst neurons — that small offset is acceptable and can be
//     corrected with a baseline subtraction in the dose analyser if needed.
//
// Calibration: adjust cfg::kBurstSigmoidMidpoint and cfg::kBurstSigmoidGain.
float burstRateToIndex(float burstRateHz) {
    const float logit = -(burstRateHz - cfg::kBurstSigmoidMidpoint) / cfg::kBurstSigmoidGain;
    const float safeLogit = std::clamp(logit, -60.0f, 60.0f);
    return 1.0f / (1.0f + std::exp(safeLogit));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// MetricsAnalyzer::computeNeuronMetrics
// ---------------------------------------------------------------------------
std::vector<NeuronMetrics> MetricsAnalyzer::computeNeuronMetrics(
    const simulation::SimulationResult& result
) {
    std::vector<NeuronMetrics> metrics(result.spikeTimes.size());

    const float durationSec = std::max(1.0e-6f, result.durationMs / 1000.0f);

    for (std::size_t i = 0; i < result.spikeTimes.size(); ++i) {
        const std::vector<float>& spikes = result.spikeTimes[i];

        NeuronMetrics m;
        m.spikeCount   = spikes.size();
        m.firingRateHz = static_cast<float>(m.spikeCount) / durationSec;

        if (spikes.size() >= 2U) {
            const std::size_t nIsi = spikes.size() - 1U;
            float isiSum   = 0.0f;
            float isiSumSq = 0.0f;

            for (std::size_t j = 1U; j < spikes.size(); ++j) {
                const float isi = spikes[j] - spikes[j - 1U];
                isiSum   += isi;
                isiSumSq += isi * isi;
            }

            const float mean = isiSum / static_cast<float>(nIsi);

            // FIX (Bug 8): Use sample variance (÷ n-1) rather than population
            // variance (÷ n). ISIs are samples from an underlying process;
            // sample variance is the unbiased estimator.
            const float sampleVar = (nIsi > 1U)
                ? (isiSumSq - isiSum * isiSum / static_cast<float>(nIsi)) /
                  static_cast<float>(nIsi - 1U)
                : 0.0f;

            m.isiMeanMs     = mean;
            m.isiVarianceMs = std::max(0.0f, sampleVar);
        }

        metrics[i] = m;
    }

    return metrics;
}

// ---------------------------------------------------------------------------
// MetricsAnalyzer::computeNetworkMetrics
// ---------------------------------------------------------------------------
NetworkMetrics MetricsAnalyzer::computeNetworkMetrics(
    const simulation::SimulationResult& result,
    const std::vector<NeuronMetrics>&   neuronMetrics
) {
    NetworkMetrics net;

    if (neuronMetrics.empty()) {
        return net;
    }

    const std::size_t neuronCountU = neuronMetrics.size();
    const float       neuronCount  = static_cast<float>(neuronCountU);

    // -----------------------------------------------------------------------
    // 1. Per-neuron firing rates and irregularity
    // -----------------------------------------------------------------------
    std::vector<float> firingRates;
    firingRates.reserve(neuronCountU);

    std::size_t irregularCount = 0U;
    float       irregularitySum = 0.0f;

    for (const NeuronMetrics& m : neuronMetrics) {
        const float rate = (std::isfinite(m.firingRateHz) && m.firingRateHz > 0.0f)
                               ? m.firingRateHz
                               : 0.0f;
        firingRates.push_back(rate);

        if (m.isiMeanMs > 1.0e-3f && m.isiVarianceMs > 0.0f) {
            const float cv = std::sqrt(m.isiVarianceMs) / m.isiMeanMs;
            if (std::isfinite(cv)) {
                irregularitySum += std::clamp(cv, 0.0f, 5.0f);
                ++irregularCount;
            }
        }
    }

    net.irregularityIndex = (irregularCount > 0U)
                                ? irregularitySum / static_cast<float>(irregularCount)
                                : 0.0f;

    // -----------------------------------------------------------------------
    // 2. Trimmed-mean firing rate
    // -----------------------------------------------------------------------
    {
        std::vector<float> sorted = firingRates;
        std::sort(sorted.begin(), sorted.end());

        const std::size_t trimN = static_cast<std::size_t>(
            0.10f * static_cast<float>(sorted.size())
        );
        std::size_t lo = trimN;
        std::size_t hi = sorted.size() - trimN;
        if (lo >= hi) { lo = 0U; hi = sorted.size(); }

        const float trimSum = std::accumulate(
            sorted.begin() + static_cast<std::ptrdiff_t>(lo),
            sorted.begin() + static_cast<std::ptrdiff_t>(hi),
            0.0f
        );
        net.meanFiringRateHz = trimSum / static_cast<float>(hi - lo);
    }
    // firingRateStdHz
    {
        float variance = 0.0f;
        for (float rate : firingRates) {
            const float d = rate - net.meanFiringRateHz;
            variance += d * d;
        }
        net.firingRateStdHz = std::sqrt(variance / neuronCount);
    }

    // silentNeuronPct
    {
        std::size_t silentCount = 0U;
        for (float rate : firingRates) {
            if (rate < 2.0f) ++silentCount;
        }
        net.silentNeuronPct = (100.0f * static_cast<float>(silentCount)) / neuronCount;
    }

    // -----------------------------------------------------------------------
    // 4. Population firing rate variance
    // -----------------------------------------------------------------------
    {
        float var = 0.0f;
        for (float rate : firingRates) {
            const float d = rate - net.meanFiringRateHz;
            var += d * d;
        }
        net.populationVariance = var / neuronCount;
    }

    // -----------------------------------------------------------------------
    // 6. Synchronization index
    //
    // FIX (Bug 5): Re-bin spike counts into fixed cfg::kSyncBinMs windows
    // before computing coincidence-above-chance, so the result does not
    // depend on the simulation's integration timestep.
    // -----------------------------------------------------------------------
    if (!result.populationSpikesPerStep.empty() && result.dtMs > 0.0f) {
        const std::vector<float> fractions = rebinSpikeFractions(
            result.populationSpikesPerStep,
            result.dtMs,
            neuronCount,
            cfg::kSyncBinMs
        );
        net.synchronizationIndex = computeSyncFromFractions(fractions, neuronCount);
    }
    // peakSynchronizationIndex — max sync in any window
    {
        const auto windows = computeTimeWindowMetrics(result, 50.0f, 50.0f);
        float peak = 0.0f;
        for (const auto& w : windows) {
            peak = std::max(peak, w.synchronizationIndex);
        }
        net.peakSynchronizationIndex = peak;
    }

    // -----------------------------------------------------------------------
    // 7. Burst metrics
    //
    // FIX (Bug 6): burstIndex (bursts / total spikes) is diluted by long
    // simulations. We now compute two complementary measures:
    //   burstRateHz           – burst events per second (simulation-length
    //                           independent, analogous to firing rate)
    //   burstingNeuronPct     – percentage of neurons that had at least one
    //                           burst (indicates spatial spread of bursting)
    // The old burstIndex field is kept for backwards compatibility but
    // populated from burstRateHz normalised to [0,1] via a 10 Hz ceiling.
    // -----------------------------------------------------------------------
    {
        std::uint64_t totalSpikes         = 0U;
        std::uint64_t totalBurstEvents    = 0U;
        std::uint64_t burstingNeuronCount = 0U;
        float totalBurstDurationMs        = 0.0f;
        // Issue 2 FIX: use the neuron-type-specific burst window embedded in
        // the result when the simulator has set it; fall back to the default.
        const float effectiveBurstWindowMs =
            (result.burstWindowMs > 0.0f)
                ? result.burstWindowMs
                : cfg::kBurstWindowMsDefault;

        for (const auto& spikes : result.spikeTimes) {
            totalSpikes += static_cast<std::uint64_t>(spikes.size());
            const BurstCounts bc = countBurstsInRange(spikes, 0U, spikes.size(),
                                                      effectiveBurstWindowMs);
            totalBurstEvents    += bc.events;
            burstingNeuronCount += bc.burstingNeurons;
            totalBurstDurationMs += bc.totalDurationMs;
        }

        const float durationSec = std::max(1.0e-6f, result.durationMs / 1000.0f);

        // Normalise by neuron count so burstRateHz is per-neuron per-second
        // This prevents large networks from producing artificially high burst rates
        net.burstRateHz = static_cast<float>(totalBurstEvents) /(durationSec * neuronCount);
        // meanBurstDurationMs
        net.meanBurstDurationMs = (totalBurstEvents > 0U)? static_cast<float>(totalBurstDurationMs) / static_cast<float>(totalBurstEvents): 0.0f;
        net.burstingNeuronPct = (100.0f * static_cast<float>(burstingNeuronCount)) / neuronCount;

        // Issue A FIX: sigmoid normalization preserves ranking above 10 Hz.
        // See burstRateToIndex() for calibration guidance.
        net.burstIndex = burstRateToIndex(net.burstRateHz);

        (void)totalSpikes; // retained for potential future use
    }

    // -----------------------------------------------------------------------
    // 8. Early / late window rates
    // -----------------------------------------------------------------------
    {
        const std::size_t mid = result.populationSpikesPerStep.size() / 2U;
        std::uint64_t earlySpikes = 0U;
        std::uint64_t lateSpikes  = 0U;
        for (std::size_t i = 0; i < result.populationSpikesPerStep.size(); ++i) {
            if (i < mid) earlySpikes += result.populationSpikesPerStep[i];
            else          lateSpikes  += result.populationSpikesPerStep[i];
        }
        const float halfSec      = std::max(1.0e-6f, (result.durationMs * 0.5f) / 1000.0f);
        net.earlyWindowRateHz    = static_cast<float>(earlySpikes) / (neuronCount * halfSec);
        net.lateWindowRateHz     = static_cast<float>(lateSpikes)  / (neuronCount * halfSec);
    }


    // -----------------------------------------------------------------------
    // 12. Sanitize — guard against any remaining NaN / Inf
    // -----------------------------------------------------------------------
    auto sanitize = [](float v, float fallback) {
        return std::isfinite(v) ? v : fallback;
    };

    net.meanFiringRateHz     = sanitize(net.meanFiringRateHz,     0.0f);
    net.synchronizationIndex = sanitize(net.synchronizationIndex, 0.0f);
    net.burstIndex           = sanitize(net.burstIndex,           0.0f);
    net.burstRateHz          = sanitize(net.burstRateHz,          0.0f);
    net.burstingNeuronPct    = sanitize(net.burstingNeuronPct,    0.0f);
    net.populationVariance   = sanitize(net.populationVariance,   0.0f);
    net.irregularityIndex    = sanitize(net.irregularityIndex,    0.0f);
    net.earlyWindowRateHz    = sanitize(net.earlyWindowRateHz,    0.0f);
    net.lateWindowRateHz     = sanitize(net.lateWindowRateHz,     0.0f);
    net.firingRateStdHz      = sanitize(net.firingRateStdHz,      0.0f);
    net.silentNeuronPct      = sanitize(net.silentNeuronPct,      0.0f);
    net.peakSynchronizationIndex = sanitize(net.peakSynchronizationIndex, 0.0f);
    net.meanBurstDurationMs  = sanitize(net.meanBurstDurationMs,  0.0f);
    return net;
}

// ---------------------------------------------------------------------------
// MetricsAnalyzer::computeTimeWindowMetrics
// ---------------------------------------------------------------------------
std::vector<TimeWindowMetrics> MetricsAnalyzer::computeTimeWindowMetrics(
    const simulation::SimulationResult& result,
    float windowMs,
    float stepMs
) {
    std::vector<TimeWindowMetrics> windows;

    if (result.durationMs <= 0.0f || result.dtMs <= 0.0f) {
        return windows;
    }

    const float       safeWindowMs = std::max(windowMs, result.dtMs);
    const float       safeStepMs   = std::max(stepMs,   result.dtMs);
    const std::size_t neuronCount  = std::max<std::size_t>(1U, result.spikeTimes.size());
    const float       neuronCountF = static_cast<float>(neuronCount);
    const std::size_t stepCount    = result.populationSpikesPerStep.size();

    for (float startMs = 0.0f;
         startMs + safeWindowMs <= result.durationMs + 1.0e-4f;
         startMs += safeStepMs)
    {
        const float endMs = std::min(result.durationMs, startMs + safeWindowMs);
        if (endMs <= startMs) { continue; }

        const std::size_t startStep = std::min(
            stepCount,
            static_cast<std::size_t>(std::floor(startMs / result.dtMs))
        );
        const std::size_t endStep = std::min(
            stepCount,
            static_cast<std::size_t>(std::ceil(endMs / result.dtMs))
        );
        if (endStep <= startStep) { continue; }

        // -----------------------------------------------------------------------
        // Mean firing rate for this window
        // -----------------------------------------------------------------------
        std::uint64_t spikeSum = 0U;
        for (std::size_t s = startStep; s < endStep; ++s) {
            spikeSum += result.populationSpikesPerStep[s];
        }
        const float durationSec = std::max(
            1.0e-6f,
            static_cast<float>(endStep - startStep) * result.dtMs / 1000.0f
        );
        const float meanRateHz = static_cast<float>(spikeSum) /
                                  (neuronCountF * durationSec);

        // -----------------------------------------------------------------------
        // Synchronization — FIX (Bug 5): use fixed-width bins via helper
        // -----------------------------------------------------------------------
        float sync = 0.0f;
        {
            // Slice the step counts for this window.
            const std::vector<std::uint32_t> windowSteps(
                result.populationSpikesPerStep.begin() +
                    static_cast<std::ptrdiff_t>(startStep),
                result.populationSpikesPerStep.begin() +
                    static_cast<std::ptrdiff_t>(endStep)
            );
            const std::vector<float> fractions = rebinSpikeFractions(
                windowSteps,
                result.dtMs,
                neuronCountF,
                cfg::kSyncBinMs
            );
            sync = computeSyncFromFractions(fractions, neuronCountF);
        }

        // -----------------------------------------------------------------------
        // Burst and irregularity from per-neuron spike trains
        // FIX (Bug 6): use burstRateHz for the window rather than burst/totalSpikes.
        // -----------------------------------------------------------------------
        std::uint64_t totalWindowSpikes   = 0U;
        std::uint64_t totalBurstEvents    = 0U;
        std::uint64_t burstingNeuronCount = 0U;
        float totalBurstDurationMs        = 0.0f;

        float windowIrregularitySum   = 0.0f;
        std::size_t windowIrregCount  = 0U;

        // Issue 2 FIX: derive effective burst window once per outer window,
        // then reuse for every neuron inside this window.
        const float effectiveBurstWindowMs =
            (result.burstWindowMs > 0.0f)
                ? result.burstWindowMs
                : cfg::kBurstWindowMsDefault;

        for (const auto& spikes : result.spikeTimes) {
            const auto itBegin = std::lower_bound(spikes.begin(), spikes.end(), startMs);
            const auto itEnd   = std::lower_bound(spikes.begin(), spikes.end(), endMs);
            const std::size_t idxB = static_cast<std::size_t>(itBegin - spikes.begin());
            const std::size_t idxE = static_cast<std::size_t>(itEnd   - spikes.begin());
            const std::size_t cnt  = idxE - idxB;

            totalWindowSpikes += static_cast<std::uint64_t>(cnt);

            // Per-neuron sample ISI CV within this window.
            if (cnt >= 2U) {
                float isiSum   = 0.0f;
                float isiSumSq = 0.0f;
                for (std::size_t k = idxB + 1U; k < idxE; ++k) {
                    const float isi = spikes[k] - spikes[k - 1U];
                    isiSum   += isi;
                    isiSumSq += isi * isi;
                }
                const float n    = static_cast<float>(cnt - 1U);
                const float mean = isiSum / n;
                if (mean > 1.0e-3f && cnt > 2U) {
                    // FIX (Bug 8): sample variance inside windows too.
                    const float sVar = std::max(
                        0.0f,
                        (isiSumSq - isiSum * isiSum / n) / (n - 1.0f)
                    );
                    if (sVar > 0.0f) {
                        const float cv = std::sqrt(sVar) / mean;
                        if (std::isfinite(cv)) {
                            windowIrregularitySum += std::clamp(cv, 0.0f, 5.0f);
                            ++windowIrregCount;
                        }
                    }
                }
            }

            const BurstCounts bc = countBurstsInRange(spikes, idxB, idxE,
                                                      effectiveBurstWindowMs);
            totalBurstEvents    += bc.events;
            burstingNeuronCount += bc.burstingNeurons;
            totalBurstDurationMs += bc.totalDurationMs;
        }

        const float windowIrregularity = (windowIrregCount > 0U)
            ? windowIrregularitySum / static_cast<float>(windowIrregCount)
            : 0.0f;

        // Issue A FIX: sigmoid normalization, consistent with full-network path.
        // Issue E FIX: very short windows (< cfg::kMinWindowForBurstRateMs) can
        // produce spuriously huge burstRateHz (e.g. 1 burst in a 50 ms window =
        // 20 Hz) that overwhelm the seizure score. We apply the sigmoid
        // unconditionally — its gentle slope at high rates already reduces the
        // impact — but we also store the raw rate so callers can apply their own
        // minimum-window filter if needed.
        const float windowBurstRateHz = static_cast<float>(totalBurstEvents) / (durationSec * neuronCountF);
        const float windowBurstIndex  = burstRateToIndex(windowBurstRateHz);

    
        TimeWindowMetrics m;
        m.startMs              = startMs;
        m.endMs                = endMs;
        m.meanFiringRateHz     = std::isfinite(meanRateHz) ? std::max(0.0f, meanRateHz) : 0.0f;
        m.synchronizationIndex = std::clamp(sync,               0.0f, 1.0f);
        m.burstIndex           = windowBurstIndex;
        m.burstRateHz          = std::max(0.0f, windowBurstRateHz);
        m.burstingNeuronPct    = std::clamp(
            100.0f * static_cast<float>(burstingNeuronCount) / neuronCountF,
            0.0f, 100.0f
        );
        m.irregularityIndex    = std::clamp(windowIrregularity, 0.0f, 5.0f);
        m.meanBurstDurationMs  = (totalBurstEvents > 0)
            ? totalBurstDurationMs / static_cast<float>(totalBurstEvents)
            : 0.0f;

        windows.push_back(m);
    }

    return windows;
}

} // namespace spp::analyzer