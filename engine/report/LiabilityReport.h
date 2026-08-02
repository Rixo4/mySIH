#pragma once

// The locked evidence-format report template (PRECISION_GAP_CLOSURE_PLAN.md
// gap 10 -- "reframe the report output from verdict to evidence"). Replaces
// PROMISING/CAUTION/NOT RECOMMENDED verdict language with: what was
// measured, how confident we are, and what it explicitly does not prove.
// Format locked in project discussion before this was written -- see
// PRECISION_GAP_CLOSURE_PLAN.md 1.4 and the project conversation log for the
// worked example (pramipexole) that was used to finalize the section list.
//
// Kept alongside LegacyLiabilityReport.h/.cpp (the pre-gap-10 format) so the
// two can be printed side-by-side and diffed during the gap 1.1/1.2 audit.

#include <optional>
#include <string>

#include "ReportTypes.h"
#include "../analyzer/PharmaDecisionEngine.h"

namespace spp::report {

std::string buildLiabilityReportText(
    const spp::analyzer::PharmaDecisionReport& report,
    const AggregatedStats& stabilityStats,
    int runCount,
    const RuntimeInput& evalInput,
    const std::string& engineInputMode,
    std::optional<bool> usedGpu = std::nullopt);

void writeLiabilityReport(
    const std::string& path, const spp::analyzer::PharmaDecisionReport& report,
    const AggregatedStats& stats, int runs,
    const RuntimeInput& input, const std::string& mode,
    std::optional<bool> usedGpu = std::nullopt);

} // namespace spp::report