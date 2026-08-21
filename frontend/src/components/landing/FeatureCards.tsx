import React from 'react';
import { motion } from 'framer-motion';
import { Activity, ArrowRight, BrainCircuit, Cpu, Database, Eye, Gauge, ShieldAlert, Zap, Layers, CheckCircle2 } from 'lucide-react';
import { Link } from 'react-router-dom';

const FEATURES = [
  {
    icon: Cpu,
    title: 'Native CUDA 12 GPU Engine',
    description: 'Simulate high-dimensional Hodgkin-Huxley ODEs with GPU-parallelized acceleration delivering sub-millisecond precision.',
    benefit: '1,000x Speedup vs CPU',
    accentColor: 'border-blue-500/30 bg-blue-950/40 text-blue-400',
  },
  {
    icon: BrainCircuit,
    title: 'Ion Channel Kinetics Sweeps',
    description: 'Configure binding parameters (IC50, Hill coefficient) for Na+, K+, and Ca2+ channels across multi-dose gradient sweeps.',
    benefit: 'Target Binding Matrix',
    accentColor: 'border-indigo-500/30 bg-indigo-950/40 text-indigo-400',
  },
  {
    icon: ShieldAlert,
    title: 'Pro-Convulsant Safety Screening',
    description: 'Automatically detect Network Instability Index (NII), epileptiform synchrony, and seizure risk liabilities before wet lab testing.',
    benefit: 'Automated Risk Categorization',
    accentColor: 'border-rose-500/30 bg-rose-950/40 text-rose-400',
  },
  {
    icon: Gauge,
    title: 'Therapeutic Safety Windows',
    description: 'Identify exact effective dose ranges, toxic threshold boundaries, and therapeutic indices with automated R^2 confidence scoring.',
    benefit: 'Precision Window Explorer',
    accentColor: 'border-emerald-500/30 bg-emerald-950/40 text-emerald-400',
  },
  {
    icon: Database,
    title: 'Audit-Ready Provenance Ledger',
    description: 'Store raw JSON input payloads, native stdout execution logs, parsed summaries, and CSV artifacts with full reproducibility.',
    benefit: '100% Scientific Auditability',
    accentColor: 'border-amber-500/30 bg-amber-950/40 text-amber-400',
  },
  {
    icon: Eye,
    title: 'Interactive Results Workspace',
    description: 'Multi-tab visualization console featuring interactive dose-effect curves, mechanistic signatures, and bidirectionally synced tables.',
    benefit: 'Real-Time Synchronized Charts',
    accentColor: 'border-cyan-500/30 bg-cyan-950/40 text-cyan-400',
  },
];

export function FeatureCards() {
  return (
    <section id="features" className="py-24 relative bg-obsidian-950 overflow-hidden">
      {/* Ambient background glow */}
      <div className="absolute top-1/4 right-0 w-[500px] h-[500px] bg-blue-600/10 rounded-full blur-[140px] pointer-events-none" />
      <div className="absolute bottom-10 left-0 w-[500px] h-[500px] bg-violet-600/10 rounded-full blur-[140px] pointer-events-none" />

      <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 relative z-10 space-y-16">
        {/* Header with Industrial-Grade Typography & Smooth Entrance Motion */}
        <div className="text-center max-w-4xl mx-auto">
          <motion.div
            initial={{ opacity: 0, y: -15 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.5 }}
            className="inline-flex items-center gap-2 px-4 py-1.5 rounded-full bg-blue-950/60 border border-blue-500/30 text-xs font-mono text-blue-200 mb-6 backdrop-blur-xl shadow-[0_0_20px_rgba(59,130,246,0.25)]"
          >
            <span className="w-2 h-2 rounded-full bg-blue-400 alert-dot shadow-[0_0_8px_#60a5fa]" />
            <span className="text-blue-300 font-bold">High-Throughput Capabilities</span>
            <span className="text-slate-600">—</span>
            <span className="text-slate-300 font-medium">GPU-Parallelized Simulation Matrix</span>
          </motion.div>

          <motion.h1
            initial={{ opacity: 0, y: 15 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.6, delay: 0.1 }}
            className="text-4xl sm:text-6xl lg:text-7xl font-extrabold text-white tracking-tight leading-[1.12] max-w-4xl font-sans mx-auto"
          >
            High-Throughput Biophysics <br />
            <span className="bg-gradient-to-r from-blue-400 via-indigo-300 to-violet-400 bg-clip-text text-transparent drop-shadow-[0_0_30px_rgba(99,102,241,0.45)]">
              At Millisecond Precision.
            </span>
          </motion.h1>

          <motion.p
            initial={{ opacity: 0, y: 15 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.6, delay: 0.2 }}
            className="mt-6 text-base sm:text-lg text-slate-300 max-w-2xl font-normal leading-relaxed mx-auto"
          >
            Gain full visibility across your in-silico biophysics pipeline with real-time AI assistance, automated ion sweeps, and effortless safety controls.
          </motion.p>
        </div>

        {/* Dense 3-Column Feature Matrix filling full page width without empty voids */}
        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
          {FEATURES.map((feature, idx) => {
            const Icon = feature.icon;
            return (
              <motion.div
                key={feature.title}
                initial={{ opacity: 0, y: 20 }}
                whileInView={{ opacity: 1, y: 0 }}
                viewport={{ once: true }}
                transition={{ duration: 0.4, delay: idx * 0.08 }}
                className="p-6 sm:p-7 rounded-3xl bg-slate-950/80 border border-white/10 hover:border-blue-500/40 backdrop-blur-2xl transition-all duration-300 hover:-translate-y-1 shadow-xl hover:shadow-[0_15px_40px_rgba(0,0,0,0.7),0_0_25px_rgba(59,130,246,0.2)] flex flex-col justify-between group"
              >
                <div className="space-y-4">
                  <div className="flex items-center justify-between">
                    <div className={`w-12 h-12 rounded-2xl border flex items-center justify-center ${feature.accentColor} shadow-inner group-hover:scale-105 transition-transform`}>
                      <Icon className="w-6 h-6" />
                    </div>
                    <span className="text-[10px] font-mono font-bold px-2.5 py-1 rounded-full bg-white/5 border border-white/10 text-slate-300">
                      {feature.benefit}
                    </span>
                  </div>

                  <h3 className="text-xl font-bold text-white group-hover:text-blue-300 transition-colors">
                    {feature.title}
                  </h3>

                  <p className="text-xs sm:text-sm text-slate-400 leading-relaxed font-sans">
                    {feature.description}
                  </p>
                </div>

                <div className="pt-6 mt-6 border-t border-white/[0.06] flex items-center justify-between text-xs font-mono text-blue-400 group-hover:text-blue-300">
                  <span className="font-bold">Explore Telemetry</span>
                  <ArrowRight className="w-4 h-4 group-hover:translate-x-1 transition-transform" />
                </div>
              </motion.div>
            );
          })}
        </div>

        {/* Full-Width Interactive Cockpit Showcase Card */}
        <div className="p-8 rounded-3xl bg-gradient-to-r from-blue-950/80 via-slate-950/90 to-indigo-950/80 border border-blue-500/30 backdrop-blur-2xl shadow-2xl flex flex-col lg:flex-row items-center justify-between gap-8">
          <div className="space-y-3 max-w-2xl">
            <span className="px-3 py-1 rounded-full bg-blue-500/20 text-blue-300 text-xs font-mono font-bold border border-blue-500/40">
              READY IN SECONDS
            </span>
            <h3 className="text-2xl sm:text-3xl font-bold text-white">
              Instant In-Silico Toxicity Reports
            </h3>
            <p className="text-xs sm:text-sm text-slate-300 leading-relaxed">
              Launch standard benchmarks like Ketamine or Diazepam, or build custom multi-target molecules with direct-manipulation patch-clamp sliders.
            </p>
          </div>

          <Link
            to="/app/dose-eval"
            className="flex items-center gap-2 px-8 py-3.5 rounded-full bg-blue-600 hover:bg-blue-500 text-white font-bold text-sm shadow-[0_0_25px_rgba(37,99,235,0.5)] transition-all hover:scale-105 shrink-0"
          >
            <span>Open Dose Lab</span>
            <ArrowRight className="w-4 h-4" />
          </Link>
        </div>
      </div>
    </section>
  );
}
