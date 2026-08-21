import React from 'react';
import { Flame } from 'lucide-react';
import { useTheme } from '../../context/ThemeContext';
import type { RunListItem } from '../../types';

interface ReceptorRiskHeatmapProps {
  runs?: RunListItem[];
}

export function ReceptorRiskHeatmap({ runs: _runs }: ReceptorRiskHeatmapProps) {
  const { theme } = useTheme();

  const matrix = [
    { channel: 'Na+', low: 'bg-emerald-500/80', med: 'bg-amber-500/80', high: 'bg-rose-500/80' },
    { channel: 'K+', low: 'bg-emerald-500/80', med: 'bg-amber-500/80', high: 'bg-rose-500/80' },
    { channel: 'Ca2+', low: 'bg-emerald-500/80', med: 'bg-amber-500/80', high: 'bg-rose-500/80' },
    { channel: 'NMDA', low: 'bg-emerald-500/80', med: 'bg-amber-500/80', high: 'bg-rose-500/80' },
  ];

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
          <Flame className="w-3.5 h-3.5 text-slate-400" />
          <div>
            <h3 className="text-xs font-semibold font-sans uppercase tracking-wider text-slate-200">
              Risk Heatmap
            </h3>
            <p className="text-[9px] text-slate-500 font-sans">Channel Liabilities</p>
          </div>
        </div>

        <span className="text-[10px] font-mono text-slate-400">4 CHANNELS</span>
      </div>

      {/* Grid */}
      <div className="space-y-1 my-0.5">
        {matrix.map((row) => (
          <div key={row.channel} className="flex items-center justify-between gap-1 text-[10px]">
            <span className="font-mono text-slate-400 w-10 text-[9px]">{row.channel}</span>
            <div className="grid grid-cols-3 gap-1 flex-1">
              <div className={`h-2.5 rounded ${row.low}`} title="Low Risk Range" />
              <div className={`h-2.5 rounded ${row.med}`} title="Elevated Range" />
              <div className={`h-2.5 rounded ${row.high}`} title="High Toxicity Range" />
            </div>
          </div>
        ))}
      </div>

      {/* Footer */}
      <div className="flex items-center justify-between text-[9px] font-sans text-slate-400 pt-1.5 border-t border-[#1b2028]">
        <span className="text-emerald-400">Low Risk</span>
        <span className="text-amber-400">Med</span>
        <span className="text-rose-400">High Risk</span>
      </div>
    </div>
  );
}
