import React, { useState } from 'react';
import { Link } from 'react-router-dom';
import {
  Beaker,
  ChevronDown,
  ChevronUp,
  GitCompare,
  History,
  Activity,
} from 'lucide-react';
import { formatDuration } from '../../lib/format';

interface ControlRailProps {
  totalRuns: number;
  completedRuns: number;
  failedRuns: number;
  averageRuntime: number | null;
  loading: boolean;
  onChannelHover?: (channel: string | null) => void;
  isMinimized?: boolean;
  onToggleMinimize?: () => void;
}

const NAV_ACTIONS = [
  { to: '/app/history', label: 'Run History',  icon: History },
  { to: '/app/compare', label: 'Compare Runs', icon: GitCompare },
];

const ION_CHANNELS = [
  { id: 'Na', label: 'Na⁺',  sub: 'Voltage-Gated',    val: '72%', r: 129, g: 140, b: 248 },
  { id: 'K',  label: 'K⁺',   sub: 'Inward Rectifier', val: '58%', r: 167, g: 139, b: 250 },
  { id: 'Ca', label: 'Ca²⁺', sub: 'L-Type Channel',   val: '34%', r: 34,  g: 211, b: 238 },
];

const SPECIMEN_TAGS = [
  { label: 'Excitatory' },
  { label: 'High Plasticity' },
  { label: 'L5 Pyramidal' },
];

export function ControlRail({
  totalRuns,
  completedRuns,
  failedRuns,
  averageRuntime,
  loading,
  onChannelHover,
}: ControlRailProps) {
  const [showChannels, setShowChannels] = useState(true);
  const [showMetrics, setShowMetrics]   = useState(true);

  return (
    <div className="relative flex flex-col max-h-full min-h-0 flex-1 observatory-panel neural-glow rounded-2xl overflow-hidden border border-indigo-500/30 text-slate-100">
      {/* Genomex Corner Reticles */}
      <div className="absolute top-0 left-0 w-2.5 h-2.5 border-t-2 border-l-2 border-indigo-400/80 z-20 pointer-events-none" />
      <div className="absolute top-0 right-0 w-2.5 h-2.5 border-t-2 border-r-2 border-indigo-400/80 z-20 pointer-events-none" />

      {/* ── Header ── */}
      <div className="px-4 pt-3.5 pb-2.5 border-b border-white/[0.08] bg-slate-950/40">
        <div className="flex items-center justify-between pr-8 mb-1.5">
          <div className="text-[9px] font-mono font-bold tracking-[0.25em] uppercase text-indigo-400">
            Research Control
          </div>
          <span className="px-1.5 py-0.5 rounded text-[7.5px] font-mono font-bold bg-amber-500/20 text-amber-300 border border-amber-500/30">
            RISK LVL LOW
          </span>
        </div>
        <div className="flex items-center justify-between pr-8">
          <span className="text-sm font-bold text-white tracking-tight">SPECIMEN VX-023</span>
          <div className="flex items-center gap-1.5 px-2 py-0.5 rounded-full bg-emerald-500/15 border border-emerald-500/30">
            <div className="w-1.5 h-1.5 rounded-full bg-emerald-400 alert-dot" />
            <span className="text-[8.5px] font-mono text-emerald-300 font-bold">ACTIVE SYNC</span>
          </div>
        </div>
      </div>

      <div className="flex-1 min-h-0 overflow-y-auto custom-scrollbar">
        {/* ── Specimen Tags (Genomex Style) ── */}
        <div className="px-3 pt-3 pb-2 border-b border-white/[0.08]">
          <div className="flex flex-wrap gap-1.5 mb-2">
            {SPECIMEN_TAGS.map((tag) => (
              <span
                key={tag.label}
                className="px-2 py-0.5 rounded-md text-[8.5px] font-mono font-bold bg-slate-900/60 text-slate-300 border border-white/10 uppercase"
              >
                {tag.label}
              </span>
            ))}
          </div>

          <Link
            to="/app/dose-eval"
            className="flex items-center justify-center gap-2 px-3 py-2.5 rounded-xl text-xs font-mono font-bold text-white bg-indigo-600/90 hover:bg-indigo-500 shadow-lg shadow-indigo-900/40 transition-all duration-200 cursor-pointer border border-cyan-400/40 hover:scale-[1.02]"
          >
            <Beaker className="w-4 h-4 text-cyan-300 shrink-0" />
            <span className="tracking-wider uppercase text-[11px]">INITIATE EVALUATION</span>
          </Link>
        </div>

        {/* ── Genomex Wireframe Diagram Card ── */}
        <div className="p-3 border-b border-white/[0.08] bg-slate-950/30">
          <div className="flex items-center justify-between mb-2">
            <span className="text-[8.5px] font-mono font-bold tracking-[0.2em] uppercase text-slate-400">
              Microcircuit Wireframe
            </span>
            <Activity className="w-3.5 h-3.5 text-cyan-400" />
          </div>
          <div className="relative h-28 rounded-xl border border-indigo-500/25 bg-slate-950/60 flex items-center justify-center overflow-hidden p-2">
            {/* SVG Wireframe Diagram */}
            <svg className="w-full h-full text-indigo-400/70" viewBox="0 0 200 100" fill="none">
              <circle cx="100" cy="50" r="35" stroke="currentColor" strokeWidth="1" strokeDasharray="3 3" />
              <polygon points="100,20 130,70 70,70" stroke="#06b6d4" strokeWidth="1.5" fill="none" />
              <circle cx="100" cy="20" r="4" fill="#6366f1" />
              <circle cx="130" cy="70" r="4" fill="#10b981" />
              <circle cx="70" cy="70" r="4" fill="#818cf8" />
              <line x1="100" y1="20" x2="100" y2="85" stroke="#34d399" strokeWidth="1" strokeDasharray="2 2" />
              <line x1="40" y1="50" x2="160" y2="50" stroke="rgba(99, 102, 241, 0.3)" strokeWidth="1" />
            </svg>
            <div className="absolute bottom-1 right-2 text-[7.5px] font-mono text-cyan-300 font-bold">
              PYRAMIDAL CORTEX SOMA
            </div>
          </div>
        </div>

        {/* ── Navigation Actions ── */}
        <div className="px-3 py-2.5 border-b border-white/[0.08] space-y-1">
          {NAV_ACTIONS.map(({ to, label, icon: Icon }) => (
            <Link
              key={to}
              to={to}
              className="group flex items-center gap-2.5 px-2.5 py-1.5 rounded-xl text-[11px] font-semibold transition-all duration-200 bg-slate-900/40 hover:bg-slate-800/60 text-slate-300 hover:text-white border border-white/[0.08] hover:border-indigo-500/40"
            >
              <Icon className="w-3.5 h-3.5 text-slate-400 group-hover:text-indigo-300 shrink-0 transition-colors" />
              <span className="truncate">{label}</span>
              {to === '/app/history' && totalRuns > 0 && (
                <span className="ml-auto text-[9.5px] font-mono font-bold tabular-nums px-1.5 py-0.5 rounded bg-slate-800/80 text-slate-300">
                  {totalRuns}
                </span>
              )}
            </Link>
          ))}
        </div>

        {/* ── Network Metrics (Collapsible Conceptual Model) ── */}
        <div className="px-3 pt-2.5 pb-2.5 border-b border-white/[0.08] bg-slate-950/20">
          <button
            onClick={() => setShowMetrics(!showMetrics)}
            className="w-full flex items-center justify-between px-1 mb-1.5 cursor-pointer text-left"
          >
            <div className="flex items-center gap-1.5">
              <span className="text-[8.5px] font-mono font-bold tracking-[0.2em] uppercase text-slate-400">
                Network Metrics
              </span>
              <span className="px-1 py-0.2 rounded text-[7.5px] font-mono font-bold bg-indigo-500/20 text-indigo-300 border border-indigo-500/30">
                MODEL
              </span>
            </div>
            {showMetrics ? <ChevronUp className="w-3 h-3 text-slate-400" /> : <ChevronDown className="w-3 h-3 text-slate-400" />}
          </button>

          {showMetrics && (
            <div className="grid grid-cols-2 gap-1.5 mt-2">
              <div className="p-2 rounded-xl bg-slate-900/40 border border-indigo-500/30 backdrop-blur-sm">
                <div className="text-[8px] font-mono text-slate-400 uppercase tracking-wider">E/I BALANCE</div>
                <div className="text-base font-mono font-bold text-white mt-0.5">
                  0.72 <span className="text-[8px] font-mono text-amber-400 font-normal">MODEL</span>
                </div>
                <div className="text-[7.5px] font-mono text-emerald-400 mt-0.5 font-bold">● STABLE</div>
              </div>
              <div className="p-2 rounded-xl bg-slate-900/40 border border-indigo-500/30 backdrop-blur-sm">
                <div className="text-[8px] font-mono text-slate-400 uppercase tracking-wider">STABILITY</div>
                <div className="text-base font-mono font-bold text-white mt-0.5">
                  0.69 <span className="text-[8px] font-mono text-amber-400 font-normal">MODEL</span>
                </div>
                <div className="text-[7.5px] font-mono text-cyan-400 mt-0.5 font-bold">● MONITORED</div>
              </div>
            </div>
          )}
        </div>

        {/* ── Ion Channels (Collapsible Conceptual Model) ── */}
        <div className="px-3 pt-2.5 pb-2.5 border-b border-white/[0.08]">
          <button
            onClick={() => setShowChannels(!showChannels)}
            className="w-full flex items-center justify-between px-1 mb-1.5 cursor-pointer text-left"
          >
            <div className="flex items-center gap-1.5">
              <span className="text-[8.5px] font-mono font-bold tracking-[0.2em] uppercase text-slate-400">
                Ion Channels
              </span>
              <span className="text-[7.5px] font-mono text-amber-400 font-bold uppercase">MODEL VIEW</span>
            </div>
            {showChannels ? <ChevronUp className="w-3 h-3 text-slate-400" /> : <ChevronDown className="w-3 h-3 text-slate-400" />}
          </button>

          {showChannels && (
            <div className="space-y-1.5 mt-2">
              {ION_CHANNELS.map(({ id, label, sub, val, r, g, b }) => (
                <div
                  key={label}
                  onMouseEnter={() => onChannelHover && onChannelHover(id)}
                  onMouseLeave={() => onChannelHover && onChannelHover(null)}
                  className="px-2.5 py-1.5 rounded-xl transition-all cursor-pointer hover:scale-[1.02] backdrop-blur-sm"
                  style={{
                    background: `rgba(${r},${g},${b},0.12)`,
                    border:     `1px solid rgba(${r},${g},${b},0.35)`,
                  }}
                >
                  <div className="flex items-center justify-between mb-1">
                    <div className="flex items-center gap-1.5">
                      <span className="text-[11px] font-mono font-bold" style={{ color: `rgb(${r},${g},${b})` }}>
                        {label}
                      </span>
                      <span className="text-[8.5px] font-mono text-slate-300">{sub}</span>
                    </div>
                    <span className="text-[9.5px] font-mono font-bold text-white">{val}</span>
                  </div>
                  <div className="w-full h-1 bg-slate-950/60 rounded-full overflow-hidden">
                    <div
                      className="h-full rounded-full transition-all duration-500"
                      style={{ width: val, backgroundColor: `rgb(${r},${g},${b})` }}
                    />
                  </div>
                </div>
              ))}
            </div>
          )}
        </div>

        {/* ── Run Metrics (Real Backend Data) ── */}
        <div className="px-3 pt-2.5 pb-2">
          <div className="px-1 mb-1.5 text-[8.5px] font-mono font-bold tracking-[0.2em] uppercase text-slate-400">
            Run Metrics
          </div>
          <div className="grid grid-cols-2 gap-1.5">
            {[
              { label: 'TOTAL',    value: loading ? '…' : String(totalRuns),                             sub: 'experiments', color: 'text-white' },
              { label: 'COMPLETE', value: loading ? '…' : String(completedRuns),                         sub: 'verified',    color: completedRuns > 0 ? 'text-emerald-400' : 'text-slate-400' },
              { label: 'FAILED',   value: loading ? '…' : String(failedRuns),                            sub: 'errors',      color: failedRuns > 0 ? 'text-rose-400' : 'text-slate-400' },
              { label: 'AVG RT',   value: loading ? '…' : (averageRuntime ? formatDuration(averageRuntime) : '—'), sub: 'runtime', color: 'text-white' },
            ].map(({ label, value, sub, color }) => (
              <div key={label} className="rounded-xl p-2 border border-white/[0.08] bg-slate-900/40 backdrop-blur-sm">
                <div className="text-[7.5px] font-mono text-slate-400 uppercase tracking-wider">{label}</div>
                <div className={`text-base font-mono font-bold mt-0.5 ${color}`}>{value}</div>
                <div className="text-[7.5px] font-mono text-slate-500">{sub}</div>
              </div>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}
