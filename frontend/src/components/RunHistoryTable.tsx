import { Link } from 'react-router-dom';
import { formatDateTime } from '../lib/format';
import type { RunListItem } from '../types';
import { StatusBadge } from './StatusBadge';
import { ArrowRight, Trash2 } from 'lucide-react';

interface RunHistoryTableProps {
  runs: RunListItem[];
  typeFilter: string;
  riskFilter: string;
  recommendationFilter: string;
  onTypeFilterChange: (value: string) => void;
  onRiskFilterChange: (value: string) => void;
  onRecommendationFilterChange: (value: string) => void;
  onDeleteRun: (runId: string) => Promise<void>;
  deletingRunId: string | null;
}

export function RunHistoryTable({
  runs,
  typeFilter,
  riskFilter,
  recommendationFilter,
  onTypeFilterChange,
  onRiskFilterChange,
  onRecommendationFilterChange,
  onDeleteRun,
  deletingRunId
}: RunHistoryTableProps) {
  return (
    <div className="glass-card overflow-hidden">
      <div className="border-b border-white/10 px-5 py-4">
        <h3 className="text-lg font-semibold text-white">Run History</h3>
        <p className="mt-1 text-sm text-slate-400">Filter stored runs and open the full report or metadata view.</p>
      </div>

      <div className="grid gap-3 border-b border-white/10 p-5 md:grid-cols-3">
        <select
          value={typeFilter}
          onChange={(event) => onTypeFilterChange(event.target.value)}
          className="rounded-2xl border border-white/10 bg-slate-950/70 px-4 py-3 text-sm text-white outline-none focus:border-cyan-400/40"
        >
          <option value="all">All Types</option>
          <option value="dose-eval">Drug Evaluation</option>
          <option value="simulate">Simulation</option>
          <option value="validate">Validation</option>
        </select>
        <select
          value={riskFilter}
          onChange={(event) => onRiskFilterChange(event.target.value)}
          className="rounded-2xl border border-white/10 bg-slate-950/70 px-4 py-3 text-sm text-white outline-none focus:border-cyan-400/40"
        >
          <option value="all">All Risk Levels</option>
          <option value="LOW">LOW</option>
          <option value="MODERATE">MODERATE</option>
          <option value="HIGH">HIGH</option>
        </select>
        <select
          value={recommendationFilter}
          onChange={(event) => onRecommendationFilterChange(event.target.value)}
          className="rounded-2xl border border-white/10 bg-slate-950/70 px-4 py-3 text-sm text-white outline-none focus:border-cyan-400/40"
        >
          <option value="all">All Recommendations</option>
          <option value="PROCEED">PROCEED</option>
          <option value="PROMISING">PROMISING</option>
          <option value="CAUTION">CAUTION</option>
          <option value="NOT RECOMMENDED">NOT RECOMMENDED</option>
        </select>
      </div>

      <div className="overflow-x-auto">
        <table className="min-w-full divide-y divide-white/8 text-left text-sm">
          <thead className="bg-slate-950/70 text-xs uppercase tracking-[0.28em] text-slate-400">
            <tr>
              <th className="px-5 py-4">Run ID</th>
              <th className="px-5 py-4">Type</th>
              <th className="px-5 py-4">Drug</th>
              <th className="px-5 py-4">Recommendation</th>
              <th className="px-5 py-4">Risk Level</th>
              <th className="px-5 py-4">Confidence</th>
              <th className="px-5 py-4">Created At</th>
              <th className="px-5 py-4">Status</th>
              <th className="px-5 py-4">Action</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-white/6">
            {runs.length === 0 ? (
              <tr>
                <td colSpan={9} className="px-5 py-10 text-center text-slate-400">
                  No runs match the selected filters.
                </td>
              </tr>
            ) : (
              runs.map((run) => (
                <tr key={run.run_id} className="bg-white/[0.02] transition hover:bg-white/[0.04]">
                  <td className="px-5 py-4 font-mono text-xs text-cyan-200">{run.run_id}</td>
                  <td className="px-5 py-4 text-slate-200">{run.report_type}</td>
                  <td className="px-5 py-4 text-slate-300">{run.drug_name ?? '—'}</td>
                  <td className="px-5 py-4 text-slate-300">{run.recommendation ?? '—'}</td>
                  <td className="px-5 py-4">
                    <StatusBadge label="Risk" value={run.risk_level} compact />
                  </td>
                  <td className="px-5 py-4 text-slate-300">{run.confidence ?? '—'}</td>
                  <td className="px-5 py-4 text-slate-300">{formatDateTime(run.created_at)}</td>
                  <td className="px-5 py-4">
                    <span className="rounded-full bg-white/5 px-3 py-1 text-xs uppercase tracking-[0.28em] text-slate-200 ring-1 ring-white/10">
                      {run.status}
                    </span>
                  </td>
                  <td className="px-5 py-4">
                    <div className="flex flex-wrap gap-2">
                      <Link
                        to={`/reports/${encodeURIComponent(run.run_id)}`}
                        className="inline-flex items-center gap-2 rounded-xl border border-white/10 bg-cyan-500/10 px-3 py-2 text-xs font-medium text-cyan-200 transition hover:border-cyan-400/30 hover:bg-cyan-500/15"
                      >
                        View Report <ArrowRight className="h-4 w-4" />
                      </Link>
                      <button
                        type="button"
                        onClick={() => onDeleteRun(run.run_id)}
                        disabled={deletingRunId === run.run_id}
                        className="inline-flex items-center gap-2 rounded-xl border border-rose-400/20 bg-rose-500/10 px-3 py-2 text-xs font-medium text-rose-200 transition hover:border-rose-300/40 hover:bg-rose-500/15 disabled:cursor-not-allowed disabled:opacity-50"
                      >
                        {deletingRunId === run.run_id ? 'Deleting...' : 'Delete'}
                        <Trash2 className="h-4 w-4" />
                      </button>
                    </div>
                  </td>
                </tr>
              ))
            )}
          </tbody>
        </table>
      </div>
    </div>
  );
}
