#include "NeuromodulatorSystem.h"

#include <algorithm>
#include <cmath>

namespace spp::synapse {

float neuromodulatorOccupancy(float dose, float ec50, float hill) {
    if (dose <= 0.0f || ec50 <= 0.0f) {
        return 0.0f;
    }
    const float h = (hill > 0.0f) ? hill : 1.0f;
    const float doseP = std::pow(dose, h);
    const float ec50P = std::pow(ec50, h);
    const float denom = ec50P + doseP;
    if (!(denom > 0.0f)) {
        return 0.0f;
    }
    return std::clamp(doseP / denom, 0.0f, 1.0f);
}

NeuromodulatorGainModifiers computeNeuromodulatorGainModifiers(
    float doseForDopamine,
    float doseForSerotonin,
    const NeuromodulatorProfile& profile
) {
    NeuromodulatorGainModifiers out;

    // D1 (dopamine): shrinks adaptation current, boosts NMDA. Both float
    // toward their ceilings as D1 occupancy rises; ceiling=default (0
    // reduction / 1.0x gain) means this specific lever stays exactly inert
    // even if the drug configures the OTHER D1 lever but not this one.
    // Uses doseForDopamine (DAT reuptake-block-amplified, see
    // ReuptakeTransporter.h's amplifiedDoseUm) rather than the raw dose.
    {
        const float occ = neuromodulatorOccupancy(doseForDopamine, profile.d1.ec50, profile.d1.hill);
        const float adaptFrac = std::clamp(profile.d1.maxAdaptationReductionFrac, 0.0f, 1.0f);
        const float nmdaCeiling = std::max(1.0f, profile.d1.maxNmdaGainFold);
        out.adaptationScale *= (1.0f - adaptFrac * occ);
        out.gMaxNmdaScale   *= (1.0f + (nmdaCeiling - 1.0f) * occ);
    }

    // D2 (dopamine): shrinks excitatory synaptic weight (release-probability
    // proxy). Uses doseForDopamine, same reasoning as D1 above.
    {
        const float occ = neuromodulatorOccupancy(doseForDopamine, profile.d2.ec50, profile.d2.hill);
        const float releaseFrac = std::clamp(profile.d2.maxReleaseReductionFrac, 0.0f, 1.0f);
        out.excitatoryWeightScale *= (1.0f - releaseFrac * occ);
    }

    // 5-HT1A (serotonin): boosts gKEff (GIRK-mediated hyperpolarization).
    // Uses doseForSerotonin (SERT reuptake-block-amplified).
    {
        const float occ = neuromodulatorOccupancy(doseForSerotonin, profile.ht1a.ec50, profile.ht1a.hill);
        const float kCeiling = std::max(1.0f, profile.ht1a.maxKGainFold);
        out.gKEffScale *= (1.0f + (kCeiling - 1.0f) * occ);
    }

    // 5-HT2A (serotonin): shrinks gKEff (reduced K+ leak) AND shrinks
    // adaptation (reduced IAHP) -- both multiply into the same running
    // scale factors as D1's adaptation lever and 5-HT1A's gK lever above,
    // so a drug hitting multiple receptors composes correctly (each
    // contributes its own multiplicative factor). Uses doseForSerotonin.
    {
        const float occ = neuromodulatorOccupancy(doseForSerotonin, profile.ht2a.ec50, profile.ht2a.hill);
        const float kReductionFrac = std::clamp(profile.ht2a.maxKReductionFrac, 0.0f, 1.0f);
        const float adaptFrac = std::clamp(profile.ht2a.maxAdaptationReductionFrac, 0.0f, 1.0f);
        out.gKEffScale      *= (1.0f - kReductionFrac * occ);
        out.adaptationScale *= (1.0f - adaptFrac * occ);
    }

    return out;
}

} // namespace spp::synapse