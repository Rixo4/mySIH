import React from 'react';
import { motion } from 'framer-motion';
import { ArrowRight, CheckCircle, ShieldCheck, Sparkles, Zap } from 'lucide-react';
import { Link } from 'react-router-dom';

export function FinalCTA() {
  return (
    <section id="cta" className="py-32 relative bg-obsidian-950 overflow-hidden text-center">
      {/* Resq.io Conical Spotlight Beam shining down */}
      <div className="absolute top-0 left-1/2 -translate-x-1/2 w-full max-w-4xl h-full conical-light-beam pointer-events-none opacity-80" />
      <div className="absolute top-1/3 left-1/2 -translate-x-1/2 -translate-y-1/2 w-96 h-96 bg-violet-600/25 rounded-full blur-[150px] pointer-events-none" />

      {/* Floating Micro-Particles */}
      <div className="absolute inset-0 bg-grid opacity-20 pointer-events-none" />

      <div className="max-w-4xl mx-auto px-4 sm:px-6 lg:px-8 relative z-10">
        <motion.div
          initial={{ opacity: 0, y: 30 }}
          whileInView={{ opacity: 1, y: 0 }}
          viewport={{ once: true }}
          transition={{ duration: 0.8 }}
          className="space-y-6"
        >
          <div className="text-xs font-mono font-bold text-violet-400 uppercase tracking-widest">Resq.io / NeuroSIH Platform</div>

          <h2 className="text-4xl sm:text-6xl lg:text-7xl font-extrabold text-white tracking-tight leading-tight font-sans">
            Book Your Demo Today
          </h2>

          <p className="text-slate-400 text-sm sm:text-base max-w-md mx-auto leading-relaxed">
            Ready to streamline your biophysical simulation & incident management? <br />
            Schedule a call with our experts today.
          </p>

          <div className="pt-6 flex justify-center">
            <Link
              to="/login"
              className="btn-primary text-sm px-10 py-4 font-sans rounded-full shadow-[0_0_50px_rgba(139,92,246,0.6)] hover:shadow-[0_0_80px_rgba(139,92,246,0.9)] transition-all"
            >
              <span>Book a demo.</span>
            </Link>
          </div>
        </motion.div>
      </div>
    </section>
  );
}
