import React, { useMemo } from 'react';
import { Layers } from 'lucide-react';
import { useTheme } from '../../context/ThemeContext';
import type { RunListItem } from '../../types';

interface AssayProgressDistributionProps {
  runs?: RunListItem[];
}

export function AssayProgressDistribution({ runs = [] }: AssayProgressDistributionProps) {
  const { theme } = useTheme();
  const safeRuns = Array.isArray(runs) ? runs : [];
  const dayNames = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];

  const barData = useMemo(() => {
    const counts: Record<string, { completed: number; partial: number; flagged: number }> = {
      Mon: { completed: 0, partial: 0, flagged: 0 },
      Tue: { completed: 0, partial: 0, flagged: 0 },
      Wed: { completed: 0, partial: 0, flagged: 0 },
      Thu: { completed: 0, partial: 0, flagged: 0 },
      Fri: { completed: 0, partial: 0, flagged: 0 },
      Sat: { completed: 0, partial: 0, flagged: 0 },
      Sun: { completed: 0, partial: 0, flagged: 0 },
    };

    safeRuns.forEach((r) => {
      if (!r) return;
      const parsedDate = r.created_at ? new Date(r.created_at) : new Date();
      const dayIndex = isNaN(parsedDate.getTime()) ? 1 : parsedDate.getDay();
      const dayKey = dayIndex === 0 ? 'Sun' : dayNames[dayIndex - 1] || 'Mon';

      if (!counts[dayKey]) {
        counts[dayKey] = { completed: 0, partial: 0, flagged: 0 };
      }

      if (r.status === 'completed') {
        counts[dayKey].completed += 1;
      } else {
        counts[dayKey].partial += 1;
      }

      if (
        (r.risk_level ?? '').toUpperCase() === 'HIGH' ||
        (r.recommendation ?? '').toUpperCase().includes('REJECT')
      ) {
        counts[dayKey].flagged += 1;
      }
    });

    return dayNames.map((day) => ({
      day,
      completed: counts[day]?.completed ?? 0,
      partial: counts[day]?.partial ?? 0,
      flagged: counts[day]?.flagged ?? 0,
    }));
  }, [safeRuns]);

  // Real success rate
  const totalCompleted = safeRuns.filter((r) => r && r.status === 'completed').length;
  const successRate =
    safeRuns.length > 0 ? ((totalCompleted / safeRuns.length) * 100).toFixed(1) : '100.0';

  const maxBarValue = Math.max(
    5,
    ...barData.map((d) => d.completed + d.partial + d.flagged)
  );

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
          <Layers className="w-3.5 h-3.5 text-slate-400" />
          <div>
            <h3 className="text-xs font-semibold font-sans uppercase tracking-wider text-slate-200">
              Assay Distribution
            </h3>
            <p className="text-[9px] text-slate-500 font-sans">Weekly Volume</p>
          </div>
        </div>

        <span className="text-[10px] font-mono font-medium px-2 py-0.5 rounded status-badge-safe">
          {successRate}% PASS
        </span>
      </div>

      {/* Stacked Bars Graph */}
      <div className="grid grid-cols-7 gap-1.5 items-end h-16 pt-1 px-1 bg-[#090b0e] rounded border border-[#1b2028]">
        {barData.map((d) => {
          const total = d.completed + d.partial + d.flagged;
          const heightPercent = Math.max(8, (total / maxBarValue) * 100);

          return (
            <div key={d.day} className="flex flex-col items-center gap-1 h-full justify-end">
              <div className="w-full flex flex-col items-center justify-end h-full">
                <div
                  style={{ height: `${heightPercent}%` }}
                  className="w-2.5 rounded-t overflow-hidden flex flex-col justify-end bg-slate-800"
                >
                  {d.flagged > 0 && (
                    <div
                      style={{ height: `${(d.flagged / (total || 1)) * 100}%` }}
                      className="w-full bg-rose-500"
                    />
                  )}
                  {d.partial > 0 && (
                    <div
                      style={{ height: `${(d.partial / (total || 1)) * 100}%` }}
                      className="w-full bg-amber-500"
                    />
                  )}
                  {d.completed > 0 && (
                    <div
                      style={{ height: `${(d.completed / (total || 1)) * 100}%` }}
                      className="w-full bg-emerald-500"
                    />
                  )}
                </div>
              </div>
              <span className="text-[9px] font-mono text-slate-500">{d.day}</span>
            </div>
          );
        })}
      </div>

      {/* Footer Legend */}
      <div className="flex items-center justify-between text-[9px] font-sans text-slate-400 pt-1.5 border-t border-[#1b2028]">
        <span className="flex items-center gap-1">
          <span className="w-1.5 h-1.5 rounded-full bg-emerald-400" /> Completed
        </span>
        <span className="flex items-center gap-1">
          <span className="w-1.5 h-1.5 rounded-full bg-amber-400" /> Partial
        </span>
        <span className="flex items-center gap-1">
          <span className="w-1.5 h-1.5 rounded-full bg-rose-400" /> Flagged
        </span>
      </div>
    </div>
  );
}
