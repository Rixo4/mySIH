#include "NeuronModel.h"
#include "ReceptorModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace spp::neuron {

namespace {

constexpr float kEpsilon = 1.0e-6f;

float safeAlphaTransform(float x, float scale, float singularLimit) {
    if (std::fabs(x) < kEpsilon) {
        return singularLimit;
    }
    return scale * x / (1.0f - std::exp(-x));
}

float clamp01(float x) {
    return std::clamp(x, 0.0f, 1.0f);
}

} // namespace

NeuronPopulation::NeuronPopulation(std::size_t neuronCount)
    : v(neuronCount),
      m(neuronCount),
      h(neuronCount),
      n(neuronCount),
      s(neuronCount),
      gNa(neuronCount),
      gK(neuronCount),
      caCa(neuronCount),
      gCa(neuronCount),
      gL(neuronCount),
      threshold(neuronCount),
      noiseStd(neuronCount),
      extCurrent(neuronCount),
      lastSpikeTime(neuronCount),
      neuronType(neuronCount, 1U) {}

void NeuronPopulation::initialize(
    float baseExternalCurrent,
    float externalCurrentStd,
    float baseNoiseStd,
    std::uint32_t seed
) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> conductanceVariation(-0.10f, 0.10f);
    std::uniform_real_distribution<float> thresholdVariation(-3.0f, 3.0f);
    std::uniform_real_distribution<float> noiseVariation(-0.22f, 0.22f);
    std::normal_distribution<float> extJitter(0.0f, externalCurrentStd);
    std::uniform_real_distribution<float> initialVoltageJitter(-5.0f, 5.0f);

    for (std::size_t i = 0; i < size(); ++i) {
        const float gNaVar = 1.0f + conductanceVariation(rng);
        const float gKVar = 1.0f + conductanceVariation(rng);
        const float gCaVar = 1.0f + conductanceVariation(rng);

        gNa[i] = params.gNa * gNaVar;
        gK[i] = params.gK * gKVar;
        gCa[i] = params.gCa * gCaVar;
        gL[i] = params.gL;

        threshold[i] = 0.0f + thresholdVariation(rng);
        noiseStd[i] = std::max(0.11f, baseNoiseStd * (1.0f + noiseVariation(rng)));
        extCurrent[i] = baseExternalCurrent + extJitter(rng);
        caCa[i] = 0.0f;

        const float v0 = params.restingVoltage + initialVoltageJitter(rng);
        const HHState steady = steadyStateAtVoltage(v0);

        v[i] = v0;
        m[i] = steady.m;
        h[i] = steady.h;
        n[i] = steady.n;
        s[i] = steady.s;

        lastSpikeTime[i] = -1.0e6f;
    }
}

float alphaM(float vMv) {
    return safeAlphaTransform((vMv + 40.0f) / 10.0f, 1.0f, 1.0f);
}

float betaM(float vMv) {
    return 4.0f * std::exp(-(vMv + 65.0f) / 18.0f);
}

float alphaH(float vMv) {
    return 0.07f * std::exp(-(vMv + 65.0f) / 20.0f);
}

float betaH(float vMv) {
    return 1.0f / (1.0f + std::exp(-(vMv + 35.0f) / 10.0f));
}

float alphaN(float vMv) {
    return safeAlphaTransform((vMv + 55.0f) / 10.0f, 0.1f, 0.1f);
}

float betaN(float vMv) {
    return 0.125f * std::exp(-(vMv + 65.0f) / 80.0f);
}

float sInf(float vMv) {
    return 1.0f / (1.0f + std::exp(-(vMv + 20.0f) / 6.5f));
}

float tauS(float vMv) {
    return 5.0f + 20.0f / (1.0f + std::exp((vMv + 25.0f) / 5.0f));
}

HHState steadyStateAtVoltage(float vMv) {
    HHState state;
    state.v = vMv;

    const float am = alphaM(vMv);
    const float bm = betaM(vMv);
    const float ah = alphaH(vMv);
    const float bh = betaH(vMv);
    const float an = alphaN(vMv);
    const float bn = betaN(vMv);

    state.m = am / (am + bm + kEpsilon);
    state.h = ah / (ah + bh + kEpsilon);
    state.n = an / (an + bn + kEpsilon);
    state.s = sInf(vMv);
    state.caCa = 0.0f;
    clampState(state);
    return state;
}

HHDerivatives computeDerivatives(
    const HHState& state,
    float iTotal,
    float gNaEff,
    float gKEff,
    float gCaEff,
    const HHParameters& params,
    const SynapticConductances& synaptic
) {
    const float iNa = gNaEff * state.m * state.m * state.m * state.h * (state.v - params.eNa);
    const float iK = gKEff * state.n * state.n * state.n * state.n * (state.v - params.eK);
    const float iCa = gCaEff * state.s * state.s * (state.v - params.eCa);
    // gAHPFloor keeps a small calcium-independent brake active even when
    // caCa is near zero -- see HHParameters comment in NeuronModel.h.
    const float iAHP = (params.gAHP * state.caCa + params.gAHPFloor) * (state.v - params.eK);
    const float iL = params.gL * (state.v - params.eL);
    const float caInflux = params.kCa * std::max(0.0f, -iCa);

    // GABA-A, NMDA, and now GABA-B are real (see SynapticConductances
    // comment) -- all computed here, not pre-summed into iTotal by the
    // caller, specifically so they see the correct per-RK4-substage voltage
    // rather than just the voltage at the start of the full timestep. This
    // matters more for NMDA than the two GABA currents: the Mg2+-block
    // fraction is itself voltage-dependent, so evaluating it at a stale
    // voltage would make the block lag the actual membrane trajectory
    // during a spike.
    const float iGABAa = synaptic.gGABAaEff * (state.v - params.eGABAa);
    const float nmdaUnblock = nmdaMgBlockFraction(state.v);
    const float iNMDA = synaptic.gNMDAEff * nmdaUnblock * (state.v - params.eNMDA);
    // GABA-B: real metabotropic K+ conductance, reuses eK -- no voltage-gated
    // block term like NMDA's, just a plain ohmic current the same shape as
    // GABA-A's.
    const float iGABAb = synaptic.gGABAbEff * (state.v - params.eK);

    HHDerivatives d;
    d.dv = (iTotal - iNa - iK - iCa - iAHP - iL - iGABAa - iNMDA - iGABAb) / params.cm;
    d.dm = alphaM(state.v) * (1.0f - state.m) - betaM(state.v) * state.m;
    d.dh = alphaH(state.v) * (1.0f - state.h) - betaH(state.v) * state.h;
    d.dn = alphaN(state.v) * (1.0f - state.n) - betaN(state.v) * state.n;
    d.ds = (sInf(state.v) - state.s) / tauS(state.v);
    d.dcaCa = caInflux - state.caCa / params.tauCa;

    return d;
}

void rk4Step(
    HHState& state,
    float dtMs,
    float iTotal,
    float gNaEff,
    float gKEff,
    float gCaEff,
    const HHParameters& params,
    const SynapticConductances& synaptic
) {
    const HHDerivatives k1 = computeDerivatives(state, iTotal, gNaEff, gKEff, gCaEff, params, synaptic);

    HHState y2 = state;
    y2.v += 0.5f * dtMs * k1.dv;
    y2.m += 0.5f * dtMs * k1.dm;
    y2.h += 0.5f * dtMs * k1.dh;
    y2.n += 0.5f * dtMs * k1.dn;
    y2.s += 0.5f * dtMs * k1.ds;
    y2.caCa += 0.5f * dtMs * k1.dcaCa;
    clampState(y2);
    const HHDerivatives k2 = computeDerivatives(y2, iTotal, gNaEff, gKEff, gCaEff, params, synaptic);

    HHState y3 = state;
    y3.v += 0.5f * dtMs * k2.dv;
    y3.m += 0.5f * dtMs * k2.dm;
    y3.h += 0.5f * dtMs * k2.dh;
    y3.n += 0.5f * dtMs * k2.dn;
    y3.s += 0.5f * dtMs * k2.ds;
    y3.caCa += 0.5f * dtMs * k2.dcaCa;
    clampState(y3);
    const HHDerivatives k3 = computeDerivatives(y3, iTotal, gNaEff, gKEff, gCaEff, params, synaptic);

    HHState y4 = state;
    y4.v += dtMs * k3.dv;
    y4.m += dtMs * k3.dm;
    y4.h += dtMs * k3.dh;
    y4.n += dtMs * k3.dn;
    y4.s += dtMs * k3.ds;
    y4.caCa += dtMs * k3.dcaCa;
    clampState(y4);
    const HHDerivatives k4 = computeDerivatives(y4, iTotal, gNaEff, gKEff, gCaEff, params, synaptic);

    state.v += (dtMs / 6.0f) * (k1.dv + 2.0f * k2.dv + 2.0f * k3.dv + k4.dv);
    state.m += (dtMs / 6.0f) * (k1.dm + 2.0f * k2.dm + 2.0f * k3.dm + k4.dm);
    state.h += (dtMs / 6.0f) * (k1.dh + 2.0f * k2.dh + 2.0f * k3.dh + k4.dh);
    state.n += (dtMs / 6.0f) * (k1.dn + 2.0f * k2.dn + 2.0f * k3.dn + k4.dn);
    state.s += (dtMs / 6.0f) * (k1.ds + 2.0f * k2.ds + 2.0f * k3.ds + k4.ds);
    state.caCa += (dtMs / 6.0f) * (k1.dcaCa + 2.0f * k2.dcaCa + 2.0f * k3.dcaCa + k4.dcaCa);

    clampState(state);

    if (!isFiniteState(state)) {
        state = steadyStateAtVoltage(params.restingVoltage);
    }
}

bool isFiniteState(const HHState& state) {
    return std::isfinite(state.v) &&
           std::isfinite(state.m) &&
           std::isfinite(state.h) &&
           std::isfinite(state.n) &&
           std::isfinite(state.s) &&
           std::isfinite(state.caCa);
}

void clampState(HHState& state) {
    state.v = std::clamp(state.v, -120.0f, 80.0f);
    state.m = clamp01(state.m);
    state.h = clamp01(state.h);
    state.n = clamp01(state.n);
    state.s = clamp01(state.s);
    state.caCa = clamp01(state.caCa);
}

} // namespace spp::neuron