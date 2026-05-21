import { motion } from 'framer-motion';
import { Cpu, Loader2, Sigma } from 'lucide-react';

interface LoadingEngineStateProps {
  title?: string;
  subtext?: string;
  steps?: string[];
  activeStep?: number;
}

export function LoadingEngineState({
  title = 'Running GPU simulations...',
  subtext = 'This may take several minutes for multi-run dose evaluation.',
  steps = [
    'Validating input',
    'Launching CUDA engine',
    'Running dose sweep',
    'Computing therapeutic window',
    'Generating report'
  ],
  activeStep = 0
}: LoadingEngineStateProps) {
  return (
    <motion.div
      initial={{ opacity: 0, y: 20 }}
      animate={{ opacity: 1, y: 0 }}
      className="glass-card border border-cyan-400/15 p-6 shadow-panel"
    >
      <div className="flex flex-col gap-6 lg:flex-row lg:items-center lg:justify-between">
        <div className="flex items-center gap-4">
          <div className="rounded-2xl bg-cyan-500/10 p-4 text-cyan-300 ring-1 ring-cyan-400/20">
            <Loader2 className="h-7 w-7 animate-spin" />
          </div>
          <div>
            <h3 className="text-xl font-semibold text-white">{title}</h3>
            <p className="mt-1 max-w-2xl text-sm text-slate-400">{subtext}</p>
          </div>
        </div>
        <div className="flex items-center gap-2 text-xs uppercase tracking-[0.3em] text-slate-400">
          <Cpu className="h-4 w-4 text-cyan-300" />
          GPU pipeline active
          <Sigma className="h-4 w-4 text-emerald-300" />
        </div>
      </div>

      <div className="mt-6 grid gap-3 md:grid-cols-5">
        {steps.map((step, index) => {
          const isActive = index === activeStep;
          const isDone = index < activeStep;
          return (
            <div
              key={step}
              className={`rounded-2xl border px-4 py-3 text-sm transition ${
                isActive
                  ? 'border-cyan-400/40 bg-cyan-500/10 text-cyan-100 shadow-glow'
                  : isDone
                    ? 'border-emerald-400/20 bg-emerald-500/10 text-emerald-200'
                    : 'border-white/10 bg-slate-950/40 text-slate-400'
              }`}
            >
              <div className="flex items-center justify-between gap-3">
                <span>{step}</span>
                <span className="text-[10px] uppercase tracking-[0.28em]">{String(index + 1).padStart(2, '0')}</span>
              </div>
            </div>
          );
        })}
      </div>
    </motion.div>
  );
}
