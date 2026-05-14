#include "Network.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

namespace spp::network {

Network::Network(const NetworkConfig& config)
    : config_(config), neuronTypes_(config.neuronCount, 1U), edges_(), matrix_() {
    if (config_.neuronCount == 0) {
        throw std::invalid_argument("Network neuron count must be > 0.");
    }
    if (config_.connectionProbability <= 0.0f || config_.connectionProbability > 1.0f) {
        throw std::invalid_argument("Connection probability must be in (0, 1].");
    }
    if (config_.excitatoryFraction <= 0.0f || config_.excitatoryFraction >= 1.0f) {
        throw std::invalid_argument("Excitatory fraction must be in (0, 1).");
    }
    if (config_.excitatoryWeightMin <= 0.0f || config_.excitatoryWeightMax <= config_.excitatoryWeightMin) {
        throw std::invalid_argument("Invalid excitatory weight range.");
    }
    if (config_.inhibitoryWeightMin <= 0.0f || config_.inhibitoryWeightMax <= config_.inhibitoryWeightMin) {
        throw std::invalid_argument("Invalid inhibitory weight range.");
    }
    if (config_.recurrentExcitatoryBias <= 0.0f || config_.recurrentExcitatoryBias >= 1.0f) {
        throw std::invalid_argument("Recurrent excitatory bias must be in (0, 1).");
    }
    if (config_.feedbackInhibitoryBias <= 0.0f || config_.feedbackInhibitoryBias >= 1.0f) {
        throw std::invalid_argument("Feedback inhibitory bias must be in (0, 1).");
    }
}

void Network::buildRandom() {
    std::mt19937 rng(config_.randomSeed);

    const std::size_t excitatoryCount = static_cast<std::size_t>(
        std::round(static_cast<float>(config_.neuronCount) * config_.excitatoryFraction)
    );

    neuronTypes_.assign(config_.neuronCount, 0U);
    for (std::size_t i = 0; i < std::min(excitatoryCount, config_.neuronCount); ++i) {
        neuronTypes_[i] = 1U;
    }
    std::shuffle(neuronTypes_.begin(), neuronTypes_.end(), rng);

    std::vector<std::uint32_t> excitatoryNeurons;
    std::vector<std::uint32_t> inhibitoryNeurons;
    excitatoryNeurons.reserve(excitatoryCount);
    inhibitoryNeurons.reserve(config_.neuronCount - std::min(excitatoryCount, config_.neuronCount));
    for (std::uint32_t i = 0U; i < static_cast<std::uint32_t>(config_.neuronCount); ++i) {
        if (neuronTypes_[i] == 1U) {
            excitatoryNeurons.push_back(i);
        } else {
            inhibitoryNeurons.push_back(i);
        }
    }

    const double meanOutDegree = std::max(1.0, static_cast<double>(config_.connectionProbability) *
                                                static_cast<double>(config_.neuronCount - 1));

    const std::size_t estimatedEdges = static_cast<std::size_t>(
        std::min<double>(
            static_cast<double>(config_.maxSynapses),
            static_cast<double>(config_.neuronCount) * meanOutDegree
        )
    );

    edges_.clear();
    edges_.reserve(estimatedEdges);

    std::poisson_distribution<int> outDegreeDist(meanOutDegree);
    std::uniform_int_distribution<std::uint32_t> neuronDist(0U, static_cast<std::uint32_t>(config_.neuronCount - 1));
    std::bernoulli_distribution recurrentExcChoice(config_.recurrentExcitatoryBias);
    std::bernoulli_distribution feedbackInhChoice(config_.feedbackInhibitoryBias);

    const float densityWeightScale = std::clamp(
        std::sqrt(0.10f / std::max(0.02f, config_.connectionProbability)),
        0.70f,
        1.30f
    );

    auto sampleFromPool = [&](const std::vector<std::uint32_t>& pool, std::uint32_t selfId) {
        if (pool.empty()) {
            std::uint32_t fallback = neuronDist(rng);
            if (fallback == selfId) {
                fallback = (fallback + 1U) % static_cast<std::uint32_t>(config_.neuronCount);
            }
            return fallback;
        }

        std::uniform_int_distribution<std::size_t> poolDist(0U, pool.size() - 1U);
        for (int tries = 0; tries < 4; ++tries) {
            const std::uint32_t candidate = pool[poolDist(rng)];
            if (candidate != selfId) {
                return candidate;
            }
        }

        std::uint32_t fallback = neuronDist(rng);
        if (fallback == selfId) {
            fallback = (fallback + 1U) % static_cast<std::uint32_t>(config_.neuronCount);
        }
        return fallback;
    };

    for (std::size_t pre = 0; pre < config_.neuronCount; ++pre) {
        const int sampled = outDegreeDist(rng);
        int outDegree = std::max(1, sampled);

        const bool excitatory = neuronTypes_[pre] == 1U;
        if (excitatory) {
            outDegree = std::max(1, static_cast<int>(std::round(static_cast<float>(outDegree) * 0.86f)));
        } else {
            outDegree = std::max(1, static_cast<int>(std::round(static_cast<float>(outDegree) * 1.00f)));
        }

        const float weightMean = excitatory ? config_.excitatoryWeightMean : config_.inhibitoryWeightMean;
        const float weightStd = std::max(0.01f, weightMean * config_.weightStdFraction);
        std::normal_distribution<float> weightDist(weightMean, weightStd);

        const std::uint32_t delayMin = std::max<std::uint32_t>(1U, config_.minDelaySteps);
        const std::uint32_t delayMax = std::max(config_.minDelaySteps, config_.maxDelaySteps);
        const std::uint32_t localDelayMax = excitatory
                                                ? std::max<std::uint32_t>(delayMin, std::min<std::uint32_t>(delayMax, 12U))
                                                : delayMax;
        std::uniform_int_distribution<std::uint32_t> delayDist(delayMin, localDelayMax);

        for (int e = 0; e < outDegree; ++e) {
            if (edges_.size() >= config_.maxSynapses) {
                break;
            }

            const bool preferExcTarget = excitatory ? recurrentExcChoice(rng) : feedbackInhChoice(rng);
            const std::vector<std::uint32_t>& primaryPool = preferExcTarget ? excitatoryNeurons : inhibitoryNeurons;
            const std::vector<std::uint32_t>& secondaryPool = preferExcTarget ? inhibitoryNeurons : excitatoryNeurons;

            std::uint32_t post = sampleFromPool(primaryPool, static_cast<std::uint32_t>(pre));
            if (post == pre && !secondaryPool.empty()) {
                post = sampleFromPool(secondaryPool, static_cast<std::uint32_t>(pre));
            }

            const float rawWeight = std::fabs(weightDist(rng)) * densityWeightScale;
            const float weight = excitatory
                                     ? std::clamp(rawWeight, config_.excitatoryWeightMin, config_.excitatoryWeightMax)
                                     : std::clamp(rawWeight, config_.inhibitoryWeightMin, config_.inhibitoryWeightMax);

            synapse::SynapseEdge edge;
            edge.preNeuron = static_cast<std::uint32_t>(pre);
            edge.postNeuron = post;
            edge.weight = weight;
            edge.delaySteps = delayDist(rng);
            edge.type = excitatory ? synapse::SynapseType::Excitatory : synapse::SynapseType::Inhibitory;
            edges_.push_back(edge);
        }

        if (edges_.size() < config_.maxSynapses && excitatory && recurrentExcChoice(rng) && !excitatoryNeurons.empty()) {
            synapse::SynapseEdge motif;
            motif.preNeuron = static_cast<std::uint32_t>(pre);
            motif.postNeuron = sampleFromPool(excitatoryNeurons, motif.preNeuron);
            motif.weight = std::clamp(
                config_.excitatoryWeightMax * 0.76f,
                config_.excitatoryWeightMin,
                config_.excitatoryWeightMax
            );
            motif.delaySteps = std::max<std::uint32_t>(1U, std::min<std::uint32_t>(6U, config_.maxDelaySteps));
            motif.type = synapse::SynapseType::Excitatory;
            edges_.push_back(motif);
        }

        if (edges_.size() < config_.maxSynapses && !excitatory && feedbackInhChoice(rng) && !excitatoryNeurons.empty()) {
            synapse::SynapseEdge motif;
            motif.preNeuron = static_cast<std::uint32_t>(pre);
            motif.postNeuron = sampleFromPool(excitatoryNeurons, motif.preNeuron);
            motif.weight = std::clamp(
                config_.inhibitoryWeightMax * 0.74f,
                config_.inhibitoryWeightMin,
                config_.inhibitoryWeightMax
            );
            motif.delaySteps = std::max<std::uint32_t>(2U, std::min<std::uint32_t>(10U, config_.maxDelaySteps));
            motif.type = synapse::SynapseType::Inhibitory;
            edges_.push_back(motif);
        }

        if (edges_.size() >= config_.maxSynapses) {
            break;
        }
    }

    matrix_.build(config_.neuronCount, edges_);
}

} // namespace spp::network
