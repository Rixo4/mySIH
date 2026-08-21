import React, { useMemo } from 'react';
import { Activity } from 'lucide-react';
import { useTheme } from '../../context/ThemeContext';
import type { RunListItem } from '../../types';

interface StabilityVolumeHistogramProps {
  runs?: RunListItem[];
}

export function StabilityVolumeHistogram({ runs = [] }: StabilityVolumeHistogramProps) {
  const { theme } = useTheme();
  const safeRuns = Array.isArray(runs) ? runs : [];

  const { bins, safePercent } = useMemo(() => {
    const defaultBins = [
      { range: '0.0-0.2', count: 4, isSafe: true },
      { range: '0.2-0.4', count: 8, isSafe: true },
      { range: '0.4-0.6', count: 5, isSafe: true },
      { range: '0.6-0.8', count: 2, isSafe: false },
      { range: '0.8-1.0', count: 1, isSafe: false },
    ];

    if (safeRuns.length === 0) {
      return { bins: defaultBins, safePercent: '85.0' };
    }

    const counts = [0, 0, 0, 0, 0];
    let safeCount = 0;

    safeRuns.forEach((r) => {
      const risk = (r?.risk_level ?? '').toUpperCase();
      if (risk === 'LOW') {
        counts[0] += 1;
        safeCount += 1;
      } else if (risk === 'MEDIUM') {
        counts[2] += 1;
        safeCount += 1;
      } else if (risk === 'HIGH') {
        counts[4] += 1;
      } else {
        counts[1] += 1;
        safeCount += 1;
      }
    });

    const maxCount = Math.max(1, ...counts);
    const mappedBins = [
      { range: '<0.2', count: counts[0], isSafe: true },
      { range: '0.2-0.4', count: counts[1], isSafe: true },
      { range: '0.4-0.6', count: counts[2], isSafe: true },
      { range: '0.6-0.8', count: counts[3], isSafe: false },
      { range: '>0.8', count: counts[4], isSafe: false },
    ];

    const percent = ((safeCount / safeRuns.length) * 100).toFixed(1);
    return { bins: mappedBins, safePercent: percent };
  }, [safeRuns]);

  const maxVal = Math.max(5, ...bins.map((b) => b.count));

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
          <Activity className="w-3.5 h-3.5 text-slate-400" />
          <div>
            <h3 className="text-xs font-semibold font-sans uppercase tracking-wider text-slate-200">
              Instability Distribution
            </h3>
            <p className="text-[9px] text-slate-500 font-sans">NII Score Bins</p>
          </div>
        </div>

        <span className="text-[10px] font-mono font-medium px-2 py-0.5 rounded status-badge-safe">
          {safePercent}% STABLE
        </span>
      </div>

      {/* Histogram Bars */}
      <div className="grid grid-cols-5 gap-2 items-end h-16 pt-1 px-2 bg-[#090b0e] rounded border border-[#1b2028]">
        {bins.map((bin) => {
          const heightPercent = Math.max(10, (bin.count / maxVal) * 100);

          return (
            <div key={bin.range} className="flex flex-col items-center gap-1 h-full justify-end">
              <div className="w-full flex flex-col items-center justify-end h-full">
                <div
                  style={{ height: `${heightPercent}%` }}
                  className={`w-3.5 rounded-t transition-all ${
                    bin.isSafe ? 'bg-sky-500' : 'bg-rose-500'
                  }`}
                />
              </div>
              <span className="text-[8px] font-mono text-slate-500">{bin.range}</span>
            </div>
          );
        })}
      </div>

      {/* Footer */}
      <div className="flex items-center justify-between text-[9px] font-sans text-slate-400 pt-1.5 border-t border-[#1b2028]">
        <span>Optimal Band (&lt;0.05)</span>
        <span>Excursion Band</span>
      </div>
    </div>
  );
}
