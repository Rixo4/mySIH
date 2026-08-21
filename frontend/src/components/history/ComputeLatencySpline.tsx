import React, { useMemo } from 'react';
import { Clock } from 'lucide-react';
import { useTheme } from '../../context/ThemeContext';
import type { RunListItem } from '../../types';

interface ComputeLatencySplineProps {
  runs?: RunListItem[];
}

export function ComputeLatencySpline({ runs = [] }: ComputeLatencySplineProps) {
  const { theme } = useTheme();
  const safeRuns = Array.isArray(runs) ? runs : [];

  const { points, avgLatency, maxLatency, pathD } = useMemo(() => {
    const latencies = safeRuns
      .slice(0, 10)
      .reverse()
      .map((r) => r.duration_seconds ?? r.runtime_seconds ?? 1.8);

    const data = latencies.length > 0 ? latencies : [1.4, 2.1, 1.8, 1.2, 2.4, 1.6, 1.9];
    const avg = data.reduce((a, b) => a + b, 0) / data.length;
    const max = Math.max(3.0, ...data);

    const svgPoints = data.map((val, idx) => {
      const x = (idx / (data.length - 1 || 1)) * 180 + 10;
      const normY = val / max;
      const y = 45 - normY * 35;
      return { x, y, val };
    });

    const d =
      svgPoints.length > 1
        ? `M ${svgPoints.map((p) => `${p.x.toFixed(1)} ${p.y.toFixed(1)}`).join(' L ')}`
        : '';

    return {
      points: svgPoints,
      avgLatency: avg.toFixed(2),
      maxLatency: max.toFixed(1),
      pathD: d,
    };
  }, [safeRuns]);

  return (
    <div
      className={`relative flex flex-col justify-between p-3.5 rounded-xl border transition-colors h-full min-h-[148px] ${
        theme === 'bright'
          ? 'bg-white border-slate-200 shadow-sm text-slate-900'
          : 'bg-[#12151c] border-[#21262d] text-slate-100'
      }`}
    >
      {/* Header */}
      <div className="flex items-center justify-between mb-2">
        <div className="flex items-center gap-1.5">
          <Clock className="w-3.5 h-3.5 text-slate-400" />
          <div>
            <h3 className="text-xs font-semibold font-sans uppercase tracking-wider text-slate-200">
              Compute Latency
            </h3>
            <p className="text-[9px] text-slate-500 font-sans">Solver ODE Runtime</p>
          </div>
        </div>

        <span className="text-[10px] font-mono font-medium px-2 py-0.5 rounded bg-slate-800 text-slate-300 border border-slate-700">
          AVG: {avgLatency}s
        </span>
      </div>

      {/* SVG Spline Chart */}
      <div className="relative w-full h-16 flex items-center justify-center bg-[#090b0e] rounded border border-[#1b2028]">
        <svg viewBox="0 0 200 50" className="w-full h-full">
          {/* Grid lines */}
          <line x1="10" y1="15" x2="190" y2="15" stroke="#21262d" strokeWidth="0.5" strokeDasharray="2,2" />
          <line x1="10" y1="35" x2="190" y2="35" stroke="#21262d" strokeWidth="0.5" strokeDasharray="2,2" />

          {/* Line */}
          {pathD && (
            <path
              d={pathD}
              fill="none"
              stroke="#38bdf8"
              strokeWidth="1.5"
            />
          )}

          {/* Points */}
          {points.map((p, i) => (
            <circle
              key={i}
              cx={p.x}
              cy={p.y}
              r="2"
              fill="#ffffff"
            />
          ))}
        </svg>
      </div>

      {/* Footer */}
      <div className="flex items-center justify-between text-[9px] font-mono text-slate-500 pt-1.5 border-t border-[#1b2028]">
        <span>0.0s Base</span>
        <span>{maxLatency}s Peak</span>
      </div>
    </div>
  );
}
