import type { ResponseMode } from './chartUtils';
import { getModeTone, getRecommendationTone, getRiskTone, responseModeLabel } from './chartUtils';

interface SummaryCardsProps {
  recommendation?: string | null;
  riskLevel?: string | null;
  confidence?: string | null;
  responseMode: ResponseMode;
  toxicThreshold?: number | null;
  activeZone?: string | null;
}

function normalizeZoneLabel(value?: string | null): string {
  if (!value) {
    return 'No validated dominant pharmacodynamic window';
  }
  const normalized = value.trim().toUpperCase();
  if (!normalized) {
    return 'No validated dominant pharmacodynamic window';
  }
  if (normalized === 'NONE' || normalized === 'NO_VALID_WINDOW') {
    return 'No validated dominant pharmacodynamic window';
  }
  return normalized.replace(/_ZONE$/, '').replaceAll('_', ' ');
}

function activeZoneTone(value?: string | null): Tone {
  const normalized = (value ?? '').trim().toUpperCase();
  if (normalized.includes('OVER_SUPPRESSION') || normalized.includes('EXCITATORY') || normalized.includes('TOXIC')) {
    return 'danger';
  }
  if (normalized.includes('NONE') || normalized.includes('NO_VALID_WINDOW')) {
    return 'warning';
  }
  if (normalized.includes('THERAPEUTIC') || normalized.includes('STABILIZING')) {
    return 'safe';
  }
  return 'neutral';
}

type Tone = 'safe' | 'warning' | 'danger' | 'neutral';

function SummaryCard({ label, value, tone, note }: { label: string; value?: string | null; tone: Tone; note?: string }) {
  const toneClasses: Record<Tone, string> = {
    safe: 'border-emerald-400/20 bg-emerald-500/10 text-emerald-50',
    warning: 'border-amber-400/20 bg-amber-500/10 text-amber-50',
    danger: 'border-rose-400/20 bg-rose-500/10 text-rose-50',
    neutral: 'border-white/10 bg-white/5 text-slate-100'
  };

  return (
    <div className={`min-h-[140px] rounded-[1.5rem] border px-5 py-5 shadow-[0_0_0_1px_rgba(255,255,255,0.02)] ${toneClasses[tone]}`}>
      <p className="text-[11px] uppercase tracking-[0.32em] opacity-75">{label}</p>
      <p className="mt-3 break-words text-xl font-semibold leading-tight xl:text-[1.55rem]">{value ?? '—'}</p>
      {note ? <p className="mt-2 text-sm leading-6 opacity-80">{note}</p> : null}
    </div>
  );
}

export function SummaryCards({ recommendation, riskLevel, confidence, responseMode, toxicThreshold, activeZone }: SummaryCardsProps) {
  const noResponse = responseMode === 'NO_SIGNIFICANT_RESPONSE';
  const responseNote = noResponse ? 'No validated pharmacodynamic response observed.' : 'The dominant biological effect class.';
  return (
    <section className="glass-card border border-cyan-400/10 bg-[rgba(6,12,24,0.9)] p-6 shadow-panel lg:p-7">
      <div className="flex flex-col gap-3 lg:flex-row lg:items-end lg:justify-between">
        <div>
          <p className="text-xs uppercase tracking-[0.34em] text-cyan-300/70">Final Decision Summary</p>
          <h3 className="mt-2 text-2xl font-semibold tracking-[-0.02em] text-white lg:text-[2rem]">Clear decision cards for quick review</h3>
          <p className="mt-3 max-w-3xl text-sm leading-7 text-slate-400">
            These cards translate the raw engine report into the shortest possible pharma-facing interpretation.
          </p>
        </div>
        <div className="rounded-2xl border border-white/10 bg-white/5 px-4 py-3 text-sm text-slate-300">
          Response mode: <span className="font-semibold text-white">{responseModeLabel(responseMode)}</span>
        </div>
      </div>

      <div className="mt-5 grid gap-4 sm:grid-cols-2 xl:grid-cols-3">
        <SummaryCard label="Recommendation" value={recommendation} tone={getRecommendationTone(recommendation)} note="High-level decision from the engine report." />
        <SummaryCard label="Risk Level" value={riskLevel} tone={getRiskTone(riskLevel)} note="Safety risk from the tested dose sweep." />
        <SummaryCard label="Confidence" value={confidence} tone="neutral" note="How stable the fitted response appears." />
        <SummaryCard label="Response Mode" value={responseModeLabel(responseMode)} tone={getModeTone(responseMode)} note={responseNote} />
        {toxicThreshold != null ? (
          <SummaryCard label="Toxic Threshold" value={toxicThreshold.toFixed(2)} tone={riskLevel?.toUpperCase().includes('HIGH') ? 'danger' : 'warning'} note="Dose where safety risk begins to dominate." />
        ) : null}
        <SummaryCard label="Active Zone" value={normalizeZoneLabel(activeZone)} tone={activeZoneTone(activeZone)} note={noResponse ? 'No validated pharmacodynamic response observed.' : 'Backend-validated dominant pharmacodynamic zone.'} />
      </div>
    </section>
  );
}
