import { getRiskTone, summarizeRiskLevel } from '../lib/format';

interface StatusBadgeProps {
  label: string;
  value?: string | null;
  compact?: boolean;
}

const toneClasses: Record<string, string> = {
  safe: 'bg-emerald-500/15 text-emerald-300 ring-emerald-400/20',
  warning: 'bg-amber-500/15 text-amber-300 ring-amber-400/20',
  danger: 'bg-rose-500/15 text-rose-300 ring-rose-400/20',
  neutral: 'bg-slate-500/15 text-slate-200 ring-white/10'
};

export function StatusBadge({ label, value, compact = false }: StatusBadgeProps) {
  const tone = getRiskTone(value);
  return (
    <span
      className={`inline-flex items-center gap-2 rounded-full px-3 py-1 text-xs font-medium ring-1 ${toneClasses[tone]} ${
        compact ? 'px-2.5 py-0.5' : ''
      }`}
    >
      <span className={`h-1.5 w-1.5 rounded-full ${tone === 'safe' ? 'bg-emerald-400' : tone === 'warning' ? 'bg-amber-400' : tone === 'danger' ? 'bg-rose-400' : 'bg-slate-300'}`} />
      {label}: {summarizeRiskLevel(value)}
    </span>
  );
}
