#include "ReuptakeTransporter.h"

#include <algorithm>
#include <cmath>

namespace spp::synapse {

namespace TransporterLibrary {

// Km values -- literature reports vary substantially by expression system
// (native tissue vs heterologous HEK/oocyte expression), which is expected
// for membrane transport kinetics; the values below are representative
// midpoints of the reported ranges, same "one representative literature
// value" discipline already used throughout ReceptorModel.h.

// EAAT1/2 (glutamate): HEK-expressed human EAAT2 Km ~48-97 uM across
// studies (Arriza et al 1994-type expression studies); 70 uM used as
// representative midpoint.
const float kEaatKmUm = 70.0f;

// GAT1 (GABA): rat GAT1 Km reported 4-11 uM depending on construct/prep
// (Mager/Kavanaugh-type electrophysiology; MD/cryo-EM studies). 7 uM used
// as representative midpoint.
const float kGat1KmUm = 7.0f;

// SERT (serotonin): reported Km spans ~0.07-1.5 uM depending on prep
// (synaptosome vs transfected-cell assays). 0.5 uM used as representative
// midpoint. Inert in this engine -- see header note.
const float kSertKmUm = 0.5f;

// DAT (dopamine): reported Km ~0.16-2.6 uM depending on prep (human vs rat
// DAT, incubation conditions). 1.0 uM used as representative midpoint.
// Inert in this engine -- see header note.
const float kDatKmUm = 1.0f;

// NET (norepinephrine): human NET Km ~0.17 uM (Vmax 904.7 pmol/min/mg),
// a comparatively tightly-reported value across studies. Inert in this
// engine -- see header note.
const float kNetKmUm = 0.17f;

} // namespace TransporterLibrary

float transporterOccupancy(float doseUm, const TransporterDrugEffect& drug) {
    if (drug.mechanism == TransporterBlockType::None) {
        return 0.0f;
    }
    if (doseUm <= 0.0f || drug.kiUm <= 0.0f) {
        return 0.0f;
    }
    const float hill = (drug.hill > 0.0f) ? drug.hill : 1.0f;
    const float doseP = std::pow(doseUm, hill);
    const float kiP = std::pow(drug.kiUm, hill);
    const float denom = kiP + doseP;
    if (!(denom > 0.0f)) {
        return 0.0f;
    }
    return std::clamp(doseP / denom, 0.0f, 1.0f);
}

float effectiveTauDecayMs(float tauBaselineMs, float doseUm, const TransporterDrugEffect& drug) {
    if (drug.mechanism == TransporterBlockType::None) {
        return tauBaselineMs; // exact baseline preservation, no drift
    }
    const float occupancy = transporterOccupancy(doseUm, drug);
    const float ceiling = std::max(1.0f, drug.maxExtensionFold);
    const float fold = 1.0f + (ceiling - 1.0f) * occupancy;
    return tauBaselineMs * fold;
}

float amplifiedDoseUm(float doseUm, const TransporterDrugEffect& drug) {
    if (drug.mechanism == TransporterBlockType::None) {
        return doseUm; // exact baseline preservation, no drift
    }
    const float occupancy = transporterOccupancy(doseUm, drug);
    const float ceiling = std::max(1.0f, drug.maxExtensionFold);
    const float fold = 1.0f + (ceiling - 1.0f) * occupancy;
    return doseUm * fold;
}

} // namespace spp::synapse