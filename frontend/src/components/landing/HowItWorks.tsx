import React from 'react';
import { motion } from 'framer-motion';
import { Activity, Beaker, CheckCircle2, Cpu, FileCheck2, Sliders, Zap, ArrowRight, Layers, ShieldCheck } from 'lucide-react';
import { Link } from 'react-router-dom';

const STEPS = [
  {
    step: '01',
    icon: Sliders,
    title: 'Target Binding Matrix',
    description: 'Configure IC50 and Hill slope coefficients for Na+, K+, and Ca2+ ion channel targets.',
    badge: 'Kinetics Input',
  },
  {
    step: '02',
    icon: Beaker,
    title: 'Titration Gradient Config',
    description: 'Set concentration sweep intervals, step increments, and stochastic repeats.',
    badge: 'Dose Range',
  },
  {
    step: '03',
    icon: Cpu,
    title: 'CUDA ODE Solvers',
    description: 'Execute high-speed parallel Hodgkin-Huxley solvers across GPU microcircuits.',
    badge: '0.001ms Step',
  },
  {
    step: '04',
    icon: Activity,
    title: 'Network Instability Index',
    description: 'Detect Network Instability Index (NII), firing frequencies, and pro-convulsant liabilities.',
    badge: 'Emergence Analytics',
  },
  {
    step: '05',
    icon: FileCheck2,
    title: 'Audit-Ready Provenance',
    description: 'Inspect interactive dose-effect curves, safety ratings, and export full JSON/CSV datasets.',
    badge: 'Instant PDF / CSV',
  },
];

export function HowItWorks() {
  return (
    <section id="how-it-works" className="py-24 relative bg-obsidian-950 overflow-hidden">
      {/* Ambient Glow background */}
      <div className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-[900px] h-[400px] bg-blue-600/10 rounded-full blur-[150px] pointer-events-none" />

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
            <span className="text-blue-300 font-bold">Automated 5-Step Pipeline</span>
            <span className="text-slate-600">—</span>
            <span className="text-slate-300 font-medium">From Kinetics to Provenance</span>
          </motion.div>

          <motion.h1
            initial={{ opacity: 0, y: 15 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.6, delay: 0.1 }}
            className="text-4xl sm:text-6xl lg:text-7xl font-extrabold text-white tracking-tight leading-[1.12] max-w-4xl font-sans mx-auto"
          >
            From Target Kinetic Inputs <br />
            <span className="bg-gradient-to-r from-blue-400 via-indigo-300 to-violet-400 bg-clip-text text-transparent drop-shadow-[0_0_30px_rgba(99,102,241,0.45)]">
              To Audit-Ready Reports.
            </span>
          </motion.h1>

          <motion.p
            initial={{ opacity: 0, y: 15 }}
            animate={{ opacity: 1, y: 0 }}
            transition={{ duration: 0.6, delay: 0.2 }}
            className="mt-6 text-base sm:text-lg text-slate-300 max-w-2xl font-normal leading-relaxed mx-auto"
          >
            From raw chemical target parameters to high-fidelity biophysical toxicity reports in under 3 minutes.
          </motion.p>
        </div>

        {/* 5-Step Process Container with Connecting Line spanning full width */}
        <div className="relative">
          {/* Connecting Line */}
          <div className="hidden lg:block absolute top-1/2 left-[5%] right-[5%] h-0.5 bg-gradient-to-r from-blue-500/20 via-indigo-500/50 to-blue-500/20 -translate-y-12 z-0" />

          <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-5 gap-5 relative z-10">
            {STEPS.map((item, index) => {
              const Icon = item.icon;
              return (
                <motion.div
                  key={item.step}
                  initial={{ opacity: 0, y: 25 }}
                  whileInView={{ opacity: 1, y: 0 }}
                  viewport={{ once: true }}
                  transition={{ duration: 0.4, delay: index * 0.08 }}
                  className="p-6 rounded-3xl bg-slate-950/80 border border-white/10 hover:border-blue-500/40 backdrop-blur-xl flex flex-col justify-between group transition-all duration-300 hover:-translate-y-1 shadow-xl hover:shadow-[0_15px_40px_rgba(0,0,0,0.7),0_0_25px_rgba(59,130,246,0.2)]"
                >
                  <div className="space-y-4">
                    {/* Step Number & Icon */}
                    <div className="flex items-center justify-between">
                      <div className="w-12 h-12 rounded-2xl bg-blue-500/15 border border-blue-400/30 text-blue-300 flex items-center justify-center group-hover:scale-110 transition-transform shadow-inner">
                        <Icon className="w-6 h-6" />
                      </div>
                      <span className="font-mono text-xs font-bold px-2.5 py-0.5 rounded-full bg-white/5 border border-white/10 text-slate-400">
                        {item.step}
                      </span>
                    </div>

                    <h3 className="text-base font-bold text-white group-hover:text-blue-300 transition-colors">
                      {item.title}
                    </h3>

                    <p className="text-xs text-slate-400 leading-relaxed font-sans">
                      {item.description}
                    </p>
                  </div>

                  <div className="pt-4 mt-4 border-t border-white/[0.06]">
                    <span className="text-[10px] font-mono text-blue-400 font-bold block">
                      {item.badge}
                    </span>
                  </div>
                </motion.div>
              );
            })}
          </div>
        </div>

        {/* Dense Architecture Overview Card (No empty spaces) */}
        <div className="p-8 rounded-3xl bg-slate-950/90 border border-white/10 backdrop-blur-2xl shadow-2xl space-y-6">
          <div className="flex flex-col sm:flex-row sm:items-center justify-between gap-4 pb-4 border-b border-white/10">
            <div>
              <h3 className="text-xl font-bold text-white font-sans flex items-center gap-2">
                <span>Hodgkin-Huxley Multi-Compartmental Simulation Engine</span>
              </h3>
              <p className="text-xs text-slate-400 mt-1">
                Deterministic and stochastic differential equations solved with adaptive Euler integrators.
              </p>
            </div>
            <Link
              to="/app/dose-eval"
              className="flex items-center gap-2 px-6 py-2.5 rounded-full bg-blue-600 hover:bg-blue-500 text-white font-bold text-xs shadow-lg transition-all"
            >
              <span>Test In Dose Lab</span>
              <ArrowRight className="w-3.5 h-3.5" />
            </Link>
          </div>

          <div className="grid grid-cols-1 sm:grid-cols-3 gap-4">
            <div className="p-4 rounded-2xl bg-white/[0.02] border border-white/5 space-y-1">
              <div className="text-xs font-mono text-blue-400 font-bold">1. Gating Dynamics</div>
              <p className="text-xs text-slate-400">Dynamic $m, h, n$ activation variables updated every 1 microsecond.</p>
            </div>
            <div className="p-4 rounded-2xl bg-white/[0.02] border border-white/5 space-y-1">
              <div className="text-xs font-mono text-indigo-400 font-bold">2. Synaptic Weights</div>
              <p className="text-xs text-slate-400">80% glutamatergic excitation / 20% GABAergic inhibition network.</p>
            </div>
            <div className="p-4 rounded-2xl bg-white/[0.02] border border-white/5 space-y-1">
              <div className="text-xs font-mono text-emerald-400 font-bold">3. NII Risk Scoring</div>
              <p className="text-xs text-slate-400">Automatic spectral burst detection across simulated EEG traces.</p>
            </div>
          </div>
        </div>
      </div>
    </section>
  );
}
