#include "NetworkAnalyzer.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace spp::analyzer {

namespace {

float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

float reductionPct(float baseline, float current) {
    if (baseline <= 1.0e-6f) return 0.0f;
    return std::max(0.0f, ((baseline - current) / baseline) * 100.0f);
}

float increasePct(float baseline, float current) {
    if (baseline <= 1.0e-6f) return 0.0f;
    return std::max(0.0f, ((current - baseline) / baseline) * 100.0f);
}

} // namespace

// ─── Main Entry Point ────────────────────────────────────────────────────────

std::vector<AnalyzedDose> NetworkAnalyzer::analyze(
    const std::vector<DoseObservation>& observations)
{
    std::vector<AnalyzedDose> results;
    if (observations.empty()) return results;

    // dose[0] is always baseline
    const RawMetrics& baseline = observations.front().metrics;

    results.reserve(observations.size());

    for (const auto& obs : observations) {
        AnalyzedDose analyzed;

        // Copy raw data
        analyzed.dose    = obs.dose;
        analyzed.blockNa = obs.blockNa;
        analyzed.blockK  = obs.blockK;
        analyzed.blockCa = obs.blockCa;
        analyzed.metrics = obs.metrics;

        // Compute deltas vs baseline
        computeDeltas(analyzed, baseline);

        // Compute derived scores
        analyzed.nii = computeNII(obs.metrics);

        const float baselineNii = computeNII(baseline);
        const float niiDelta    = analyzed.nii - baselineNii;

        analyzed.suppressionScore = computeSuppressionScore(
            analyzed.rateChangePct,
            analyzed.silentNeuronDelta
        );

        analyzed.excitabilityScore = computeExcitabilityScore(
            analyzed.rateChangePct,
            analyzed.burstRateDelta,
            analyzed.irregularityDelta
        );

        analyzed.stabilizationScore = computeStabilizationScore(
            analyzed.syncReductionPct,
            analyzed.burstDurationDelta,
            niiDelta,
            analyzed.rateChangePct,
            analyzed.irregularityDelta
        );

        analyzed.seizureProbability = computeSeizureProbability(
            obs.metrics,
            analyzed.nii
        );

        // Classify mechanism and state
        analyzed.mechanismSignature = detectMechanism(analyzed);
        analyzed.networkState       = classifyState(analyzed);

        results.push_back(analyzed);
    }

    return results;
}

// ─── Delta Computation ───────────────────────────────────────────────────────

void NetworkAnalyzer::computeDeltas(
    AnalyzedDose& dose,
    const RawMetrics& baseline)
{
    const RawMetrics& m = dose.metrics;

    // Rate
    const float baseRate = std::max(1.0e-6f, baseline.meanFiringRateHz);
    dose.rateChangePct = ((m.meanFiringRateHz - baseRate) / baseRate) * 100.0f;

    // Sync
    dose.syncDelta        = m.synchronizationIndex - baseline.synchronizationIndex;
    dose.syncReductionPct = reductionPct(baseline.synchronizationIndex,
                                          m.synchronizationIndex);
    dose.peakSyncDelta    = m.peakSynchronizationIndex - baseline.peakSynchronizationIndex;

    // Burst
    dose.burstRateDelta     = m.burstRateHz - baseline.burstRateHz;
    dose.burstDurationDelta = m.meanBurstDurationMs - baseline.meanBurstDurationMs;

    // Irregularity
    dose.irregularityDelta = m.irregularityIndex - baseline.irregularityIndex;

    // Silent neurons
    dose.silentNeuronDelta = m.silentNeuronPct - baseline.silentNeuronPct;
}

// ─── NII ─────────────────────────────────────────────────────────────────────
// Network Instability Index
// Pure function of current raw metrics
// Weights: sync=0.30, burst=0.30, irregularity=0.25, populationVariance=0.15

float NetworkAnalyzer::computeNII(const RawMetrics& m) {
    const float syncNorm  = clamp01(m.synchronizationIndex);
    const float burstNorm = clamp01(m.burstRateHz / 10.0f);
    const float irregNorm = clamp01(m.irregularityIndex / 2.0f);
    const float popVarNorm= clamp01(m.populationVariance / 300.0f);

    return clamp01(
        0.30f * syncNorm  +
        0.30f * burstNorm +
        0.25f * irregNorm +
        0.15f * popVarNorm
    );
}

// ─── Suppression Score ───────────────────────────────────────────────────────
// Only fires when rate decreases
// Corroborated by silent neuron increase

float NetworkAnalyzer::computeSuppressionScore(
    float rateChangePct,
    float silentNeuronDelta)
{
    if (rateChangePct >= 0.0f) return 0.0f;

    const float rateSuppression  = clamp01(-rateChangePct / 70.0f);
    const float silentCorroboration = clamp01(silentNeuronDelta / 30.0f);

    return clamp01(0.65f * rateSuppression + 0.35f * silentCorroboration);
}

// ─── Excitability Score ──────────────────────────────────────────────────────
// Captures K-block signature:
//   burst increase + irregularity increase + moderate rate increase

float NetworkAnalyzer::computeExcitabilityScore(
    float rateChangePct,
    float burstRateDelta,
    float irregularityDelta)
{
    // Rate increase component
    const float rateDrive  = clamp01(rateChangePct / 50.0f);

    // Burst increase component — primary K-block signal
    const float burstDrive = clamp01(burstRateDelta / 5.0f);

    // Irregularity increase — secondary K-block signal
    const float irregDrive = clamp01(irregularityDelta / 1.0f);

    return clamp01(
        0.20f * rateDrive  +
        0.30f * burstDrive +
        0.50f * irregDrive  // irregularity is the strongest K-block network signal
    );
}

// ─── Stabilization Score ─────────────────────────────────────────────────────
// Captures Ca-block signature:
//   sync reduction + burst duration shortening + NII reduction
//   while firing rate is preserved

float NetworkAnalyzer::computeStabilizationScore(
    float syncReductionPct,
    float burstDurationDelta,
    float niiDelta,
    float rateChangePct,
    float irregularityDelta)
{
    const bool rateViable = (rateChangePct > -15.0f);
    const bool notExcitatoryPattern = (irregularityDelta < 0.08f);
    if (!rateViable || !notExcitatoryPattern) return 0.0f;

    const float syncBenefit  = clamp01(syncReductionPct / 30.0f);
    const float burstBenefit = clamp01(-burstDurationDelta / 20.0f);
    const float niiBenefit   = clamp01(-niiDelta / 0.30f);

    return clamp01(0.40f * syncBenefit + 0.35f * burstBenefit + 0.25f * niiBenefit);
}

// ─── Seizure Probability ─────────────────────────────────────────────────────

float NetworkAnalyzer::computeSeizureProbability(
    const RawMetrics& m,
    float nii)
{
    const float syncNorm  = clamp01(m.synchronizationIndex);
    const float burstNorm = clamp01(m.burstRateHz / 10.0f);
    const float irregNorm = clamp01(m.irregularityIndex / 2.0f);
    const float niiNorm   = clamp01(nii);

    const float score =
        0.35f * syncNorm  +
        0.30f * burstNorm +
        0.20f * irregNorm +
        0.15f * niiNorm;

    // Sigmoid
    const float logit = 12.0f * (score - 0.55f);
    const float safeLogit = std::clamp(logit, -60.0f, 60.0f);
    return 1.0f / (1.0f + std::exp(-safeLogit));
}

// ─── Mechanism Detection ─────────────────────────────────────────────────────

MechanismSignature NetworkAnalyzer::detectMechanism(const AnalyzedDose& dose) {
    constexpr float kMinBlock = 0.05f;  // 5% block floor before attributing a channel

    const float na = dose.blockNa, k = dose.blockK, ca = dose.blockCa;
    const float maxBlock = std::max({na, k, ca});
    if (maxBlock < kMinBlock) return MechanismSignature::Unknown;

    const int meaningful = (na > kMinBlock) + (k > kMinBlock) + (ca > kMinBlock);
    if (meaningful >= 2) {
        std::array<float,3> vals{na, k, ca};
        std::sort(vals.begin(), vals.end(), std::greater<float>());
        if (vals[1] > vals[0] * 0.6f) return MechanismSignature::Mixed;  // genuinely comparable
    }

    if (maxBlock == na) return MechanismSignature::NaBlock;
    if (maxBlock == k)  return MechanismSignature::KBlock;
    return MechanismSignature::CaBlock;
}

// ─── Network State Classification ────────────────────────────────────────────

NetworkState NetworkAnalyzer::classifyState(
    const AnalyzedDose& dose)
{
    const float seizureProb  = dose.seizureProbability;
    const float sync         = dose.metrics.synchronizationIndex;
    const float nii          = dose.nii;
    const float silentPct    = dose.metrics.silentNeuronPct;
    const float exciScore    = dose.excitabilityScore;
    const float suppScore    = dose.suppressionScore;

    // 1. Neural Suppression — firing collapsed
    if (silentPct > 80.0f || dose.rateChangePct <= -90.0f) {
        return NetworkState::NeuralSuppression;
    }

    // 2. Depolarization Block
    // Was highly active then crashed to silence
    if (dose.rateChangePct < -60.0f &&
    dose.metrics.peakSynchronizationIndex > 0.60f) {
    return NetworkState::DepolarizationBlock;
    }

    // 3. Seizure Active
    if (seizureProb >= 0.80f &&
        (sync >= 0.70f || dose.metrics.burstRateHz >= 8.0f)) {
        return NetworkState::SeizureActive;
    }

    // 4. Seizure Risk
    if (seizureProb >= 0.50f ||
        sync        >= 0.60f ||
        nii         >= 0.65f) {
        return NetworkState::SeizureRisk;
    }

    // 5. Hyperexcitable — K-block signature
    if (exciScore > 0.50f &&
        (dose.burstRateDelta > 2.0f ||
         dose.irregularityDelta > 0.20f)) {
        return NetworkState::Hyperexcitable;
    }

    // 6. Mild Instability
    if (nii      > 0.40f ||
        exciScore > 0.30f ||
        suppScore > 0.30f) {
        return NetworkState::MildInstability;
    }

    // 7. Stable
    return NetworkState::Stable;
}

} // namespace spp::analyzer