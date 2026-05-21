#pragma once

#include <string>
#include <vector>

#include "../analyzer/Metrics.h"
#include "../analyzer/PharmaDecisionEngine.h"

namespace spp::output {

struct NetworkMetricRecord {
    std::string scenario;
    float dose = 0.0f;
    analyzer::NetworkMetrics metrics;
    std::string classification;
};

struct DoseResponsePoint {
    float dose = 0.0f;
    float meanFiringRateHz = 0.0f;
    float synchronization = 0.0f;
    float burstIndex = 0.0f;
    float nii = 0.0f;
    float seizureProbabilityPct = 0.0f;
    float suppressionPct = 0.0f;
    float stabilityScore = 0.0f;
    std::string classification;
};

class CsvWriter {
public:
    static void writeNeuronStats(
        const std::string& filePath,
        const std::vector<analyzer::NeuronMetrics>& neuronMetrics
    );

    static void writeNetworkMetrics(
        const std::string& filePath,
        const std::vector<NetworkMetricRecord>& records
    );

    static void writeDoseResponse(
        const std::string& filePath,
        const std::vector<DoseResponsePoint>& points
    );

    static void writeDrugSummary(
        const std::string& filePath,
        const std::vector<analyzer::DrugDecisionPoint>& points
    );

    static void writeTimeMetrics(
        const std::string& filePath,
        const std::vector<analyzer::TimeWindowMetrics>& points
    );
};

} // namespace spp::output
