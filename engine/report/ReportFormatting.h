#pragma once

// Small, pure formatting helpers used by the report layer, extracted from
// engine/main.cpp (see ReportTypes.h for the extraction rationale). Header-
// only/inline since these are tiny and this avoids adding another .cpp to
// CMakeLists.txt for a handful of one-line functions.

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace spp::report {

constexpr int kRuntimeOutputPrecision = 2;

// Gap 1.1 audit fix (ziconotide): picomolar-potency peptide drugs need
// IC50/dose values as small as ~1e-5, but every caller below formats with a
// small fixed decimal count (2-4 places). A genuinely nonzero value that
// small was silently rounding to "0.0000" everywhere it appears -- the
// header's Ca IC50, the Dose Range Tested line, every per-dose row, and the
// Excitatory/Effective range -- even though the engine was computing real,
// distinct per-dose effects underneath (confirmed via ziconotide's own
// report: R^2=0.88, 17.1% max effect, MEDIUM evidence quality, i.e. a real
// simulation result hidden behind an all-zeros display). This was a display
// bug, not a simulation bug -- the underlying per-dose values were correct,
// only unrepresentable at fixed precision. Fix: if a nonzero value would
// print as all zeros at the requested fixed precision, fall back to
// scientific notation with 2 significant digits instead. Normal-magnitude
// configs (the overwhelming majority) are completely unaffected by this --
// it only activates when fixed-precision would actively lose the value.
inline std::string formatRuntimeNumber(double value, int precision = kRuntimeOutputPrecision) {
    if (value != 0.0) {
        const double roundedAway = std::pow(10.0, -precision) * 0.5;
        if (std::fabs(value) < roundedAway) {
            std::ostringstream sci;
            sci << std::scientific << std::setprecision(2) << value;
            return sci.str();
        }
    }
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