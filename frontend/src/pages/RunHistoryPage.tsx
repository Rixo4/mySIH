import { useEffect, useMemo, useState } from 'react';
import { motion } from 'framer-motion';
import { History } from 'lucide-react';
import { deleteRun, getRuns } from '../api/client';
import { RunHistoryTable } from '../components/RunHistoryTable';
import { StatusBadge } from '../components/StatusBadge';
import type { RunListItem } from '../types';

export function RunHistoryPage() {
  const [runs, setRuns] = useState<RunListItem[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [typeFilter, setTypeFilter] = useState('all');
  const [riskFilter, setRiskFilter] = useState('all');
  const [recommendationFilter, setRecommendationFilter] = useState('all');
  const [deletingRunId, setDeletingRunId] = useState<string | null>(null);

  useEffect(() => {
    let active = true;
    async function load() {
      try {
        setLoading(true);
        const response = await getRuns();
        if (active) {
          setRuns(response.runs);
        }
      } catch (err) {
        if (active) {
          setError(err instanceof Error ? err.message : 'Backend unreachable');
        }
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

  const filteredRuns = useMemo(
    () =>
      runs.filter((run) => {
        const typeMatch = typeFilter === 'all' || run.report_type === typeFilter;
        const riskMatch = riskFilter === 'all' || (run.risk_level ?? '').toUpperCase() === riskFilter;
        const recommendationMatch =
          recommendationFilter === 'all' || (run.recommendation ?? '').toUpperCase().includes(recommendationFilter.toUpperCase());
        return typeMatch && riskMatch && recommendationMatch;
      }),
    [recommendationFilter, riskFilter, runs, typeFilter]
  );

  async function handleDeleteRun(runId: string) {
    const confirmed = window.confirm('Delete this run history entry? This removes the stored record and its artifacts.');
    if (!confirmed) {
      return;
    }

    try {
      setDeletingRunId(runId);
      await deleteRun(runId);
      setRuns((currentRuns) => currentRuns.filter((run) => run.run_id !== runId));
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to delete run');
    } finally {
      setDeletingRunId(null);
    }
  }

  return (
    <div className="space-y-6 pb-24 xl:pb-8">
      <motion.div initial={{ opacity: 0, y: 12 }} animate={{ opacity: 1, y: 0 }} className="glass-card p-6">
        <div className="flex flex-col gap-4 lg:flex-row lg:items-center lg:justify-between">
          <div>
            <p className="text-xs uppercase tracking-[0.32em] text-cyan-300/70">Run History</p>
            <h2 className="mt-2 text-3xl font-semibold text-white">Audit-ready run ledger</h2>
            <p className="mt-3 max-w-3xl text-sm leading-7 text-slate-400">Filter engine runs by type, risk, and recommendation. Open any row to inspect the full report and metadata.</p>
          </div>
          <StatusBadge label="Runs" value={`${runs.length} Stored`} />
        </div>
      </motion.div>

      {error ? <div className="rounded-2xl border border-rose-400/20 bg-rose-500/10 px-5 py-4 text-sm text-rose-200">{error}</div> : null}
      {loading ? <div className="glass-card p-6 text-sm text-slate-400">Loading run history...</div> : null}

      <RunHistoryTable
        runs={filteredRuns}
        typeFilter={typeFilter}
        riskFilter={riskFilter}
        recommendationFilter={recommendationFilter}
        onTypeFilterChange={setTypeFilter}
        onRiskFilterChange={setRiskFilter}
        onRecommendationFilterChange={setRecommendationFilter}
        onDeleteRun={handleDeleteRun}
        deletingRunId={deletingRunId}
      />
    </div>
  );
}
