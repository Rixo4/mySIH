#pragma once

// Small, pure formatting helpers used by the report layer, extracted from
// engine/main.cpp (see ReportTypes.h for the extraction rationale). Header-
// only/inline since these are tiny and this avoids adding another .cpp to
// CMakeLists.txt for a handful of one-line functions.

#include <iomanip>
#include <sstream>
#include <string>

namespace spp::report {

constexpr int kRuntimeOutputPrecision = 2;

inline std::string formatRuntimeNumber(double value, int precision = kRuntimeOutputPrecision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

inline std::string getStability(float stdRate, float stdSync) {
    if (stdRate<1.0f&&stdSync<0.05f) return "HIGH";
    if (stdRate<2.0f&&stdSync<0.10f) return "MEDIUM";
    return "LOW";
}

} // namespace spp::report
