#include "CsvWriter.h"

#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace spp::output {

void CsvWriter::writeNeuronStats(
    const std::string& filePath,
    const std::vector<analyzer::NeuronMetrics>& neuronMetrics
) {
    std::ofstream out(filePath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("Unable to open neuron stats output file: " + filePath);
    }

    out << "neuron_id,spike_count,firing_rate_hz,isi_mean_ms,isi_variance_ms\n";
    out << std::fixed << std::setprecision(6);

    for (std::size_t i = 0; i < neuronMetrics.size(); ++i) {
        const auto& m = neuronMetrics[i];
        out << i << ',' << m.spikeCount << ',' << m.firingRateHz << ',' << m.isiMeanMs << ',' << m.isiVarianceMs << '\n';
    }
}

void CsvWriter::writeNetworkMetrics(
    const std::string& filePath,
    const std::vector<NetworkMetricRecord>& records
) {
    std::ofstream out(filePath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("Unable to open network metrics output file: " + filePath);
    }

    out << "scenario,dose,classification,mean_firing_rate_hz,synchronization,burst_index,population_variance,voltage_variance,irregularity_index,early_rate_hz,late_rate_hz,nii,seizure_probability_pct,suppression_pct,stability_score\n";
    out << std::fixed << std::setprecision(6);

    for (const auto& rec : records) {
        out << rec.scenario << ','
            << rec.dose << ','
            << rec.classification << ','
            << rec.metrics.meanFiringRateHz << ','
            << rec.metrics.synchronizationIndex << ','
            << rec.metrics.burstIndex << ','
            << rec.metrics.populationVariance << ','
            << 0.0 << ','
            << rec.metrics.irregularityIndex << ','
            << rec.metrics.earlyWindowRateHz << ','
            << rec.metrics.lateWindowRateHz << ','
            << 0.0 << ','
            << 0.0 << ','
            << 0.0 << ','
            << rec.metrics.stabilityScore << '\n';
    }
}

void CsvWriter::writeDoseResponse(
    const std::string& filePath,
    const std::vector<DoseResponsePoint>& points
) {
    std::ofstream out(filePath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("Unable to open dose response output file: " + filePath);
    }

    out << "dose,classification,mean_firing_rate_hz,synchronization,burst_index,nii,seizure_probability_pct,suppression_pct,stability_score\n";
    out << std::fixed << std::setprecision(6);

    for (const auto& p : points) {
        out << p.dose << ','
            << p.classification << ','
            << p.meanFiringRateHz << ','
            << p.synchronization << ','
            << p.burstIndex << ','
            << p.nii << ','
            << p.seizureProbabilityPct << ','
            << p.suppressionPct << ','
            << p.stabilityScore << '\n';
    }
}

void CsvWriter::writeDrugSummary(
    const std::string& filePath,
    const std::vector<analyzer::DrugDecisionPoint>& points
) {
    std::ofstream out(filePath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("Unable to open drug summary output file: " + filePath);
    }

    out << "dose,seizure_probability_pct,suppression_pct,risk_score,classification,early_warning_index,seizure_slope_pct_per_dose\n";
    out << std::fixed << std::setprecision(6);

    for (const auto& p : points) {
        out << p.dose << ','
            << p.seizureProbabilityPct << ','
            << p.suppressionPct << ','
            << p.riskScore << ','
            << p.classification << ','
            << p.earlyWarningIndex << ','
            << p.seizureSlopePctPerDose << '\n';
    }
}

void CsvWriter::writeTimeMetrics(
    const std::string& filePath,
    const std::vector<analyzer::TimeWindowMetrics>& points
) {
    std::ofstream out(filePath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("Unable to open time metrics output file: " + filePath);
    }

    out << "time_start_ms,time_end_ms,mean_firing_rate_hz,synchronization,burst_index,burst_rate_hz,bursting_neuron_pct,irregularity_index,mean_burst_duration_ms\n";
    out << std::fixed << std::setprecision(6);

    for (const auto& p : points) {
        out << p.startMs << ','
            << p.endMs << ','
            << p.meanFiringRateHz << ','
            << p.synchronizationIndex << ','
            << p.burstIndex << ','
            << p.burstRateHz << ','
            << p.burstingNeuronPct << ','
            << p.irregularityIndex << ','
            << p.meanBurstDurationMs << '\n';
    }
}

} // namespace spp::output
