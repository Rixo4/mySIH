// This file implements the SeizureDetector class, which classifies neural
// network states based on various metrics and final voltages.
#include "SeizureDetector.h"

#include <algorithm>
#include <cmath>

namespace spp::analyzer {

NetworkState SeizureDetector::classify(
    const NetworkMetrics& metrics,
    const std::vector<float>& finalVoltages
) {
    // -----------------------------------------------------------------------
    // Priority 9: Sanitize every metric used in classification.
    // std::isfinite guards + clamp ensure no NaN/Inf can corrupt decisions.
    // -----------------------------------------------------------------------
    const float meanRate = std::isfinite(metrics.meanFiringRateHz)
        ? std::max(0.0f, metrics.meanFiringRateHz)
        : 0.0f;

    const float sync = std::clamp(
        std::isfinite(metrics.synchronizationIndex) ? metrics.synchronizationIndex : 0.0f,
        0.0f, 1.0f
    );

    const float burst = std::clamp(
        std::isfinite(metrics.burstIndex) ? metrics.burstIndex : 0.0f,
        0.0f, 1.0f
    );

    const float seizureProbFrac = std::clamp(
        (std::isfinite(metrics.seizureProbabilityPct) ? metrics.seizureProbabilityPct : 0.0f) / 100.0f,
        0.0f, 1.0f
    );

    const float suppressionFrac = std::clamp(
        (std::isfinite(metrics.suppressionPct) ? metrics.suppressionPct : 0.0f) / 100.0f,
        0.0f, 1.0f
    );

    // Priority 2 + 9: NII was previously ignored entirely. Sanitize and use it.
    const float nii = std::clamp(
        std::isfinite(metrics.nii) ? metrics.nii : 0.0f,
        0.0f, 1.0f
    );

    // -----------------------------------------------------------------------
    // Depolarization fraction from final voltages.
    // Priority 6: threshold raised from 0.25 → 0.50 (25 % was too easily
    // triggered; genuine depolarization block requires the majority of neurons
    // to be stuck above resting potential).
    // -----------------------------------------------------------------------
    std::size_t depolarizedCount = 0U;
    for (float v : finalVoltages) {
        if (std::isfinite(v) && v > -20.0f) {
            ++depolarizedCount;
        }
    }
    const float depolarizedFraction = finalVoltages.empty()
        ? 0.0f
        : static_cast<float>(depolarizedCount) / static_cast<float>(finalVoltages.size());

    // -----------------------------------------------------------------------
    // Priority 7: Classification runs top-to-bottom in a strict severity
    // hierarchy. Every state is checked before falling through to Stable,
    // so no abnormal network can reach Stable by accident.
    //
    // Hierarchy (most severe → least severe):
    //   1. NeuralSuppression
    //   2. DepolarizationBlock
    //   3. SeizureActive
    //   4. SeizureRisk          ← Priority 1: new pre-seizure warning stage
    //   5. Hyperexcitable
    //   6. MildInstability
    //   7. Stable
    // -----------------------------------------------------------------------

    // 1. NeuralSuppression — checked first; a suppressed network may coincidentally
    //    show low burst/sync so it must be captured before those checks.
    if (suppressionFrac > 0.80f) {
        return NetworkState::NeuralSuppression;
    }

    // 2. DepolarizationBlock — requires majority depolarization AND low firing.
    //    Priority 6: raised depolarizedFraction threshold 0.25 → 0.50,
    //    tightened meanRate threshold 6.0 → 5.0.
    if (seizureProbFrac >= 0.80f && sync >= 0.70f) {
        if (depolarizedFraction >= 0.50f && meanRate < 5.0f) {
            return NetworkState::DepolarizationBlock;
        }
    }

    // 3. SeizureActive — high seizure probability with either strong sync OR
    //    severe bursting. Priority 5: burst >= 0.15 added as an alternative
    //    criterion so K-block / Ca-block ictal patterns are not missed when
    //    sync stays moderate.
    if (seizureProbFrac >= 0.80f &&
        (sync >= 0.70f || burst >= 0.15f))
    {
        return NetworkState::SeizureActive;
    }

    // 4. SeizureRisk — Priority 1: genuine pre-seizure warning state.
    //    Previously this enum value existed but was NEVER returned, making
    //    the system jump directly from Stable to SeizureActive with no warning.
    //    Now triggered by either:
    //      • elevated seizure probability (≥ 0.50), or
    //      • high synchronization (≥ 0.60), or
    //      • high NII indicating composite network instability (≥ 0.65).
    //    Priority 2: NII is included here as a first-class criterion.
    if (seizureProbFrac >= 0.50f ||
        sync             >= 0.60f ||
        nii              >= 0.65f)
    {
        return NetworkState::SeizureRisk;
    }

    // 5. Hyperexcitable — Priority 3: high firing rate alone is insufficient;
    //    requires corroborating elevated sync OR burst. A fast but regular,
    //    unsynchronised network is not inherently pathological.
    if (meanRate > 15.0f &&
        (sync > 0.40f || burst > 0.05f))
    {
        return NetworkState::Hyperexcitable;
    }

    // 6. MildInstability — Priority 4: broadened from burst-only to include
    //    sync, NII, and seizure probability so that subtle multi-marker
    //    instability is caught before it progresses.
    //    Priority 2: nii > 0.40 added as a standalone trigger.
    if (burst        > 0.08f ||
        sync         > 0.40f ||
        nii          > 0.40f ||
        seizureProbFrac > 0.30f)
    {
        return NetworkState::MildInstability;
    }

    // 7. Stable — reached only after every pathological state above has been
    //    explicitly ruled out. Priority 7 satisfied.
    return NetworkState::Stable;
}

// -----------------------------------------------------------------------
// Priority 8: toString() now returns distinct strings for every state.
// SeizureRisk and SeizureActive previously both returned "Seizure",
// making them indistinguishable in logs and UI.
// Fixed namespace typo: was spp::analyer, now spp::analyzer.
// -----------------------------------------------------------------------
std::string SeizureDetector::toString(NetworkState state) {
    switch (state) {
        case NetworkState::Stable:
            return "Stable";
        case NetworkState::MildInstability:
            return "Mild Instability";
        case NetworkState::Hyperexcitable:
            return "Hyperexcitable";
        case NetworkState::SeizureRisk:
            return "Seizure Risk";
        case NetworkState::SeizureActive:
            return "Active Seizure";
        case NetworkState::DepolarizationBlock:
            return "Depolarization Block";
        case NetworkState::NeuralSuppression:
            return "Suppression";
        default:
            return "Stable";
    }
}

} // namespace spp::analyzer