import React, { useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { ArrowRight, Beaker, ChevronDown, ChevronUp, Clock, Copy, FileText } from 'lucide-react';
import { formatDuration, getRiskTone } from '../../lib/format';
import type { RunListItem } from '../../types';

interface ResearchStreamProps {
  runs: RunListItem[];
  loading: boolean;
}

export function ResearchStream({ runs, loading }: ResearchStreamProps) {
  const navigate = useNavigate();
  const [isExpanded, setIsExpanded] = useState(true);

  return (
    <div className="observatory-panel rounded-2xl overflow-hidden border border-emerald-500/30 shadow-2xl transition-all duration-300">
      {/* ── Header Strip ── */}
      <div className="flex items-center justify-between px-4 py-2.5 border-b border-white/[0.08] bg-slate-950/40">
        <div className="flex items-center gap-3">
          <div className="w-2 h-2 rounded-full bg-emerald-400 alert-dot" />
          <span className="text-[10px] font-mono font-bold tracking-[0.25em] uppercase text-slate-300">
            Live Research Activity Ledger
          </span>
          {runs.length > 0 && (
            <span className="px-2 py-0.5 rounded-full text-[9px] font-mono font-bold bg-emerald-500/20 text-emerald-300 border border-emerald-500/30">
              {runs.length} RECORDS
            </span>
          )}
        </div>

        <div className="flex items-center gap-3">
          <Link
            to="/app/history"
            className="inline-flex items-center gap-1 text-[10px] font-mono font-bold text-cyan-400 hover:text-cyan-300 transition-colors"
          >
            <span>Full Ledger</span>
            <ArrowRight className="w-3 h-3" />
          </Link>
          <button
            onClick={() => setIsExpanded(!isExpanded)}
            className="p-1 rounded-lg text-slate-400 hover:text-white hover:bg-slate-800/60 transition-colors cursor-pointer"
            title={isExpanded ? 'Minimize Stream' : 'Expand Stream'}
          >
            {isExpanded ? <ChevronDown className="w-4 h-4" /> : <ChevronUp className="w-4 h-4" />}
          </button>
        </div>
      </div>

      {/* ── Stream Timeline Body ── */}
      {isExpanded && (
        <>
          {loading ? (
            <div className="px-4 py-4 text-xs font-mono text-slate-400">Loading experimental timeline…</div>
          ) : runs.length === 0 ? (
            <div className="flex items-center gap-4 px-5 py-4">
              <div className="w-8 h-8 rounded-xl bg-indigo-500/10 border border-indigo-500/30 flex items-center justify-center shrink-0">
                <Beaker className="w-4 h-4 text-indigo-400" />
              </div>
              <div>
                <p className="text-xs font-semibold text-white">No drug evaluation runs recorded yet.</p>
                <Link
                  to="/app/dose-eval"
                  className="mt-0.5 inline-flex items-center gap-1 text-[10px] font-mono font-bold text-cyan-400 hover:text-cyan-300 transition-colors"
                >
                  <span>Launch First Evaluation</span>
                  <ArrowRight className="w-3 h-3" />
                </Link>
              </div>
            </div>
          ) : (
            /* High-Density Horizontal Activity Timeline */
            <div className="relative flex overflow-x-auto p-3 gap-3 items-center custom-scrollbar">
              {/* Horizontal timeline connector line */}
              <div
                className="absolute left-6 right-6 top-1/2 -translate-y-1/2 h-0.5 bg-indigo-500/30 pointer-events-none z-0"
              />

              {runs.slice(0, 8).map((run) => {
                const s   = (run.status ?? '').toUpperCase();
                const ok  = s === 'COMPLETED' || s === 'SUCCESS';
                const act = s === 'RUNNING'   || s === 'QUEUED';
                const err = s === 'FAILED';
                const riskTone = getRiskTone(run.risk_level ?? run.recommendation);

                const timestamp = run.created_at
                  ? new Date(run.created_at).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false })
                  : run.run_id.includes('T')
                  ? run.run_id.split('T')[1]?.slice(0, 6).replace(/(\d{2})(\d{2})(\d{2})/, '$1:$2:$3') || '14:03:29'
                  : '14:03:29';

                const dotClass = ok  ? 'bg-emerald-400'
                               : act ? 'bg-amber-400 alert-dot'
                               : err ? 'bg-rose-400'
                                     : 'bg-slate-500';

                const statusCls = ok  ? 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/40'
                                : act ? 'bg-amber-500/20 text-amber-300 border border-amber-500/40'
                                : err ? 'bg-rose-500/20 text-rose-300 border border-rose-500/40'
                                      : 'bg-slate-800/60 text-slate-400 border border-slate-700/60';

                const riskCls = riskTone === 'safe'    ? 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/40'
                              : riskTone === 'warning'  ? 'bg-amber-500/20 text-amber-300 border border-amber-500/40'
                              : riskTone === 'danger'   ? 'bg-rose-500/20 text-rose-300 border border-rose-500/40'
                                                        : 'bg-slate-800/60 text-slate-400 border border-slate-700/60';

                return (
                  <div
                    key={run.run_id}
                    className="relative z-10 flex-none p-3 rounded-xl border border-white/[0.1] bg-slate-950/45 backdrop-blur-md hover:border-emerald-400/50 transition-all shadow-xl group flex flex-col justify-between"
                    style={{ width: '260px' }}
                  >
                    <div>
                      {/* Real Timestamp Header */}
                      <div className="flex items-center justify-between mb-1 pb-1 border-b border-white/[0.06]">
                        <div className="flex items-center gap-1.5 text-[8.5px] font-mono font-bold text-cyan-300">
                          <Clock className="w-3 h-3 text-cyan-400" />
                          <span>{timestamp}</span>
                        </div>
                        {(run.duration_seconds || run.runtime_seconds) && (
                          <span className="text-[8px] font-mono text-slate-300">
                            {formatDuration(run.duration_seconds || run.runtime_seconds || 0)}
                          </span>
                        )}
                      </div>

                      {/* Status dot + Drug Name */}
                      <div className="flex items-center gap-2 mb-1">
                        <div className={`w-2 h-2 rounded-full shrink-0 ${dotClass}`} />
                        <div className="text-[11px] font-mono font-bold text-white truncate">
                          {run.drug_name ?? 'Unnamed Compound'}
                        </div>
                      </div>

                      {/* Run ID */}
                      <div className="text-[8.5px] font-mono text-indigo-300/90 truncate mb-1.5">
                        {run.run_id}
                      </div>

                      {/* Status & Risk Pills */}
                      <div className="flex items-center gap-1 flex-wrap mb-2">
                        <span className={`text-[8px] font-mono font-bold uppercase px-1.5 py-0.2 rounded ${statusCls}`}>
                          {run.status}
                        </span>
                        {(run.risk_level ?? run.recommendation) && (
                          <span className={`text-[8px] font-mono font-bold uppercase px-1.5 py-0.2 rounded ${riskCls}`}>
                            {(run.risk_level ?? run.recommendation ?? '').slice(0, 12)}
                          </span>
                        )}
                      </div>
                    </div>

                    {/* Always-Visible Actions */}
                    <div className="flex gap-1.5 pt-1.5 border-t border-white/[0.06]">
                      <button
                        onClick={() => navigate(`/app/reports/${run.run_id}`)}
                        className="flex-1 flex items-center justify-center gap-1 px-2 py-1 rounded-lg bg-indigo-600/90 hover:bg-indigo-500 text-white text-[9.5px] font-semibold transition-all cursor-pointer shadow-md shadow-indigo-900/40 border border-indigo-400/30"
                      >
                        <FileText className="w-3 h-3" />
                        <span>Report</span>
                      </button>
                      <button
                        onClick={() => navigate(`/app/dose-eval?duplicateRunId=${run.run_id}`)}
                        className="flex-1 flex items-center justify-center gap-1 px-2 py-1 rounded-lg bg-slate-900/50 hover:bg-slate-800/70 text-slate-200 text-[9.5px] font-semibold border border-white/10 transition-all cursor-pointer backdrop-blur-sm"
                      >
                        <Copy className="w-3.5 h-3.5" />
                        <span>Dup</span>
                      </button>
                    </div>
                  </div>
                );
              })}
            </div>
          )}
        </>
      )}
    </div>
  );
}
