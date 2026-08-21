import React, { useEffect, useRef, useState } from 'react';
import { NeuroCanvas3D, type HelixSegmentData } from './NeuroCanvas3D';
import { ControlRail } from './ControlRail';
import { AssayPanel } from './AssayPanel';
import { ResearchStream } from './ResearchStream';
import type { RunDetailResponse, RunListItem } from '../../types';
import { SlidersHorizontal, Activity, Clock, X, Zap, Pin, PinOff } from 'lucide-react';

interface NeuralCommandCenterProps {
  runs: RunListItem[];
  latestDetails: RunDetailResponse[];
  loading: boolean;
  error: string | null;
  onRetry: () => void;
  stats: {
    total: number;
    completed: number;
    failed: number;
    running: number;
  };
  averageRuntime: number | null;
}

export function NeuralCommandCenter({
  runs,
  loading,
  error,
  onRetry,
  stats,
  averageRuntime,
}: NeuralCommandCenterProps) {
  const containerRef = useRef<HTMLDivElement>(null);

  const [, setChannelHighlight]   = useState<string | null>(null);
  const [, setReceptorHighlight] = useState<string | null>(null);
  const [selected3DNeuron, setSelected3DNeuron] = useState<HelixSegmentData | null>(null);
  const [dimens, setDimens]                     = useState({ w: 1200, h: 700 });

  // Flanking Panel Visibility States (Default OPEN on Desktop for Genomex Richness)
  const [showLeftRail, setShowLeftRail]   = useState(true);
  const [showRightRail, setShowRightRail] = useState(true);
  const [showBottomStream, setShowBottomStream] = useState(false);

  // Pin state for user preference
  const [isPinned, setIsPinned] = useState(true);

  // 1.0s Boot Sequence
  const [initProgress, setInitProgress] = useState(0);
  const [bootText, setBootText]         = useState('BIOPHYSICAL RESEARCH SYSTEM INITIALIZING…');

  useEffect(() => {
    const t1 = setTimeout(() => { setInitProgress(0.4); setBootText('GENOMEX 3D MICROCIRCUIT ONLINE'); }, 300);
    const t2 = setTimeout(() => { setInitProgress(0.8); }, 600);
    const t3 = setTimeout(() => { setInitProgress(1.0); setBootText('RESEARCH CONSOLE READY'); }, 1000);
    return () => { clearTimeout(t1); clearTimeout(t2); clearTimeout(t3); };
  }, []);

  const latestRun           = runs[0];
  const isSimulationRunning = latestRun?.status === 'running' || latestRun?.status === 'queued';

  // ResizeObserver for responsive bounds
  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    function updateDim() {
      const w = el!.clientWidth;
      setDimens({ w, h: el!.clientHeight });
      // Auto-adjust layout on smaller screens
      if (w < 1024) {
        setShowLeftRail(false);
        setShowRightRail(false);
      }
    }
    updateDim();
    const ro = new ResizeObserver(updateDim);
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  // Keyboard Shortcuts: Alt+1 (Left), Alt+2 (Right), Alt+3 (Stream), ESC (Close)
  useEffect(() => {
    function handleKey(e: KeyboardEvent) {
      if (e.key === 'Escape') {
        if (selected3DNeuron) setSelected3DNeuron(null);
        if (!isPinned) {
          setShowLeftRail(false);
          setShowRightRail(false);
          setShowBottomStream(false);
        }
      } else if (e.altKey && e.key === '1') {
        e.preventDefault();
        setShowLeftRail((prev) => !prev);
      } else if (e.altKey && e.key === '2') {
        e.preventDefault();
        setShowRightRail((prev) => !prev);
      } else if (e.altKey && e.key === '3') {
        e.preventDefault();
        setShowBottomStream((prev) => !prev);
      }
    }
    window.addEventListener('keydown', handleKey);
    return () => window.removeEventListener('keydown', handleKey);
  }, [selected3DNeuron, isPinned]);

  return (
    <div
      ref={containerRef}
      className="relative w-full h-[calc(100vh-4.25rem)] min-h-[650px] overflow-hidden observatory-bg select-none"
    >
      {/* ── Layer 1: 100% Full-Viewport WebGL 3D Cortical Stage ── */}
      <div className="absolute inset-0 z-0">
        <NeuroCanvas3D
          isRunning={isSimulationRunning}
          selectedNeuronId={selected3DNeuron?.id || null}
          onSelectNeuron={setSelected3DNeuron}
        />
      </div>

      {/* ── Booting Status Indicator ── */}
      {initProgress < 1.0 && (
        <div className="absolute top-12 left-1/2 -translate-x-1/2 z-30 pointer-events-none">
          <div className="px-4 py-1.5 rounded-full bg-slate-950/90 border border-cyan-500/40 text-[10px] font-mono font-bold text-cyan-200 uppercase tracking-widest backdrop-blur-md shadow-2xl flex items-center gap-2">
            <div className="w-2 h-2 rounded-full bg-cyan-400 alert-dot" />
            <span>{bootText}</span>
          </div>
        </div>
      )}

      {/* ── Layer 2: 3D Selected Neuron Scientific Inspection Panel ── */}
      {selected3DNeuron && (
        <div className="absolute right-96 top-16 z-30 w-72 backdrop-blur-md bg-slate-950/75 border border-cyan-500/40 rounded-2xl p-4 shadow-2xl animate-fadeIn">
          <div className="flex items-center justify-between pb-2 border-b border-white/10 mb-3">
            <div className="flex items-center gap-2">
              <Zap className="w-4 h-4 text-cyan-400" />
              <span className="font-mono text-xs font-bold text-cyan-300 uppercase tracking-wider">
                {selected3DNeuron.id} INSPECTION
              </span>
            </div>
            <button
              onClick={() => setSelected3DNeuron(null)}
              className="p-1 rounded bg-slate-900/60 border border-white/10 text-slate-400 hover:text-white cursor-pointer"
            >
              <X className="w-3.5 h-3.5" />
            </button>
          </div>
          <div className="space-y-2 text-xs font-mono">
            <div className="flex justify-between">
              <span className="text-slate-400">Neuron Name:</span>
              <span className="text-white font-bold">{selected3DNeuron.name}</span>
            </div>
            <div className="flex justify-between">
              <span className="text-slate-400">Type:</span>
              <span className="text-cyan-300 font-bold">{selected3DNeuron.type}</span>
            </div>
            <div className="flex justify-between">
              <span className="text-slate-400">Cortical Layer:</span>
              <span className="text-white font-bold">{selected3DNeuron.layer}</span>
            </div>
            <div className="flex justify-between">
              <span className="text-slate-400">Depolarization:</span>
              <span className="text-emerald-400 font-bold">{selected3DNeuron.depolarization} mV</span>
            </div>
            <div className="flex justify-between">
              <span className="text-slate-400">Status Tag:</span>
              <span className="text-indigo-300 font-bold">{selected3DNeuron.statusTag}</span>
            </div>
          </div>
          <div className="mt-3 pt-2 border-t border-white/10 flex items-center justify-between text-[9px] font-mono text-slate-400 uppercase">
            <span>MODEL VIEW STATUS</span>
            <span className="px-1.5 py-0.5 rounded bg-emerald-500/20 text-emerald-300 font-bold">ACTIVE CASCADE</span>
          </div>
        </div>
      )}

      {/* ── Layer 3: TOP INSTRUMENT CONTROL STRIP (Toggle Flanking Panels) ── */}
      <div className="absolute top-4 left-6 right-6 z-25 flex items-center justify-between pointer-events-none">
        <div className="flex items-center gap-2 pointer-events-auto">
          <button
            onClick={() => setShowLeftRail((prev) => !prev)}
            className={`flex items-center gap-1.5 px-3.5 py-1.5 rounded-full text-[11px] font-mono font-bold border transition-all cursor-pointer shadow-lg backdrop-blur-2xl ${
              showLeftRail
                ? 'bg-violet-600/30 border-violet-400/50 text-violet-200 shadow-[0_0_15px_rgba(139,92,246,0.3)]'
                : 'bg-obsidian-950/60 border-white/10 text-slate-400 hover:text-white'
            }`}
            title="Toggle Research Control [Alt+1]"
          >
            <SlidersHorizontal className="w-3.5 h-3.5 text-violet-400" />
            <span>RESEARCH CONTROL</span>
          </button>

          <button
            onClick={() => setShowRightRail((prev) => !prev)}
            className={`flex items-center gap-1.5 px-3.5 py-1.5 rounded-full text-[11px] font-mono font-bold border transition-all cursor-pointer shadow-lg backdrop-blur-2xl ${
              showRightRail
                ? 'bg-cyan-600/30 border-cyan-400/50 text-cyan-200 shadow-[0_0_15px_rgba(6,182,212,0.3)]'
                : 'bg-obsidian-950/60 border-white/10 text-slate-400 hover:text-white'
            }`}
            title="Toggle Latest Assay [Alt+2]"
          >
            <Activity className="w-3.5 h-3.5 text-cyan-400" />
            <span>ASSAY ANALYTICS</span>
          </button>

          <button
            onClick={() => setShowBottomStream((prev) => !prev)}
            className={`flex items-center gap-1.5 px-3.5 py-1.5 rounded-full text-[11px] font-mono font-bold border transition-all cursor-pointer shadow-lg backdrop-blur-2xl ${
              showBottomStream
                ? 'bg-emerald-600/30 border-emerald-400/50 text-emerald-200 shadow-[0_0_15px_rgba(16,185,129,0.3)]'
                : 'bg-obsidian-950/60 border-white/10 text-slate-400 hover:text-white'
            }`}
            title="Toggle Activity Ledger [Alt+3]"
          >
            <Clock className="w-3.5 h-3.5 text-emerald-400" />
            <span>LEDGER</span>
            {runs.length > 0 && (
              <span className="px-1.5 py-0.2 rounded-full text-[8.5px] bg-emerald-500/20 text-emerald-300 font-bold">
                {runs.length}
              </span>
            )}
          </button>
        </div>

        {/* Pin Layout Toggle */}
        <div className="pointer-events-auto">
          <button
            onClick={() => setIsPinned((prev) => !prev)}
            className={`flex items-center gap-1.5 px-3 py-1.5 rounded-full text-[10px] font-mono font-bold border transition-all cursor-pointer backdrop-blur-2xl ${
              isPinned
                ? 'bg-obsidian-900/80 border-violet-500/40 text-violet-300'
                : 'bg-obsidian-950/60 border-white/10 text-slate-400'
            }`}
            title="Pin / Unpin Instrument Layout"
          >
            {isPinned ? <Pin className="w-3.5 h-3.5 text-violet-400" /> : <PinOff className="w-3.5 h-3.5 text-slate-400" />}
            <span>{isPinned ? 'LAYOUT PINNED' : 'AUTO-HIDE'}</span>
          </button>
        </div>
      </div>

      {/* ── Layer 4: SCIENTIFIC INSTRUMENT PANELS ── */}
      {/* 1. Left Flanking Panel: Research Control & Specimen Analytics */}
      {showLeftRail && (
        <div className="absolute left-6 top-14 z-30 w-80 max-h-[calc(100vh-140px)] flex flex-col min-h-0 animate-fadeIn transition-all duration-300">
          <div className="relative max-h-full flex-1 min-h-0 backdrop-blur-2xl bg-obsidian-900/90 border border-violet-500/30 shadow-2xl rounded-2xl overflow-hidden flex flex-col">
            <button
              onClick={() => setShowLeftRail(false)}
              className="absolute top-3 right-3 z-50 p-1.5 rounded-full bg-white/10 border border-white/10 text-rose-100 hover:text-white hover:bg-rose-900/80 hover:border-rose-400/50 transition-colors cursor-pointer"
              title="Close Left Instrument"
            >
              <X className="w-3.5 h-3.5" />
            </button>
            <ControlRail
              totalRuns={stats.total}
              completedRuns={stats.completed}
              failedRuns={stats.failed}
              averageRuntime={averageRuntime}
              loading={loading}
              onChannelHover={setChannelHighlight}
            />
          </div>
        </div>
      )}

      {/* 2. Right Flanking Panel: Latest Assay & Receptor Analytics */}
      {showRightRail && (
        <div className="absolute right-6 top-14 z-30 w-80 max-h-[calc(100vh-140px)] flex flex-col min-h-0 animate-fadeIn transition-all duration-300">
          <div className="relative max-h-full flex-1 min-h-0 backdrop-blur-2xl bg-obsidian-900/90 border border-cyan-500/30 shadow-2xl rounded-2xl overflow-hidden flex flex-col">
            <button
              onClick={() => setShowRightRail(false)}
              className="absolute top-3 right-3 z-50 p-1.5 rounded-full bg-white/10 border border-white/10 text-rose-100 hover:text-white hover:bg-rose-900/80 hover:border-rose-400/50 transition-colors cursor-pointer"
              title="Close Right Instrument"
            >
              <X className="w-3.5 h-3.5" />
            </button>
            <AssayPanel
              latestRun={latestRun}
              loading={loading}
              onReceptorHover={setReceptorHighlight}
            />
          </div>
        </div>
      )}

      {/* 3. Bottom Horizontal Stream Panel: Activity Ledger */}
      {showBottomStream && (
        <div className="absolute left-6 right-6 bottom-6 z-40 animate-fadeIn transition-all duration-300">
          <div className="relative backdrop-blur-2xl bg-obsidian-900/85 border border-emerald-500/35 shadow-2xl rounded-3xl overflow-hidden">
            <button
              onClick={() => setShowBottomStream(false)}
              className="absolute top-3 right-3 z-50 p-1.5 rounded-full bg-white/10 border border-white/10 text-rose-100 hover:text-white hover:bg-rose-900/80 hover:border-rose-400/50 transition-colors cursor-pointer"
              title="Close Stream"
            >
              <X className="w-3.5 h-3.5" />
            </button>
            <ResearchStream runs={runs} loading={loading} />
          </div>
        </div>
      )}

      {/* Connection Notice Bar */}
      {error && (
        <div
          className="absolute top-16 left-1/2 -translate-x-1/2 z-50 flex items-center gap-3 px-4 py-2 rounded-xl text-xs font-mono backdrop-blur-xl shadow-2xl"
          style={{
            background: 'rgba(190, 18, 60, 0.25)',
            border:     '1px solid rgba(244, 63, 94, 0.45)',
            color:      'rgb(253, 164, 175)',
          }}
        >
          <span>⚠ Connection Notice: {error}</span>
          <button
            onClick={onRetry}
            className="text-rose-300 hover:text-white underline cursor-pointer text-[10px] uppercase font-bold"
          >
            Retry Sync
          </button>
        </div>
      )}
    </div>
  );
}
