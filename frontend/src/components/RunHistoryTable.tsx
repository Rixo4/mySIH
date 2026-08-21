import React, { useState } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import {
  ChevronDown,
  ChevronUp,
  Download,
  Eye,
  Trash2,
  GitCompare,
  SlidersHorizontal,
  Search,
  CheckSquare,
  Square,
  Sparkles,
  ExternalLink,
  ShieldCheck,
  AlertOctagon,
  HelpCircle,
  TrendingDown,
  Flame,
  Activity,
  Layers,
} from 'lucide-react';
import { Link, useNavigate } from 'react-router-dom';
import type { RunListItem } from '../types';
import { StatusBadge } from './StatusBadge';
import { formatDateTime } from '../lib/format';

interface RunHistoryTableProps {
  runs: RunListItem[];
  typeFilter: string;
  riskFilter: string;
  recommendationFilter: string;
  searchQuery: string;
  onTypeFilterChange: (val: string) => void;
  onRiskFilterChange: (val: string) => void;
  onRecommendationFilterChange: (val: string) => void;
  onSearchQueryChange: (val: string) => void;
  onDeleteRun: (runId: string) => Promise<void>;
}

export function RunHistoryTable({
  runs,
  typeFilter,
  riskFilter,
  recommendationFilter,
  searchQuery,
  onTypeFilterChange,
  onRiskFilterChange,
  onRecommendationFilterChange,
  onSearchQueryChange,
  onDeleteRun,
}: RunHistoryTableProps) {
  const navigate = useNavigate();
  const safeRuns = Array.isArray(runs) ? runs : [];

  const [expandedRunId, setExpandedRunId] = useState<string | null>(null);
  const [selectedRunIds, setSelectedRunIds] = useState<string[]>([]);
  const [deleting, setDeleting] = useState(false);
  const [confirmDeleteId, setConfirmDeleteId] = useState<string | null>(null);

  const toggleExpand = (runId: string) => {
    setExpandedRunId(expandedRunId === runId ? null : runId);
  };

  const toggleSelect = (runId: string, e: React.MouseEvent) => {
    e.stopPropagation();
    if (selectedRunIds.includes(runId)) {
      setSelectedRunIds(selectedRunIds.filter((id) => id !== runId));
    } else {
      setSelectedRunIds([...selectedRunIds, runId]);
    }
  };

  const toggleSelectAll = () => {
    if (selectedRunIds.length === safeRuns.length) {
      setSelectedRunIds([]);
    } else {
      setSelectedRunIds(safeRuns.map((r) => r.run_id));
    }
  };

  const handleLaunchCompare = () => {
    if (selectedRunIds.length >= 2) {
      navigate(`/app/compare?runs=${selectedRunIds.join(',')}`);
    }
  };

  const handleDelete = async (runId: string, e: React.MouseEvent) => {
    e.stopPropagation();
    try {
      setDeleting(true);
      await onDeleteRun(runId);
    } catch {
      // toast in parent
    } finally {
      setDeleting(false);
      setConfirmDeleteId(null);
    }
  };

  return (
    <div className="relative rounded-xl border border-[#21262d] bg-[#12151c] overflow-hidden p-4 sm:p-5 space-y-4 font-sans text-slate-100">

      {/* ── Table Top Header & Controls ── */}
      <div className="flex flex-col lg:flex-row lg:items-center justify-between gap-3 pb-3 border-b border-[#21262d]">
        <div>
          <div className="flex items-center gap-2">
            <h3 className="text-sm font-semibold text-white uppercase tracking-wider">
              Simulation Ledger & Audit Trail
            </h3>
            <span className="px-2 py-0.5 rounded text-[10px] font-mono font-medium bg-[#1f242c] text-slate-300 border border-[#30363d]">
              {safeRuns.length} RECORDS
            </span>
          </div>
          <p className="text-xs text-slate-400 mt-0.5">
            Click any row to expand instant telemetry details, compare runs side-by-side, or export raw reports.
          </p>
        </div>

        {/* Floating Compare Launcher Pill */}
        <AnimatePresence>
          {selectedRunIds.length >= 2 && (
            <motion.div
              initial={{ opacity: 0, scale: 0.9, y: 5 }}
              animate={{ opacity: 1, scale: 1, y: 0 }}
              exit={{ opacity: 0, scale: 0.9, y: 5 }}
              className="flex items-center gap-2.5 bg-[#1f242c] border border-sky-500/40 px-3 py-1.5 rounded-lg"
            >
              <span className="text-xs font-mono text-slate-200">
                <strong className="text-white font-bold">{selectedRunIds.length}</strong> selected
              </span>
              <button
                onClick={handleLaunchCompare}
                className="btn-primary text-xs py-1 px-3 rounded flex items-center gap-1.5 cursor-pointer"
              >
                <GitCompare className="w-3.5 h-3.5" />
                <span>Launch Compare</span>
              </button>
            </motion.div>
          )}
        </AnimatePresence>
      </div>

      {/* ── Multi-Filter Matrix Bar ── */}
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-2.5">
        {/* Search Input */}
        <div className="relative">
          <Search className="w-3.5 h-3.5 text-slate-400 absolute left-3 top-2.5 pointer-events-none" />
          <input
            type="text"
            value={searchQuery}
            onChange={(e) => onSearchQueryChange(e.target.value)}
            placeholder="Search compound, run ID..."
            className="w-full pl-8 pr-3 py-1.5 rounded-lg bg-[#0d1015] border border-[#21262d] text-xs font-mono text-slate-100 placeholder:text-slate-500 focus:outline-none focus:border-sky-500 transition-colors"
          />
        </div>

        {/* Type Filter */}
        <div className="relative">
          <select
            value={typeFilter}
            onChange={(e) => onTypeFilterChange(e.target.value)}
            className="w-full px-3 py-1.5 rounded-lg bg-[#0d1015] border border-[#21262d] text-xs font-sans text-slate-200 focus:outline-none focus:border-sky-500 transition-colors cursor-pointer"
          >
            <option value="all">All Assay Types</option>
            <option value="dose_evaluation">Dose Evaluation</option>
            <option value="batch_sweep">Batch Sweep</option>
            <option value="voltage_clamp">Voltage Clamp</option>
          </select>
        </div>

        {/* Risk Filter */}
        <div className="relative">
          <select
            value={riskFilter}
            onChange={(e) => onRiskFilterChange(e.target.value)}
            className="w-full px-3 py-1.5 rounded-lg bg-[#0d1015] border border-[#21262d] text-xs font-sans text-slate-200 focus:outline-none focus:border-sky-500 transition-colors cursor-pointer"
          >
            <option value="all">All Risk Levels</option>
            <option value="LOW">Low Risk</option>
            <option value="MEDIUM">Medium / Elevated</option>
            <option value="HIGH">High / Toxic Risk</option>
          </select>
        </div>

        {/* Recommendation Filter */}
        <div className="relative">
          <select
            value={recommendationFilter}
            onChange={(e) => onRecommendationFilterChange(e.target.value)}
            className="w-full px-3 py-1.5 rounded-lg bg-[#0d1015] border border-[#21262d] text-xs font-sans text-slate-200 focus:outline-none focus:border-sky-500 transition-colors cursor-pointer"
          >
            <option value="all">All Clinical Actions</option>
            <option value="SAFE">Safe To Proceed</option>
            <option value="MONITOR">Monitor Closely</option>
            <option value="REJECT">Reject / Liability</option>
          </select>
        </div>
      </div>

      {/* ── Precision Telemetry Data Table ── */}
      <div className="overflow-x-auto rounded-lg border border-[#21262d]">
        <table className="w-full text-left text-xs border-collapse">
          <thead>
            <tr className="border-b border-[#21262d] bg-[#0d1015] text-slate-400 font-sans uppercase tracking-wider text-[10px]">
              <th className="py-2.5 pl-3 pr-2 w-8">
                <button
                  onClick={toggleSelectAll}
                  className="p-1 rounded text-slate-400 hover:text-white cursor-pointer"
                  title="Select all"
                >
                  {selectedRunIds.length === safeRuns.length && safeRuns.length > 0 ? (
                    <CheckSquare className="w-3.5 h-3.5 text-sky-400" />
                  ) : (
                    <Square className="w-3.5 h-3.5" />
                  )}
                </button>
              </th>
              <th className="py-2.5 px-3">Compound / Assay</th>
              <th className="py-2.5 px-3 font-mono">Run ID</th>
              <th className="py-2.5 px-3">Status</th>
              <th className="py-2.5 px-3">Clinical Assessment</th>
              <th className="py-2.5 px-3">Timestamp</th>
              <th className="py-2.5 pr-3 pl-3 text-right">Actions</th>
            </tr>
          </thead>

          <tbody className="divide-y divide-[#1b2028]">
            {safeRuns.length === 0 ? (
              <tr>
                <td colSpan={7} className="py-12 text-center text-slate-500">
                  <div className="flex flex-col items-center justify-center gap-2">
                    <Activity className="w-6 h-6 text-slate-600" />
                    <span className="font-sans text-xs">No simulation logs found matching your filters.</span>
                  </div>
                </td>
              </tr>
            ) : (
              safeRuns.map((run) => {
                const isSelected = selectedRunIds.includes(run.run_id);
                const isExpanded = expandedRunId === run.run_id;

                return (
                  <React.Fragment key={run.run_id}>
                    <tr
                      onClick={() => toggleExpand(run.run_id)}
                      className={`group cursor-pointer transition-colors duration-100 ${
                        isSelected
                          ? 'bg-[#1f242c]'
                          : isExpanded
                          ? 'bg-[#161a22]'
                          : 'hover:bg-[#161a22]'
                      }`}
                    >
                      {/* Checkbox */}
                      <td className="py-2.5 pl-3 pr-2" onClick={(e) => e.stopPropagation()}>
                        <button
                          onClick={(e) => toggleSelect(run.run_id, e)}
                          className="p-1 rounded text-slate-400 hover:text-white cursor-pointer"
                        >
                          {isSelected ? (
                            <CheckSquare className="w-3.5 h-3.5 text-sky-400" />
                          ) : (
                            <Square className="w-3.5 h-3.5" />
                          )}
                        </button>
                      </td>

                      {/* Compound Name */}
                      <td className="py-2.5 px-3">
                        <div className="flex items-center gap-2">
                          <span className="font-medium text-white font-sans">
                            {run.drug_name || 'Unnamed Protocol'}
                          </span>
                          {isExpanded ? (
                            <ChevronUp className="w-3 h-3 text-sky-400" />
                          ) : (
                            <ChevronDown className="w-3 h-3 text-slate-500 opacity-0 group-hover:opacity-100 transition-opacity" />
                          )}
                        </div>
                        <div className="text-[10px] font-mono text-slate-500">
                          {run.report_type || 'dose_evaluation'}
                        </div>
                      </td>

                      {/* Run ID */}
                      <td className="py-2.5 px-3 text-slate-300 font-mono text-[11px]">
                        {run.run_id}
                      </td>

                      {/* Status */}
                      <td className="py-2.5 px-3">
                        <span
                          className={`inline-flex items-center gap-1 px-2 py-0.5 rounded text-[10px] font-mono font-medium ${
                            run.status === 'completed'
                              ? 'status-badge-safe'
                              : run.status === 'failed'
                              ? 'status-badge-danger'
                              : 'status-badge-warning'
                          }`}
                        >
                          <span
                            className={`w-1.5 h-1.5 rounded-full ${
                              run.status === 'completed'
                                ? 'bg-emerald-400'
                                : run.status === 'failed'
                                ? 'bg-rose-400'
                                : 'bg-amber-400 animate-pulse'
                            }`}
                          />
                          {run.status.toUpperCase()}
                        </span>
                      </td>

                      {/* Risk Assessment */}
                      <td className="py-2.5 px-3">
                        <StatusBadge
                          label="Risk"
                          value={run.risk_level ?? run.recommendation ?? 'N/A'}
                          compact
                        />
                      </td>

                      {/* Timestamp */}
                      <td className="py-2.5 px-3 text-slate-400 text-[11px] font-mono">
                        {formatDateTime(run.created_at)}
                      </td>

                      {/* Actions */}
                      <td className="py-2.5 pr-3 pl-3 text-right" onClick={(e) => e.stopPropagation()}>
                        <div className="flex items-center justify-end gap-1">
                          <Link
                            to={`/app/reports/${run.run_id}`}
                            className="p-1 rounded text-slate-400 hover:text-white hover:bg-[#21262d] transition-colors"
                            title="View Full Report"
                          >
                            <Eye className="w-3.5 h-3.5" />
                          </Link>

                          <button
                            onClick={(e) => handleDelete(run.run_id, e)}
                            disabled={deleting}
                            className="p-1 rounded text-slate-400 hover:text-rose-400 hover:bg-[#21262d] transition-colors cursor-pointer"
                            title="Delete Run"
                          >
                            <Trash2 className="w-3.5 h-3.5" />
                          </button>
                        </div>
                      </td>
                    </tr>

                    {/* ── Expandable Details Drawer ── */}
                    {isExpanded && (
                      <tr className="bg-[#0e1117] border-b border-[#21262d]">
                        <td colSpan={7} className="p-4">
                          <div className="grid grid-cols-1 md:grid-cols-3 gap-3">
                            <div className="p-3 rounded border border-[#21262d] bg-[#12151c] space-y-1">
                              <span className="text-[10px] font-sans uppercase tracking-wider text-slate-400">
                                Target & Mechanism
                              </span>
                              <div className="text-xs font-sans text-slate-200">
                                {run.drug_name ? `${run.drug_name} In-Silico Assay` : 'Voltage Clamp Sweep'}
                              </div>
                            </div>

                            <div className="p-3 rounded border border-[#21262d] bg-[#12151c] space-y-1">
                              <span className="text-[10px] font-sans uppercase tracking-wider text-slate-400">
                                Confidence Rating
                              </span>
                              <div className="text-xs font-mono text-emerald-400 font-bold">
                                {run.confidence ? `${run.confidence.toUpperCase()} CONFIDENCE` : 'HIGH CONFIDENCE'}
                              </div>
                            </div>

                            <div className="p-3 rounded border border-[#21262d] bg-[#12151c] flex items-center justify-between">
                              <div>
                                <span className="text-[10px] font-sans uppercase tracking-wider text-slate-400">
                                  Full Telemetry
                                </span>
                                <div className="text-xs text-slate-400">
                                  Open detailed report & charts
                                </div>
                              </div>
                              <Link
                                to={`/app/reports/${run.run_id}`}
                                className="btn-primary text-xs px-3 py-1.5 rounded flex items-center gap-1"
                              >
                                <span>Inspect</span>
                                <ExternalLink className="w-3 h-3" />
                              </Link>
                            </div>
                          </div>
                        </td>
                      </tr>
                    )}
                  </React.Fragment>
                );
              })
            )}
          </tbody>
        </table>
      </div>
    </div>
  );
}
