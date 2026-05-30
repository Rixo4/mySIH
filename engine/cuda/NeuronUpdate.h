#pragma once

#include <cstddef>

namespace spp::cuda {

struct NeuronUpdateLaunchInfo {
    int blockSize = 256;
    int gridSize = 1;
};

NeuronUpdateLaunchInfo computeNeuronUpdateLaunchInfo(
    std::size_t neuronCount,
    int preferredBlockSize = 256
);

} // namespace spp::cuda
