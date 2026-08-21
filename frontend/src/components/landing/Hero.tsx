import React from 'react';
import { motion } from 'framer-motion';
import { Activity, ArrowRight, BrainCircuit, Cpu, Layers, Play, ShieldCheck, Sparkles, Zap, Beaker } from 'lucide-react';
import { Link } from 'react-router-dom';

const TICKER_ITEMS = [
  { label: 'CUDA 12 Acceleration', value: '1,000x Speedup', badge: 'GPU Native' },
  { label: 'Hodgkin-Huxley Solvers', value: '0.001ms Precision', badge: 'ODE Engine' },
  { label: 'Cortical Microcircuit', value: '80% Excitatory / 20% Inhibitory', badge: 'Biophysical' },
  { label: 'Pro-Convulsant Screen', value: '99.4% Accuracy', badge: 'Safety' },
  { label: 'Ion Channel Sweep', value: 'Na+, K+, Ca2+ Kinetics', badge: 'Target' },
  { label: 'In-Silico PK/PD', value: 'Multi-Dose Modeling', badge: 'Automated' },
  { label: 'Seizure Risk Index', value: 'Real-Time NII Alert', badge: 'Predictive' },
];

export function Hero() {
  return (
    <section className="relative pt-32 pb-24 overflow-hidden flex flex-col justify-between min-h-screen">
      {/* Background Volumetric Spotlight Flare & Subtle Grid */}
      <div className="absolute inset-0 bg-grid opacity-25 pointer-events-none" />
      <div className="absolute top-1/4 left-1/2 -translate-x-1/2 -translate-y-1/2 w-[800px] h-[800px] bg-blue-600/10 rounded-full blur-[150px] pointer-events-none" />
      <div className="absolute top-1/3 right-1/4 w-[600px] h-[600px] bg-violet-600/15 rounded-full blur-[130px] pointer-events-none" />

      {/* Main Centered Hero Content */}
      <div className="relative max-w-6xl mx-auto px-4 sm:px-6 lg:px-8 text-center z-10 flex-1 flex flex-col justify-center items-center">
        {/* Animated Live Status Pill (TradeWise Style) */}
        <motion.div
          initial={{ opacity: 0, y: -15 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.5 }}
          className="inline-flex items-center gap-2.5 px-4 py-1.5 rounded-full bg-blue-950/60 border border-blue-500/30 text-xs font-mono text-blue-200 mb-8 backdrop-blur-xl shadow-[0_0_20px_rgba(59,130,246,0.25)]"
        >
          <span className="w-2 h-2 rounded-full bg-blue-400 alert-dot shadow-[0_0_8px_#60a5fa]" />
          <span className="text-blue-300 font-bold">Live Biophysics Engine</span>
          <span className="text-slate-600">—</span>
          <span className="text-slate-300 font-medium">0.001ms ODE Solvers & Gated Channel Dynamics</span>
        </motion.div>

        {/* Hero Headline (TradeWise Bold Two-Tone Font Style) */}
        <motion.h1
          initial={{ opacity: 0, y: 15 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.6, delay: 0.1 }}
          className="text-5xl sm:text-7xl lg:text-8xl font-black text-white tracking-tight leading-[1.08] max-w-5xl font-sans"
        >
          Predict Neural Toxicity <br />
          <span className="bg-gradient-to-r from-blue-400 via-indigo-300 to-violet-400 bg-clip-text text-transparent drop-shadow-[0_0_35px_rgba(99,102,241,0.5)]">
            Before Clinical Trials.
          </span>
        </motion.h1>

        {/* Subheadline with Balanced Width */}
        <motion.p
          initial={{ opacity: 0, y: 15 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.6, delay: 0.2 }}
          className="mt-6 text-base sm:text-xl text-slate-300 max-w-3xl font-normal leading-relaxed"
        >
          Silicon Patient automatically simulates multi-target patch-clamp binding, membrane voltage transients, and Network Instability Indices — turning raw chemical parameters into instant, actionable safety signals.
        </motion.p>

        {/* Dual High-Contrast CTA Action Buttons */}
        <motion.div
          initial={{ opacity: 0, y: 15 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.6, delay: 0.3 }}
          className="mt-9 flex flex-col sm:flex-row items-center justify-center gap-4 w-full sm:w-auto"
        >
          <Link
            to="/app/dose-eval"
            className="flex items-center justify-center gap-2.5 px-8 py-3.5 rounded-full bg-blue-600 hover:bg-blue-500 text-white font-bold text-sm transition-all duration-200 shadow-[0_0_25px_rgba(37,99,235,0.5)] hover:shadow-[0_0_35px_rgba(59,130,246,0.7)] hover:-translate-y-0.5 active:translate-y-0 cursor-pointer w-full sm:w-auto group"
          >
            <Zap className="w-4 h-4 text-blue-200 fill-blue-200" />
            <span>Start Screening Free</span>
            <ArrowRight className="w-4 h-4 text-blue-200 group-hover:translate-x-1 transition-transform" />
          </Link>

          <Link
            to="/how-it-works"
            className="flex items-center justify-center gap-2 px-8 py-3.5 rounded-full bg-slate-900/80 hover:bg-slate-800/90 border border-white/10 hover:border-white/20 text-slate-200 hover:text-white font-semibold text-sm transition-all backdrop-blur-xl cursor-pointer w-full sm:w-auto"
          >
            <span>See How It Works</span>
          </Link>
        </motion.div>

        {/* Social Proof / Feature Guarantee Row (TradeWise Style) */}
        <motion.div
          initial={{ opacity: 0 }}
          animate={{ opacity: 1 }}
          transition={{ duration: 0.6, delay: 0.4 }}
          className="mt-8 flex flex-wrap items-center justify-center gap-6 sm:gap-10 text-xs font-mono text-slate-400"
        >
          <span className="flex items-center gap-1.5 text-slate-300">
            <ShieldCheck className="w-4 h-4 text-blue-400" />
            <span>No local install required</span>
          </span>
          <span className="text-slate-600 hidden sm:inline">•</span>
          <span className="flex items-center gap-1.5 text-slate-300">
            <Activity className="w-4 h-4 text-indigo-400" />
            <span>12,000+ simulated sweeps</span>
          </span>
          <span className="text-slate-600 hidden sm:inline">•</span>
          <span className="flex items-center gap-1.5 text-slate-300">
            <Zap className="w-4 h-4 text-cyan-400" />
            <span>Sub-millisecond ODE solver</span>
          </span>
        </motion.div>

        {/* 3D Tilted Perspective Floating Dashboard Preview Cockpit */}
        <motion.div
          initial={{ opacity: 0, y: 40 }}
          animate={{ opacity: 1, y: 0 }}
          transition={{ duration: 0.9, delay: 0.45 }}
          className="mt-14 w-full max-w-5xl hero-3d-preview-container"
        >
          <div className="hero-3d-preview-card p-6 sm:p-8 rounded-3xl bg-obsidian-900/90 border border-blue-500/20 backdrop-blur-2xl text-left relative overflow-hidden shadow-[0_20px_60px_rgba(0,0,0,0.8),0_0_40px_rgba(59,130,246,0.15)]">
            {/* Ambient inner glow */}
            <div className="absolute -top-24 -right-24 w-96 h-96 bg-blue-600/15 rounded-full blur-3xl pointer-events-none" />

            {/* Dashboard Header Bar */}
            <div className="flex flex-wrap items-center justify-between gap-4 pb-6 border-b border-white/10">
              <div>
                <div className="text-xs font-mono text-blue-400 uppercase tracking-widest font-semibold mb-1 flex items-center gap-2">
                  <Beaker className="w-3.5 h-3.5 text-blue-400" />
                  SILICON PATIENT / IN-SILICO BIOPHYSICS OS
                </div>
                <h3 className="text-xl sm:text-2xl font-bold text-white flex items-center gap-2">
                  Active Simulation: <span className="text-blue-300 font-mono">Ketamine-Analog-04</span>
                </h3>
              </div>

              {/* Status Badges Row */}
              <div className="flex items-center gap-2 flex-wrap">
                <div className="px-3 py-1 rounded-full bg-blue-500/15 border border-blue-400/30 text-[11px] font-mono text-blue-300 font-bold">
                  <span className="text-white">L5 Cortical</span> Soma Model
                </div>
                <div className="px-3 py-1 rounded-full bg-emerald-500/15 border border-emerald-400/30 text-[11px] font-mono text-emerald-300 font-bold">
                  <span className="text-white">LOW RISK</span> Pro-Convulsant
                </div>
                <div className="px-3 py-1 rounded-full bg-slate-800 border border-white/10 text-[11px] font-mono text-slate-300">
                  <span className="font-bold text-white">0.001 ms</span> Step
                </div>
              </div>
            </div>

            {/* Dashboard Content Grid (3 Columns) */}
            <div className="mt-6 grid grid-cols-1 md:grid-cols-3 gap-5">
              {/* Tile 1: Target Influx Potency */}
              <div className="p-4 rounded-2xl bg-slate-950/70 border border-white/5 space-y-2">
                <div className="text-[10px] font-mono text-slate-400 uppercase">Gated Channel Inhibition</div>
                <div className="flex items-center justify-between text-xs font-mono text-white">
                  <span>Na⁺ Channel</span>
                  <strong className="text-blue-400">42% (250 µM)</strong>
                </div>
                <div className="w-full h-1.5 bg-slate-900 rounded-full overflow-hidden">
                  <div className="w-[42%] h-full bg-blue-500 rounded-full shadow-[0_0_8px_#3b82f6]" />
                </div>

                <div className="flex items-center justify-between text-xs font-mono text-white pt-1">
                  <span>K⁺ Channel</span>
                  <strong className="text-violet-400">77% (8 µM)</strong>
                </div>
                <div className="w-full h-1.5 bg-slate-900 rounded-full overflow-hidden">
                  <div className="w-[77%] h-full bg-violet-500 rounded-full shadow-[0_0_8px_#8b5cf6]" />
                </div>
              </div>

              {/* Tile 2: Real-Time ODE Solver */}
              <div className="p-4 rounded-2xl bg-slate-950/70 border border-white/5 space-y-2">
                <div className="text-[10px] font-mono text-slate-400 uppercase">ODE Solver Telemetry</div>
                <div className="text-2xl font-bold font-mono text-emerald-400">99.4%</div>
                <p className="text-[11px] text-slate-400 font-sans">
                  Trajectory convergence stability across 33 Monte Carlo sweeps.
                </p>
              </div>

              {/* Tile 3: Safety Window Rating */}
              <div className="p-4 rounded-2xl bg-slate-950/70 border border-white/5 space-y-2">
                <div className="text-[10px] font-mono text-slate-400 uppercase">Therapeutic Window</div>
                <div className="text-2xl font-bold font-mono text-blue-300">0 – 25 µM</div>
                <p className="text-[11px] text-slate-400 font-sans">
                  Optimal stabilization threshold without epileptiform burst discharge.
                </p>
              </div>
            </div>
          </div>
        </motion.div>
      </div>

      {/* ── Continuous Live Ticker Footer Bar ── */}
      <div className="w-full mt-20 border-y border-white/10 bg-slate-950/80 backdrop-blur-md py-3.5 overflow-hidden">
        <div className="flex items-center gap-8 animate-ticker whitespace-nowrap">
          {[...TICKER_ITEMS, ...TICKER_ITEMS].map((item, idx) => (
            <div key={idx} className="flex items-center gap-3 text-xs font-mono text-slate-300 shrink-0">
              <span className="px-2 py-0.5 rounded-full bg-blue-500/15 border border-blue-400/30 text-blue-300 text-[10px] font-bold">
                {item.badge}
              </span>
              <span className="text-slate-400">{item.label}:</span>
              <span className="text-white font-semibold">{item.value}</span>
              <span className="text-slate-700 ml-4">•</span>
            </div>
          ))}
        </div>
      </div>
    </section>
  );
}
