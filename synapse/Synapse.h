#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace spp::synapse {

enum class SynapseType : std::uint8_t {
    Excitatory = 0,
    Inhibitory = 1
};

struct SynapseEdge {
    std::uint32_t preNeuron = 0;
    std::uint32_t postNeuron = 0;
    float weight = 0.0f;
    std::uint32_t delaySteps = 1;
    SynapseType type = SynapseType::Excitatory;
};

class DelayBuffer {
public:
    DelayBuffer();
    DelayBuffer(std::size_t neuronCount, std::size_t delaySteps);

    void resize(std::size_t neuronCount, std::size_t delaySteps);
    void clear();

    [[nodiscard]] std::size_t neuronCount() const { return neuronCount_; }
    [[nodiscard]] std::size_t delaySteps() const { return delaySteps_; }

    void pushSpikes(const std::vector<std::uint8_t>& spikes);
    [[nodiscard]] std::uint8_t getDelayedSpike(std::size_t neuronId, std::size_t delaySteps) const;

private:
    std::size_t neuronCount_;
    std::size_t delaySteps_;
    std::size_t head_;
    std::vector<std::uint8_t> buffer_;

    [[nodiscard]] std::size_t rowIndexForDelay(std::size_t delaySteps) const;
};

class SynapseMatrix {
public:
    void build(std::size_t neuronCount, const std::vector<SynapseEdge>& edges);

    [[nodiscard]] std::size_t neuronCount() const { return neuronCount_; }
    [[nodiscard]] std::size_t edgeCount() const { return incomingPre_.size(); }

    void accumulateSynapticCurrents(
        const DelayBuffer& delayBuffer,
        std::vector<float>& excitatoryCurrent,
        std::vector<float>& inhibitoryCurrent
    ) const;

private:
    std::size_t neuronCount_ = 0;
    std::vector<std::uint32_t> incomingOffsets_;
    std::vector<std::uint32_t> incomingPre_;
    std::vector<std::uint32_t> incomingDelay_;
    std::vector<float> incomingWeight_;
    std::vector<std::int8_t> incomingSign_;
};

} // namespace spp::synapse
