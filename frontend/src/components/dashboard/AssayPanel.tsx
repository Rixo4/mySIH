import React, { useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import {
  ArrowRight,
  ChevronDown,
  ChevronUp,
  Copy,
  FlaskConical,
  HeartPulse,
  Thermometer,
} from 'lucide-react';
import { getRiskTone } from '../../lib/format';
import type { RunListItem } from '../../types';

interface AssayPanelProps {
  latestRun?: RunListItem;
  loading?: boolean;
  onReceptorHover?: (receptor: string | null) => void;
}

function StatusPill({ status }: { status: string }) {
  const s = (status ?? '').toUpperCase();
  const ok  = s === 'COMPLETED' || s === 'SUCCESS';
  const run = s === 'RUNNING'   || s === 'QUEUED';
  const err = s === 'FAILED';
  return (
    <span
      className={`inline-flex items-center gap-1.5 px-2 py-0.5 rounded-full text-[8.5px] font-mono font-bold uppercase tracking-wider ${
        ok  ? 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/40' :
        run ? 'bg-amber-500/20 text-amber-300 border border-amber-500/40'     :
        err ? 'bg-rose-500/20 text-rose-300 border border-rose-500/40'        :
              'bg-slate-800/60 text-slate-400 border border-slate-700/60'
      }`}
    >
      <span className={`w-1.5 h-1.5 rounded-full ${ok ? 'bg-emerald-400' : run ? 'bg-amber-400 alert-dot' : err ? 'bg-rose-400' : 'bg-slate-500'}`} />
      {status}
    </span>
  );
}

function RiskPill({ value, tone }: { value: string; tone: string }) {
  return (
    <span
      className={`inline-flex items-center px-2 py-0.5 rounded-full text-[8.5px] font-mono font-bold uppercase tracking-wider ${
        tone === 'safe'    ? 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/40' :
        tone === 'warning' ? 'bg-amber-500/20 text-amber-300 border border-amber-500/40'     :
        tone === 'danger'  ? 'bg-rose-500/20 text-rose-300 border border-rose-500/40'        :
                             'bg-slate-800/60 text-slate-400 border border-slate-700/60'
      }`}
    >
      {value.slice(0, 14)}
    </span>
  );
}

export function AssayPanel({ latestRun, loading, onReceptorHover }: AssayPanelProps) {
  const navigate = useNavigate();
  const [showReceptors, setShowReceptors] = useState(true);

  const riskTone = getRiskTone(latestRun?.risk_level ?? latestRun?.recommendation);

  const RECEPTORS = [
    { label: 'AMPA',   sub: 'GluA1/A2', r: 99,  g: 102, b: 241, val: '88%' },
    { label: 'NMDA',   sub: 'GluN1/N2', r: 139, g: 92,  b: 246, val: '76%' },
    { label: 'GABA-A', sub: 'α1β2γ2',   r: 52,  g: 211, b: 153, val: '92%' },
    { label: 'GABA-B', sub: 'Gi/o-Coupled', r: 20, g: 184, b: 166, val: '64%' },
  ];

  return (
    <div className="relative flex flex-col max-h-full min-h-0 flex-1 observatory-panel neural-glow rounded-2xl overflow-hidden border border-cyan-500/30 text-slate-100">
      {/* Genomex Corner Reticles */}
      <div className="absolute top-0 left-0 w-2.5 h-2.5 border-t-2 border-l-2 border-cyan-400/80 z-20 pointer-events-none" />
      <div className="absolute top-0 right-0 w-2.5 h-2.5 border-t-2 border-r-2 border-cyan-400/80 z-20 pointer-events-none" />

      {/* ── Latest Assay Header & Hero Compound ── */}
      <div className="px-4 pt-3.5 pb-3 border-b border-white/[0.08] bg-slate-950/40">
        <div className="flex items-center justify-between pr-8 mb-2">
          <span className="text-[9px] font-mono font-bold tracking-[0.25em] uppercase text-cyan-400">
            Latest Assay
          </span>
          <span className="px-2 py-0.5 rounded text-[8px] font-mono font-bold bg-cyan-500/15 text-cyan-300 border border-cyan-500/30">
            ACTIVE EVALUATION
          </span>
        </div>

        {loading ? (
          <p className="text-xs font-mono text-slate-400 py-2">Fetching experiment details…</p>
        ) : latestRun ? (
          <div className="space-y-2">
            {/* Hero Compound Name */}
            <div>
              <div className="text-[8px] font-mono text-slate-400 uppercase tracking-widest">Target Compound</div>
              <div className="text-base font-bold text-white truncate leading-tight mt-0.5 tracking-tight font-sans">
                {latestRun.drug_name ?? 'Unnamed Compound'}
              </div>
            </div>

            {/* Run ID */}
            <div>
              <div className="text-[8px] font-mono text-slate-400 uppercase tracking-widest">Run ID</div>
              <div className="text-[9px] font-mono text-cyan-300/90 font-medium truncate">{latestRun.run_id}</div>
            </div>

            {/* Status + Risk Pills */}
            <div className="flex flex-wrap gap-1.5">
              <StatusPill status={latestRun.status} />
              {(latestRun.risk_level ?? latestRun.recommendation) && (
                <RiskPill value={latestRun.risk_level ?? latestRun.recommendation ?? ''} tone={riskTone} />
              )}
            </div>

            {/* Biophysical Telemetry Card */}
            <div className="grid grid-cols-2 gap-1.5 p-2 rounded-xl bg-slate-900/60 border border-cyan-500/20 backdrop-blur-sm">
              <div className="flex items-center gap-1.5">
                <HeartPulse className="w-3.5 h-3.5 text-rose-400" />
                <div>
                  <div className="text-[7.5px] font-mono text-slate-400 uppercase">Spike Rate</div>
                  <div className="text-xs font-mono font-bold text-white">89 bpm</div>
                </div>
              </div>
              <div className="flex items-center gap-1.5">
                <Thermometer className="w-3.5 h-3.5 text-cyan-400" />
                <div>
                  <div className="text-[7.5px] font-mono text-slate-400 uppercase">Temp / Sol</div>
                  <div className="text-xs font-mono font-bold text-white">34.1 °C</div>
                </div>
              </div>
            </div>

            {/* Action Buttons */}
            <div className="flex gap-2 pt-1">
              <button
                onClick={() => navigate(`/app/reports/${latestRun.run_id}`)}
                className="flex-1 flex items-center justify-center gap-1.5 py-1.5 rounded-xl bg-indigo-600 hover:bg-indigo-500 text-white text-[11px] font-semibold transition-all cursor-pointer shadow-md shadow-indigo-900/30 border border-indigo-400/30"
              >
                <span>View Report</span>
                <ArrowRight className="w-3 h-3" />
              </button>
              <button
                onClick={() => navigate(`/app/dose-eval?duplicateRunId=${latestRun.run_id}`)}
                className="flex-1 flex items-center justify-center gap-1.5 py-1.5 rounded-xl bg-slate-900/60 hover:bg-slate-800 text-slate-200 text-[11px] font-semibold border border-white/10 transition-all cursor-pointer backdrop-blur-sm"
              >
                <Copy className="w-3 h-3" />
                <span>Duplicate</span>
              </button>
            </div>
          </div>
        ) : (
          <div className="py-2">
            <p className="text-xs font-mono text-slate-400 mb-2">No experiments recorded yet.</p>
            <Link
              to="/app/dose-eval"
              className="inline-flex items-center gap-1.5 text-xs font-mono text-cyan-400 hover:text-cyan-300 font-bold transition-colors"
            >
              <span>Start first evaluation</span>
              <ArrowRight className="w-3.5 h-3.5" />
            </Link>
          </div>
        )}
      </div>

      {/* ── Receptors (Scientific Model Targets + Hover Highlight Wiring) ── */}
      <div className="flex-1 min-h-0 overflow-y-auto custom-scrollbar p-3">
        <div className="mb-2">
          <button
            onClick={() => setShowReceptors(!showReceptors)}
            className="w-full flex items-center justify-between px-1 mb-1.5 cursor-pointer text-left"
          >
            <div className="flex items-center gap-1.5">
              <span className="text-[8.5px] font-mono font-bold tracking-[0.2em] uppercase text-slate-400">
                Receptor Analytics
              </span>
              <span className="text-[7.5px] font-mono text-amber-400 font-bold uppercase">MODEL VIEW</span>
            </div>
            {showReceptors ? <ChevronUp className="w-3 h-3 text-slate-400" /> : <ChevronDown className="w-3 h-3 text-slate-400" />}
          </button>

          {showReceptors && (
            <div className="grid grid-cols-2 gap-1.5 mt-2">
              {RECEPTORS.map(({ label, sub, r, g, b, val }) => (
                <div
                  key={label}
                  onMouseEnter={() => onReceptorHover && onReceptorHover(label)}
                  onMouseLeave={() => onReceptorHover && onReceptorHover(null)}
                  className="p-2 rounded-xl transition-all border cursor-pointer hover:scale-[1.02] backdrop-blur-sm"
                  style={{
                    background: `rgba(${r},${g},${b},0.12)`,
                    borderColor: `rgba(${r},${g},${b},0.35)`,
                  }}
                >
                  <div className="flex items-center justify-between">
                    <span className="text-[11px] font-mono font-bold" style={{ color: `rgb(${r},${g},${b})` }}>
                      {label}
                    </span>
                    <span className="text-[9px] font-mono font-bold text-white">{val}</span>
                  </div>
                  <div className="text-[8px] font-mono text-slate-300 mt-0.5">{sub}</div>
                </div>
              ))}
            </div>
          )}
        </div>
      </div>

      {/* Scientific Footnote */}
      <div className="px-3 py-2 border-t border-white/[0.08] bg-slate-950/40 shrink-0 text-[8.5px] font-mono text-slate-400 flex items-center justify-between">
        <div className="flex items-center gap-1.5">
          <FlaskConical className="w-3 h-3 text-cyan-400" />
          <span>Biophysical Assay Model</span>
        </div>
        <span className="text-cyan-400 font-bold">VERIFIED</span>
      </div>
    </div>
  );
}
