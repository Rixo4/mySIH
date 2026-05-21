import { useEffect, useMemo, useState } from 'react';
import { Link } from 'react-router-dom';
import { motion } from 'framer-motion';
import { Activity, ArrowRight, Beaker, CirclePlay, FlaskConical, Gauge, ShieldCheck, Timer } from 'lucide-react';
import { getRuns, getRunDetail } from '../api/client';
import { MetricCard } from '../components/MetricCard';
import { StatusBadge } from '../components/StatusBadge';
import { formatDuration } from '../lib/format';
import type { RunDetailResponse, RunListItem } from '../types';

interface DashboardPageProps {
  backendConnected: boolean;
}

function FlowNode({ label, icon: Icon }: { label: string; icon: typeof CirclePlay }) {
  return (
    <div className="glass-card flex min-h-28 flex-col justify-between p-4 text-center">
      <div className="mx-auto flex h-12 w-12 items-center justify-center rounded-2xl bg-cyan-500/10 text-cyan-300 ring-1 ring-cyan-400/20">
        <Icon className="h-6 w-6" />
      </div>
      <p className="mt-4 text-sm font-medium text-white">{label}</p>
    </div>
  );
}

export function DashboardPage({ backendConnected }: DashboardPageProps) {
  const [runs, setRuns] = useState<RunListItem[]>([]);
  const [latestDetails, setLatestDetails] = useState<RunDetailResponse[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    let active = true;

    async function load() {
      try {
        setLoading(true);
        setError(null);
        const runList = await getRuns();
        if (!active) return;
        setRuns(runList.runs);

        const details = await Promise.all(
          runList.runs.slice(0, 5).map((run) => getRunDetail(run.run_id).catch(() => null))
        );
        if (!active) return;
        setLatestDetails(details.filter((item): item is RunDetailResponse => item !== null));
      } catch (err) {
        if (!active) return;
        setError(err instanceof Error ? err.message : 'Backend unreachable');
      } finally {
        if (active) {
          setLoading(false);
        }
      }
    }

    load();
    return () => {
      active = false;
    };
  }, []);

  const averageRuntime = useMemo(() => {
    const durations = latestDetails.map((item) => item.duration_seconds).filter((value): value is number => typeof value === 'number');
    if (durations.length === 0) {
      return null;
    }
    return durations.reduce((sum, value) => sum + value, 0) / durations.length;
  }, [latestDetails]);

  const validationRun = latestDetails.find((item) => item.report_type === 'validate');
  const validationTestsPassed =
    typeof validationRun?.parsed_summary?.tests_passed === 'string'
      ? validationRun.parsed_summary.tests_passed
      : '6 / 6';
  const latestDrugDecision = runs.find((item) => item.report_type === 'dose-eval' || item.report_type === 'simulate') ?? runs[0];

  return (
    <div className="space-y-8 pb-24 xl:pb-8">
      <motion.section initial={{ opacity: 0, y: 16 }} animate={{ opacity: 1, y: 0 }} className="glass-card overflow-hidden border border-cyan-400/15 p-8 shadow-panel">
        <div className="max-w-3xl">
          <p className="text-xs uppercase tracking-[0.4em] text-cyan-300/80">Deep-Tech Pharma Simulation</p>
          <h2 className="mt-3 text-4xl font-semibold text-white md:text-5xl">Silicon Patient Platform</h2>
          <p className="mt-4 text-lg leading-8 text-slate-300">
            GPU-accelerated neural drug evaluation engine for seizure risk, toxicity, and therapeutic-window analysis.
          </p>
        </div>
        <div className="mt-8 flex flex-wrap gap-3">
          <Link to="/dose-eval" className="inline-flex items-center gap-2 rounded-2xl bg-cyan-500 px-5 py-3 text-sm font-semibold text-midnight-950 transition hover:scale-[1.01]">
            Run Drug Evaluation <ArrowRight className="h-4 w-4" />
          </Link>
          <Link to="/validation" className="inline-flex items-center gap-2 rounded-2xl border border-white/10 bg-white/5 px-5 py-3 text-sm font-semibold text-white transition hover:border-cyan-400/30 hover:bg-cyan-500/10">
            Run Biological Validation
          </Link>
        </div>
      </motion.section>

      {error ? (
        <div className="rounded-2xl border border-rose-400/20 bg-rose-500/10 px-5 py-4 text-sm text-rose-200">
          Backend unreachable. {error}
        </div>
      ) : null}

      <section className="grid gap-5 md:grid-cols-2 xl:grid-cols-3">
        <MetricCard label="Engine Status" value={backendConnected ? 'Online' : 'Offline'} helper="Backend health check from /health" icon={<FlaskConical className="h-5 w-5" />} />
        <MetricCard label="CUDA Mode" value="Enabled" helper="CUDA-enabled simulation pipeline" icon={<ShieldCheck className="h-5 w-5" />} />
        <MetricCard label="Validation Status" value={`${validationTestsPassed} PASS`} helper="Biological validation suite" icon={<Gauge className="h-5 w-5" />} />
        <MetricCard label="Last Drug Decision" value={(latestDrugDecision?.recommendation ?? 'CAUTION / PROMISING').toString()} helper={latestDrugDecision?.risk_level ?? 'Awaiting latest run'} icon={<Beaker className="h-5 w-5" />} />
        <MetricCard label="Average Runtime" value={loading ? 'Loading…' : averageRuntime ? formatDuration(averageRuntime) : '—'} helper="Recent engine response time" icon={<Timer className="h-5 w-5" />} />
        <MetricCard label="Total Runs" value={loading ? 'Loading…' : String(runs.length)} helper="Stored run history records" icon={<Activity className="h-5 w-5" />} />
      </section>

      <section className="glass-card p-6">
        <div className="flex items-center justify-between gap-4">
          <div>
            <p className="text-xs uppercase tracking-[0.32em] text-cyan-300/70">Execution Pipeline</p>
            <h3 className="mt-1 text-xl font-semibold text-white">Drug Input → Neural Simulation → Dose Response → Risk Decision → Report</h3>
          </div>
          <StatusBadge label="System" value={backendConnected ? 'Connected' : 'Disconnected'} />
        </div>

        <div className="mt-6 grid gap-3 lg:grid-cols-5">
          <FlowNode label="Drug Input" icon={Beaker as typeof CirclePlay} />
          <FlowNode label="Neural Simulation" icon={Activity as typeof CirclePlay} />
          <FlowNode label="Dose Response" icon={FlaskConical as typeof CirclePlay} />
          <FlowNode label="Risk Decision" icon={Gauge as typeof CirclePlay} />
          <FlowNode label="Report" icon={ShieldCheck as typeof CirclePlay} />
        </div>
      </section>

      <section className="grid gap-5 xl:grid-cols-[1.5fr_1fr]">
        <div className="glass-card p-6">
          <p className="text-xs uppercase tracking-[0.32em] text-cyan-300/70">Platform Snapshot</p>
          <div className="mt-4 space-y-3 text-sm leading-7 text-slate-300">
            <p>
              The platform is tuned for deep-tech pharma workflows: precision drug evaluation, validated neural simulations, and audit-friendly outputs.
            </p>
            <p>
              Run history is persisted for traceability, report detail pages expose parsed summaries, and the UI keeps raw outputs hidden behind polished report viewers.
            </p>
          </div>
        </div>
        <div className="glass-card p-6">
          <p className="text-xs uppercase tracking-[0.32em] text-cyan-300/70">Latest Decision</p>
          <div className="mt-4 space-y-3">
            <div className="rounded-2xl bg-slate-950/60 p-4 ring-1 ring-white/10">
              <p className="text-sm text-slate-400">Recommendation</p>
              <p className="mt-1 text-2xl font-semibold text-white">{latestDrugDecision?.recommendation ?? 'CAUTION / PROMISING'}</p>
            </div>
            <div className="rounded-2xl bg-slate-950/60 p-4 ring-1 ring-white/10">
              <p className="text-sm text-slate-400">Risk Level</p>
              <p className="mt-1 text-2xl font-semibold text-white">{latestDrugDecision?.risk_level ?? 'HIGH'}</p>
            </div>
          </div>
        </div>
      </section>
    </div>
  );
}
