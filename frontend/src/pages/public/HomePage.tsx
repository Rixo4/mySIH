import React from 'react';
import { Navbar } from '../../components/landing/Navbar';
import { Hero } from '../../components/landing/Hero';
import { FeatureCards } from '../../components/landing/FeatureCards';
import { FinalCTA } from '../../components/landing/FinalCTA';
import { Footer } from '../../components/landing/Footer';
import { ArrowRight, BrainCircuit, Cpu, ShieldCheck } from 'lucide-react';
import { Link } from 'react-router-dom';

import { AnimatedContent } from '../../components/common/AnimatedContent';

export function HomePage() {
  return (
    <div className="min-h-screen bg-obsidian-950 text-slate-100 font-sans antialiased overflow-x-hidden relative">
      <Navbar />

      <main className="relative z-10">
        {/* Futuristic Hero Section */}
        <Hero />

        {/* Bento Capabilities Grid */}
        <FeatureCards />

        {/* Quick Highlights Summary Grid with AnimatedContent */}
        <section className="py-20 bg-obsidian-900/60 border-y border-white/10">
          <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8">
            <AnimatedContent distance={80} direction="vertical" duration={0.8} threshold={0.15}>
              <div className="grid grid-cols-1 md:grid-cols-3 gap-8">
                <div className="card p-6 flex flex-col justify-between">
                  <div>
                    <div className="w-10 h-10 rounded-full bg-violet-600/20 text-violet-400 border border-violet-500/30 flex items-center justify-center mb-4">
                      <Cpu className="w-5 h-5" />
                    </div>
                    <h3 className="text-lg font-bold text-white mb-2">CUDA 12 Biophysics Engine</h3>
                    <p className="text-xs text-slate-400 leading-relaxed">
                      GPU-parallelized ODE differential equation solvers running at 0.001ms precision.
                    </p>
                  </div>
                  <Link to="/features" className="mt-4 text-xs font-mono font-bold text-violet-400 hover:text-white flex items-center gap-1">
                    Explore Features <ArrowRight className="w-3.5 h-3.5" />
                  </Link>
                </div>

                <div className="card p-6 flex flex-col justify-between">
                  <div>
                    <div className="w-10 h-10 rounded-full bg-emerald-500/20 text-emerald-400 border border-emerald-500/30 flex items-center justify-center mb-4">
                      <BrainCircuit className="w-5 h-5" />
                    </div>
                    <h3 className="text-lg font-bold text-white mb-2">Automated Toxicity Sweeps</h3>
                    <p className="text-xs text-slate-400 leading-relaxed">
                      Simulate multi-dose target binding gradients and calculate Network Instability Index.
                    </p>
                  </div>
                  <Link to="/how-it-works" className="mt-4 text-xs font-mono font-bold text-emerald-400 hover:text-white flex items-center gap-1">
                    View 5-Step Pipeline <ArrowRight className="w-3.5 h-3.5" />
                  </Link>
                </div>

                <div className="card p-6 flex flex-col justify-between">
                  <div>
                    <div className="w-10 h-10 rounded-full bg-indigo-500/20 text-indigo-400 border border-indigo-500/30 flex items-center justify-center mb-4">
                      <ShieldCheck className="w-5 h-5" />
                    </div>
                    <h3 className="text-lg font-bold text-white mb-2">Audit-Ready Provenance</h3>
                    <p className="text-xs text-slate-400 leading-relaxed">
                      100% reproducible JSON input payloads, native execution logs, and benchmark suites.
                    </p>
                  </div>
                  <Link to="/showcase" className="mt-4 text-xs font-mono font-bold text-indigo-400 hover:text-white flex items-center gap-1">
                    Inspect Showcase <ArrowRight className="w-3.5 h-3.5" />
                  </Link>
                </div>
              </div>
            </AnimatedContent>
          </div>
        </section>

        {/* High-Impact Final Call To Action */}
        <FinalCTA />
      </main>

      <Footer />
    </div>
  );
}
