#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace spp::neuron {

struct HHParameters {
    float cm = 1.0f;             // uF/cm^2
    float gNa = 120.0f;          // mS/cm^2
    float gK = 36.0f;            // mS/cm^2
    float gCa = 1.2f;            // mS/cm^2
    float gL = 0.3f;             // mS/cm^2
    float eNa = 50.0f;           // mV
    float eK = -77.0f;           // mV
    float eCa = 120.0f;          // mV
    float eL = -54.4f;           // mV
    float gAHP  = 5.0f;          // Ca-activated K conductance mS/cm²
    float tauCa = 80.0f;         // Ca decay time constant ms
    float restingVoltage = -65.0f;
};

struct HHState {
    float v = -65.0f;
    float m = 0.05f;
    float h = 0.6f;
    float n = 0.32f;
    float s = 0.05f;
    float caCa = 0.0f; // intracellular calcium concentration
};

struct HHDerivatives {
    float dv = 0.0f;
    float dm = 0.0f;
    float dh = 0.0f;
    float dn = 0.0f;
    float ds = 0.0f;
    float dcaCa = 0.0f; // derivative of intracellular calcium concentration
};

class NeuronPopulation {
public:
    explicit NeuronPopulation(std::size_t neuronCount);

    void initialize(float baseExternalCurrent, float externalCurrentStd, float baseNoiseStd, std::uint32_t seed);

    [[nodiscard]] std::size_t size() const { return v.size(); }

    HHParameters params;

    std::vector<float> v;
    std::vector<float> m;
    std::vector<float> h;
    std::vector<float> n;
    std::vector<float> s;

    std::vector<float> gNa;
    std::vector<float> gK;
    std::vector<float> gCa;
    std::vector<float> gL;

    std::vector<float> threshold;
    std::vector<float> noiseStd;
    std::vector<float> extCurrent;
    std::vector<float> lastSpikeTime;
    std::vector<std::uint8_t> neuronType; // 1=Excitatory, 0=Inhibitory
};

float alphaM(float vMv);
float betaM(float vMv);
float alphaH(float vMv);
float betaH(float vMv);
float alphaN(float vMv);
float betaN(float vMv);
float sInf(float vMv);
float tauS(float vMv);

HHState steadyStateAtVoltage(float vMv);

HHDerivatives computeDerivatives(
    const HHState& state,
    float iTotal,
    float gNaEff,
    float gKEff,
    float gCaEff,
    const HHParameters& params
);

void rk4Step(
    HHState& state,
    float dtMs,
    float iTotal,
    float gNaEff,
    float gKEff,
    float gCaEff,
    const HHParameters& params
);

bool isFiniteState(const HHState& state);
void clampState(HHState& state);

} // namespace spp::neuron
