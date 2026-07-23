#include "ReceptorModel.h"

#include <cmath>
#include <algorithm>

namespace spp::neuron {

float nmdaMgBlockFraction(float voltageMv, float extracellularMgMm) {
    // B(V) = 1 / (1 + ([Mg2+]_o / 3.57 mM) * exp(-V / 16.13 mV))
    // See header comment for citation and cross-check.
    const float mg = std::max(0.0f, extracellularMgMm);
    const float z  = std::clamp(-voltageMv / 16.13f, -60.0f, 60.0f);
    const float denom = 1.0f + (mg / 3.57f) * std::exp(z);
    if (!std::isfinite(denom) || denom <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(1.0f / denom, 0.0f, 1.0f);
}

float DualExpKernel::normFactor() const {
    // Peak-normalize a dual-exponential rise/decay waveform:
    //   f(t) = exp(-t/tauDecay) - exp(-t/tauRise)
    // Guard against tauRise == tauDecay (division by zero in tPeak) by
    // nudging tauRise apart slightly -- kinetically indistinguishable from
    // the intended value for any receptor used in this engine.
    float tauRise  = tauRiseMs;
    float tauDecay = tauDecayMs;
    if (std::fabs(tauDecay - tauRise) < 1.0e-4f) {
        tauRise -= 1.0e-3f;
    }
    if (tauRise <= 0.0f || tauDecay <= 0.0f) {
        return 1.0f;
    }

    const float tPeak = (tauRise * tauDecay) / (tauDecay - tauRise) *
                         std::log(tauDecay / tauRise);
    if (!std::isfinite(tPeak) || tPeak < 0.0f) {
        return 1.0f;
    }

    const float peakValue = std::exp(-tPeak / tauDecay) - std::exp(-tPeak / tauRise);
    if (!std::isfinite(peakValue) || std::fabs(peakValue) < 1.0e-9f) {
        return 1.0f;
    }
    return 1.0f / peakValue;
}

float dualExpWaveform(const DualExpKernel& kernel, float tMs) {
    const float t = tMs - kernel.onsetDelayMs;
    if (t < 0.0f) {
        return 0.0f;
    }

    float tauRise  = kernel.tauRiseMs;
    float tauDecay = kernel.tauDecayMs;
    if (std::fabs(tauDecay - tauRise) < 1.0e-4f) {
        tauRise -= 1.0e-3f;
    }
    if (tauRise <= 0.0f || tauDecay <= 0.0f) {
        return 0.0f;
    }

    const float raw = std::exp(-t / tauDecay) - std::exp(-t / tauRise);
    const float normalized = raw * kernel.normFactor();
    return std::clamp(normalized, 0.0f, 1.5f); // small headroom for numerical overshoot near t=0
}

} // namespace spp::neuron