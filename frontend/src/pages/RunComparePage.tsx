import React, { useEffect, useState } from 'react';
import { useSearchParams, Link, useNavigate } from 'react-router-dom';
import { ArrowLeft, GitCompare, FileText, CheckCircle2, ShieldAlert, Activity } from 'lucide-react';
import { getRunDetail } from '../api/client';
import type { RunDetailResponse } from '../types';
import { formatDuration } from '../lib/format';

export function RunComparePage() {
  const [searchParams] = useSearchParams();
  const navigate = useNavigate();

  const runIdsParam = searchParams.get('runIds') || '';
  const runIds = runIdsParam.split(',').filter(Boolean);

  const [details, setDetails] = useState<RunDetailResponse[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (runIds.length === 0) {
      setLoading(false);
      return;
    }

    setLoading(true);
    setError(null);
    Promise.all(runIds.map((id) => getRunDetail(id)))
      .then((data) => setDetails(data))
      .catch((err) => setError(err instanceof Error ? err.message : 'Failed to fetch runs'))
      .finally(() => setLoading(false));
  }, [runIdsParam]);

  return (
    <div className="max-w-7xl mx-auto p-4 sm:p-6 space-y-6 pb-28">
      {/* Top Bar */}
      <div className="flex items-center justify-between">
        <Link
          to="/app/history"
          className="inline-flex items-center gap-2 px-4 py-2 rounded-xl border border-slate-800 bg-slate-900 text-xs font-semibold text-slate-300 hover:text-white hover:bg-slate-800 transition-all"
        >
          <ArrowLeft className="w-4 h-4" /> Back to History
        </Link>

        <div className="flex items-center gap-2 text-xs font-mono text-cyan-400">
          <GitCompare className="w-4 h-4" /> Comparing {runIds.length} Experiment Runs
        </div>
      </div>

      {loading && (
        <div className="p-8 text-center text-xs text-slate-400 border border-slate-800 rounded-2xl bg-slate-900/50">
          Loading comparison telemetry...
        </div>
      )}

      {error && (
        <div className="p-4 rounded-xl border border-rose-800/40 bg-rose-950/40 text-xs text-rose-300">
          {error}
        </div>
      )}

      {runIds.length < 2 && !loading && (
        <div className="p-8 text-center border border-dashed border-slate-800 rounded-2xl space-y-3">
          <GitCompare className="w-8 h-8 text-slate-600 mx-auto" />
          <p className="text-xs text-slate-400">
            Please select at least 2 runs from the Run History table to perform a side-by-side comparison.
          </p>
          <Link
            to="/app/history"
            className="inline-block px-4 py-2 rounded-xl bg-cyan-500 text-slate-950 font-bold text-xs"
          >
            Select Runs from History
          </Link>
        </div>
      )}

      {details.length >= 2 && (
        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
          {details.map((run) => (
            <div
              key={run.run_id}
              className="p-6 rounded-2xl border border-slate-800 bg-slate-900/60 backdrop-blur-xl shadow-xl space-y-5 flex flex-col justify-between"
            >
              <div className="space-y-4">
                <div className="flex items-center justify-between">
                  <span className="text-xs font-mono font-bold text-cyan-400">{run.run_id}</span>
                  <span className="px-2.5 py-1 rounded text-[10px] font-bold bg-slate-800 border border-slate-700 text-slate-200">
                    {run.risk_level || 'EVALUATED'}
                  </span>
                </div>

                <div>
                  <h3 className="text-lg font-bold text-white">{run.drug_name || 'Unnamed Compound'}</h3>
                  <p className="text-xs text-slate-400 mt-0.5 font-mono">
                    Report Type: {run.report_type}
                  </p>
                </div>

                <div className="space-y-2 border-t border-b border-slate-800 py-3 text-xs font-mono">
                  <div className="flex justify-between">
                    <span className="text-slate-500">Recommendation:</span>
                    <span className="text-slate-200 font-bold">{run.recommendation || 'CAUTION'}</span>
                  </div>
                  <div className="flex justify-between">
                    <span className="text-slate-500">Confidence:</span>
                    <span className="text-emerald-400 font-bold">{run.confidence || 'High'}</span>
                  </div>
                  <div className="flex justify-between">
                    <span className="text-slate-500">Duration:</span>
                    <span className="text-slate-300">{formatDuration(run.duration_seconds)}</span>
                  </div>
                  <div className="flex justify-between">
                    <span className="text-slate-500">Toxic Threshold:</span>
                    <span className="text-amber-300">
                      {(run.parsed_summary?.toxic_threshold as string) || 'None'}
                    </span>
                  </div>
                </div>
              </div>

              <div className="pt-2 flex items-center justify-between">
                <button
                  onClick={() => navigate(`/app/reports/${run.run_id}`)}
                  className="w-full flex items-center justify-center gap-2 px-3 py-2 rounded-xl bg-slate-800 hover:bg-slate-700 text-xs font-semibold text-slate-200 transition-colors"
                >
                  <FileText className="w-3.5 h-3.5" /> Inspect Full Run
                </button>
              </div>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
