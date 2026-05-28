import { useEffect, useState } from 'react';
import { motion } from 'framer-motion';
import { AlertCircle, PlayCircle } from 'lucide-react';
import { LoadingEngineState } from '../components/LoadingEngineState';
import { MetricCard } from '../components/MetricCard';
import { ReportViewer } from '../components/ReportViewer';
import { ValidationCard } from '../components/ValidationCard';
import { StatusBadge } from '../components/StatusBadge';
import { parseValidationSections } from '../lib/validation';
import type { BackendRunResponse } from '../types';
import { useRunTask } from '../context/RunTaskContext';

function summaryValue(result: BackendRunResponse | null, key: string): string {
  const value = result?.parsed_summary?.[key];
  return typeof value === 'string' ? value : typeof value === 'number' ? String(value) : '—';
}

export function ValidationPage() {
  const { validationState, runValidationTask } = useRunTask();
  const [activeStep, setActiveStep] = useState(0);
  const loading = validationState.status === 'queued' || validationState.status === 'running';
  const result = validationState.result;
  const error = validationState.error;

  const startValidation = async () => {
    setActiveStep(0);
    await runValidationTask();
  };

  useEffect(() => {
    if (!loading) {
      if (validationState.status === 'completed' || validationState.status === 'failed') {
        setActiveStep(4);
      }
      return;
    }

    const timer = window.setInterval(() => {
      setActiveStep((step) => (step + 1) % 5);
    }, 1100);

    return () => {
      window.clearInterval(timer);
    };
  }, [loading, validationState.status]);

  const sections = parseValidationSections(result?.raw_report);

  return (
    <div className="space-y-6 pb-24 xl:pb-8">
      <motion.div initial={{ opacity: 0, y: 12 }} animate={{ opacity: 1, y: 0 }} className="glass-card p-6">
        <div className="flex flex-col gap-4 lg:flex-row lg:items-center lg:justify-between">
          <div>
            <p className="text-xs uppercase tracking-[0.32em] text-cyan-300/70">Internal Biological Benchmark Suite</p>
            <h2 className="mt-2 text-3xl font-semibold text-white">Run the internal biological benchmark suite</h2>
            <p className="mt-3 max-w-3xl text-sm leading-7 text-slate-400">
              Validate baseline behavior, E/I balance, dose response, temporal evolution, calcium block, and synaptic disruption.
            </p>
          </div>
          <button
            type="button"
            onClick={startValidation}
            disabled={loading}
            className="inline-flex items-center gap-2 rounded-2xl bg-gradient-to-r from-cyan-500 to-emerald-500 px-5 py-3 text-sm font-semibold text-midnight-950 transition hover:scale-[1.01] disabled:cursor-not-allowed disabled:opacity-60"
          >
            <PlayCircle className="h-4 w-4" /> Run Internal Benchmark Suite
          </button>
        </div>
      </motion.div>

      {loading ? <LoadingEngineState title="Running internal benchmark suite..." subtext="Executing six benchmark tests against the engine state." activeStep={activeStep} /> : null}

      {error ? (
        <div className="rounded-2xl border border-rose-400/20 bg-rose-500/10 px-5 py-4 text-sm text-rose-200">
          <div className="flex items-center gap-2 font-semibold">
            <AlertCircle className="h-4 w-4" /> {error}
          </div>
        </div>
      ) : null}

      {result ? (
        <div className="space-y-5">
          <div className="grid gap-4 md:grid-cols-3">
            <MetricCard label="Tests Passed" value={summaryValue(result, 'tests_passed')} />
            <MetricCard label="Performance" value={summaryValue(result, 'performance')} />
            <MetricCard label="System Status" value={summaryValue(result, 'system_status')} />
          </div>

          <StatusBadge label="Internal Benchmark" value={summaryValue(result, 'system_status')} />

          <div className="grid gap-4 xl:grid-cols-3">
            {sections.map((section) => (
              <ValidationCard key={section.name} label={section.name} status={section.status} detail={section.details} />
            ))}
          </div>

          <details className="glass-card overflow-hidden">
            <summary className="cursor-pointer list-none px-5 py-4 text-lg font-semibold text-white">
              Full Internal Benchmark Report
            </summary>
            <div className="border-t border-white/10">
              {result.raw_report ? <ReportViewer title="Internal Biological Benchmark Report" report={result.raw_report} /> : <div className="p-5 text-sm text-slate-400">No report text returned.</div>}
            </div>
          </details>
        </div>
      ) : null}
    </div>
  );
}
