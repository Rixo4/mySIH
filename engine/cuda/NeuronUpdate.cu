#include "CudaSimulator.h"
#include "NeuronUpdate.h"

#ifdef SPP_USE_CUDA

#include <cuda_runtime.h>

#include <cmath>
#include <cstdint>
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
};

__device__ Deriv derivatives(
    float v,
    float m,
    float h,
    float n,
    float s,
    float iTotal,
    float gNa,
    float gK,
    float gCa,
    float cm,
    float eNa,
    float eK,
    float eCa,
    float gL,
    float eL
) {
    Deriv d;
    const float iNa = gNa * m * m * m * h * (v - eNa);
    const float iK = gK * n * n * n * n * (v - eK);
    const float iCa = gCa * s * s * (v - eCa);
    const float iLeak = gL * (v - eL);

    d.dv = (iTotal - iNa - iK - iCa - iLeak) / cm;
    d.dm = alphaM(v) * (1.0f - m) - betaM(v) * m;
    d.dh = alphaH(v) * (1.0f - h) - betaH(v) * h;
    d.dn = alphaN(v) * (1.0f - n) - betaN(v) * n;
    d.ds = (sInf(v) - s) / tauS(v);

    return d;
}

__device__ void clampState(float& v, float& m, float& h, float& n, float& s) {
    v = fminf(80.0f, fmaxf(-120.0f, v));
    m = clamp01(m);
    h = clamp01(h);
    n = clamp01(n);
    s = clamp01(s);
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

    float iTotal = iSyn[i] + iExt[i] + iNoise[i];
    if (!isfinite(iTotal)) {
        iTotal = 0.0f;
    }

    const float safeGNa = (isfinite(gNaEff[i]) && gNaEff[i] >= 0.0f) ? gNaEff[i] : 0.0f;
    const float safeGK = (isfinite(gKEff[i]) && gKEff[i] >= 0.0f) ? gKEff[i] : 0.0f;
    const float safeGCa = (isfinite(gCaEff[i]) && gCaEff[i] >= 0.0f) ? gCaEff[i] : 0.0f;

    const Deriv k1 = derivatives(yv, ym, yh, yn, ys, iTotal, safeGNa, safeGK, safeGCa, cm, eNa, eK, eCa, gL, eL);

    float y2v = yv + 0.5f * dtMs * k1.dv;
    float y2m = ym + 0.5f * dtMs * k1.dm;
    float y2h = yh + 0.5f * dtMs * k1.dh;
    float y2n = yn + 0.5f * dtMs * k1.dn;
    float y2s = ys + 0.5f * dtMs * k1.ds;
    clampState(y2v, y2m, y2h, y2n, y2s);

    const Deriv k2 = derivatives(y2v, y2m, y2h, y2n, y2s, iTotal, safeGNa, safeGK, safeGCa, cm, eNa, eK, eCa, gL, eL);

    float y3v = yv + 0.5f * dtMs * k2.dv;
    float y3m = ym + 0.5f * dtMs * k2.dm;
    float y3h = yh + 0.5f * dtMs * k2.dh;
    float y3n = yn + 0.5f * dtMs * k2.dn;
    float y3s = ys + 0.5f * dtMs * k2.ds;
    clampState(y3v, y3m, y3h, y3n, y3s);

    const Deriv k3 = derivatives(y3v, y3m, y3h, y3n, y3s, iTotal, safeGNa, safeGK, safeGCa, cm, eNa, eK, eCa, gL, eL);

    float y4v = yv + dtMs * k3.dv;
    float y4m = ym + dtMs * k3.dm;
    float y4h = yh + dtMs * k3.dh;
    float y4n = yn + dtMs * k3.dn;
    float y4s = ys + dtMs * k3.ds;
    clampState(y4v, y4m, y4h, y4n, y4s);

    const Deriv k4 = derivatives(y4v, y4m, y4h, y4n, y4s, iTotal, safeGNa, safeGK, safeGCa, cm, eNa, eK, eCa, gL, eL);

    yv += (dtMs / 6.0f) * (k1.dv + 2.0f * k2.dv + 2.0f * k3.dv + k4.dv);
    ym += (dtMs / 6.0f) * (k1.dm + 2.0f * k2.dm + 2.0f * k3.dm + k4.dm);
    yh += (dtMs / 6.0f) * (k1.dh + 2.0f * k2.dh + 2.0f * k3.dh + k4.dh);
    yn += (dtMs / 6.0f) * (k1.dn + 2.0f * k2.dn + 2.0f * k3.dn + k4.dn);
    ys += (dtMs / 6.0f) * (k1.ds + 2.0f * k2.ds + 2.0f * k3.ds + k4.ds);

    clampState(yv, ym, yh, yn, ys);

    if (!isfinite(yv) || !isfinite(ym) || !isfinite(yh) || !isfinite(yn) || !isfinite(ys)) {
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

struct CudaSimulator::DeviceBuffers {
    float* v = nullptr;
    float* m = nullptr;
    float* h = nullptr;
    float* n = nullptr;
    float* s = nullptr;

    float* threshold = nullptr;
    float* lastSpikeTime = nullptr;

    float* iSyn = nullptr;
    float* iExt = nullptr;
    float* iNoise = nullptr;
    float* gNaEff = nullptr;
    float* gKEff = nullptr;
    float* gCaEff = nullptr;

    std::uint8_t* spikes = nullptr;
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

        checkCuda(cudaMalloc(&buffers_->v, floatBytes), "cudaMalloc v");
        checkCuda(cudaMalloc(&buffers_->m, floatBytes), "cudaMalloc m");
        checkCuda(cudaMalloc(&buffers_->h, floatBytes), "cudaMalloc h");
        checkCuda(cudaMalloc(&buffers_->n, floatBytes), "cudaMalloc n");
        checkCuda(cudaMalloc(&buffers_->s, floatBytes), "cudaMalloc s");

        checkCuda(cudaMalloc(&buffers_->threshold, floatBytes), "cudaMalloc threshold");
        checkCuda(cudaMalloc(&buffers_->lastSpikeTime, floatBytes), "cudaMalloc lastSpikeTime");

        checkCuda(cudaMalloc(&buffers_->iSyn, floatBytes), "cudaMalloc iSyn");
        checkCuda(cudaMalloc(&buffers_->iExt, floatBytes), "cudaMalloc iExt");
        checkCuda(cudaMalloc(&buffers_->iNoise, floatBytes), "cudaMalloc iNoise");
        checkCuda(cudaMalloc(&buffers_->gNaEff, floatBytes), "cudaMalloc gNaEff");
        checkCuda(cudaMalloc(&buffers_->gKEff, floatBytes), "cudaMalloc gKEff");
        checkCuda(cudaMalloc(&buffers_->gCaEff, floatBytes), "cudaMalloc gCaEff");

        checkCuda(cudaMalloc(&buffers_->spikes, spikeBytes), "cudaMalloc spikes");
        available_ = true;
    } catch (...) {
        available_ = false;
        if (buffers_ != nullptr) {
            cudaFree(buffers_->v);
            cudaFree(buffers_->m);
            cudaFree(buffers_->h);
            cudaFree(buffers_->n);
            cudaFree(buffers_->s);
            cudaFree(buffers_->threshold);
            cudaFree(buffers_->lastSpikeTime);
            cudaFree(buffers_->iSyn);
            cudaFree(buffers_->iExt);
            cudaFree(buffers_->iNoise);
            cudaFree(buffers_->gNaEff);
            cudaFree(buffers_->gKEff);
            cudaFree(buffers_->gCaEff);
            cudaFree(buffers_->spikes);
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

    cudaFree(buffers_->threshold);
    cudaFree(buffers_->lastSpikeTime);

    cudaFree(buffers_->iSyn);
    cudaFree(buffers_->iExt);
    cudaFree(buffers_->iNoise);
    cudaFree(buffers_->gNaEff);
    cudaFree(buffers_->gKEff);
    cudaFree(buffers_->gCaEff);

    cudaFree(buffers_->spikes);
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
    const std::vector<float>& threshold,
    const std::vector<float>& lastSpikeTime
) {
    if (!available_) {
        throw std::runtime_error("CUDA simulator is not available.");
    }
    if (v.size() != neuronCount_ || m.size() != neuronCount_ || h.size() != neuronCount_ ||
        n.size() != neuronCount_ || s.size() != neuronCount_ || threshold.size() != neuronCount_ ||
        lastSpikeTime.size() != neuronCount_) {
        throw std::invalid_argument("CUDA upload vectors must all match neuron count.");
    }

    const std::size_t bytes = neuronCount_ * sizeof(float);

    checkCuda(cudaMemcpy(buffers_->v, v.data(), bytes, cudaMemcpyHostToDevice), "copy v H2D");
    checkCuda(cudaMemcpy(buffers_->m, m.data(), bytes, cudaMemcpyHostToDevice), "copy m H2D");
    checkCuda(cudaMemcpy(buffers_->h, h.data(), bytes, cudaMemcpyHostToDevice), "copy h H2D");
    checkCuda(cudaMemcpy(buffers_->n, n.data(), bytes, cudaMemcpyHostToDevice), "copy n H2D");
    checkCuda(cudaMemcpy(buffers_->s, s.data(), bytes, cudaMemcpyHostToDevice), "copy s H2D");
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
    std::vector<float>& lastSpikeTime,
    std::vector<std::uint8_t>& spikes
) {
    if (!available_) {
        throw std::runtime_error("CUDA simulator is not available.");
    }

    if (iSyn.size() != neuronCount_ || iExt.size() != neuronCount_ || iNoise.size() != neuronCount_ ||
        gNaEff.size() != neuronCount_ || gKEff.size() != neuronCount_ || gCaEff.size() != neuronCount_ ||
        v.size() != neuronCount_ || m.size() != neuronCount_ || h.size() != neuronCount_ ||
        n.size() != neuronCount_ || s.size() != neuronCount_ || lastSpikeTime.size() != neuronCount_ ||
        spikes.size() != neuronCount_) {
        throw std::invalid_argument("CUDA step vectors must all match neuron count.");
    }

    const std::size_t floatBytes = neuronCount_ * sizeof(float);
    const std::size_t spikeBytes = neuronCount_ * sizeof(std::uint8_t);

    checkCuda(cudaMemcpy(buffers_->iSyn, iSyn.data(), floatBytes, cudaMemcpyHostToDevice), "copy iSyn H2D");
    checkCuda(cudaMemcpy(buffers_->iExt, iExt.data(), floatBytes, cudaMemcpyHostToDevice), "copy iExt H2D");
    checkCuda(cudaMemcpy(buffers_->iNoise, iNoise.data(), floatBytes, cudaMemcpyHostToDevice), "copy iNoise H2D");
    checkCuda(cudaMemcpy(buffers_->gNaEff, gNaEff.data(), floatBytes, cudaMemcpyHostToDevice), "copy gNaEff H2D");
    checkCuda(cudaMemcpy(buffers_->gKEff, gKEff.data(), floatBytes, cudaMemcpyHostToDevice), "copy gKEff H2D");
    checkCuda(cudaMemcpy(buffers_->gCaEff, gCaEff.data(), floatBytes, cudaMemcpyHostToDevice), "copy gCaEff H2D");

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
        buffers_->lastSpikeTime,
        buffers_->spikes
    );

    checkCuda(cudaGetLastError(), "kernel launch");

    checkCuda(cudaMemcpy(spikes.data(), buffers_->spikes, spikeBytes, cudaMemcpyDeviceToHost), "copy spikes D2H");
}

void CudaSimulator::downloadState(
    std::vector<float>& v,
    std::vector<float>& m,
    std::vector<float>& h,
    std::vector<float>& n,
    std::vector<float>& s,
    std::vector<float>& lastSpikeTime
) {
    if (!available_) {
        throw std::runtime_error("CUDA simulator is not available.");
    }

    if (v.size() != neuronCount_ || m.size() != neuronCount_ || h.size() != neuronCount_ ||
        n.size() != neuronCount_ || s.size() != neuronCount_ || lastSpikeTime.size() != neuronCount_) {
        throw std::invalid_argument("CUDA download vectors must all match neuron count.");
    }

    const std::size_t floatBytes = neuronCount_ * sizeof(float);

    checkCuda(cudaMemcpy(v.data(), buffers_->v, floatBytes, cudaMemcpyDeviceToHost), "copy v D2H");
    checkCuda(cudaMemcpy(m.data(), buffers_->m, floatBytes, cudaMemcpyDeviceToHost), "copy m D2H");
    checkCuda(cudaMemcpy(h.data(), buffers_->h, floatBytes, cudaMemcpyDeviceToHost), "copy h D2H");
    checkCuda(cudaMemcpy(n.data(), buffers_->n, floatBytes, cudaMemcpyDeviceToHost), "copy n D2H");
    checkCuda(cudaMemcpy(s.data(), buffers_->s, floatBytes, cudaMemcpyDeviceToHost), "copy s D2H");
    checkCuda(cudaMemcpy(lastSpikeTime.data(), buffers_->lastSpikeTime, floatBytes, cudaMemcpyDeviceToHost), "copy lastSpike D2H");
}

} // namespace spp::cuda

#endif