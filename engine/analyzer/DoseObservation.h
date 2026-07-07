#pragma once

#include "RawMetrics.h"

namespace spp::analyzer {

struct DoseObservation {
    float dose    = 0.0f;
    float blockNa = 0.0f;
    float blockK  = 0.0f;
    float blockCa = 0.0f;
    RawMetrics metrics;
};

} // namespace spp::analyzer