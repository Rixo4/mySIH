import React, { useEffect, useMemo, useState } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import {
  History,
  RefreshCw,
  Activity,
  CheckCircle2,
  AlertTriangle,
  Clock,
  Layers,
  Database,
  Download,
  X,
} from 'lucide-react';
import { deleteRun, getRuns } from '../api/client';
import { RunHistoryTable } from '../components/RunHistoryTable';
import { useAuth } from '../context/AuthContext';
import { useRunTask } from '../context/RunTaskContext';
import { useToast } from '../context/ToastContext';
import { useTheme } from '../context/ThemeContext';
import type { RunListItem } from '../types';
import { formatDuration } from '../lib/format';

import { AssayProgressDistribution } from '../components/history/AssayProgressDistribution';
import { ComputeLatencySpline } from '../components/history/ComputeLatencySpline';
import { StabilityVolumeHistogram } from '../components/history/StabilityVolumeHistogram';
import { ReceptorRiskHeatmap } from '../components/history/ReceptorRiskHeatmap';
import { ErrorBoundary } from '../components/common/ErrorBoundary';

type HistoryFocusWidget = 'progress' | 'latency' | 'volume' | 'heatmap' | null;

export function RunHistoryPage() {
  const { user } = useAuth();
  const { theme } = useTheme();
  const { drugEvaluationState } = useRunTask();
  const { addToast } = useToast();

  const [runs, setRuns] = useState<RunListItem[]>([]);
  const [loading, setLoading] = useState(true);
  const [refreshing, setRefreshing] = useState(false);
  const [_error, setError] = useState<string | null>(null);
  const [focusedWidget, setFocusedWidget] = useState<HistoryFocusWidget>(null);

  const [typeFilter, setTypeFilter] = useState('all');
  const [riskFilter, setRiskFilter] = useState('all');
  const [recommendationFilter, setRecommendationFilter] = useState('all');
  const [searchQuery, setSearchQuery] = useState('');

  // Close zoomed view on Escape key
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        setFocusedWidget(null);
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, []);

  async function refreshRuns(isManual = false) {
    try {
      if (isManual) setRefreshing(true);
      else setLoading(true);

      const response = await getRuns();
      setRuns(Array.isArray(response?.runs) ? response.runs : []);
      setError(null);
      if (isManual) {
        addToast({
          type: 'success',
          title: 'Ledger Synced',
          message: 'Retrieved latest simulation assay logs.',
        });
      }
    } catch (err) {
      console.warn('Could not fetch runs:', err);
      setRuns([]);
      setError(err instanceof Error ? err.message : 'Backend unreachable');
    } finally {
      setLoading(false);
      setRefreshing(false);
    }
  }

  useEffect(() => {
    void refreshRuns();
  }, []);

  useEffect(() => {
    if (drugEvaluationState.status === 'completed') {
      void refreshRuns();
    }
  }, [drugEvaluationState.status]);

  const filteredRuns = useMemo(() => {
    const safeList = Array.isArray(runs) ? runs : [];
    return safeList.filter((run) => {
      if (!run) return false;
      const typeMatch = typeFilter === 'all' || run.report_type === typeFilter;
      const riskMatch =
        riskFilter === 'all' || (run.risk_level ?? '').toUpperCase() === riskFilter;
      const recommendationMatch =
        recommendationFilter === 'all' ||
        (run.recommendation ?? '').toUpperCase().includes(recommendationFilter.toUpperCase());

      const searchMatch =
        !searchQuery ||
        (run.run_id && run.run_id.toLowerCase().includes(searchQuery.toLowerCase())) ||
        (run.drug_name && run.drug_name.toLowerCase().includes(searchQuery.toLowerCase()));

      return typeMatch && riskMatch && recommendationMatch && searchMatch;
    });
  }, [runs, typeFilter, riskFilter, recommendationFilter, searchQuery]);

  // Analytics Metrics
  const metrics = useMemo(() => {
    const safeList = Array.isArray(runs) ? runs : [];
    const total = safeList.length;
    const completed = safeList.filter((r) => r && r.status === 'completed').length;
    const highRisk = safeList.filter(
      (r) =>
        r &&
        ((r.risk_level ?? '').toUpperCase() === 'HIGH' ||
          (r.recommendation ?? '').toUpperCase().includes('REJECT'))
    ).length;
    const runtimes = safeList
      .map((r) => r?.duration_seconds ?? r?.runtime_seconds ?? null)
      .filter((v): v is number => typeof v === 'number');
    const avgRuntime =
      runtimes.length > 0 ? runtimes.reduce((a, b) => a + b, 0) / runtimes.length : null;

    return { total, completed, highRisk, avgRuntime };
  }, [runs]);

  const handleExportJson = () => {
    const dataStr =
      'data:text/json;charset=utf-8,' + encodeURIComponent(JSON.stringify(runs, null, 2));
    const downloadAnchor = document.createElement('a');
    downloadAnchor.setAttribute('href', dataStr);
    downloadAnchor.setAttribute('download', 'silicon_patient_run_history.json');
    document.body.appendChild(downloadAnchor);
    downloadAnchor.click();
    downloadAnchor.remove();
  };

  async function handleDeleteRun(runId: string) {
    try {
      await deleteRun(runId);
      setRuns((currentRuns) => currentRuns.filter((run) => run.run_id !== runId));
      addToast({
        type: 'success',
        title: 'Run Deleted',
        message: `Removed experiment run ${runId}`,
      });
    } catch (err) {
      const msg = err instanceof Error ? err.message : 'Failed to delete run';
      addToast({ type: 'error', title: 'Deletion Failed', message: msg });
      throw err;
    }
  }

  return (
    <div className="relative min-h-screen p-3 sm:p-5 space-y-3.5 pb-28 font-sans text-slate-100 bg-[#08080f] overflow-hidden">

      {/* ── Background Atmosphere Layers ── */}
      <div
        className="absolute inset-0 pointer-events-none opacity-20"
        style={{
          backgroundImage: 'radial-gradient(circle at 1px 1px, rgba(37, 99, 235, 0.15) 1px, transparent 0)',
          backgroundSize: '36px 36px',
        }}
      />
      <div
        className="absolute inset-0 pointer-events-none"
        style={{
          background: 'radial-gradient(ellipse 80% 45% at 50% 0%, rgba(30, 64, 175, 0.18) 0%, transparent 60%)',
        }}
      />

      {/* ── SECTION 1: Instrumentation Header Panel (TradeWise Glow & Telemetry Pods) ── */}
      <div className="relative z-10 rounded-2xl border border-slate-800/80 bg-[#0d0d18] p-4 sm:p-5 shadow-xl overflow-hidden">
        {/* Ambient Glow Backdrop */}
        <div
          className="absolute inset-0 pointer-events-none opacity-30"
          style={{
            background: 'radial-gradient(circle at 60% 50%, rgba(99, 102, 241, 0.15) 0%, rgba(56, 189, 248, 0.06) 35%, transparent 70%)',
          }}
        />
        <div className="absolute right-1/3 top-1/2 -translate-y-1/2 w-48 h-48 bg-purple-600/10 rounded-full blur-3xl pointer-events-none" />

        <div className="relative z-10 flex flex-col lg:flex-row lg:items-center lg:justify-between gap-4">
          <div className="space-y-1.5">
            {/* Badges Row */}
            <div className="flex items-center gap-2">
              <span className="text-[10px] font-mono font-bold px-2.5 py-0.5 rounded bg-[#111120] text-slate-300 border border-slate-800">
                AUDIT LEDGER v2.4
              </span>
              <span className="flex items-center gap-1.5 text-[10px] font-mono font-bold text-emerald-400">
                <span className="w-2 h-2 rounded-full bg-emerald-400 shadow-[0_0_8px_#34d399] animate-pulse" />
                IMMUTABLE SYNC
              </span>
            </div>

            {/* Title & Subtitle */}
            <h1 className="text-lg sm:text-xl font-bold tracking-tight text-white">
              {user?.full_name ? `${user.full_name}'s Experiment History` : 'Simulation Run History'}
            </h1>
            <p className="text-xs text-slate-400 max-w-2xl leading-relaxed">
              Repository of patch-clamp simulations, dose sweeps, voltage traces, and biophysical safety ratings with multi-run differential comparison.
            </p>
          </div>

          {/* Right Telemetry Pods Container */}
          <div className="bg-[#0a0a14]/90 border border-slate-800/80 p-2 sm:p-2.5 rounded-2xl flex flex-wrap items-center gap-2.5 shadow-xl backdrop-blur-md shrink-0">
            {/* Pod 1: Total Runs */}
            <div className="p-2 sm:p-2.5 rounded-xl bg-[#111120]/90 border border-slate-800/80 min-w-[95px] space-y-0.5">
              <div className="flex items-center gap-1.5 text-[9px] font-sans uppercase font-bold text-slate-400">
                <Layers size={11} className="text-slate-400" />
                <span>Total Runs</span>
              </div>
              <div className="text-xs sm:text-sm font-mono font-black text-white">
                {metrics.total}
              </div>
            </div>

            {/* Pod 2: Completed */}
            <div className="p-2 sm:p-2.5 rounded-xl bg-[#111120]/90 border border-slate-800/80 min-w-[95px] space-y-0.5">
              <div className="flex items-center gap-1.5 text-[9px] font-sans uppercase font-bold text-emerald-400">
                <CheckCircle2 size={11} className="text-emerald-400" />
                <span>Completed</span>
              </div>
              <div className="text-xs sm:text-sm font-mono font-black text-emerald-400">
                {metrics.completed}
              </div>
            </div>

            {/* Pod 3: Risk Flags */}
            <div className="p-2 sm:p-2.5 rounded-xl bg-[#111120]/90 border border-slate-800/80 min-w-[95px] space-y-0.5">
              <div className="flex items-center gap-1.5 text-[9px] font-sans uppercase font-bold text-rose-400">
                <AlertTriangle size={11} className="text-rose-400" />
                <span>Risk Flags</span>
              </div>
              <div className="text-xs sm:text-sm font-mono font-black text-rose-400">
                {metrics.highRisk}
              </div>
            </div>

            {/* Pod 4: Avg Compute */}
            <div className="p-2 sm:p-2.5 rounded-xl bg-[#111120]/90 border border-slate-800/80 min-w-[105px] space-y-0.5">
              <div className="flex items-center gap-1.5 text-[9px] font-sans uppercase font-bold text-sky-400">
                <Clock size={11} className="text-sky-400" />
                <span>Avg Compute</span>
              </div>
              <div className="text-xs sm:text-sm font-mono font-bold text-slate-200">
                {metrics.avgRuntime ? formatDuration(metrics.avgRuntime) : '—'}
              </div>
            </div>
          </div>
        </div>

        {/* Action Controls Ribbon */}
        <div className="relative z-10 mt-3 pt-3 border-t border-slate-800/80 flex flex-wrap items-center justify-between gap-2">
          <div className="flex items-center gap-2">
            <span className="text-[11px] font-mono text-slate-400 font-medium">
              Ledger Actions:
            </span>
            <button
              onClick={handleExportJson}
              className="text-xs px-2.5 py-1 rounded-lg bg-[#111120] border border-slate-800 hover:border-blue-500/50 hover:bg-blue-500/10 text-slate-300 hover:text-white transition-all cursor-pointer font-sans flex items-center gap-1.5"
            >
              <Download className="w-3.5 h-3.5 text-blue-400" />
              <span>Export JSON</span>
            </button>

            <button
              onClick={() => refreshRuns(true)}
              disabled={loading || refreshing}
              className="text-xs px-2.5 py-1 rounded-lg bg-blue-500/10 border border-blue-500/30 hover:border-blue-400 text-blue-300 hover:text-white transition-all cursor-pointer font-sans flex items-center gap-1.5"
            >
              <RefreshCw className={`w-3.5 h-3.5 text-blue-400 ${refreshing ? 'animate-spin' : ''}`} />
              <span>{refreshing ? 'Syncing...' : 'Sync Ledger'}</span>
            </button>
          </div>

          <div className="text-[10px] font-mono text-slate-500">
            Filtering <strong className="text-slate-300">{filteredRuns.length}</strong> of <strong className="text-slate-300">{runs.length}</strong> recorded runs
          </div>
        </div>
      </div>

      {/* ── SECTION 2: 4 Telemetry Panels Grid (TradeWise Glass Layout) ── */}
      <div className="relative z-10 grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-3.5">
        <div
          onClick={() => setFocusedWidget('progress')}
          className="cursor-pointer rounded-2xl border border-slate-800/80 hover:border-blue-500/40 bg-[#0d0d18] p-3 shadow-xl transition-all"
        >
          <ErrorBoundary>
            <AssayProgressDistribution runs={runs} />
          </ErrorBoundary>
        </div>

        <div
          onClick={() => setFocusedWidget('latency')}
          className="cursor-pointer rounded-2xl border border-slate-800/80 hover:border-blue-500/40 bg-[#0d0d18] p-3 shadow-xl transition-all"
        >
          <ErrorBoundary>
            <ComputeLatencySpline runs={runs} />
          </ErrorBoundary>
        </div>

        <div
          onClick={() => setFocusedWidget('volume')}
          className="cursor-pointer rounded-2xl border border-slate-800/80 hover:border-blue-500/40 bg-[#0d0d18] p-3 shadow-xl transition-all"
        >
          <ErrorBoundary>
            <StabilityVolumeHistogram runs={runs} />
          </ErrorBoundary>
        </div>

        <div
          onClick={() => setFocusedWidget('heatmap')}
          className="cursor-pointer rounded-2xl border border-slate-800/80 hover:border-blue-500/40 bg-[#0d0d18] p-3 shadow-xl transition-all"
        >
          <ErrorBoundary>
            <ReceptorRiskHeatmap runs={runs} />
          </ErrorBoundary>
        </div>
      </div>

      {/* ── SECTION 3: Interactive Simulation Ledger Table (TradeWise Container) ── */}
      <div className="relative z-10 rounded-2xl border border-slate-800/80 bg-[#0d0d18] p-3.5 sm:p-4 shadow-xl">
        <ErrorBoundary>
          <RunHistoryTable
            runs={filteredRuns}
            typeFilter={typeFilter}
            riskFilter={riskFilter}
            recommendationFilter={recommendationFilter}
            searchQuery={searchQuery}
            onTypeFilterChange={setTypeFilter}
            onRiskFilterChange={setRiskFilter}
            onRecommendationFilterChange={setRecommendationFilter}
            onSearchQueryChange={setSearchQuery}
            onDeleteRun={handleDeleteRun}
          />
        </ErrorBoundary>
      </div>

      {/* ═════════════════════════════════════════════════════════════════
          CLICK-TO-ZOOM CENTERED OVERLAY (Flat Modal)
          ═════════════════════════════════════════════════════════════════ */}
      <AnimatePresence>
        {focusedWidget !== null && (
          <div className="fixed top-12 left-0 right-0 bottom-0 z-40 flex items-center justify-center p-4 sm:p-8">
            <motion.div
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              exit={{ opacity: 0 }}
              onClick={() => setFocusedWidget(null)}
              className="absolute inset-0 bg-black/80 backdrop-blur-xs cursor-pointer"
            />

            <motion.div
              initial={{ opacity: 0, scale: 0.96 }}
              animate={{ opacity: 1, scale: 1 }}
              exit={{ opacity: 0, scale: 0.96 }}
              transition={{ duration: 0.15 }}
              className="relative z-10 w-full max-w-2xl p-6 rounded-2xl border border-slate-800/90 bg-[#0d0d18] shadow-2xl"
            >
              {/* Close Button */}
              <button
                type="button"
                onClick={() => setFocusedWidget(null)}
                className="absolute top-4 right-4 p-1.5 rounded-lg border border-slate-800 bg-[#111120] text-slate-300 hover:text-white cursor-pointer"
                title="Close Zoom (Esc)"
              >
                <X className="w-4 h-4" />
              </button>

              {/* Render Focused Widget */}
              <div className="w-full pt-2">
                <ErrorBoundary>
                  {focusedWidget === 'progress' && <AssayProgressDistribution runs={runs} />}
                  {focusedWidget === 'latency' && <ComputeLatencySpline runs={runs} />}
                  {focusedWidget === 'volume' && <StabilityVolumeHistogram runs={runs} />}
                  {focusedWidget === 'heatmap' && <ReceptorRiskHeatmap runs={runs} />}
                </ErrorBoundary>
              </div>
            </motion.div>
          </div>
        )}
      </AnimatePresence>
    </div>
  );
}
