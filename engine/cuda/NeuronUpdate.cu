#include "CudaSimulator.h"
#include "NeuronUpdate.h"
#include "../neuron/ReceptorModel.h"

#ifdef SPP_USE_CUDA

#include <cuda_runtime.h>
#include <curand_kernel.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace spp::cuda {

namespace {

void checkCuda(cudaError_t status, const char* message) {
    if (status != cudaSuccess) {
        throw std::runtime_error(std::string(message) + ": " + cudaGetErrorString(status));
    }
}

__device__ float clamp01(float x) {
    return fminf(1.0f, fmaxf(0.0f, x));
}

// PHASE2_PLAN.md step 6: device mirrors of ReceptorModel.cpp's
// nmdaMgBlockFraction and DualExpKernel::normFactor -- identical formulas,
// just using the CUDA math intrinsics (expf/logf/isfinite) instead of
// std::exp/std::log so they compile in device code. Kept as free functions
// rather than __host__ __device__ shared with ReceptorModel.cpp since that
// file's versions use std::clamp/std::exp, which aren't device-callable.
// Defined up here (before derivatives(), which calls the first one) since
// CUDA/C++ requires a function to be declared before its call site within
// a translation unit.
__device__ float nmdaMgBlockFractionDevice(float voltageMv, float extracellularMgMm = 1.0f) {
    const float mg = fmaxf(0.0f, extracellularMgMm);
    const float z = fminf(60.0f, fmaxf(-60.0f, -voltageMv / 16.13f));
    const float denom = 1.0f + (mg / 3.57f) * expf(z);
    if (!isfinite(denom) || denom <= 0.0f) {
        return 0.0f;
    }
    return clamp01(1.0f / denom);
}

__device__ float dualExpNormFactorDevice(float tauRiseMs, float tauDecayMs) {
    float tauRise = tauRiseMs;
    float tauDecay = tauDecayMs;
    if (fabsf(tauDecay - tauRise) < 1.0e-4f) {
        tauRise -= 1.0e-3f;
    }
    if (tauRise <= 0.0f || tauDecay <= 0.0f) {
        return 1.0f;
    }
    const float tPeak = (tauRise * tauDecay) / (tauDecay - tauRise) * logf(tauDecay / tauRise);
    if (!isfinite(tPeak) || tPeak < 0.0f) {
        return 1.0f;
    }
    const float peakValue = expf(-tPeak / tauDecay) - expf(-tPeak / tauRise);
    if (!isfinite(peakValue) || fabsf(peakValue) < 1.0e-9f) {
        return 1.0f;
    }
    return 1.0f / peakValue;
}

__device__ float alphaM(float v) {
    const float x = (v + 40.0f) / 10.0f;
    if (fabsf(x) < 1.0e-6f) {
        return 1.0f;
    }
    return x / (1.0f - expf(-x));
}

__device__ float betaM(float v) {
    return 4.0f * expf(-(v + 65.0f) / 18.0f);
}

__device__ float alphaH(float v) {
    return 0.07f * expf(-(v + 65.0f) / 20.0f);
}

__device__ float betaH(float v) {
    return 1.0f / (1.0f + expf(-(v + 35.0f) / 10.0f));
}

__device__ float alphaN(float v) {
    const float x = (v + 55.0f) / 10.0f;
    if (fabsf(x) < 1.0e-6f) {
        return 0.1f;
    }
    return 0.1f * x / (1.0f - expf(-x));
}

__device__ float betaN(float v) {
    return 0.125f * expf(-(v + 65.0f) / 80.0f);
}

__device__ float sInf(float v) {
    return 1.0f / (1.0f + expf(-(v + 20.0f) / 6.5f));
}

__device__ float tauS(float v) {
    return 5.0f + 20.0f / (1.0f + expf((v + 25.0f) / 5.0f));
}

struct Deriv {
    float dv;
    float dm;
    float dh;
    float dn;
    float ds;
    float dcaCa;
};

__device__ Deriv derivatives(
    float v,
    float m,
    float h,
    float n,
    float s,
    float caCa,
    float iTotal,
    float gNa,
    float gK,
    float gCa,
    float cm,
    float eNa,
    float eK,
    float eCa,
    float gL,
    float eL,
    float gAHP,
    float tauCa,
    float kCa,
    float gAHPFloor,
    // PHASE2_PLAN.md step 6: the four receptor currents, mirroring
    // NeuronModel.cpp's computeDerivatives exactly -- same reasoning for
    // computing them here (not pre-summed into iTotal by the caller) so
    // they see the correct per-RK4-substage voltage, which matters most for
    // NMDA's voltage-dependent Mg2+ block. gXEff are already peak-scaled
    // (gMaxX * raw 0..1 conductance * drug modifier) by the caller, exactly
    // like gNa/gK/gCa above.
    float gAMPAEff,
    float gNMDAEff,
    float gGABAaEff,
    float gGABAbEff,
    float eGABAa,
    float eNMDA,
    float eAMPA
) {
    Deriv d;
    const float iNa = gNa * m * m * m * h * (v - eNa);
    const float iK = gK * n * n * n * n * (v - eK);
    const float iCa = gCa * s * s * (v - eCa);
    const float iAHP = (gAHP * caCa + gAHPFloor) * (v - eK);
    const float iLeak = gL * (v - eL);
    const float caInflux = kCa * fmaxf(0.0f, -iCa);

    const float iGABAa = gGABAaEff * (v - eGABAa);
    const float nmdaUnblock = nmdaMgBlockFractionDevice(v);
    const float iNMDA = gNMDAEff * nmdaUnblock * (v - eNMDA);
    // GABA-B: real metabotropic K+ conductance, reuses eK, same as CPU.
    const float iGABAb = gGABAbEff * (v - eK);

    // AMPA: same near-threshold driving-force floor as NeuronModel.cpp --
    // see that file's long comment for why this is needed (AMPA's ~0mV
    // reversal sits almost exactly at this network's spike threshold).
    constexpr float kAmpaMinDrivingForceMv = 15.0f;
    float ampaDrivingForce = v - eAMPA;
    if (ampaDrivingForce < 0.0f) {
        ampaDrivingForce = fminf(ampaDrivingForce, -kAmpaMinDrivingForceMv);
    } else {
        ampaDrivingForce = fmaxf(ampaDrivingForce, kAmpaMinDrivingForceMv);
    }
    const float iAMPA = gAMPAEff * ampaDrivingForce;

    d.dv = (iTotal - iNa - iK - iCa - iAHP - iLeak - iGABAa - iNMDA - iGABAb - iAMPA) / cm;
    d.dm = alphaM(v) * (1.0f - m) - betaM(v) * m;
    d.dh = alphaH(v) * (1.0f - h) - betaH(v) * h;
    d.dn = alphaN(v) * (1.0f - n) - betaN(v) * n;
    d.ds = (sInf(v) - s) / tauS(v);
    d.dcaCa = caInflux - caCa / tauCa;

    return d;
}

__device__ void clampState(float& v, float& m, float& h, float& n, float& s, float& caCa) {
    v = fminf(80.0f, fmaxf(-120.0f, v));
    m = clamp01(m);
    h = clamp01(h);
    n = clamp01(n);
    s = clamp01(s);
    caCa = clamp01(caCa);
}

__device__ std::size_t rowIndexForDelay(
    std::size_t oldHead,
    std::size_t delaySteps,
    std::size_t delay
) {
    const std::size_t clampedDelay = fmaxf(1.0f, fminf(static_cast<float>(delay), static_cast<float>(delaySteps)));
    const std::size_t back = clampedDelay - 1;
    return (oldHead + delaySteps - back) % delaySteps;
}

__device__ float hillBlockDevice(float dose, float ic50, float hill) {
    const float safeDose = fmaxf(dose, 0.0f);
    const float safeIc50 = fmaxf(ic50, 1.0e-12f);
    const float safeHill = fmaxf(hill, 1.0e-6f);
    const float dosePow = powf(safeDose, safeHill);
    const float ic50Pow = powf(safeIc50, safeHill);
    return clamp01(dosePow / (dosePow + ic50Pow + 1.0e-12f));
}

// Phase 3c: vesicle pool release draw -- curand has no built-in binomial
// generator, so this mirrors std::binomial_distribution<int>'s own
// semantics exactly (n independent Bernoulli trials) rather than
// approximating with a normal/Poisson curve. Cheap and exact for the small
// n this is actually called with (RRP is realistically 5-20 vesicles per
// NeurotransmitterPool.h's literature-informed defaults); capped at 128 to
// bound the loop if a config is ever misconfigured far outside that range.
__device__ int binomialDrawDevice(int n, float p, curandState* state) {
    if (n <= 0 || p <= 0.0f) return 0;
    if (p >= 1.0f) return n;
    const int nClamped = min(n, 128);
    int count = 0;
    for (int k = 0; k < nClamped; ++k) {
        if (curand_uniform(state) < p) ++count;
    }
    return count;
}

__device__ void integrateNeuronState(
    std::size_t i,
    float timeMs,
    float dtMs,
    float refractoryMs,
    float maxTotalCurrent,
    const neuron::HHParameters& params,
    const float* threshold,
    float iSyn,
    float iExt,
    float iNoise,
    float gNaEff,
    float gKEff,
    float gCaEff,
    // PHASE2_PLAN.md step 6: receptor "eff" conductances, already peak-
    // scaled and drug-modified by the caller (fusedBatchedStepKernel),
    // constant across this timestep's four RK4 substages -- same treatment
    // as gNaEff/gKEff/gCaEff above. Default to 0.0f (no-op) so the
    // non-receptor-aware hhStepKernel's call sites don't need to change.
    float gAMPAEff,
    float gNMDAEff,
    float gGABAaEff,
    float gGABAbEff,
    float* v,
    float* m,
    float* h,
    float* n,
    float* s,
    float* caCa,
    float* lastSpikeTime,
    std::uint8_t* spikeOut
) {
    const float oldV = v[i];

    float yv = oldV;
    float ym = m[i];
    float yh = h[i];
    float yn = n[i];
    float ys = s[i];
    float yca = caCa[i];

    float iTotal = iSyn + iExt + iNoise;
    if (!isfinite(iTotal)) {
        iTotal = 0.0f;
    }
    iTotal = fminf(maxTotalCurrent, fmaxf(-maxTotalCurrent, iTotal));

    const float safeGNa = (isfinite(gNaEff) && gNaEff >= 0.0f) ? gNaEff : 0.0f;
    const float safeGK = (isfinite(gKEff) && gKEff >= 0.0f) ? gKEff : 0.0f;
    const float safeGCa = (isfinite(gCaEff) && gCaEff >= 0.0f) ? gCaEff : 0.0f;

    const Deriv k1 = derivatives(yv, ym, yh, yn, ys, yca, iTotal, safeGNa, safeGK, safeGCa,
                                 params.cm, params.eNa, params.eK, params.eCa, params.gL, params.eL,
                                 params.gAHP, params.tauCa, params.kCa, params.gAHPFloor,
                                 gAMPAEff, gNMDAEff, gGABAaEff, gGABAbEff,
                                 params.eGABAa, params.eNMDA, params.eAMPA);

    float y2v = yv + 0.5f * dtMs * k1.dv;
    float y2m = ym + 0.5f * dtMs * k1.dm;
    float y2h = yh + 0.5f * dtMs * k1.dh;
    float y2n = yn + 0.5f * dtMs * k1.dn;
    float y2s = ys + 0.5f * dtMs * k1.ds;
    float y2ca = yca + 0.5f * dtMs * k1.dcaCa;
    clampState(y2v, y2m, y2h, y2n, y2s, y2ca);

    const Deriv k2 = derivatives(y2v, y2m, y2h, y2n, y2s, y2ca, iTotal, safeGNa, safeGK, safeGCa,
                                 params.cm, params.eNa, params.eK, params.eCa, params.gL, params.eL,
                                 params.gAHP, params.tauCa, params.kCa, params.gAHPFloor,
                                 gAMPAEff, gNMDAEff, gGABAaEff, gGABAbEff,
                                 params.eGABAa, params.eNMDA, params.eAMPA);

    float y3v = yv + 0.5f * dtMs * k2.dv;
    float y3m = ym + 0.5f * dtMs * k2.dm;
    float y3h = yh + 0.5f * dtMs * k2.dh;
    float y3n = yn + 0.5f * dtMs * k2.dn;
    float y3s = ys + 0.5f * dtMs * k2.ds;
    float y3ca = yca + 0.5f * dtMs * k2.dcaCa;
    clampState(y3v, y3m, y3h, y3n, y3s, y3ca);

    const Deriv k3 = derivatives(y3v, y3m, y3h, y3n, y3s, y3ca, iTotal, safeGNa, safeGK, safeGCa,
                                 params.cm, params.eNa, params.eK, params.eCa, params.gL, params.eL,
                                 params.gAHP, params.tauCa, params.kCa, params.gAHPFloor,
                                 gAMPAEff, gNMDAEff, gGABAaEff, gGABAbEff,
                                 params.eGABAa, params.eNMDA, params.eAMPA);

    float y4v = yv + dtMs * k3.dv;
    float y4m = ym + dtMs * k3.dm;
    float y4h = yh + dtMs * k3.dh;
    float y4n = yn + dtMs * k3.dn;
    float y4s = ys + dtMs * k3.ds;
    float y4ca = yca + dtMs * k3.dcaCa;
    clampState(y4v, y4m, y4h, y4n, y4s, y4ca);

    const Deriv k4 = derivatives(y4v, y4m, y4h, y4n, y4s, y4ca, iTotal, safeGNa, safeGK, safeGCa,
                                 params.cm, params.eNa, params.eK, params.eCa, params.gL, params.eL,
                                 params.gAHP, params.tauCa, params.kCa, params.gAHPFloor,
                                 gAMPAEff, gNMDAEff, gGABAaEff, gGABAbEff,
                                 params.eGABAa, params.eNMDA, params.eAMPA);

    yv += (dtMs / 6.0f) * (k1.dv + 2.0f * k2.dv + 2.0f * k3.dv + k4.dv);
    ym += (dtMs / 6.0f) * (k1.dm + 2.0f * k2.dm + 2.0f * k3.dm + k4.dm);
    yh += (dtMs / 6.0f) * (k1.dh + 2.0f * k2.dh + 2.0f * k3.dh + k4.dh);
    yn += (dtMs / 6.0f) * (k1.dn + 2.0f * k2.dn + 2.0f * k3.dn + k4.dn);
    ys += (dtMs / 6.0f) * (k1.ds + 2.0f * k2.ds + 2.0f * k3.ds + k4.ds);
    yca += (dtMs / 6.0f) * (k1.dcaCa + 2.0f * k2.dcaCa + 2.0f * k3.dcaCa + k4.dcaCa);

    clampState(yv, ym, yh, yn, ys, yca);

    if (!isfinite(yv) || !isfinite(ym) || !isfinite(yh) || !isfinite(yn) || !isfinite(ys) || !isfinite(yca)) {
        yv = params.restingVoltage;
        const float am = alphaM(params.restingVoltage);
        const float bm = betaM(params.restingVoltage);
        const float ah = alphaH(params.restingVoltage);
        const float bh = betaH(params.restingVoltage);
        const float an = alphaN(params.restingVoltage);
        const float bn = betaN(params.restingVoltage);
        ym = am / (am + bm + 1.0e-6f);
        yh = ah / (ah + bh + 1.0e-6f);
        yn = an / (an + bn + 1.0e-6f);
        ys = sInf(params.restingVoltage);
        yca = 0.0f;
    }

    const bool refractory = (timeMs - lastSpikeTime[i]) < refractoryMs;
    const bool crossed = (oldV <= threshold[i]) && (yv > threshold[i]);
    const std::uint8_t didSpike = (!refractory && crossed) ? 1U : 0U;

    if (didSpike != 0U) {
        lastSpikeTime[i] = timeMs;
    }

    v[i] = yv;
    m[i] = ym;
    h[i] = yh;
    n[i] = yn;
    s[i] = ys;
    caCa[i] = yca;
    *spikeOut = didSpike;
}

__global__ void initCurandStatesKernel(std::size_t neuronCount, std::uint32_t seed, curandState* states) {
    const std::size_t i = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= neuronCount) {
        return;
    }
    curand_init(static_cast<unsigned long long>(seed) + static_cast<unsigned long long>(i), 0, 0, &states[i]);
}

__global__ void fusedBatchedStepKernel(
    BatchedStepLaunchInfo launchInfo,
    BatchedStepDevicePointers devicePointers
) {
    const std::size_t i = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= launchInfo.neuronCount) {
        return;
    }

    curandState* rngStates = static_cast<curandState*>(devicePointers.rngStates);
    curandState localState = rngStates[i];

    const std::size_t newHead = (launchInfo.oldHead + 1) % launchInfo.delaySteps;
    const std::size_t writeIndex = launchInfo.batchStepIndex * launchInfo.neuronCount + i;

    float iExcPulse = 0.0f;
    float iInhPulse = 0.0f;

    const std::uint32_t begin = devicePointers.incomingOffsets[i];
    const std::uint32_t end = devicePointers.incomingOffsets[i + 1];
    for (std::uint32_t idx = begin; idx < end; ++idx) {
        const std::size_t row = rowIndexForDelay(launchInfo.oldHead, launchInfo.delaySteps, devicePointers.incomingDelay[idx]);
        const std::size_t pre = static_cast<std::size_t>(devicePointers.incomingPre[idx]);
        const std::size_t spikeIndex = row * launchInfo.neuronCount + pre;
        const std::uint8_t spike = devicePointers.delayBuffer[spikeIndex];
        if (spike == 0U) {
            continue;
        }

        // Phase 3c: vesicle pool release scale, 1.0 (no-op) unless
        // launchInfo.vesiclePoolEnabled -- mirrors Synapse.cpp's
        // accumulateReceptorConductances multiplying incomingWeight_[idx]
        // by delayBuffer.getDelayedReleaseScale(...) exactly. Same ring
        // buffer indexing (spikeIndex) as the spike bit above, since both
        // represent the same (presynaptic neuron, delayed step) event.
        const float releaseScale = devicePointers.releaseScale[spikeIndex];

        if (devicePointers.incomingSign[idx] > 0) {
            iExcPulse += devicePointers.incomingWeight[idx] * releaseScale;
        } else {
            iInhPulse += devicePointers.incomingWeight[idx] * releaseScale;
        }
    }

    float excState = devicePointers.iExcState[i];
    float inhState = devicePointers.iInhState[i];
    float adapt = devicePointers.adaptationCurrent[i];

    const float synExcDecay = expf(-launchInfo.dtMs / fmaxf(1.0f, launchInfo.synTauExcMs));
    const float synInhDecay = expf(-launchInfo.dtMs / fmaxf(1.0f, launchInfo.synTauInhMs));
    const float adaptationDecay = expf(-launchInfo.dtMs / fmaxf(1.0f, launchInfo.adaptationTauMs));

    adapt = fminf(launchInfo.adaptationMaxCurrent, fmaxf(0.0f, adapt * adaptationDecay));

    const float excAccum = excState * synExcDecay + iExcPulse;
    const float inhAccum = inhState * synInhDecay + iInhPulse;
    excState = fminf(launchInfo.maxSynCurrent, fmaxf(0.0f, excAccum));
    inhState = fminf(launchInfo.maxSynCurrent, fmaxf(0.0f, inhAccum));

    float iSyn = excState - inhState;
    if (!isfinite(iSyn)) {
        iSyn = 0.0f;
    } else {
        iSyn = fminf(launchInfo.maxSynCurrent, fmaxf(-launchInfo.maxSynCurrent, iSyn));
    }

    float iNoise = curand_normal(&localState) * devicePointers.noiseStd[i];
    if (!isfinite(iNoise)) {
        iNoise = 0.0f;
    }

    const float extWithAdapt = devicePointers.extCurrentWithBackground[i] - adapt;
    const float iExt = isfinite(extWithAdapt) ? extWithAdapt : 0.0f;

    const float safeGNa = (isfinite(devicePointers.baseGNa[i]) && devicePointers.baseGNa[i] > 0.0f) ? devicePointers.baseGNa[i] : 0.0f;
    const float safeGK = (isfinite(devicePointers.baseGK[i]) && devicePointers.baseGK[i] > 0.0f) ? devicePointers.baseGK[i] : 0.0f;
    const float safeGCa = (isfinite(devicePointers.baseGCa[i]) && devicePointers.baseGCa[i] > 0.0f) ? devicePointers.baseGCa[i] : 0.0f;

    const float* drug = &devicePointers.drugParams[i * 7U];
    const float dose = drug[6] * launchInfo.doseScale;
    const float blockNa = hillBlockDevice(dose, drug[0], drug[3]);
    const float blockK = hillBlockDevice(dose, drug[1], drug[4]);
    const float blockCa = hillBlockDevice(dose, drug[2], drug[5]);

    // SYNC WARNING: 0.05f (Na) / 0.02f (Ca) here, and the gKEff 0.05f floor
    // a little further down in this same kernel, are hand-duplicated copies
    // of DrugModel::kNaConductanceFloor / kKConductanceFloor /
    // kCaConductanceFloor and the conductanceFloor() formula in
    // engine/drug/DrugModel.h/.cpp. This kernel is compiled by nvcc in its
    // own translation unit and cannot call that C++ function directly, so
    // the math is re-typed here by hand instead. GPU is this project's
    // default compute backend -- if you change the floor constants or
    // formula on the host side and forget this copy, GPU runs will silently
    // keep the OLD behavior while CPU runs pick up the new one. This exact
    // silent divergence cost real debugging time during the Gap 1.2
    // Ca-blocker calibration investigation (two rounds of "confirmed
    // rebuild, still bit-for-bit identical output" before this independent
    // copy was found) -- see PRECISION_GAP_CLOSURE_PLAN.md. If you touch
    // either side, touch both.
    const float gNaEff = fmaxf(0.05f * safeGNa, safeGNa * fmaxf(0.0f, 1.0f - blockNa));
    const float gCaEff = fmaxf(0.02f * safeGCa, safeGCa * fmaxf(0.0f, 1.0f - blockCa));

    // Phase 3c: vesicle pool continuous refill -- every thread, every step,
    // regardless of whether it spikes this step, mirrors
    // synapse::refillVesiclePools exactly (bounded relaxation toward each
    // pool's own fresh size, RRP's rate additionally scaled by Reserve's
    // fractional availability). No-op when vesiclePoolEnabled is false.
    float vesicleRrp = devicePointers.vesicleRrp[i];
    float vesicleReserve = devicePointers.vesicleReserve[i];
    if (launchInfo.vesiclePoolEnabled != 0) {
        const float reserveTau = fmaxf(1.0f, launchInfo.vesiclePoolReserveRefillTauMs);
        vesicleReserve += (launchInfo.dtMs / reserveTau) * (launchInfo.vesiclePoolReserveSize - vesicleReserve);
        vesicleReserve = clamp01(vesicleReserve / fmaxf(1.0e-6f, launchInfo.vesiclePoolReserveSize)) * launchInfo.vesiclePoolReserveSize;

        const float rrpTau = fmaxf(1.0f, launchInfo.vesiclePoolRrpRefillTauMs);
        const float reserveAvailability = (launchInfo.vesiclePoolReserveSize > 1.0e-6f)
            ? clamp01(vesicleReserve / launchInfo.vesiclePoolReserveSize)
            : 0.0f;
        vesicleRrp += (launchInfo.dtMs / rrpTau) * (launchInfo.vesiclePoolRrpSize - vesicleRrp) * reserveAvailability;
        vesicleRrp = clamp01(vesicleRrp / fmaxf(1.0e-6f, launchInfo.vesiclePoolRrpSize)) * launchInfo.vesiclePoolRrpSize;

        devicePointers.vesicleReserve[i] = vesicleReserve;
        devicePointers.vesicleRrp[i] = vesicleRrp;
    }

    // Phase 3c: neuromodulator gain (D1/D2/5-HT1A/5-HT2A) -- mirrors
    // DrugModel::computeNeuromodulatorGainModifiers (CPU,
    // NeuromodulatorSystem.cpp) exactly, computed per-thread from this
    // neuron's own `dose` (already available above for channel block).
    // All four scale factors default to 1.0 when unconfigured (ec50=1e9,
    // ceilings inert), same bit-identical-no-op-by-construction discipline
    // as every other Phase 1/2/3 mechanism.
    float nmodGKEffScale = 1.0f;
    float nmodGMaxNmdaScale = 1.0f;
    float nmodAdaptationScale = 1.0f;
    float nmodExcitatoryWeightScale = 1.0f;
    {
        // Phase 3c retrofit: SERT/DAT reuptake block dose-amplification --
        // mirrors DrugModel::amplifiedDoseForDopamine/amplifiedDoseForSerotonin
        // (CPU) exactly. DAT amplifies the dose D1/D2 see; SERT amplifies
        // the dose 5-HT1A/5-HT2A see. Both default to the raw `dose`
        // unchanged when unconfigured (mechanism=None), same no-op-by-
        // construction discipline as gat1Occupancy above.
        float doseForDopamine = dose;
        if (launchInfo.datMechanism != 0) {
            const float datOcc = hillBlockDevice(dose, launchInfo.datKiUm, launchInfo.datHill);
            const float datCeiling = fmaxf(1.0f, launchInfo.datMaxExtensionFold);
            doseForDopamine = dose * (1.0f + (datCeiling - 1.0f) * datOcc);
        }
        float doseForSerotonin = dose;
        if (launchInfo.sertMechanism != 0) {
            const float sertOcc = hillBlockDevice(dose, launchInfo.sertKiUm, launchInfo.sertHill);
            const float sertCeiling = fmaxf(1.0f, launchInfo.sertMaxExtensionFold);
            doseForSerotonin = dose * (1.0f + (sertCeiling - 1.0f) * sertOcc);
        }

        // D1: shrinks adaptation current, boosts NMDA gain.
        const float d1Occ = hillBlockDevice(doseForDopamine, launchInfo.d1Ec50, launchInfo.d1Hill);
        const float d1AdaptFrac = clamp01(launchInfo.d1MaxAdaptationReductionFrac);
        const float d1NmdaCeiling = fmaxf(1.0f, launchInfo.d1MaxNmdaGainFold);
        nmodAdaptationScale *= (1.0f - d1AdaptFrac * d1Occ);
        nmodGMaxNmdaScale   *= (1.0f + (d1NmdaCeiling - 1.0f) * d1Occ);

        // D2: shrinks excitatory synaptic weight (release-probability proxy).
        const float d2Occ = hillBlockDevice(doseForDopamine, launchInfo.d2Ec50, launchInfo.d2Hill);
        const float d2ReleaseFrac = clamp01(launchInfo.d2MaxReleaseReductionFrac);
        nmodExcitatoryWeightScale *= (1.0f - d2ReleaseFrac * d2Occ);

        // 5-HT1A: boosts gKEff (GIRK-mediated hyperpolarization).
        const float ht1aOcc = hillBlockDevice(doseForSerotonin, launchInfo.ht1aEc50, launchInfo.ht1aHill);
        const float ht1aKCeiling = fmaxf(1.0f, launchInfo.ht1aMaxKGainFold);
        nmodGKEffScale *= (1.0f + (ht1aKCeiling - 1.0f) * ht1aOcc);

        // 5-HT2A: shrinks gKEff (reduced K+ leak) AND shrinks adaptation
        // (reduced IAHP).
        const float ht2aOcc = hillBlockDevice(doseForSerotonin, launchInfo.ht2aEc50, launchInfo.ht2aHill);
        const float ht2aKReductionFrac = clamp01(launchInfo.ht2aMaxKReductionFrac);
        const float ht2aAdaptFrac = clamp01(launchInfo.ht2aMaxAdaptationReductionFrac);
        nmodGKEffScale      *= (1.0f - ht2aKReductionFrac * ht2aOcc);
        nmodAdaptationScale *= (1.0f - ht2aAdaptFrac * ht2aOcc);
    }

    // 0.05f floor here is the K counterpart to the SYNC WARNING above this
    // function's gNaEff/gCaEff lines -- same DrugModel::kKConductanceFloor
    // duplication, same rule: keep in sync with engine/drug/DrugModel.h.
    const float gKEff = fmaxf(0.05f * safeGK, safeGK * (1.0f - blockK)) * nmodGKEffScale;

    // PHASE2_PLAN.md step 6: per-receptor conductance accumulation, mirrors
    // Synapse.cpp::accumulateReceptorConductances exactly -- same "decay
    // then impulse" discretization and peak-normalized dual-exp kernel,
    // driven by the same excInput/inhInput (here iExcPulse/iInhPulse) this
    // thread already computed above for the now-retired flat iSyn path.
    // Tau constants come directly from ReceptorKinetics (constexpr, usable
    // in device code as-is -- no host/device duplication needed).
    using namespace spp::neuron::ReceptorKinetics;

    // Phase 3a: GAT1 reuptake block (tiagabine) -- extends GABA-A's and
    // GABA-B's decay time constant by a dose-dependent bounded fold, same
    // Hill-occupancy math as the channel/receptor mechanisms below, just
    // applied to a kinetic time constant instead of a conductance. Mirrors
    // DrugModel::computeReceptorKineticsModifiers (CPU) exactly. No
    // per-neuron override array needed here (unlike the CPU
    // ReceptorKineticsOverride) -- dose is already per-neuron via drug[6],
    // and gat1KiUm/gat1Hill/gat1MaxExtensionFold are shared scalars for the
    // whole batch (one compound under test), so each thread just computes
    // its own effective tau inline from its own dose. Rise kinetics are
    // never drug-modified (see ReuptakeTransporter.h design note).
    float gabaATauDecayEff = kGabaATauDecayMs;
    float gabaBTauDecayEff = kGabaBTauDecayMs;
    if (launchInfo.gat1Mechanism != 0) {
        const float gat1Occupancy = hillBlockDevice(dose, launchInfo.gat1KiUm, launchInfo.gat1Hill);
        const float gat1Ceiling = fmaxf(1.0f, launchInfo.gat1MaxExtensionFold);
        const float gat1Fold = 1.0f + (gat1Ceiling - 1.0f) * gat1Occupancy;
        gabaATauDecayEff = kGabaATauDecayMs * gat1Fold;
        gabaBTauDecayEff = kGabaBTauDecayMs * gat1Fold;
    }

    const float ampaDecayF  = expf(-launchInfo.dtMs / kAmpaTauDecayMs);
    const float ampaRiseF   = expf(-launchInfo.dtMs / kAmpaTauRiseMs);
    const float nmdaDecayF  = expf(-launchInfo.dtMs / kNmdaTauDecayMs);
    const float nmdaRiseF   = expf(-launchInfo.dtMs / kNmdaTauRiseMs);
    const float gabaADecayF = expf(-launchInfo.dtMs / gabaATauDecayEff);
    const float gabaARiseF  = expf(-launchInfo.dtMs / kGabaATauRiseMs);
    const float gabaBDecayF = expf(-launchInfo.dtMs / gabaBTauDecayEff);
    const float gabaBRiseF  = expf(-launchInfo.dtMs / kGabaBTauRiseMs);

    const float ampaNorm  = dualExpNormFactorDevice(kAmpaTauRiseMs, kAmpaTauDecayMs);
    const float nmdaNorm  = dualExpNormFactorDevice(kNmdaTauRiseMs, kNmdaTauDecayMs);
    const float gabaANorm = dualExpNormFactorDevice(kGabaATauRiseMs, gabaATauDecayEff);
    const float gabaBNorm = dualExpNormFactorDevice(kGabaBTauRiseMs, gabaBTauDecayEff);

    const float ampaDecayAcc  = devicePointers.receptorAmpaDecay[i]  * ampaDecayF  + iExcPulse;
    const float ampaRiseAcc   = devicePointers.receptorAmpaRise[i]   * ampaRiseF   + iExcPulse;
    const float nmdaDecayAcc  = devicePointers.receptorNmdaDecay[i]  * nmdaDecayF  + iExcPulse;
    const float nmdaRiseAcc   = devicePointers.receptorNmdaRise[i]   * nmdaRiseF   + iExcPulse;
    const float gabaADecayAcc = devicePointers.receptorGabaADecay[i] * gabaADecayF + iInhPulse;
    const float gabaARiseAcc  = devicePointers.receptorGabaARise[i]  * gabaARiseF  + iInhPulse;
    const float gabaBDecayAcc = devicePointers.receptorGabaBDecay[i] * gabaBDecayF + iInhPulse;
    const float gabaBRiseAcc  = devicePointers.receptorGabaBRise[i]  * gabaBRiseF  + iInhPulse;

    devicePointers.receptorAmpaDecay[i]  = ampaDecayAcc;
    devicePointers.receptorAmpaRise[i]   = ampaRiseAcc;
    devicePointers.receptorNmdaDecay[i]  = nmdaDecayAcc;
    devicePointers.receptorNmdaRise[i]   = nmdaRiseAcc;
    devicePointers.receptorGabaADecay[i] = gabaADecayAcc;
    devicePointers.receptorGabaARise[i]  = gabaARiseAcc;
    devicePointers.receptorGabaBDecay[i] = gabaBDecayAcc;
    devicePointers.receptorGabaBRise[i]  = gabaBRiseAcc;

    const float gAMPARaw  = fmaxf(0.0f, ampaNorm  * (ampaDecayAcc  - ampaRiseAcc));
    const float gNMDARaw  = fmaxf(0.0f, nmdaNorm  * (nmdaDecayAcc  - nmdaRiseAcc));
    const float gGABAaRaw = fmaxf(0.0f, gabaANorm * (gabaADecayAcc - gabaARiseAcc));
    const float gGABAbRaw = fmaxf(0.0f, gabaBNorm * (gabaBDecayAcc - gabaBRiseAcc));

    // Phase 3b: GABA-A desensitization -- mirrors Synapse.cpp's
    // accumulateReceptorConductances exactly (Euler step on a 0..1
    // "tiredness" state, driven by this thread's own raw GABA-A conductance
    // as the activation proxy). See Synapse.h's DesensitizationConfig for
    // the full design comment. Off by default (desensitizationEnabled ==
    // false) -- gGABAaFinal == gGABAaRaw unchanged, zero behavior change.
    float gGABAaFinal = gGABAaRaw;
    if (launchInfo.desensitizationEnabled) {
        const float kDesenseStep = launchInfo.dtMs / fmaxf(1.0f, launchInfo.desensitizationTauDesenseMs);
        const float kRecoverStep = launchInfo.dtMs / fmaxf(1.0f, launchInfo.desensitizationTauRecoveryMs);
        float d = devicePointers.gabaADesensitization[i];
        d += kDesenseStep * gGABAaRaw * (1.0f - d) - kRecoverStep * d;
        d = clamp01(d);
        devicePointers.gabaADesensitization[i] = d;
        gGABAaFinal = gGABAaRaw * (1.0f - launchInfo.desensitizationMaxAttenuation * d);
    }

    // Receptor drug profile (block/potentiate/agonist) -- mirrors
    // DrugModel::computeReceptorModifiers exactly. Single shared profile
    // for the whole batch, same `dose` already computed above for channel
    // block (both share the same doseScale onset timeline).
    float ampaResidual = 1.0f;
    if (launchInfo.ampaMechanism == 1) { // Block
        ampaResidual = fmaxf(0.0f, 1.0f - hillBlockDevice(dose, launchInfo.ampaEc50, launchInfo.ampaHill));
    }
    float nmdaResidual = 1.0f;
    if (launchInfo.nmdaMechanism == 1) { // Block
        nmdaResidual = fmaxf(0.0f, 1.0f - hillBlockDevice(dose, launchInfo.nmdaEc50, launchInfo.nmdaHill));
    }
    float gabaAPotentiation = 1.0f;
    if (launchInfo.gabaAMechanism == 2) { // Potentiate
        const float occupancy = hillBlockDevice(dose, launchInfo.gabaAEc50, launchInfo.gabaAHill);
        const float ceiling = isfinite(launchInfo.gabaAMaxPotentiation)
                                   ? fmaxf(1.0f, launchInfo.gabaAMaxPotentiation)
                                   : 1.0f;
        gabaAPotentiation = 1.0f + (ceiling - 1.0f) * occupancy;
    }
    float gabaBAgonistActivation = 0.0f;
    if (launchInfo.gabaBMechanism == 3) { // Agonist
        gabaBAgonistActivation = clamp01(hillBlockDevice(dose, launchInfo.gabaBEc50, launchInfo.gabaBHill));
    }

    // Peak-scale + drug-modify, same formulas as BatchedSimulationEngine.cpp's
    // CPU fallback loop (synapticEff[i].gXEff assignments).
    const float gAMPAEff = fminf(
        launchInfo.ampaConductanceCeiling,
        fmaxf(0.0f, launchInfo.gMaxAMPA * gAMPARaw * ampaResidual * nmodExcitatoryWeightScale)
    );
    const float gNMDAEff = launchInfo.gMaxNMDA * nmodGMaxNmdaScale * gNMDARaw * nmdaResidual * nmodExcitatoryWeightScale;
    const float gGABAaEff = launchInfo.gMaxGABAa * gGABAaFinal * gabaAPotentiation;
    const float gGABAbEff = launchInfo.gMaxGABAb * gGABAbRaw +
                             launchInfo.gMaxGABAbAgonist * gabaBAgonistActivation;

    std::uint8_t spike = 0U;
    integrateNeuronState(
        i,
        launchInfo.timeMs,
        launchInfo.dtMs,
        launchInfo.refractoryMs,
        launchInfo.maxTotalCurrent,
        launchInfo.params,
        devicePointers.threshold,
        // CPU parity: iSyn is always 0.0f now that every synaptic current
        // flows through the true-conductance receptor path above -- see
        // BatchedSimulationEngine.cpp's CPU loop, where the `iSyn` vector
        // is likewise fixed at 0.0f for the same reason. excState/inhState
        // above are still decayed/stored (harmless, avoids restructuring
        // buffer allocation) but are no longer read into the current sum.
        0.0f,
        iExt,
        iNoise,
        gNaEff,
        gKEff,
        gCaEff,
        gAMPAEff,
        gNMDAEff,
        gGABAaEff,
        gGABAbEff,
        devicePointers.v,
        devicePointers.m,
        devicePointers.h,
        devicePointers.n,
        devicePointers.s,
        devicePointers.caCa,
        devicePointers.lastSpikeTime,
        &spike
    );

    if (spike != 0U) {
        const float adaptStep = ((devicePointers.neuronType[i] == 1U)
                                    ? launchInfo.adaptationIncrement
                                    : launchInfo.adaptationIncrement * launchInfo.adaptationInhibitoryScale)
                                    * nmodAdaptationScale;
        adapt = fminf(launchInfo.adaptationMaxCurrent, fmaxf(0.0f, adapt + adaptStep));
    }

    // Phase 3c: spike-triggered vesicle release -- mirrors
    // synapse::triggerVesicleRelease exactly, using this thread's own gCaEff
    // (already computed above) as the Ca-dependent release-probability
    // drive proxy, and this thread's own curandState (already loaded as
    // localState, consumed here BEFORE the rngStates[i]=localState write-
    // back below so the draw is properly seeded/advanced per thread per
    // step). Returns 1.0 (no-op) whenever vesiclePoolEnabled is false or
    // this neuron didn't spike -- see NeurotransmitterPool.h's baseline-
    // preservation note (a fresh/undepleted pool returns ~1.0 in
    // expectation, so every already-calibrated conductance ceiling keeps
    // working unchanged the instant a run starts).
    float releaseScaleOut = 1.0f;
    if (launchInfo.vesiclePoolEnabled != 0 && spike != 0U) {
        const float drive = fmaxf(0.0f, gCaEff);
        const float releaseProb = clamp01(1.0f - expf(-fmaxf(0.0f, launchInfo.vesiclePoolCalciumFactor) * drive));
        const float expectedFreshRelease = releaseProb * launchInfo.vesiclePoolRrpSize;

        const int nAvailable = static_cast<int>(lroundf(fmaxf(0.0f, vesicleRrp)));
        int released = 0;
        if (nAvailable > 0 && releaseProb > 0.0f) {
            released = binomialDrawDevice(nAvailable, releaseProb, &localState);
        }
        vesicleRrp = fminf(launchInfo.vesiclePoolRrpSize, fmaxf(0.0f, vesicleRrp - static_cast<float>(released)));
        devicePointers.vesicleRrp[i] = vesicleRrp;

        releaseScaleOut = (expectedFreshRelease > 1.0e-6f)
            ? (static_cast<float>(released) / expectedFreshRelease)
            : 1.0f;
    }

    devicePointers.iExcState[i] = excState;
    devicePointers.iInhState[i] = inhState;
    devicePointers.adaptationCurrent[i] = adapt;
    devicePointers.delayBuffer[newHead * launchInfo.neuronCount + i] = spike;
    devicePointers.releaseScale[newHead * launchInfo.neuronCount + i] = releaseScaleOut;
    devicePointers.spikeHistory[writeIndex] = spike;

    rngStates[i] = localState;
}

__global__ void hhStepKernel(
    std::size_t neuronCount,
    float timeMs,
    float dtMs,
    float cm,
    float eNa,
    float eK,
    float eCa,
    float gL,
    float eL,
    float gAHP,
    float tauCa,
    float kCa,
    float gAHPFloor,
    float restV,
    float refractoryMs,
    const float* iSyn,
    const float* iExt,
    const float* iNoise,
    const float* gNaEff,
    const float* gKEff,
    const float* gCaEff,
    const float* threshold,
    float* v,
    float* m,
    float* h,
    float* n,
    float* s,
    float* caCa,
    float* lastSpikeTime,
    std::uint8_t* spikes
) {
    const std::size_t i = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= neuronCount) {
        return;
    }

    const float oldV = v[i];

    float yv = oldV;
    float ym = m[i];
    float yh = h[i];
    float yn = n[i];
    float ys = s[i];
    float yca = caCa[i];

    float iTotal = iSyn[i] + iExt[i] + iNoise[i];
    if (!isfinite(iTotal)) {
        iTotal = 0.0f;
    }

    const float safeGNa = (isfinite(gNaEff[i]) && gNaEff[i] >= 0.0f) ? gNaEff[i] : 0.0f;
    const float safeGK = (isfinite(gKEff[i]) && gKEff[i] >= 0.0f) ? gKEff[i] : 0.0f;
    const float safeGCa = (isfinite(gCaEff[i]) && gCaEff[i] >= 0.0f) ? gCaEff[i] : 0.0f;

    // NOTE: hhStepKernel (the non-batched single-run GPU step, used only by
    // SimulationEngine's plain --simulate GPU mode, not by --dose-eval)
    // is intentionally left without receptor support as part of the
    // PHASE2_PLAN.md step 6 scope decision -- see NeuronUpdate.cu's top
    // comment / the CudaSimulator.h step() doc. Passing 0.0f for all four
    // gXEff keeps this compiling against derivatives()'s new signature
    // with receptor currents evaluating to exactly zero, i.e. unchanged
    // pre-Phase-2 behavior for this path.
    const Deriv k1 = derivatives(yv, ym, yh, yn, ys, yca, iTotal, safeGNa, safeGK, safeGCa, cm, eNa, eK, eCa, gL, eL, gAHP, tauCa, kCa, gAHPFloor, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    float y2v = yv + 0.5f * dtMs * k1.dv;
    float y2m = ym + 0.5f * dtMs * k1.dm;
    float y2h = yh + 0.5f * dtMs * k1.dh;
    float y2n = yn + 0.5f * dtMs * k1.dn;
    float y2s = ys + 0.5f * dtMs * k1.ds;
    float y2ca = yca + 0.5f * dtMs * k1.dcaCa;
    clampState(y2v, y2m, y2h, y2n, y2s, y2ca);

    const Deriv k2 = derivatives(y2v, y2m, y2h, y2n, y2s, y2ca, iTotal, safeGNa, safeGK, safeGCa, cm, eNa, eK, eCa, gL, eL, gAHP, tauCa, kCa, gAHPFloor, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    float y3v = yv + 0.5f * dtMs * k2.dv;
    float y3m = ym + 0.5f * dtMs * k2.dm;
    float y3h = yh + 0.5f * dtMs * k2.dh;
    float y3n = yn + 0.5f * dtMs * k2.dn;
    float y3s = ys + 0.5f * dtMs * k2.ds;
    float y3ca = yca + 0.5f * dtMs * k2.dcaCa;
    clampState(y3v, y3m, y3h, y3n, y3s, y3ca);

    const Deriv k3 = derivatives(y3v, y3m, y3h, y3n, y3s, y3ca, iTotal, safeGNa, safeGK, safeGCa, cm, eNa, eK, eCa, gL, eL, gAHP, tauCa, kCa, gAHPFloor, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    float y4v = yv + dtMs * k3.dv;
    float y4m = ym + dtMs * k3.dm;
    float y4h = yh + dtMs * k3.dh;
    float y4n = yn + dtMs * k3.dn;
    float y4s = ys + dtMs * k3.ds;
    float y4ca = yca + dtMs * k3.dcaCa;
    clampState(y4v, y4m, y4h, y4n, y4s, y4ca);

    const Deriv k4 = derivatives(y4v, y4m, y4h, y4n, y4s, y4ca, iTotal, safeGNa, safeGK, safeGCa, cm, eNa, eK, eCa, gL, eL, gAHP, tauCa, kCa, gAHPFloor, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    yv += (dtMs / 6.0f) * (k1.dv + 2.0f * k2.dv + 2.0f * k3.dv + k4.dv);
    ym += (dtMs / 6.0f) * (k1.dm + 2.0f * k2.dm + 2.0f * k3.dm + k4.dm);
    yh += (dtMs / 6.0f) * (k1.dh + 2.0f * k2.dh + 2.0f * k3.dh + k4.dh);
    yn += (dtMs / 6.0f) * (k1.dn + 2.0f * k2.dn + 2.0f * k3.dn + k4.dn);
    ys += (dtMs / 6.0f) * (k1.ds + 2.0f * k2.ds + 2.0f * k3.ds + k4.ds);
    yca += (dtMs / 6.0f) * (k1.dcaCa + 2.0f * k2.dcaCa + 2.0f * k3.dcaCa + k4.dcaCa);

    clampState(yv, ym, yh, yn, ys, yca);

    if (!isfinite(yv) || !isfinite(ym) || !isfinite(yh) || !isfinite(yn) || !isfinite(ys) || !isfinite(yca)) {
        yv = restV;
        const float am = alphaM(restV);
        const float bm = betaM(restV);
        const float ah = alphaH(restV);
        const float bh = betaH(restV);
        const float an = alphaN(restV);
        const float bn = betaN(restV);
        ym = am / (am + bm + 1.0e-6f);
        yh = ah / (ah + bh + 1.0e-6f);
        yn = an / (an + bn + 1.0e-6f);
        ys = sInf(restV);
        yca = 0.0f;
    }

    const bool refractory = (timeMs - lastSpikeTime[i]) < refractoryMs;
    const bool crossed = (oldV <= threshold[i]) && (yv > threshold[i]);
    const std::uint8_t didSpike = (!refractory && crossed) ? 1U : 0U;

    if (didSpike != 0U) {
        lastSpikeTime[i] = timeMs;
    }

    v[i] = yv;
    m[i] = ym;
    h[i] = yh;
    n[i] = yn;
    s[i] = ys;
    caCa[i] = yca;
    spikes[i] = didSpike;
}

} // namespace

NeuronUpdateLaunchInfo computeNeuronUpdateLaunchInfo(
    std::size_t neuronCount,
    int preferredBlockSize
) {
    const int safeBlockSize = (preferredBlockSize > 0) ? preferredBlockSize : 256;
    const int gridSize = static_cast<int>((neuronCount + static_cast<std::size_t>(safeBlockSize) - 1) /
                                          static_cast<std::size_t>(safeBlockSize));
    return NeuronUpdateLaunchInfo{safeBlockSize, gridSize};
}

void initializeBatchedRandomStates(
    std::size_t neuronCount,
    std::uint32_t seed,
    void* rngStates
) {
    const NeuronUpdateLaunchInfo launchInfo = computeNeuronUpdateLaunchInfo(neuronCount, 256);
    initCurandStatesKernel<<<launchInfo.gridSize, launchInfo.blockSize>>>(
        neuronCount,
        seed,
        static_cast<curandState*>(rngStates)
    );
}

void launchBatchedStepKernel(
    const BatchedStepLaunchInfo& launchInfo,
    const BatchedStepDevicePointers& devicePointers
) {
    const NeuronUpdateLaunchInfo stepLaunch = computeNeuronUpdateLaunchInfo(launchInfo.neuronCount, 256);
    fusedBatchedStepKernel<<<stepLaunch.gridSize, stepLaunch.blockSize>>>(launchInfo, devicePointers);
}

// Number of per-step input arrays packed into one contiguous transfer:
// iSyn, iExt, iNoise, gNaEff, gKEff, gCaEff (in this order).
constexpr int kPackedInputCount = 6;

struct CudaSimulator::DeviceBuffers {
    float* v = nullptr;
    float* m = nullptr;
    float* h = nullptr;
    float* n = nullptr;
    float* s = nullptr;
    float* caCa = nullptr;

    float* threshold = nullptr;
    float* lastSpikeTime = nullptr;

    // One contiguous device allocation holding all six per-step input
    // arrays back-to-back. Sub-pointers below point *into* this block at
    // fixed offsets, so the kernel signature (six separate pointers) does
    // not need to change even though the transfer is now a single call.
    float* inputBlock = nullptr;
    float* iSyn = nullptr;
    float* iExt = nullptr;
    float* iNoise = nullptr;
    float* gNaEff = nullptr;
    float* gKEff = nullptr;
    float* gCaEff = nullptr;

    std::uint8_t* spikes = nullptr;

    // Batched resident state for the fused GPU path.
    std::uint32_t* incomingOffsets = nullptr;
    std::uint32_t* incomingPre = nullptr;
    std::uint32_t* incomingDelay = nullptr;
    float* incomingWeight = nullptr;
    std::int8_t* incomingSign = nullptr;

    std::uint8_t* batchedDelayBuffer = nullptr;
    float* batchedIExcState = nullptr;
    float* batchedIInhState = nullptr;
    float* batchedAdaptationCurrent = nullptr;
    float* batchedExtCurrentWithBackground = nullptr;
    float* batchedNoiseStd = nullptr;
    float* batchedBaseGNa = nullptr;
    float* batchedBaseGK = nullptr;
    float* batchedBaseGCa = nullptr;
    std::uint8_t* batchedNeuronType = nullptr;
    float* batchedDrugParams = nullptr;
    curandState* batchedRngStates = nullptr;
    std::uint8_t* batchedSpikeHistory = nullptr;

    // PHASE2_PLAN.md step 6: persistent per-neuron receptor conductance
    // state, one contiguous 8*neuronCount_ allocation (same packing style
    // as inputBlock above) with eight sub-pointers into it, mirroring
    // synapse::ReceptorConductanceState's eight fields exactly.
    float* batchedReceptorState = nullptr;
    float* batchedReceptorAmpaDecay = nullptr;
    float* batchedReceptorAmpaRise = nullptr;
    float* batchedReceptorNmdaDecay = nullptr;
    float* batchedReceptorNmdaRise = nullptr;
    float* batchedReceptorGabaADecay = nullptr;
    float* batchedReceptorGabaARise = nullptr;
    float* batchedReceptorGabaBDecay = nullptr;
    float* batchedReceptorGabaBRise = nullptr;

    // Receptor peak-conductance scales + flattened drug profile, stored
    // here (same pattern as batchedRefractoryMs etc. below) so stepBatched
    // can populate BatchedStepLaunchInfo's new fields each call without
    // needing them threaded through its own parameter list.
    float batchedGMaxAMPA = 0.0f;
    float batchedGMaxNMDA = 0.0f;
    float batchedGMaxGABAa = 0.0f;
    float batchedGMaxGABAb = 0.0f;
    float batchedGMaxGABAbAgonist = 0.0f;
    float batchedAmpaConductanceCeiling = 1.0f;
    int batchedAmpaMechanism = 0;
    float batchedAmpaEc50 = 1.0e9f;
    float batchedAmpaHill = 1.0f;
    int batchedNmdaMechanism = 0;
    float batchedNmdaEc50 = 1.0e9f;
    float batchedNmdaHill = 1.0f;
    int batchedGabaAMechanism = 0;
    float batchedGabaAEc50 = 1.0e9f;
    float batchedGabaAHill = 1.0f;
    float batchedGabaAMaxPotentiation = 1.0f;
    int batchedGabaBMechanism = 0;
    float batchedGabaBEc50 = 1.0e9f;
    float batchedGabaBHill = 1.0f;

    // Phase 3a: GAT1 reuptake block -- same storage pattern as the
    // receptor fields above.
    int batchedGat1Mechanism = 0;
    float batchedGat1KiUm = 1.0e9f;
    float batchedGat1Hill = 1.0f;
    float batchedGat1MaxExtensionFold = 1.0f;

    // Phase 3b: GABA-A desensitization -- persistent per-neuron 0..1 state
    // (separate single-float allocation, see NeuronUpdate.h's
    // BatchedStepDevicePointers comment) plus the shared-scalar config,
    // same storage pattern as the GAT1 fields above.
    float* batchedGabaADesensitization = nullptr;
    bool batchedDesensitizationEnabled = false;
    float batchedDesensitizationTauDesenseMs = 30000.0f;
    float batchedDesensitizationTauRecoveryMs = 124000.0f;
    float batchedDesensitizationMaxAttenuation = 0.9f;

    // Phase 3c: neuromodulator gain -- stateless (no persistent per-neuron
    // buffer, unlike desensitization above), same shared-scalar storage
    // pattern as the GAT1 fields.
    float batchedD1Ec50 = 1.0e9f;
    float batchedD1Hill = 1.0f;
    float batchedD1MaxAdaptationReductionFrac = 0.0f;
    float batchedD1MaxNmdaGainFold = 1.0f;
    float batchedD2Ec50 = 1.0e9f;
    float batchedD2Hill = 1.0f;
    float batchedD2MaxReleaseReductionFrac = 0.0f;
    float batchedHt1aEc50 = 1.0e9f;
    float batchedHt1aHill = 1.0f;
    float batchedHt1aMaxKGainFold = 1.0f;
    float batchedHt2aEc50 = 1.0e9f;
    float batchedHt2aHill = 1.0f;
    float batchedHt2aMaxKReductionFrac = 0.0f;
    float batchedHt2aMaxAdaptationReductionFrac = 0.0f;

    // Phase 3c retrofit: SERT/DAT reuptake block dose-amplification --
    // stateless, same shared-scalar storage pattern as the neuromodulator
    // fields above.
    int batchedSertMechanism = 0;
    float batchedSertKiUm = 1.0e9f;
    float batchedSertHill = 1.0f;
    float batchedSertMaxExtensionFold = 1.0f;
    int batchedDatMechanism = 0;
    float batchedDatKiUm = 1.0e9f;
    float batchedDatHill = 1.0f;
    float batchedDatMaxExtensionFold = 1.0f;

    // Phase 3c: vesicle pool dynamics -- releaseScale is a parallel ring
    // buffer to batchedDelayBuffer (same neuronCount_*delaySteps_ shape);
    // vesicleRrp/vesicleReserve are persistent per-neuron state, same
    // storage pattern as batchedGabaADesensitization above.
    float* batchedReleaseScale = nullptr;
    float* batchedVesicleRrp = nullptr;
    float* batchedVesicleReserve = nullptr;
    int batchedVesiclePoolEnabled = 0;
    float batchedVesiclePoolRrpSize = 10.0f;
    float batchedVesiclePoolReserveSize = 100.0f;
    float batchedVesiclePoolRrpRefillTauMs = 1500.0f;
    float batchedVesiclePoolReserveRefillTauMs = 20000.0f;
    float batchedVesiclePoolCalciumFactor = 1.0f;

    std::size_t batchedDelaySteps = 0;
    std::uint32_t batchedDelayHead = 0;
    std::size_t batchedBatchWindowSteps = 0;

    float batchedRefractoryMs = 0.0f;
    float batchedDtMs = 0.0f;
    float batchedSynTauExcMs = 0.0f;
    float batchedSynTauInhMs = 0.0f;
    float batchedMaxSynCurrent = 0.0f;
    float batchedMaxTotalCurrent = 0.0f;
    float batchedAdaptationTauMs = 0.0f;
    float batchedAdaptationIncrement = 0.0f;
    float batchedAdaptationMaxCurrent = 0.0f;
    float batchedAdaptationInhibitoryScale = 0.0f;
    neuron::HHParameters batchedParams;

    // Pinned (page-locked) host staging buffers. Packing happens into
    // these on the host (cheap memcpy) before a single H2D transfer, and
    // spikes are copied out via these before/after a single D2H transfer.
    // Page-locked memory avoids the driver's internal staging copy that
    // pageable std::vector-backed memory requires on every transfer.
    float* pinnedInput = nullptr;
    std::uint8_t* pinnedSpikes = nullptr;
};

CudaSimulator::CudaSimulator(std::size_t neuronCount)
    : neuronCount_(neuronCount), available_(false), buffers_(nullptr) {
    int deviceCount = 0;
    if (cudaGetDeviceCount(&deviceCount) != cudaSuccess || deviceCount <= 0) {
        return;
    }

    if (neuronCount_ == 0) {
        return;
    }

    buffers_ = new DeviceBuffers();

    try {
        const std::size_t floatBytes = neuronCount_ * sizeof(float);
        const std::size_t spikeBytes = neuronCount_ * sizeof(std::uint8_t);
        const std::size_t inputBlockBytes = floatBytes * static_cast<std::size_t>(kPackedInputCount);

        checkCuda(cudaMalloc(&buffers_->v, floatBytes), "cudaMalloc v");
        checkCuda(cudaMalloc(&buffers_->m, floatBytes), "cudaMalloc m");
        checkCuda(cudaMalloc(&buffers_->h, floatBytes), "cudaMalloc h");
        checkCuda(cudaMalloc(&buffers_->n, floatBytes), "cudaMalloc n");
        checkCuda(cudaMalloc(&buffers_->s, floatBytes), "cudaMalloc s");
        checkCuda(cudaMalloc(&buffers_->caCa, floatBytes), "cudaMalloc caCa");

        checkCuda(cudaMalloc(&buffers_->threshold, floatBytes), "cudaMalloc threshold");
        checkCuda(cudaMalloc(&buffers_->lastSpikeTime, floatBytes), "cudaMalloc lastSpikeTime");

        checkCuda(cudaMalloc(&buffers_->inputBlock, inputBlockBytes), "cudaMalloc inputBlock");
        buffers_->iSyn   = buffers_->inputBlock + 0 * neuronCount_;
        buffers_->iExt   = buffers_->inputBlock + 1 * neuronCount_;
        buffers_->iNoise = buffers_->inputBlock + 2 * neuronCount_;
        buffers_->gNaEff = buffers_->inputBlock + 3 * neuronCount_;
        buffers_->gKEff  = buffers_->inputBlock + 4 * neuronCount_;
        buffers_->gCaEff = buffers_->inputBlock + 5 * neuronCount_;

        checkCuda(cudaMalloc(&buffers_->spikes, spikeBytes), "cudaMalloc spikes");

        checkCuda(cudaHostAlloc(&buffers_->pinnedInput, inputBlockBytes, cudaHostAllocDefault),
                   "cudaHostAlloc pinnedInput");
        checkCuda(cudaHostAlloc(reinterpret_cast<void**>(&buffers_->pinnedSpikes), spikeBytes, cudaHostAllocDefault),
                   "cudaHostAlloc pinnedSpikes");

        available_ = true;
    } catch (...) {
        available_ = false;
        if (buffers_ != nullptr) {
            cudaFree(buffers_->v);
            cudaFree(buffers_->m);
            cudaFree(buffers_->h);
            cudaFree(buffers_->n);
            cudaFree(buffers_->s);
            cudaFree(buffers_->caCa);
            cudaFree(buffers_->threshold);
            cudaFree(buffers_->lastSpikeTime);
            cudaFree(buffers_->inputBlock);
            cudaFree(buffers_->spikes);
            cudaFree(buffers_->incomingOffsets);
            cudaFree(buffers_->incomingPre);
            cudaFree(buffers_->incomingDelay);
            cudaFree(buffers_->incomingWeight);
            cudaFree(buffers_->incomingSign);
            cudaFree(buffers_->batchedDelayBuffer);
            cudaFree(buffers_->batchedIExcState);
            cudaFree(buffers_->batchedIInhState);
            cudaFree(buffers_->batchedAdaptationCurrent);
            cudaFree(buffers_->batchedExtCurrentWithBackground);
            cudaFree(buffers_->batchedNoiseStd);
            cudaFree(buffers_->batchedBaseGNa);
            cudaFree(buffers_->batchedBaseGK);
            cudaFree(buffers_->batchedBaseGCa);
            cudaFree(buffers_->batchedNeuronType);
            cudaFree(buffers_->batchedDrugParams);
            cudaFree(buffers_->batchedRngStates);
            cudaFree(buffers_->batchedSpikeHistory);
            cudaFree(buffers_->batchedReceptorState);
            cudaFree(buffers_->batchedGabaADesensitization);
            cudaFree(buffers_->batchedReleaseScale);
            cudaFree(buffers_->batchedVesicleRrp);
            cudaFree(buffers_->batchedVesicleReserve);
            if (buffers_->pinnedInput != nullptr) cudaFreeHost(buffers_->pinnedInput);
            if (buffers_->pinnedSpikes != nullptr) cudaFreeHost(buffers_->pinnedSpikes);
            delete buffers_;
            buffers_ = nullptr;
        }
        throw;
    }
}

CudaSimulator::~CudaSimulator() {
    if (buffers_ == nullptr) {
        return;
    }

    cudaFree(buffers_->v);
    cudaFree(buffers_->m);
    cudaFree(buffers_->h);
    cudaFree(buffers_->n);
    cudaFree(buffers_->s);
    cudaFree(buffers_->caCa);

    cudaFree(buffers_->threshold);
    cudaFree(buffers_->lastSpikeTime);

    // iSyn/iExt/iNoise/gNaEff/gKEff/gCaEff are offsets into inputBlock,
    // not separate allocations — only inputBlock itself is freed.
    cudaFree(buffers_->inputBlock);

    cudaFree(buffers_->spikes);
    cudaFree(buffers_->incomingOffsets);
    cudaFree(buffers_->incomingPre);
    cudaFree(buffers_->incomingDelay);
    cudaFree(buffers_->incomingWeight);
    cudaFree(buffers_->incomingSign);
    cudaFree(buffers_->batchedDelayBuffer);
    cudaFree(buffers_->batchedIExcState);
    cudaFree(buffers_->batchedIInhState);
    cudaFree(buffers_->batchedAdaptationCurrent);
    cudaFree(buffers_->batchedExtCurrentWithBackground);
    cudaFree(buffers_->batchedNoiseStd);
    cudaFree(buffers_->batchedBaseGNa);
    cudaFree(buffers_->batchedBaseGK);
    cudaFree(buffers_->batchedBaseGCa);
    cudaFree(buffers_->batchedNeuronType);
    cudaFree(buffers_->batchedDrugParams);
    cudaFree(buffers_->batchedRngStates);
    cudaFree(buffers_->batchedSpikeHistory);
    cudaFree(buffers_->batchedReceptorState);
    cudaFree(buffers_->batchedGabaADesensitization);
    cudaFree(buffers_->batchedReleaseScale);
    cudaFree(buffers_->batchedVesicleRrp);
    cudaFree(buffers_->batchedVesicleReserve);

    if (buffers_->pinnedInput != nullptr) cudaFreeHost(buffers_->pinnedInput);
    if (buffers_->pinnedSpikes != nullptr) cudaFreeHost(buffers_->pinnedSpikes);

    delete buffers_;
    buffers_ = nullptr;
}

bool CudaSimulator::available() const {
    return available_;
}

void CudaSimulator::uploadInitialState(
    const std::vector<float>& v,
    const std::vector<float>& m,
    const std::vector<float>& h,
    const std::vector<float>& n,
    const std::vector<float>& s,
    const std::vector<float>& caCa,
    const std::vector<float>& threshold,
    const std::vector<float>& lastSpikeTime
) {
    if (!available_) {
        throw std::runtime_error("CUDA simulator is not available.");
    }
    if (v.size() != neuronCount_ || m.size() != neuronCount_ || h.size() != neuronCount_ ||
        n.size() != neuronCount_ || s.size() != neuronCount_ || caCa.size() != neuronCount_ ||
        threshold.size() != neuronCount_ || lastSpikeTime.size() != neuronCount_) {
        throw std::invalid_argument("CUDA upload vectors must all match neuron count.");
    }

    const std::size_t bytes = neuronCount_ * sizeof(float);

    checkCuda(cudaMemcpy(buffers_->v, v.data(), bytes, cudaMemcpyHostToDevice), "copy v H2D");
    checkCuda(cudaMemcpy(buffers_->m, m.data(), bytes, cudaMemcpyHostToDevice), "copy m H2D");
    checkCuda(cudaMemcpy(buffers_->h, h.data(), bytes, cudaMemcpyHostToDevice), "copy h H2D");
    checkCuda(cudaMemcpy(buffers_->n, n.data(), bytes, cudaMemcpyHostToDevice), "copy n H2D");
    checkCuda(cudaMemcpy(buffers_->s, s.data(), bytes, cudaMemcpyHostToDevice), "copy s H2D");
    checkCuda(cudaMemcpy(buffers_->caCa, caCa.data(), bytes, cudaMemcpyHostToDevice), "copy caCa H2D");
    checkCuda(cudaMemcpy(buffers_->threshold, threshold.data(), bytes, cudaMemcpyHostToDevice), "copy threshold H2D");
    checkCuda(cudaMemcpy(buffers_->lastSpikeTime, lastSpikeTime.data(), bytes, cudaMemcpyHostToDevice), "copy lastSpike H2D");
}

void CudaSimulator::step(
    float timeMs,
    float dtMs,
    const neuron::HHParameters& params,
    float refractoryMs,
    const std::vector<float>& iSyn,
    const std::vector<float>& iExt,
    const std::vector<float>& iNoise,
    const std::vector<float>& gNaEff,
    const std::vector<float>& gKEff,
    const std::vector<float>& gCaEff,
    std::vector<float>& v,
    std::vector<float>& m,
    std::vector<float>& h,
    std::vector<float>& n,
    std::vector<float>& s,
    std::vector<float>& caCa,
    std::vector<float>& lastSpikeTime,
    std::vector<std::uint8_t>& spikes
) {
    if (!available_) {
        throw std::runtime_error("CUDA simulator is not available.");
    }

    if (iSyn.size() != neuronCount_ || iExt.size() != neuronCount_ || iNoise.size() != neuronCount_ ||
        gNaEff.size() != neuronCount_ || gKEff.size() != neuronCount_ || gCaEff.size() != neuronCount_ ||
        v.size() != neuronCount_ || m.size() != neuronCount_ || h.size() != neuronCount_ ||
        n.size() != neuronCount_ || s.size() != neuronCount_ || caCa.size() != neuronCount_ ||
        lastSpikeTime.size() != neuronCount_ || spikes.size() != neuronCount_) {
        throw std::invalid_argument("CUDA step vectors must all match neuron count.");
    }

    const std::size_t floatBytes = neuronCount_ * sizeof(float);
    const std::size_t spikeBytes = neuronCount_ * sizeof(std::uint8_t);
    const std::size_t inputBlockBytes = floatBytes * static_cast<std::size_t>(kPackedInputCount);

    // Pack all six per-step arrays into the pinned staging buffer (cheap
    // host-side memcpy), then issue exactly one H2D transfer instead of six.
    std::memcpy(buffers_->pinnedInput + 0 * neuronCount_, iSyn.data(), floatBytes);
    std::memcpy(buffers_->pinnedInput + 1 * neuronCount_, iExt.data(), floatBytes);
    std::memcpy(buffers_->pinnedInput + 2 * neuronCount_, iNoise.data(), floatBytes);
    std::memcpy(buffers_->pinnedInput + 3 * neuronCount_, gNaEff.data(), floatBytes);
    std::memcpy(buffers_->pinnedInput + 4 * neuronCount_, gKEff.data(), floatBytes);
    std::memcpy(buffers_->pinnedInput + 5 * neuronCount_, gCaEff.data(), floatBytes);

    checkCuda(cudaMemcpy(buffers_->inputBlock, buffers_->pinnedInput, inputBlockBytes, cudaMemcpyHostToDevice),
              "copy packed inputs H2D");

    const NeuronUpdateLaunchInfo launchInfo = computeNeuronUpdateLaunchInfo(neuronCount_, 256);

    hhStepKernel<<<launchInfo.gridSize, launchInfo.blockSize>>>(
        neuronCount_,
        timeMs,
        dtMs,
        params.cm,
        params.eNa,
        params.eK,
        params.eCa,
        params.gL,
        params.eL,
        params.gAHP,
        params.tauCa,
        params.kCa,
        params.gAHPFloor,
        params.restingVoltage,
        refractoryMs,
        buffers_->iSyn,
        buffers_->iExt,
        buffers_->iNoise,
        buffers_->gNaEff,
        buffers_->gKEff,
        buffers_->gCaEff,
        buffers_->threshold,
        buffers_->v,
        buffers_->m,
        buffers_->h,
        buffers_->n,
        buffers_->s,
        buffers_->caCa,
        buffers_->lastSpikeTime,
        buffers_->spikes
    );

    checkCuda(cudaGetLastError(), "kernel launch");

    checkCuda(cudaMemcpy(buffers_->pinnedSpikes, buffers_->spikes, spikeBytes, cudaMemcpyDeviceToHost),
              "copy spikes D2H");
    std::memcpy(spikes.data(), buffers_->pinnedSpikes, spikeBytes);
}

void CudaSimulator::downloadState(
    std::vector<float>& v,
    std::vector<float>& m,
    std::vector<float>& h,
    std::vector<float>& n,
    std::vector<float>& s,
    std::vector<float>& caCa,
    std::vector<float>& lastSpikeTime
) {
    if (!available_) {
        throw std::runtime_error("CUDA simulator is not available.");
    }

    if (v.size() != neuronCount_ || m.size() != neuronCount_ || h.size() != neuronCount_ ||
        n.size() != neuronCount_ || s.size() != neuronCount_ || caCa.size() != neuronCount_ ||
        lastSpikeTime.size() != neuronCount_) {
        throw std::invalid_argument("CUDA download vectors must all match neuron count.");
    }

    const std::size_t floatBytes = neuronCount_ * sizeof(float);

    checkCuda(cudaMemcpy(v.data(), buffers_->v, floatBytes, cudaMemcpyDeviceToHost), "copy v D2H");
    checkCuda(cudaMemcpy(m.data(), buffers_->m, floatBytes, cudaMemcpyDeviceToHost), "copy m D2H");
    checkCuda(cudaMemcpy(h.data(), buffers_->h, floatBytes, cudaMemcpyDeviceToHost), "copy h D2H");
    checkCuda(cudaMemcpy(n.data(), buffers_->n, floatBytes, cudaMemcpyDeviceToHost), "copy n D2H");
    checkCuda(cudaMemcpy(s.data(), buffers_->s, floatBytes, cudaMemcpyDeviceToHost), "copy s D2H");
    checkCuda(cudaMemcpy(caCa.data(), buffers_->caCa, floatBytes, cudaMemcpyDeviceToHost), "copy caCa D2H");
    checkCuda(cudaMemcpy(lastSpikeTime.data(), buffers_->lastSpikeTime, floatBytes, cudaMemcpyDeviceToHost), "copy lastSpike D2H");
}

void CudaSimulator::initializeBatchedSimulation(
    std::size_t delaySteps,
    const std::vector<std::uint32_t>& incomingOffsets,
    const std::vector<std::uint32_t>& incomingPre,
    const std::vector<std::uint32_t>& incomingDelay,
    const std::vector<float>& incomingWeight,
    const std::vector<std::int8_t>& incomingSign,
    const std::vector<float>& extCurrentWithBackground,
    const std::vector<float>& threshold,
    const std::vector<float>& noiseStd,
    const std::vector<float>& baseGNa,
    const std::vector<float>& baseGK,
    const std::vector<float>& baseGCa,
    const std::vector<std::uint8_t>& neuronType,
    const std::vector<float>& drugParams,
    const neuron::HHParameters& params,
    float refractoryMs,
    float dtMs,
    float adaptationTauMs,
    float adaptationIncrement,
    float adaptationMaxCurrent,
    float adaptationInhibitoryScale,
    float maxTotalCurrent,
    float synTauExcMs,
    float synTauInhMs,
    float maxSynCurrent,
    float /*drugOnsetTauMs*/,
    std::uint32_t rngSeed,
    std::size_t batchWindowSteps,
    // PHASE2_PLAN.md step 6: receptor peak-conductance scales
    // (SimulationConfig::gMax*) and flattened ReceptorDrugProfile -- see
    // BatchedStepLaunchInfo's comment in NeuronUpdate.h for the mechanism
    // int encoding.
    float gMaxAMPA,
    float gMaxNMDA,
    float gMaxGABAa,
    float gMaxGABAb,
    float gMaxGABAbAgonist,
    float ampaConductanceCeiling,
    int ampaMechanism,
    float ampaEc50,
    float ampaHill,
    int nmdaMechanism,
    float nmdaEc50,
    float nmdaHill,
    int gabaAMechanism,
    float gabaAEc50,
    float gabaAHill,
    float gabaAMaxPotentiation,
    int gabaBMechanism,
    float gabaBEc50,
    float gabaBHill,
    // Phase 3a: GAT1 reuptake block -- see NeuronUpdate.h's
    // BatchedStepLaunchInfo comment.
    int gat1Mechanism,
    float gat1KiUm,
    float gat1Hill,
    float gat1MaxExtensionFold,
    // Phase 3b: GABA-A desensitization -- see NeuronUpdate.h's
    // BatchedStepLaunchInfo comment.
    bool desensitizationEnabled,
    float desensitizationTauDesenseMs,
    float desensitizationTauRecoveryMs,
    float desensitizationMaxAttenuation,
    // Phase 3c: neuromodulator gain (D1/D2/5-HT1A/5-HT2A) -- see
    // NeuronUpdate.h's BatchedStepLaunchInfo comment / NeuromodulatorSystem.h
    // for the design.
    float d1Ec50,
    float d1Hill,
    float d1MaxAdaptationReductionFrac,
    float d1MaxNmdaGainFold,
    float d2Ec50,
    float d2Hill,
    float d2MaxReleaseReductionFrac,
    float ht1aEc50,
    float ht1aHill,
    float ht1aMaxKGainFold,
    float ht2aEc50,
    float ht2aHill,
    float ht2aMaxKReductionFrac,
    float ht2aMaxAdaptationReductionFrac,
    // Phase 3c retrofit: SERT/DAT reuptake block dose-amplification -- see
    // NeuronUpdate.h's BatchedStepLaunchInfo comment.
    int sertMechanism,
    float sertKiUm,
    float sertHill,
    float sertMaxExtensionFold,
    int datMechanism,
    float datKiUm,
    float datHill,
    float datMaxExtensionFold,
    // Phase 3c: vesicle pool dynamics -- see NeuronUpdate.h's
    // BatchedStepLaunchInfo comment / NeurotransmitterPool.h for the design.
    int vesiclePoolEnabled,
    float vesiclePoolRrpSize,
    float vesiclePoolReserveSize,
    float vesiclePoolRrpRefillTauMs,
    float vesiclePoolReserveRefillTauMs,
    float vesiclePoolCalciumFactor
) {
    if (!available_) {
        throw std::runtime_error("CUDA simulator is not available.");
    }
    if (incomingOffsets.size() != neuronCount_ + 1 || incomingPre.size() != incomingWeight.size() ||
        incomingPre.size() != incomingDelay.size() || incomingPre.size() != incomingSign.size() ||
        extCurrentWithBackground.size() != neuronCount_ || threshold.size() != neuronCount_ ||
        noiseStd.size() != neuronCount_ || baseGNa.size() != neuronCount_ || baseGK.size() != neuronCount_ ||
        baseGCa.size() != neuronCount_ || neuronType.size() != neuronCount_ || drugParams.size() != neuronCount_ * 7U) {
        throw std::invalid_argument("Batched simulation inputs must match neuron count.");
    }

    const std::size_t floatBytes = neuronCount_ * sizeof(float);
    const std::size_t byteBytes = neuronCount_ * sizeof(std::uint8_t);
    const std::size_t offsetBytes = (neuronCount_ + 1U) * sizeof(std::uint32_t);
    const std::size_t edgeCount = incomingPre.size();
    const std::size_t edgeBytes = edgeCount * sizeof(std::uint32_t);
    const std::size_t weightBytes = edgeCount * sizeof(float);
    const std::size_t signBytes = edgeCount * sizeof(std::int8_t);
    const std::size_t drugBytes = drugParams.size() * sizeof(float);

    cudaFree(buffers_->incomingOffsets);
    cudaFree(buffers_->incomingPre);
    cudaFree(buffers_->incomingDelay);
    cudaFree(buffers_->incomingWeight);
    cudaFree(buffers_->incomingSign);
    cudaFree(buffers_->batchedDelayBuffer);
    cudaFree(buffers_->batchedIExcState);
    cudaFree(buffers_->batchedIInhState);
    cudaFree(buffers_->batchedAdaptationCurrent);
    cudaFree(buffers_->batchedExtCurrentWithBackground);
    cudaFree(buffers_->batchedNoiseStd);
    cudaFree(buffers_->batchedBaseGNa);
    cudaFree(buffers_->batchedBaseGK);
    cudaFree(buffers_->batchedBaseGCa);
    cudaFree(buffers_->batchedNeuronType);
    cudaFree(buffers_->batchedDrugParams);
    cudaFree(buffers_->batchedRngStates);
    cudaFree(buffers_->batchedSpikeHistory);
    cudaFree(buffers_->batchedReceptorState);
    cudaFree(buffers_->batchedGabaADesensitization);
    cudaFree(buffers_->batchedReleaseScale);
    cudaFree(buffers_->batchedVesicleRrp);
    cudaFree(buffers_->batchedVesicleReserve);

    checkCuda(cudaMalloc(&buffers_->incomingOffsets, offsetBytes), "cudaMalloc incomingOffsets");
    checkCuda(cudaMalloc(&buffers_->incomingPre, edgeBytes), "cudaMalloc incomingPre");
    checkCuda(cudaMalloc(&buffers_->incomingDelay, edgeBytes), "cudaMalloc incomingDelay");
    checkCuda(cudaMalloc(&buffers_->incomingWeight, weightBytes), "cudaMalloc incomingWeight");
    checkCuda(cudaMalloc(&buffers_->incomingSign, signBytes), "cudaMalloc incomingSign");

    buffers_->batchedDelaySteps = std::max<std::size_t>(1, delaySteps);
    buffers_->batchedDelayHead = 0;
    buffers_->batchedBatchWindowSteps = std::max<std::size_t>(1, batchWindowSteps);
    buffers_->batchedRefractoryMs = refractoryMs;
    buffers_->batchedDtMs = dtMs;
    buffers_->batchedSynTauExcMs = synTauExcMs;
    buffers_->batchedSynTauInhMs = synTauInhMs;
    buffers_->batchedMaxSynCurrent = maxSynCurrent;
    buffers_->batchedMaxTotalCurrent = maxTotalCurrent;
    buffers_->batchedAdaptationTauMs = adaptationTauMs;
    buffers_->batchedAdaptationIncrement = adaptationIncrement;
    buffers_->batchedAdaptationMaxCurrent = adaptationMaxCurrent;
    buffers_->batchedAdaptationInhibitoryScale = adaptationInhibitoryScale;
    buffers_->batchedParams = params;

    buffers_->batchedGMaxAMPA = gMaxAMPA;
    buffers_->batchedGMaxNMDA = gMaxNMDA;
    buffers_->batchedGMaxGABAa = gMaxGABAa;
    buffers_->batchedGMaxGABAb = gMaxGABAb;
    buffers_->batchedGMaxGABAbAgonist = gMaxGABAbAgonist;
    buffers_->batchedAmpaConductanceCeiling = ampaConductanceCeiling;
    buffers_->batchedAmpaMechanism = ampaMechanism;
    buffers_->batchedAmpaEc50 = ampaEc50;
    buffers_->batchedAmpaHill = ampaHill;
    buffers_->batchedNmdaMechanism = nmdaMechanism;
    buffers_->batchedNmdaEc50 = nmdaEc50;
    buffers_->batchedNmdaHill = nmdaHill;
    buffers_->batchedGabaAMechanism = gabaAMechanism;
    buffers_->batchedGabaAEc50 = gabaAEc50;
    buffers_->batchedGabaAHill = gabaAHill;
    buffers_->batchedGabaAMaxPotentiation = gabaAMaxPotentiation;
    buffers_->batchedGabaBMechanism = gabaBMechanism;
    buffers_->batchedGabaBEc50 = gabaBEc50;
    buffers_->batchedGabaBHill = gabaBHill;
    buffers_->batchedGat1Mechanism = gat1Mechanism;
    buffers_->batchedGat1KiUm = gat1KiUm;
    buffers_->batchedGat1Hill = gat1Hill;
    buffers_->batchedGat1MaxExtensionFold = gat1MaxExtensionFold;
    buffers_->batchedDesensitizationEnabled = desensitizationEnabled;
    buffers_->batchedDesensitizationTauDesenseMs = desensitizationTauDesenseMs;
    buffers_->batchedDesensitizationTauRecoveryMs = desensitizationTauRecoveryMs;
    buffers_->batchedDesensitizationMaxAttenuation = desensitizationMaxAttenuation;
    buffers_->batchedD1Ec50 = d1Ec50;
    buffers_->batchedD1Hill = d1Hill;
    buffers_->batchedD1MaxAdaptationReductionFrac = d1MaxAdaptationReductionFrac;
    buffers_->batchedD1MaxNmdaGainFold = d1MaxNmdaGainFold;
    buffers_->batchedD2Ec50 = d2Ec50;
    buffers_->batchedD2Hill = d2Hill;
    buffers_->batchedD2MaxReleaseReductionFrac = d2MaxReleaseReductionFrac;
    buffers_->batchedHt1aEc50 = ht1aEc50;
    buffers_->batchedHt1aHill = ht1aHill;
    buffers_->batchedHt1aMaxKGainFold = ht1aMaxKGainFold;
    buffers_->batchedHt2aEc50 = ht2aEc50;
    buffers_->batchedHt2aHill = ht2aHill;
    buffers_->batchedHt2aMaxKReductionFrac = ht2aMaxKReductionFrac;
    buffers_->batchedHt2aMaxAdaptationReductionFrac = ht2aMaxAdaptationReductionFrac;
    buffers_->batchedSertMechanism = sertMechanism;
    buffers_->batchedSertKiUm = sertKiUm;
    buffers_->batchedSertHill = sertHill;
    buffers_->batchedSertMaxExtensionFold = sertMaxExtensionFold;
    buffers_->batchedDatMechanism = datMechanism;
    buffers_->batchedDatKiUm = datKiUm;
    buffers_->batchedDatHill = datHill;
    buffers_->batchedDatMaxExtensionFold = datMaxExtensionFold;
    buffers_->batchedVesiclePoolEnabled = vesiclePoolEnabled;
    buffers_->batchedVesiclePoolRrpSize = vesiclePoolRrpSize;
    buffers_->batchedVesiclePoolReserveSize = vesiclePoolReserveSize;
    buffers_->batchedVesiclePoolRrpRefillTauMs = vesiclePoolRrpRefillTauMs;
    buffers_->batchedVesiclePoolReserveRefillTauMs = vesiclePoolReserveRefillTauMs;
    buffers_->batchedVesiclePoolCalciumFactor = vesiclePoolCalciumFactor;

    checkCuda(cudaMalloc(&buffers_->batchedDelayBuffer, neuronCount_ * buffers_->batchedDelaySteps * sizeof(std::uint8_t)), "cudaMalloc batchedDelayBuffer");
    checkCuda(cudaMalloc(&buffers_->batchedIExcState, floatBytes), "cudaMalloc batchedIExcState");
    checkCuda(cudaMalloc(&buffers_->batchedIInhState, floatBytes), "cudaMalloc batchedIInhState");
    checkCuda(cudaMalloc(&buffers_->batchedAdaptationCurrent, floatBytes), "cudaMalloc batchedAdaptationCurrent");
    checkCuda(cudaMalloc(&buffers_->batchedExtCurrentWithBackground, floatBytes), "cudaMalloc batchedExtCurrentWithBackground");
    checkCuda(cudaMalloc(&buffers_->batchedNoiseStd, floatBytes), "cudaMalloc batchedNoiseStd");
    checkCuda(cudaMalloc(&buffers_->batchedBaseGNa, floatBytes), "cudaMalloc batchedBaseGNa");
    checkCuda(cudaMalloc(&buffers_->batchedBaseGK, floatBytes), "cudaMalloc batchedBaseGK");
    checkCuda(cudaMalloc(&buffers_->batchedBaseGCa, floatBytes), "cudaMalloc batchedBaseGCa");
    checkCuda(cudaMalloc(&buffers_->batchedNeuronType, byteBytes), "cudaMalloc batchedNeuronType");
    checkCuda(cudaMalloc(&buffers_->batchedDrugParams, drugBytes), "cudaMalloc batchedDrugParams");
    checkCuda(cudaMalloc(&buffers_->batchedRngStates, neuronCount_ * sizeof(curandState)), "cudaMalloc batchedRngStates");
    checkCuda(cudaMalloc(&buffers_->batchedSpikeHistory, buffers_->batchedBatchWindowSteps * neuronCount_ * sizeof(std::uint8_t)), "cudaMalloc batchedSpikeHistory");

    // PHASE2_PLAN.md step 6: one packed 8*neuronCount_ allocation for the
    // receptor conductance state, same sub-pointer-into-one-block pattern
    // as inputBlock in the constructor above.
    const std::size_t receptorStateBytes = floatBytes * 8U;
    checkCuda(cudaMalloc(&buffers_->batchedReceptorState, receptorStateBytes), "cudaMalloc batchedReceptorState");
    buffers_->batchedReceptorAmpaDecay  = buffers_->batchedReceptorState + 0 * neuronCount_;
    buffers_->batchedReceptorAmpaRise   = buffers_->batchedReceptorState + 1 * neuronCount_;
    buffers_->batchedReceptorNmdaDecay  = buffers_->batchedReceptorState + 2 * neuronCount_;
    buffers_->batchedReceptorNmdaRise   = buffers_->batchedReceptorState + 3 * neuronCount_;
    buffers_->batchedReceptorGabaADecay = buffers_->batchedReceptorState + 4 * neuronCount_;
    buffers_->batchedReceptorGabaARise  = buffers_->batchedReceptorState + 5 * neuronCount_;
    buffers_->batchedReceptorGabaBDecay = buffers_->batchedReceptorState + 6 * neuronCount_;
    buffers_->batchedReceptorGabaBRise  = buffers_->batchedReceptorState + 7 * neuronCount_;

    // Phase 3b: single-float-per-neuron desensitization state, separate
    // allocation (not packed into the 8-wide receptor state block above --
    // it's one value, not a decay/rise pair).
    checkCuda(cudaMalloc(&buffers_->batchedGabaADesensitization, floatBytes), "cudaMalloc batchedGabaADesensitization");
    checkCuda(cudaMemset(buffers_->batchedGabaADesensitization, 0, floatBytes), "memset batchedGabaADesensitization");

    // Phase 3c: vesicle pool dynamics. releaseScale can't be zero-memset
    // like the state above -- its no-op value is 1.0 (multiplicative
    // passthrough, see NeurotransmitterPool.h's baseline-preservation
    // note), and 1.0f's bit pattern isn't all-zero-bytes, so it's filled on
    // the host and copied in. Same reasoning for vesicleRrp/vesicleReserve:
    // their fresh/no-op value is each pool's own configured size, not zero.
    // This only runs once per initializeBatchedSimulation call (not
    // per-step), so a host-side fill + single H2D copy is cheap enough not
    // to need an init kernel.
    const std::size_t releaseScaleBytes = neuronCount_ * buffers_->batchedDelaySteps * sizeof(float);
    checkCuda(cudaMalloc(&buffers_->batchedReleaseScale, releaseScaleBytes), "cudaMalloc batchedReleaseScale");
    checkCuda(cudaMalloc(&buffers_->batchedVesicleRrp, floatBytes), "cudaMalloc batchedVesicleRrp");
    checkCuda(cudaMalloc(&buffers_->batchedVesicleReserve, floatBytes), "cudaMalloc batchedVesicleReserve");
    {
        std::vector<float> releaseScaleInit(neuronCount_ * buffers_->batchedDelaySteps, 1.0f);
        std::vector<float> vesicleRrpInit(neuronCount_, vesiclePoolRrpSize);
        std::vector<float> vesicleReserveInit(neuronCount_, vesiclePoolReserveSize);
        checkCuda(cudaMemcpy(buffers_->batchedReleaseScale, releaseScaleInit.data(), releaseScaleBytes, cudaMemcpyHostToDevice), "copy releaseScale init H2D");
        checkCuda(cudaMemcpy(buffers_->batchedVesicleRrp, vesicleRrpInit.data(), floatBytes, cudaMemcpyHostToDevice), "copy vesicleRrp init H2D");
        checkCuda(cudaMemcpy(buffers_->batchedVesicleReserve, vesicleReserveInit.data(), floatBytes, cudaMemcpyHostToDevice), "copy vesicleReserve init H2D");
    }

    checkCuda(cudaMemset(buffers_->batchedDelayBuffer, 0, neuronCount_ * buffers_->batchedDelaySteps * sizeof(std::uint8_t)), "memset batchedDelayBuffer");
    checkCuda(cudaMemset(buffers_->batchedIExcState, 0, floatBytes), "memset batchedIExcState");
    checkCuda(cudaMemset(buffers_->batchedIInhState, 0, floatBytes), "memset batchedIInhState");
    checkCuda(cudaMemset(buffers_->batchedAdaptationCurrent, 0, floatBytes), "memset batchedAdaptationCurrent");
    checkCuda(cudaMemset(buffers_->batchedSpikeHistory, 0, buffers_->batchedBatchWindowSteps * neuronCount_ * sizeof(std::uint8_t)), "memset batchedSpikeHistory");
    checkCuda(cudaMemset(buffers_->batchedReceptorState, 0, receptorStateBytes), "memset batchedReceptorState");

    checkCuda(cudaMemcpy(buffers_->incomingOffsets, incomingOffsets.data(), offsetBytes, cudaMemcpyHostToDevice), "copy incomingOffsets H2D");
    checkCuda(cudaMemcpy(buffers_->incomingPre, incomingPre.data(), edgeBytes, cudaMemcpyHostToDevice), "copy incomingPre H2D");
    checkCuda(cudaMemcpy(buffers_->incomingDelay, incomingDelay.data(), edgeBytes, cudaMemcpyHostToDevice), "copy incomingDelay H2D");
    checkCuda(cudaMemcpy(buffers_->incomingWeight, incomingWeight.data(), weightBytes, cudaMemcpyHostToDevice), "copy incomingWeight H2D");
    checkCuda(cudaMemcpy(buffers_->incomingSign, incomingSign.data(), signBytes, cudaMemcpyHostToDevice), "copy incomingSign H2D");

    checkCuda(cudaMemcpy(buffers_->batchedExtCurrentWithBackground, extCurrentWithBackground.data(), floatBytes, cudaMemcpyHostToDevice), "copy batchedExtCurrentWithBackground H2D");
    checkCuda(cudaMemcpy(buffers_->threshold, threshold.data(), floatBytes, cudaMemcpyHostToDevice), "copy threshold H2D");
    checkCuda(cudaMemcpy(buffers_->batchedNoiseStd, noiseStd.data(), floatBytes, cudaMemcpyHostToDevice), "copy batchedNoiseStd H2D");
    checkCuda(cudaMemcpy(buffers_->batchedBaseGNa, baseGNa.data(), floatBytes, cudaMemcpyHostToDevice), "copy batchedBaseGNa H2D");
    checkCuda(cudaMemcpy(buffers_->batchedBaseGK, baseGK.data(), floatBytes, cudaMemcpyHostToDevice), "copy batchedBaseGK H2D");
    checkCuda(cudaMemcpy(buffers_->batchedBaseGCa, baseGCa.data(), floatBytes, cudaMemcpyHostToDevice), "copy batchedBaseGCa H2D");
    checkCuda(cudaMemcpy(buffers_->batchedNeuronType, neuronType.data(), byteBytes, cudaMemcpyHostToDevice), "copy batchedNeuronType H2D");
    checkCuda(cudaMemcpy(buffers_->batchedDrugParams, drugParams.data(), drugBytes, cudaMemcpyHostToDevice), "copy batchedDrugParams H2D");

    // Assume the caller already uploaded initial state via uploadInitialState().
    // The legacy state buffers and batched state share the same state arrays.
    initializeBatchedRandomStates(neuronCount_, rngSeed, buffers_->batchedRngStates);
    checkCuda(cudaGetLastError(), "initialize batched random states");
}

void CudaSimulator::stepBatched(float timeMs, float doseScale, std::size_t batchStepIndex) {
    if (!available_ || buffers_ == nullptr || buffers_->batchedRngStates == nullptr) {
        throw std::runtime_error("CUDA batched simulator is not initialized.");
    }

    BatchedStepLaunchInfo launchInfo;
    launchInfo.neuronCount = neuronCount_;
    launchInfo.delaySteps = buffers_->batchedDelaySteps;
    launchInfo.oldHead = buffers_->batchedDelayHead;
    launchInfo.batchStepIndex = batchStepIndex;
    launchInfo.timeMs = timeMs;
    launchInfo.dtMs = buffers_->batchedDtMs;
    launchInfo.doseScale = doseScale;
    launchInfo.refractoryMs = buffers_->batchedRefractoryMs;
    launchInfo.synTauExcMs = buffers_->batchedSynTauExcMs;
    launchInfo.synTauInhMs = buffers_->batchedSynTauInhMs;
    launchInfo.maxSynCurrent = buffers_->batchedMaxSynCurrent;
    launchInfo.maxTotalCurrent = buffers_->batchedMaxTotalCurrent;
    launchInfo.adaptationTauMs = buffers_->batchedAdaptationTauMs;
    launchInfo.adaptationIncrement = buffers_->batchedAdaptationIncrement;
    launchInfo.adaptationMaxCurrent = buffers_->batchedAdaptationMaxCurrent;
    launchInfo.adaptationInhibitoryScale = buffers_->batchedAdaptationInhibitoryScale;
    launchInfo.params = buffers_->batchedParams;

    launchInfo.gMaxAMPA = buffers_->batchedGMaxAMPA;
    launchInfo.gMaxNMDA = buffers_->batchedGMaxNMDA;
    launchInfo.gMaxGABAa = buffers_->batchedGMaxGABAa;
    launchInfo.gMaxGABAb = buffers_->batchedGMaxGABAb;
    launchInfo.gMaxGABAbAgonist = buffers_->batchedGMaxGABAbAgonist;
    launchInfo.ampaConductanceCeiling = buffers_->batchedAmpaConductanceCeiling;
    launchInfo.ampaMechanism = buffers_->batchedAmpaMechanism;
    launchInfo.ampaEc50 = buffers_->batchedAmpaEc50;
    launchInfo.ampaHill = buffers_->batchedAmpaHill;
    launchInfo.nmdaMechanism = buffers_->batchedNmdaMechanism;
    launchInfo.nmdaEc50 = buffers_->batchedNmdaEc50;
    launchInfo.nmdaHill = buffers_->batchedNmdaHill;
    launchInfo.gabaAMechanism = buffers_->batchedGabaAMechanism;
    launchInfo.gabaAEc50 = buffers_->batchedGabaAEc50;
    launchInfo.gabaAHill = buffers_->batchedGabaAHill;
    launchInfo.gabaAMaxPotentiation = buffers_->batchedGabaAMaxPotentiation;
    launchInfo.gabaBMechanism = buffers_->batchedGabaBMechanism;
    launchInfo.gabaBEc50 = buffers_->batchedGabaBEc50;
    launchInfo.gabaBHill = buffers_->batchedGabaBHill;
    launchInfo.gat1Mechanism = buffers_->batchedGat1Mechanism;
    launchInfo.gat1KiUm = buffers_->batchedGat1KiUm;
    launchInfo.gat1Hill = buffers_->batchedGat1Hill;
    launchInfo.gat1MaxExtensionFold = buffers_->batchedGat1MaxExtensionFold;
    launchInfo.desensitizationEnabled = buffers_->batchedDesensitizationEnabled;
    launchInfo.desensitizationTauDesenseMs = buffers_->batchedDesensitizationTauDesenseMs;
    launchInfo.desensitizationTauRecoveryMs = buffers_->batchedDesensitizationTauRecoveryMs;
    launchInfo.desensitizationMaxAttenuation = buffers_->batchedDesensitizationMaxAttenuation;
    launchInfo.d1Ec50 = buffers_->batchedD1Ec50;
    launchInfo.d1Hill = buffers_->batchedD1Hill;
    launchInfo.d1MaxAdaptationReductionFrac = buffers_->batchedD1MaxAdaptationReductionFrac;
    launchInfo.d1MaxNmdaGainFold = buffers_->batchedD1MaxNmdaGainFold;
    launchInfo.d2Ec50 = buffers_->batchedD2Ec50;
    launchInfo.d2Hill = buffers_->batchedD2Hill;
    launchInfo.d2MaxReleaseReductionFrac = buffers_->batchedD2MaxReleaseReductionFrac;
    launchInfo.ht1aEc50 = buffers_->batchedHt1aEc50;
    launchInfo.ht1aHill = buffers_->batchedHt1aHill;
    launchInfo.ht1aMaxKGainFold = buffers_->batchedHt1aMaxKGainFold;
    launchInfo.ht2aEc50 = buffers_->batchedHt2aEc50;
    launchInfo.ht2aHill = buffers_->batchedHt2aHill;
    launchInfo.ht2aMaxKReductionFrac = buffers_->batchedHt2aMaxKReductionFrac;
    launchInfo.ht2aMaxAdaptationReductionFrac = buffers_->batchedHt2aMaxAdaptationReductionFrac;
    launchInfo.sertMechanism = buffers_->batchedSertMechanism;
    launchInfo.sertKiUm = buffers_->batchedSertKiUm;
    launchInfo.sertHill = buffers_->batchedSertHill;
    launchInfo.sertMaxExtensionFold = buffers_->batchedSertMaxExtensionFold;
    launchInfo.datMechanism = buffers_->batchedDatMechanism;
    launchInfo.datKiUm = buffers_->batchedDatKiUm;
    launchInfo.datHill = buffers_->batchedDatHill;
    launchInfo.datMaxExtensionFold = buffers_->batchedDatMaxExtensionFold;
    launchInfo.vesiclePoolEnabled = buffers_->batchedVesiclePoolEnabled;
    launchInfo.vesiclePoolRrpSize = buffers_->batchedVesiclePoolRrpSize;
    launchInfo.vesiclePoolReserveSize = buffers_->batchedVesiclePoolReserveSize;
    launchInfo.vesiclePoolRrpRefillTauMs = buffers_->batchedVesiclePoolRrpRefillTauMs;
    launchInfo.vesiclePoolReserveRefillTauMs = buffers_->batchedVesiclePoolReserveRefillTauMs;
    launchInfo.vesiclePoolCalciumFactor = buffers_->batchedVesiclePoolCalciumFactor;

    BatchedStepDevicePointers devicePointers;
    devicePointers.incomingOffsets = buffers_->incomingOffsets;
    devicePointers.incomingPre = buffers_->incomingPre;
    devicePointers.incomingDelay = buffers_->incomingDelay;
    devicePointers.incomingWeight = buffers_->incomingWeight;
    devicePointers.incomingSign = buffers_->incomingSign;
    devicePointers.delayBuffer = buffers_->batchedDelayBuffer;
    devicePointers.iExcState = buffers_->batchedIExcState;
    devicePointers.iInhState = buffers_->batchedIInhState;
    devicePointers.adaptationCurrent = buffers_->batchedAdaptationCurrent;
    devicePointers.extCurrentWithBackground = buffers_->batchedExtCurrentWithBackground;
    devicePointers.noiseStd = buffers_->batchedNoiseStd;
    devicePointers.threshold = buffers_->threshold;
    devicePointers.neuronType = buffers_->batchedNeuronType;
    devicePointers.baseGNa = buffers_->batchedBaseGNa;
    devicePointers.baseGK = buffers_->batchedBaseGK;
    devicePointers.baseGCa = buffers_->batchedBaseGCa;
    devicePointers.drugParams = buffers_->batchedDrugParams;
    devicePointers.v = buffers_->v;
    devicePointers.m = buffers_->m;
    devicePointers.h = buffers_->h;
    devicePointers.n = buffers_->n;
    devicePointers.s = buffers_->s;
    devicePointers.caCa = buffers_->caCa;
    devicePointers.lastSpikeTime = buffers_->lastSpikeTime;
    devicePointers.rngStates = buffers_->batchedRngStates;
    devicePointers.spikeHistory = buffers_->batchedSpikeHistory;

    devicePointers.receptorAmpaDecay = buffers_->batchedReceptorAmpaDecay;
    devicePointers.receptorAmpaRise = buffers_->batchedReceptorAmpaRise;
    devicePointers.receptorNmdaDecay = buffers_->batchedReceptorNmdaDecay;
    devicePointers.receptorNmdaRise = buffers_->batchedReceptorNmdaRise;
    devicePointers.receptorGabaADecay = buffers_->batchedReceptorGabaADecay;
    devicePointers.receptorGabaARise = buffers_->batchedReceptorGabaARise;
    devicePointers.receptorGabaBDecay = buffers_->batchedReceptorGabaBDecay;
    devicePointers.receptorGabaBRise = buffers_->batchedReceptorGabaBRise;
    devicePointers.gabaADesensitization = buffers_->batchedGabaADesensitization;
    devicePointers.releaseScale = buffers_->batchedReleaseScale;
    devicePointers.vesicleRrp = buffers_->batchedVesicleRrp;
    devicePointers.vesicleReserve = buffers_->batchedVesicleReserve;

    launchBatchedStepKernel(launchInfo, devicePointers);
    checkCuda(cudaGetLastError(), "batched step kernel launch");

    buffers_->batchedDelayHead = static_cast<std::uint32_t>((buffers_->batchedDelayHead + 1U) % buffers_->batchedDelaySteps);
}

void CudaSimulator::downloadBatchedSpikeHistory(
    std::vector<std::uint8_t>& spikeHistory,
    std::size_t stepCountInBatch
) const {
    if (!available_ || buffers_ == nullptr || buffers_->batchedSpikeHistory == nullptr) {
        throw std::runtime_error("CUDA batched simulator is not initialized.");
    }

    const std::size_t bytes = stepCountInBatch * neuronCount_ * sizeof(std::uint8_t);
    spikeHistory.resize(stepCountInBatch * neuronCount_);
    checkCuda(cudaMemcpy(spikeHistory.data(), buffers_->batchedSpikeHistory, bytes, cudaMemcpyDeviceToHost), "copy batched spikeHistory D2H");
}

} // namespace spp::cuda

#endif