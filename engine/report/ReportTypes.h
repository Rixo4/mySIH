#pragma once

// Report-facing plain data types, extracted from engine/main.cpp as part of
// PRECISION_GAP_CLOSURE_PLAN.md gap 1.3/1.4 cleanup: main.cpp's report
// generation logic (~900 lines) lived inline in the same anonymous namespace
// as config parsing and simulation orchestration. This header holds exactly
// the types the report layer needs as input -- SimulationConfig and
// RuntimeInput (the CLI/JSON-facing config, distinct from
// spp::simulation::SimulationConfig, the lower-level engine-facing one) and
// AggregatedStats (multi-run statistics). RunResult, MetricStats, and
// SigmoidFitResult stay in main.cpp -- confirmed (grep) that no report code
// references them directly; only AggregatedStats crosses the boundary.

#include <string>

namespace spp::report {

struct SimulationConfig {
    int neuron_count    = 1500;
    double sim_time     = 400.0;
    double dt           = 0.01;
    double dose         = 10.0;
    double ic50_na      = 50.0;
    double ic50_k       = 50.0;
    double ic50_ca      = 120.0;
    double hill         = 3.0;
    double connectivity = 0.10;
    double excitatory_ratio     = 0.8;
    // NOTE (historical): the "fires once then goes silent" bug from an
    // earlier session was NOT a drive problem -- it was calcium-driven
    // depolarization block (see the gCa comment in neuron/NeuronModel.h),
    // fixed by dropping gCa from 8 to 0.5. That fix is still correct and
    // still in place.
    //
    // UPDATE (this session): a *different* collapse resurfaced at
    // external_current=2.5 after the Phase 2 receptor work landed -- every
    // neuron fired exactly once (from the shared initial-condition burst)
    // then went permanently silent for the rest of the run (early_rate_hz
    // 5.0, late_rate_hz 0.0 exactly, at neuron_count=1500/400ms). Bisected
    // by individually zeroing every Phase 2 addition (gMaxGABAa,
    // gabaBFraction, ampaFraction, nmdaFraction), the adaptation current,
    // and even tripling recurrent excitatory weight strength -- none of it
    // changed the outcome at all. Only external_current mattered:
    //   2.5  -> dead (0 Hz after the first volley)
    //   5.0  -> dead (still 0 Hz, no change at all)
    //   6.6  -> 12.5 Hz, healthy (early 10.1 -> late 15.3, ramping up, no
    //           collapse) -- lands right at the ~12 Hz this value always
    //           targeted, so this is the new default.
    //   7.0  -> 17.5 Hz, still healthy but past target
    //   10.0 -> 41.3 Hz, overshooting
    //   20.0 -> 87.8 Hz, badly overshooting
    // Root cause not fully identified (why 2.5 stopped being enough is
    // still open), but the fix is confirmed: 6.6 restores the documented
    // ~9.8-12 Hz baseline with 0% silent and no collapse across the full
    // 400ms run. If this needs re-tuning later, re-run this same sweep
    // rather than assuming 2.5 (or any old value) still works.
    double external_current     = 6.6;
    double noise_level          = 0.35;
    double excitatory_weight_scale = 1.0;
    double inhibitory_weight_scale = 1.0;

    // PHASE2_PLAN.md step 4: receptor pharmacology (block/potentiate/
    // agonist), one config surface per receptor, same generic IC50/Hill-
    // style pattern as ic50_na/k/ca above. Defaults are deliberately inert
    // (huge ec50 / no-op ceiling), matching ReceptorDrugProfile.h's own
    // stated default policy -- an unconfigured run has zero receptor drug
    // effect, same as before this step existed. Mechanism per receptor is
    // fixed by receptor identity (AMPA/NMDA always Block, GABA-A always
    // Potentiate, GABA-B always Agonist) rather than user-selectable, since
    // that's what the Phase 2 validation drug set (§5) actually needs --
    // see ReceptorDrugProfile.h for why these three mechanism families
    // exist and what a mismatched mechanism/receptor pairing would mean.
    double ic50_ampa              = 1.0e9;
    double hill_ampa              = 1.0;
    double ic50_nmda              = 1.0e9;
    double hill_nmda              = 1.0;
    double ec50_gabaA             = 1.0e9;
    double hill_gabaA             = 1.0;
    double max_potentiation_gabaA = 1.0;
    double ec50_gabaB             = 1.0e9;
    double hill_gabaB             = 1.0;

    // Phase 3a, step 1 of the drug set (GAT1/tiagabine only -- see
    // ReuptakeTransporter.h design note): a transporter-blocking drug
    // extends a receptor's decay time constant instead of touching its
    // gMax/occupancy. GAT1 block is fixed Competitive by transporter
    // identity (tiagabine's real mechanism), same "mechanism fixed by
    // identity, JSON only supplies numbers" pattern as buildReceptorProfile
    // above. Defaults are inert (huge Ki, no extension ceiling), so an
    // unconfigured run is a bit-identical no-op, same policy as every other
    // receptor field here.
    double ki_gat1            = 1.0e9;
    double hill_gat1          = 1.0;
    double max_extension_gat1 = 1.0;

    // Phase 3a, remaining 3 drugs (SSRI/cocaine/reboxetine): SERT/DAT/NET
    // reuptake block. Serotonin/dopamine/norepinephrine have NO receptor
    // current in this engine (that's Phase 3c's neuromodulator gain
    // system, not built yet), so these three are REPORT-ONLY -- they
    // produce real, literature-sourced occupancy/clearance-fold numbers in
    // the Neurotransmitter Profile section, but deliberately no network/
    // firing-rate effect, since there's no receptor for them to act on.
    // Not wired into DoseObservation/AnalyzedDose/detectMechanism at all
    // (unlike GAT1) -- they have no observable dynamics to attribute a
    // mechanism signature to.
    double ki_sert            = 1.0e9;
    double hill_sert          = 1.0;
    double max_extension_sert = 1.0;
    double ki_dat             = 1.0e9;
    double hill_dat           = 1.0;
    double max_extension_dat  = 1.0;
    double ki_net             = 1.0e9;
    double hill_net           = 1.0;
    double max_extension_net  = 1.0;

    // Phase 3b: GABA-A desensitization ("receptor tiredness"). OFF by
    // default -- see SimulationConfig::desensitizationEnabled in
    // simulation/SimulationEngine.h for why this is safe to leave off for
    // every existing (short-duration) drug config. sim_time above already
    // exists as the run-duration knob -- a 3b desensitization test simply
    // sets both desensitization_enabled and a much larger sim_time (tens of
    // seconds, in ms) in the same JSON file.
    bool desensitization_enabled          = false;
    double desensitization_tau_desense_ms  = 30000.0;
    double desensitization_tau_recovery_ms = 124000.0;
    double desensitization_max_attenuation = 0.9;

    // Phase 3c: vesicle pool dynamics ("synaptic fatigue"), see
    // engine/synapse/NeurotransmitterPool.h for the full design (including
    // why depletion is observable within a default-duration run but recovery
    // is not -- same "sim_time is the knob for the slow dynamics" pattern as
    // desensitization above). CPU-only until the GPU kernel is ported (see
    // validateConfig()) -- OFF by default, zero behavior change unless a
    // test deliberately opts in.
    bool vesicle_pool_enabled              = false;
    double vesicle_pool_rrp_size           = 10.0;
    double vesicle_pool_reserve_size       = 100.0;
    double vesicle_pool_rrp_refill_tau_ms     = 1500.0;
    double vesicle_pool_reserve_refill_tau_ms = 20000.0;
    double vesicle_pool_calcium_factor     = 0.3; // calibrated: Dobrunz 2002 release prob ~0.2-0.3

    // Phase 3c: neuromodulator gain system (D1/D2/5-HT1A/5-HT2A), see
    // engine/synapse/NeuromodulatorSystem.h for the full design/literature
    // basis. Defaults are inert (ec50 huge, gain ceilings inert), matching
    // every other Phase 1/2/3 mechanism's "unconfigured = bit-identical
    // no-op" policy.
    double ec50_d1                  = 1.0e9;
    double hill_d1                  = 1.0;
    double max_adaptation_reduction_d1 = 0.0; // 0..1 fraction
    double max_nmda_gain_d1         = 1.0;    // fold, >=1

    double ec50_d2                  = 1.0e9;
    double hill_d2                  = 1.0;
    double max_release_reduction_d2 = 0.0;    // 0..1 fraction
    // Tier 2.1 D2 postsynaptic split -- see NeuromodulatorSystem.h's
    // DopamineD2Action comment. Second independent curve, own EC50/Hill.
    double postsynaptic_ec50_d2               = 1.0e9;
    double postsynaptic_hill_d2               = 1.0;
    double max_postsynaptic_ca_reduction_d2   = 0.0;  // 0..1 fraction

    double ec50_ht1a                = 1.0e9;
    double hill_ht1a                = 1.0;
    double max_k_gain_ht1a          = 1.0;    // fold, >=1
    // Tier 2.1: 5-HT1A presynaptic autoreceptor pathway -- see
    // NeuromodulatorSystem.h's Serotonin5HT1AAction comment.
    double autoreceptor_ec50_ht1a           = 1.0e9;
    double autoreceptor_hill_ht1a           = 1.0;
    double max_autoreceptor_suppression_ht1a = 0.0;  // 0..1 fraction
    // Tier 2.1 correction: time-dependent desensitization of the
    // autoreceptor above (huge/inert defaults = never desensitizes,
    // reproduces the old dose-only behavior).
    double autoreceptor_tau_desense_ms_ht1a  = 1.0e12;
    double autoreceptor_tau_recovery_ms_ht1a = 1.0e9;
    // Real-timescale fix: decouples the autoreceptor's elapsed exposure
    // time from this run's own simulated duration -- see
    // NeuromodulatorSystem.h's Serotonin5HT1AAction comment. 0 = inert
    // (exposure time == this run's own elapsed sim time, old behavior).
    double autoreceptor_exposure_offset_ms_ht1a = 0.0;

    double ec50_ht2a                = 1.0e9;
    double hill_ht2a                = 1.0;
    double max_k_reduction_ht2a          = 0.0; // 0..1 fraction
    double max_adaptation_reduction_ht2a = 0.0; // 0..1 fraction

    bool use_cuda    = true;
    bool export_csv  = true;
    std::string output_folder = "output_data";
};

struct RuntimeInput {
    SimulationConfig config;
    std::string drug_name = "GenericCompound";
    bool run_dose_sweep   = false;
    double sweep_start    = 0.0;
    double sweep_end      = 100.0;
    int sweep_points      = 10;
};

struct AggregatedStats {
    float meanRate      = 0.0f; float stdRate      = 0.0f;
    float meanSync      = 0.0f; float stdSync      = 0.0f;
    float meanBurst     = 0.0f; float stdBurst     = 0.0f;
    float meanBurstRate = 0.0f;
    float meanISI       = 0.0f; float stdISI       = 0.0f;
    float meanPopVar    = 0.0f;
    float stdRate2      = 0.0f;
    float meanPeakSync            = 0.0f;
    float meanBurstingNeuronPct   = 0.0f;
    float meanBurstDurationMs     = 0.0f;
    float meanFiringRateStdHz     = 0.0f;
    float meanSilentNeuronPct     = 0.0f;
    float meanLateWindowSilentNeuronPct = 0.0f;  // Gap 1.3 fix
    float meanEarlyWindowRateHz   = 0.0f;
    float meanLateWindowRateHz    = 0.0f;
    float meanFirstThirdRateHz    = 0.0f;
    float meanMiddleThirdRateHz   = 0.0f;
    float meanLastThirdRateHz     = 0.0f;
};

} // namespace spp::report