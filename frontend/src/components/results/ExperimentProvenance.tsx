import React from 'react';
import { Database, Calendar, Clock, Cpu, FileJson, CheckCircle2 } from 'lucide-react';
import type { RunDetailResponse } from '../../types';
import { formatDuration } from '../../lib/format';

interface ExperimentProvenanceProps {
  runDetail: RunDetailResponse;
}

export const ExperimentProvenance: React.FC<ExperimentProvenanceProps> = ({ runDetail }) => {
  const payload = runDetail.input_payload as any;

  return (
    <div className="p-5 rounded-2xl border border-slate-800 bg-slate-900/40 backdrop-blur-md space-y-4">
      <div className="flex items-center gap-2 text-xs font-mono uppercase tracking-widest text-slate-400 font-semibold">
        <Database className="w-4 h-4 text-cyan-400" /> Experiment Provenance & Audit Metadata
      </div>

      <div className="grid grid-cols-1 md:grid-cols-4 gap-4 text-xs font-mono">
        <div className="p-3 rounded-xl bg-slate-950/60 border border-slate-800">
          <span className="text-[10px] text-slate-500 uppercase">Run ID</span>
          <div className="text-cyan-400 font-bold text-sm mt-0.5">{runDetail.run_id}</div>
        </div>

        <div className="p-3 rounded-xl bg-slate-950/60 border border-slate-800">
          <span className="text-[10px] text-slate-500 uppercase">Engine Input Mode</span>
          <div className="text-slate-200 font-medium text-xs mt-0.5">{runDetail.engine_input_mode}</div>
        </div>

        <div className="p-3 rounded-xl bg-slate-950/60 border border-slate-800">
          <span className="text-[10px] text-slate-500 uppercase">Execution Duration</span>
          <div className="text-slate-200 font-medium text-xs mt-0.5">
            {runDetail.duration_seconds ? formatDuration(runDetail.duration_seconds) : '—'}
          </div>
        </div>

        <div className="p-3 rounded-xl bg-slate-950/60 border border-slate-800">
          <span className="text-[10px] text-slate-500 uppercase">Created Timestamp</span>
          <div className="text-slate-200 font-medium text-xs mt-0.5">
            {new Date(runDetail.created_at).toLocaleString()}
          </div>
        </div>
      </div>

      {payload && typeof payload === 'object' && (
        <details className="mt-2 text-xs">
          <summary className="cursor-pointer font-mono text-cyan-400 hover:text-cyan-300 flex items-center gap-1 select-none">
            <FileJson className="w-3.5 h-3.5" /> View Raw Submitted Input JSON Payload
          </summary>
          <pre className="mt-2 p-3 rounded-xl bg-slate-950/90 border border-slate-800 text-[11px] font-mono text-slate-300 overflow-x-auto">
            {JSON.stringify(payload, null, 2)}
          </pre>
        </details>
      )}
    </div>
  );
};
