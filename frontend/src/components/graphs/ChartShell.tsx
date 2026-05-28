import type { ReactNode } from 'react';

export interface ChartLegendItem {
  label: string;
  color: string;
}

interface ChartShellProps {
  id: string;
  title: string;
  subtitle: string;
  description: string;
  legendItems?: ChartLegendItem[];
  chartHeightClassName?: string;
  children: ReactNode;
}

export function ChartShell({
  id,
  title,
  subtitle,
  description,
  legendItems,
  chartHeightClassName = 'h-[360px] lg:h-[380px]',
  children
}: ChartShellProps) {
  return (
    <div id={id} className="glass-card overflow-hidden border border-white/10 bg-[rgba(6,12,24,0.88)] shadow-panel">
      <div className="border-b border-white/10 px-6 py-5">
        <p className="text-[11px] uppercase tracking-[0.34em] text-cyan-300/75">{subtitle}</p>
        <h4 className="mt-2 text-xl font-semibold tracking-[-0.01em] text-white">{title}</h4>
        <p className="mt-2 text-sm leading-6 text-slate-400">{description}</p>
        {legendItems?.length ? (
          <div className="mt-4 flex flex-wrap gap-2">
            {legendItems.map((item) => (
              <span key={item.label} className="inline-flex items-center gap-2 rounded-full border border-white/10 bg-white/5 px-3 py-1.5 text-[11px] font-medium text-slate-200">
                <span className="h-2.5 w-2.5 rounded-full" style={{ backgroundColor: item.color }} />
                {item.label}
              </span>
            ))}
          </div>
        ) : null}
      </div>
      <div className={`px-3 py-5 sm:px-4 ${chartHeightClassName}`}>
        <div className="h-full w-full">{children}</div>
      </div>
    </div>
  );
}
