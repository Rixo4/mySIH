#include "CudaSimulator.h"

#include <stdexcept>

namespace spp::cuda {

#ifndef SPP_USE_CUDA

CudaSimulator::CudaSimulator(std::size_t neuronCount)
    : neuronCount_(neuronCount), available_(false), buffers_(nullptr) {}

CudaSimulator::~CudaSimulator() = default;

bool CudaSimulator::available() const {
    return false;
}

void CudaSimulator::uploadInitialState(
    const std::vector<float>&,
    const std::vector<float>&,
    const std::vector<float>&,
    const std::vector<float>&,
    const std::vector<float>&,
    const std::vector<float>&,
    const std::vector<float>&
) {
    throw std::runtime_error("CUDA support is not enabled in this build.");
}

void CudaSimulator::step(
    float,
    float,
    const neuron::HHParameters&,
    float,
    const std::vector<float>&,
    const std::vector<float>&,
    const std::vector<float>&,
    const std::vector<float>&,
    const std::vector<float>&,
    const std::vector<float>&,
    std::vector<float>&,
    std::vector<float>&,
    std::vector<float>&,
    std::vector<float>&,
    std::vector<float>&,
    std::vector<float>&,
    std::vector<std::uint8_t>&
) {
    throw std::runtime_error("CUDA support is not enabled in this build.");
}

void CudaSimulator::downloadState(
    std::vector<float>&,
    std::vector<float>&,
    std::vector<float>&,
    std::vector<float>&,
    std::vector<float>&,
    std::vector<float>&
) {
    throw std::runtime_error("CUDA support is not enabled in this build.");
}

#endif

} // namespace spp::cuda
