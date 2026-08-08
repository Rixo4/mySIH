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
    float doseForNorepinephrine,
    const NeuromodulatorProfile& profile,
    float currentTimeMs
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
    // proxy, PRESYNAPTIC autoreceptor pathway). Uses doseForDopamine, same
    // reasoning as D1 above.
    {
        const float occ = neuromodulatorOccupancy(doseForDopamine, profile.d2.ec50, profile.d2.hill);
        const float releaseFrac = std::clamp(profile.d2.maxReleaseReductionFrac, 0.0f, 1.0f);
        out.excitatoryWeightScale *= (1.0f - releaseFrac * occ);
    }

    // D2 POSTSYNAPTIC (Tier 2.1): a second, independent Hill curve on the
    // same dose (own EC50/Hill -- genuinely different signaling route from
    // the presynaptic curve above, not a relabeled copy) that shrinks
    // gCaEff -- striatal D2 suppresses L-type Ca2+ currents via a
    // PLCbeta1-IP3-calcineurin cascade, confirmed NOT mediated by the
    // classical Gi/cAMP pathway the presynaptic release-probability lever
    // uses (Hernandez-Lopez et al 2000, J Neurosci 20:8987). Inert
    // (gCaEffScale stays 1.0) when maxPostsynapticCaReductionFrac == 0, so
    // any config that doesn't set these three new fields is a bit-identical
    // no-op -- same guarantee as every other mechanism in this file.
    {
        const float occ = neuromodulatorOccupancy(
            doseForDopamine, profile.d2.postsynapticEc50, profile.d2.postsynapticHill);
        const float caReductionFrac = std::clamp(profile.d2.maxPostsynapticCaReductionFrac, 0.0f, 1.0f);
        out.gCaEffScale *= (1.0f - caReductionFrac * occ);
    }

    // 5-HT1A (serotonin): boosts gKEff (GIRK-mediated hyperpolarization).
    // Uses doseForSerotonin (SERT reuptake-block-amplified).
    //
    // Tier 2.1: autoreceptor occupancy is a SECOND Hill curve on the same
    // dose, typically saturating at a lower EC50 (more sensitive) than the
    // postsynaptic curve above. It attenuates how much of the postsynaptic
    // gain gets expressed -- attenuation = 1.0 at zero autoreceptor
    // occupancy, shrinking toward (1 - maxAutoreceptorSuppressionFrac) as
    // the autoreceptor saturates. Net effect: at low dose, the
    // (already-saturating) autoreceptor curve suppresses most of the
    // (still-small) postsynaptic occupancy's expression -- weak net gKEff
    // change. At high dose, the autoreceptor curve is already saturated
    // (attenuation floored) while postsynaptic occupancy keeps climbing --
    // the full gKEff gain effect emerges. This reproduces the qualitative
    // early-suppression-then-emerging-effect pattern without any new
    // neuron population or endogenous release/clearance model. Inert
    // (attenuation == 1.0 always) when maxAutoreceptorSuppressionFrac == 0,
    // so any config that doesn't set the new fields is a bit-identical
    // no-op -- same guarantee as every other mechanism in this file.
    {
        const float occ = neuromodulatorOccupancy(doseForSerotonin, profile.ht1a.ec50, profile.ht1a.hill);
        const float kCeiling = std::max(1.0f, profile.ht1a.maxKGainFold);
        const float autoOcc = neuromodulatorOccupancy(
            doseForSerotonin, profile.ht1a.autoreceptorEc50, profile.ht1a.autoreceptorHill);
        const float autoSuppressFrac = std::clamp(profile.ht1a.maxAutoreceptorSuppressionFrac, 0.0f, 1.0f);

        // Tier 2.1 correction: D(t) is the closed-form solution of
        // dD/dt = kDesense*autoOcc*(1-D) - kRecover*D for CONSTANT autoOcc
        // (valid because dose, hence occupancy, does not change during a
        // fixed-dose run). D(0)=0 (fresh receptor). As t grows, D climbs
        // toward its steady state Dss = kDesense*autoOcc / (kDesense*autoOcc
        // + kRecover), and the autoreceptor's effective suppression ceiling
        // shrinks toward autoSuppressFrac*(1-Dss) -- i.e. the receptor
        // desensitizes and lets more of the postsynaptic effect through
        // over time, at a FIXED dose. tauDesenseMs at its inert default
        // (huge) makes kDesense~0, so Dss~0 and D(t)~0 for any realistic
        // t -- exactly reproduces the old dose-only behavior when the new
        // fields are left unconfigured.
        const float tauDesense  = std::max(1.0f, profile.ht1a.autoreceptorTauDesenseMs);
        const float tauRecovery = std::max(1.0f, profile.ht1a.autoreceptorTauRecoveryMs);
        const float kDesense  = 1.0f / tauDesense;
        const float kRecovery = 1.0f / tauRecovery;
        const float rate = kDesense * autoOcc + kRecovery;
        const float dSteadyState = (rate > 1.0e-12f) ? (kDesense * autoOcc / rate) : 0.0f;
        // autoreceptorExposureOffsetMs (see header comment) decouples the
        // desensitization state's elapsed time from the run's own simulated
        // duration, so a short/cheap probe run can evaluate D(t) at a real
        // literature timescale (days-to-weeks) without simulating that much
        // network time. Defaults to 0.0f, so t == currentTimeMs exactly
        // when unconfigured -- no behavior change for any existing config.
        const float t = std::max(0.0f, currentTimeMs + profile.ht1a.autoreceptorExposureOffsetMs);
        const float dNow = dSteadyState * (1.0f - std::exp(-rate * t));

        const float effectiveSuppressFrac = autoSuppressFrac * (1.0f - dNow);
        const float attenuation = 1.0f - effectiveSuppressFrac * autoOcc;
        out.gKEffScale *= (1.0f + (kCeiling - 1.0f) * occ * attenuation);
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

    // ALPHA-2 PRESYNAPTIC (norepinephrine, Tier 2.2): shrinks excitatory
    // synaptic weight (release-probability proxy) -- same Gi-coupled
    // presynaptic-autoreceptor lever as D2's presynaptic pathway above, just
    // driven by doseForNorepinephrine (NET reuptake-block-amplified, see
    // ReuptakeTransporter.h's amplifiedDoseUm) instead of dopamine's dose.
    // Composes multiplicatively with D2's presynaptic contribution to
    // excitatoryWeightScale -- a drug hitting both release-probability
    // pathways combines correctly.
    {
        const float occ = neuromodulatorOccupancy(
            doseForNorepinephrine, profile.alpha2.presynapticEc50, profile.alpha2.presynapticHill);
        const float releaseFrac = std::clamp(profile.alpha2.maxPresynapticReleaseReductionFrac, 0.0f, 1.0f);
        out.excitatoryWeightScale *= (1.0f - releaseFrac * occ);
    }

    // ALPHA-2 POSTSYNAPTIC (norepinephrine, Tier 2.2): a second, independent
    // Hill curve on the same dose (own EC50/Hill -- a genuinely distinct PFC
    // pyramidal-spine cAMP-HCN-channel pathway, not a relabeled copy of the
    // presynaptic autoreceptor) that shrinks adaptationScale -- guanfacine's
    // real clinical mechanism (Wang et al 2007, Cell 129:397), same net
    // "less braking" lever as D1's adaptation-reduction above. Composes
    // multiplicatively with D1's and 5-HT2A's contributions to
    // adaptationScale.
    {
        const float occ = neuromodulatorOccupancy(
            doseForNorepinephrine, profile.alpha2.postsynapticEc50, profile.alpha2.postsynapticHill);
        const float adaptFrac = std::clamp(profile.alpha2.maxPostsynapticAdaptationReductionFrac, 0.0f, 1.0f);
        out.adaptationScale *= (1.0f - adaptFrac * occ);
    }

    // BETA (norepinephrine, Tier 2.2 completion): INCREASES
    // adaptationScaleExcitatory (more braking, EXCITATORY NEURONS ONLY) --
    // the mirror image of D1's/alpha-2-postsynaptic's decrease, same
    // target family, opposite direction. See BetaAction's header comment
    // for the literature (beta1-AR opens the same HCN channels alpha-2
    // closes; high NE/cAMP-PKA suppresses PFC persistent firing) --
    // deliberately not modeled the same direction as D1 despite sharing
    // Gs coupling. maxAdaptationIncreaseFold is a ceiling FOLD (>=1), same
    // convention as D1's maxNmdaGainFold, not a 0..1 reduction fraction.
    // Tier 2.4 part 1 correction: this used to multiply the SHARED
    // adaptationScale (applied to both cell types), which produced a real,
    // reproducible paradoxical population-level EXCITATORY reading on real
    // hardware (see Tier 2.2's writeup) -- leading hypothesis was E/I
    // disinhibition from suppressing inhibitory neurons too, when beta's
    // source literature is specifically about PFC PYRAMIDAL (excitatory)
    // cells. Now multiplies the excitatory-only field instead -- see
    // NeuromodulatorGainModifiers' comment for the full rationale.
    {
        const float occ = neuromodulatorOccupancy(doseForNorepinephrine, profile.beta.ec50, profile.beta.hill);
        const float increaseCeiling = std::max(1.0f, profile.beta.maxAdaptationIncreaseFold);
        out.adaptationScaleExcitatory *= (1.0f + (increaseCeiling - 1.0f) * occ);
    }

    // ALPHA-1 (norepinephrine, Tier 2.2 completion): INCREASES
    // adaptationScaleExcitatory (more braking, EXCITATORY NEURONS ONLY),
    // same lever family/direction as beta above -- both suppress PFC
    // pyramidal firing via different G-protein routes. See Alpha1Action's
    // header comment for the literature (Arnsten lab: PKC-mediated dlPFC
    // PYRAMIDAL firing suppression) -- deliberately not modeled the same
    // direction as 5-HT2A despite sharing Gq coupling. No gKEff lever
    // (unlike 5-HT2A) since no K+-conductance-specific citation was found
    // this session for alpha-1. Tier 2.4 part 1 correction: same fix as
    // beta above, same reasoning -- see that comment.
    {
        const float occ = neuromodulatorOccupancy(doseForNorepinephrine, profile.alpha1.ec50, profile.alpha1.hill);
        const float increaseCeiling = std::max(1.0f, profile.alpha1.maxAdaptationIncreaseFold);
        out.adaptationScaleExcitatory *= (1.0f + (increaseCeiling - 1.0f) * occ);
    }

    return out;
}

} // namespace spp::synapse