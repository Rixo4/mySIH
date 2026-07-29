#include "NeurotransmitterPool.h"

#include <algorithm>
#include <cmath>

namespace spp::synapse {

void refillVesiclePools(VesiclePoolState& state, const VesiclePoolConfig& cfg, float dtMs) {
    if (!cfg.enabled || dtMs <= 0.0f) {
        return;
    }

    // Reserve relaxes toward its own fresh size on reserveRefillTauMs,
    // sourced from the (untracked, "unlimited") Recovery pool -- see header
    // note on why this is a bounded relaxation rather than the plan's
    // literal unbounded mass-action source term.
    const float reserveTau = std::max(1.0f, cfg.reserveRefillTauMs);
    state.reserve += (dtMs / reserveTau) * (cfg.reserveSize - state.reserve);
    state.reserve = std::clamp(state.reserve, 0.0f, cfg.reserveSize);

    // RRP relaxes toward its own fresh size on rrpRefillTauMs, but the rate
    // is additionally scaled by Reserve's own fractional availability --
    // a depleted Reserve genuinely slows RRP refilling, preserving the
    // plan's intended coupling between the two pools without an unbounded
    // mass-action term.
    const float rrpTau = std::max(1.0f, cfg.rrpRefillTauMs);
    const float reserveAvailability = (cfg.reserveSize > 1.0e-6f)
        ? std::clamp(state.reserve / cfg.reserveSize, 0.0f, 1.0f)
        : 0.0f;
    state.rrp += (dtMs / rrpTau) * (cfg.rrpSize - state.rrp) * reserveAvailability;
    state.rrp = std::clamp(state.rrp, 0.0f, cfg.rrpSize);
}

float triggerVesicleRelease(
    VesiclePoolState& state,
    const VesiclePoolConfig& cfg,
    float excitatoryDriveProxy,
    std::mt19937& rng
) {
    if (!cfg.enabled) {
        return 1.0f;
    }

    const float drive = std::max(0.0f, excitatoryDriveProxy);
    const float releaseProb = std::clamp(
        1.0f - std::exp(-std::max(0.0f, cfg.calciumFactor) * drive),
        0.0f, 1.0f
    );

    // Expected release from a completely FRESH (undepleted) pool at this
    // same drive/probability -- the baseline-preservation reference. See
    // header note: dividing by this (not rrpSize, not releaseProb alone)
    // is what makes a fresh pool return ~1.0 in expectation.
    const float expectedFreshRelease = releaseProb * cfg.rrpSize;

    // Binomial draw from the CURRENT (possibly depleted) pool. n must be a
    // non-negative integer; rrp is a continuous relaxation variable, so
    // round rather than floor to avoid a small systematic downward bias.
    const int nAvailable = static_cast<int>(std::lround(std::max(0.0f, state.rrp)));
    int released = 0;
    if (nAvailable > 0 && releaseProb > 0.0f) {
        std::binomial_distribution<int> releaseDraw(nAvailable, releaseProb);
        released = releaseDraw(rng);
    }

    state.rrp = std::clamp(state.rrp - static_cast<float>(released), 0.0f, cfg.rrpSize);

    if (expectedFreshRelease <= 1.0e-6f) {
        // No meaningful drive this spike (releaseProb ~0) -- scale is
        // undefined by the ratio, but there's also nothing to scale
        // (downstream conductance contribution from this spike is
        // negligible either way). Return 1.0 rather than dividing by ~0.
        return 1.0f;
    }

    return static_cast<float>(released) / expectedFreshRelease;
}

} // namespace spp::synapse