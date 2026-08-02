#pragma once

// The pre-gap-10 report format (PROMISING/CAUTION/NOT RECOMMENDED verdict
// style), extracted verbatim from engine/main.cpp -- see ReportTypes.h for
// the extraction rationale. Kept alongside the new LiabilityReport.h/.cpp
// (the locked evidence-format template, PRECISION_GAP_CLOSURE_PLAN.md gap
// 10) specifically so the two can be printed side-by-side and diffed during
// the gap 1.1 audit, per that plan's own stated deliverable ("reviewed side
// by side with the old format") -- not kept out of nostalgia or an oversight.
// Candidate for deletion once the audit is done and the new format is fully
// trusted.

#include <optional>
#include <string>

#include "ReportTypes.h"
#include "../analyzer/PharmaDecisionEngine.h"

namespace spp::report {

std::string buildDrugEvaluationReportText(
    const spp::analyzer::PharmaDecisionReport& report,
    const AggregatedStats& stabilityStats,
    int runCount,
    const RuntimeInput& evalInput,
    const std::string& engineInputMode,
    std::optional<bool> usedGpu = std::nullopt);

void writeDrugEvaluationReport(
    const std::string& path, const spp::analyzer::PharmaDecisionReport& report,
    const AggregatedStats& stats, int runs,
    const RuntimeInput& input, const std::string& mode,
    std::optional<bool> usedGpu = std::nullopt);

} // namespace spp::report