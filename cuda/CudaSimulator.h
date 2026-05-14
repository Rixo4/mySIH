#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../neuron/NeuronModel.h"

namespace spp::cuda {

class CudaSimulator {
public:
    explicit CudaSimulator(std::size_t neuronCount);
    ~CudaSimulator();

    CudaSimulator(const CudaSimulator&) = delete;
    CudaSimulator& operator=(const CudaSimulator&) = delete;

    [[nodiscard]] bool available() const;

    void uploadInitialState(
        const std::vector<float>& v,
        const std::vector<float>& m,
        const std::vector<float>& h,
        const std::vector<float>& n,
        const std::vector<float>& s,
        const std::vector<float>& threshold,
        const std::vector<float>& lastSpikeTime
    );

    void step(
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
    );

    void downloadState(
        std::vector<float>& v,
        std::vector<float>& m,
        std::vector<float>& h,
        std::vector<float>& n,
        std::vector<float>& s,
        std::vector<float>& lastSpikeTime
    );

private:
    std::size_t neuronCount_;
    bool available_;

    struct DeviceBuffers;
    DeviceBuffers* buffers_;
};

} // namespace spp::cuda
