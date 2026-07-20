#include "CudaSimulator.h"
#include "NeuronUpdate.h"

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
    float kCa
) {
    Deriv d;
    const float iNa = gNa * m * m * m * h * (v - eNa);
    const float iK = gK * n * n * n * n * (v - eK);
    const float iCa = gCa * s * s * (v - eCa);
    const float iAHP = gAHP * caCa * (v - eK);
    const float iLeak = gL * (v - eL);
    const float caInflux = kCa * fmaxf(0.0f, -iCa);

    d.dv = (iTotal - iNa - iK - iCa - iAHP - iLeak) / cm;
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
                                 params.gAHP, params.tauCa, params.kCa);

    float y2v = yv + 0.5f * dtMs * k1.dv;
    float y2m = ym + 0.5f * dtMs * k1.dm;
    float y2h = yh + 0.5f * dtMs * k1.dh;
    float y2n = yn + 0.5f * dtMs * k1.dn;
    float y2s = ys + 0.5f * dtMs * k1.ds;
    float y2ca = yca + 0.5f * dtMs * k1.dcaCa;
    clampState(y2v, y2m, y2h, y2n, y2s, y2ca);

    const Deriv k2 = derivatives(y2v, y2m, y2h, y2n, y2s, y2ca, iTotal, safeGNa, safeGK, safeGCa,
                                 params.cm, params.eNa, params.eK, params.eCa, params.gL, params.eL,
                                 params.gAHP, params.tauCa, params.kCa);

    float y3v = yv + 0.5f * dtMs * k2.dv;
    float y3m = ym + 0.5f * dtMs * k2.dm;
    float y3h = yh + 0.5f * dtMs * k2.dh;
    float y3n = yn + 0.5f * dtMs * k2.dn;
    float y3s = ys + 0.5f * dtMs * k2.ds;
    float y3ca = yca + 0.5f * dtMs * k2.dcaCa;
    clampState(y3v, y3m, y3h, y3n, y3s, y3ca);

    const Deriv k3 = derivatives(y3v, y3m, y3h, y3n, y3s, y3ca, iTotal, safeGNa, safeGK, safeGCa,
                                 params.cm, params.eNa, params.eK, params.eCa, params.gL, params.eL,
                                 params.gAHP, params.tauCa, params.kCa);

    float y4v = yv + dtMs * k3.dv;
    float y4m = ym + dtMs * k3.dm;
    float y4h = yh + dtMs * k3.dh;
    float y4n = yn + dtMs * k3.dn;
    float y4s = ys + dtMs * k3.ds;
    float y4ca = yca + dtMs * k3.dcaCa;
    clampState(y4v, y4m, y4h, y4n, y4s, y4ca);

    const Deriv k4 = derivatives(y4v, y4m, y4h, y4n, y4s, y4ca, iTotal, safeGNa, safeGK, safeGCa,
                                 params.cm, params.eNa, params.eK, params.eCa, params.gL, params.eL,
                                 params.gAHP, params.tauCa, params.kCa);

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

        if (devicePointers.incomingSign[idx] > 0) {
            iExcPulse += devicePointers.incomingWeight[idx];
        } else {
            iInhPulse += devicePointers.incomingWeight[idx];
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

    const float gNaEff = fmaxf(0.05f * safeGNa, safeGNa * fmaxf(0.0f, 1.0f - blockNa));
    const float gKEff = fmaxf(0.05f * safeGK, safeGK * (1.0f - blockK));
    const float gCaEff = fmaxf(0.02f * safeGCa, safeGCa * fmaxf(0.0f, 1.0f - blockCa));

    std::uint8_t spike = 0U;
    integrateNeuronState(
        i,
        launchInfo.timeMs,
        launchInfo.dtMs,
        launchInfo.refractoryMs,
        launchInfo.maxTotalCurrent,
        launchInfo.params,
        devicePointers.threshold,
        iSyn,
        iExt,
        iNoise,
        gNaEff,
        gKEff,
        gCaEff,
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
        const float adaptStep = (devicePointers.neuronType[i] == 1U)
                                    ? launchInfo.adaptationIncrement
                                    : launchInfo.adaptationIncrement * launchInfo.adaptationInhibitoryScale;
        adapt = fminf(launchInfo.adaptationMaxCurrent, fmaxf(0.0f, adapt + adaptStep));
    }

    devicePointers.iExcState[i] = excState;
    devicePointers.iInhState[i] = inhState;
    devicePointers.adaptationCurrent[i] = adapt;
    devicePointers.delayBuffer[newHead * launchInfo.neuronCount + i] = spike;
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

    const Deriv k1 = derivatives(yv, ym, yh, yn, ys, yca, iTotal, safeGNa, safeGK, safeGCa, cm, eNa, eK, eCa, gL, eL, gAHP, tauCa, kCa);

    float y2v = yv + 0.5f * dtMs * k1.dv;
    float y2m = ym + 0.5f * dtMs * k1.dm;
    float y2h = yh + 0.5f * dtMs * k1.dh;
    float y2n = yn + 0.5f * dtMs * k1.dn;
    float y2s = ys + 0.5f * dtMs * k1.ds;
    float y2ca = yca + 0.5f * dtMs * k1.dcaCa;
    clampState(y2v, y2m, y2h, y2n, y2s, y2ca);

    const Deriv k2 = derivatives(y2v, y2m, y2h, y2n, y2s, y2ca, iTotal, safeGNa, safeGK, safeGCa, cm, eNa, eK, eCa, gL, eL, gAHP, tauCa, kCa);

    float y3v = yv + 0.5f * dtMs * k2.dv;
    float y3m = ym + 0.5f * dtMs * k2.dm;
    float y3h = yh + 0.5f * dtMs * k2.dh;
    float y3n = yn + 0.5f * dtMs * k2.dn;
    float y3s = ys + 0.5f * dtMs * k2.ds;
    float y3ca = yca + 0.5f * dtMs * k2.dcaCa;
    clampState(y3v, y3m, y3h, y3n, y3s, y3ca);

    const Deriv k3 = derivatives(y3v, y3m, y3h, y3n, y3s, y3ca, iTotal, safeGNa, safeGK, safeGCa, cm, eNa, eK, eCa, gL, eL, gAHP, tauCa, kCa);

    float y4v = yv + dtMs * k3.dv;
    float y4m = ym + dtMs * k3.dm;
    float y4h = yh + dtMs * k3.dh;
    float y4n = yn + dtMs * k3.dn;
    float y4s = ys + dtMs * k3.ds;
    float y4ca = yca + dtMs * k3.dcaCa;
    clampState(y4v, y4m, y4h, y4n, y4s, y4ca);

    const Deriv k4 = derivatives(y4v, y4m, y4h, y4n, y4s, y4ca, iTotal, safeGNa, safeGK, safeGCa, cm, eNa, eK, eCa, gL, eL, gAHP, tauCa, kCa);

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
    std::size_t batchWindowSteps
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

    checkCuda(cudaMemset(buffers_->batchedDelayBuffer, 0, neuronCount_ * buffers_->batchedDelaySteps * sizeof(std::uint8_t)), "memset batchedDelayBuffer");
    checkCuda(cudaMemset(buffers_->batchedIExcState, 0, floatBytes), "memset batchedIExcState");
    checkCuda(cudaMemset(buffers_->batchedIInhState, 0, floatBytes), "memset batchedIInhState");
    checkCuda(cudaMemset(buffers_->batchedAdaptationCurrent, 0, floatBytes), "memset batchedAdaptationCurrent");
    checkCuda(cudaMemset(buffers_->batchedSpikeHistory, 0, buffers_->batchedBatchWindowSteps * neuronCount_ * sizeof(std::uint8_t)), "memset batchedSpikeHistory");

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