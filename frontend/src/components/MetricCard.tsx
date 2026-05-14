import type { ReactNode } from 'react';

interface MetricCardProps {
  label: string;
  value: string;
  helper?: string;
  icon?: ReactNode;
}

export function MetricCard({ label, value, helper, icon }: MetricCardProps) {
  return (
    <div className="glass-card p-5">
      <div className="flex items-start justify-between gap-3">
        <div>
          <p className="text-xs uppercase tracking-[0.28em] text-slate-400">{label}</p>
          <h3 className="mt-2 text-2xl font-semibold text-white">{value}</h3>
          {helper ? <p className="mt-2 text-sm text-slate-400">{helper}</p> : null}
        </div>
        {icon ? <div className="rounded-2xl bg-slate-900/80 p-3 text-cyan-300 ring-1 ring-white/10">{icon}</div> : null}
      </div>
    </div>
  );
}
