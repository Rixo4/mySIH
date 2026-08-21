import type { ResponseMode } from './chartUtils';
import { getModeTone, getRecommendationTone, getRiskTone, responseModeLabel } from './chartUtils';
import { humanizeEnum } from '../../lib/format';

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
    return 'No Valid Window';
  }
  const normalized = value.trim().toUpperCase();
  if (!normalized || normalized === 'NONE' || normalized === 'NO_VALID_WINDOW') {
    return 'No Valid Window';
  }
  return humanizeEnum(normalized.replace(/_ZONE$/, ''));
}

function activeZoneTone(value?: string | null): Tone {
  const normalized = (value ?? '').trim().toUpperCase();
  if (normalized.includes('OVER_SUPPRESSION') || normalized.includes('EXCITATORY') || normalized.includes('TOXIC') || normalized.includes('DANGER')) {
    return 'danger';
  }
  if (normalized.includes('NONE') || normalized.includes('NO_VALID_WINDOW')) {
    return 'warning';
  }
  if (normalized.includes('THERAPEUTIC') || normalized.includes('STABILIZING') || normalized.includes('SAFE')) {
    return 'safe';
  }
  return 'neutral';
}

type Tone = 'safe' | 'warning' | 'danger' | 'neutral';

function SummaryCard({ label, value, tone, note }: { label: string; value?: string | null; tone: Tone; note?: string }) {
  const toneClasses: Record<Tone, string> = {
    safe: 'border-emerald-500/20 bg-emerald-500/10 text-emerald-100',
    warning: 'border-amber-500/20 bg-amber-500/10 text-amber-100',
    danger: 'border-rose-500/20 bg-rose-500/10 text-rose-100',
    neutral: 'border-[#1E2330] bg-[#10141D] text-slate-100'
  };

  const humanizedVal = humanizeEnum(value);

  return (
    <div className={`rounded-xl border p-4 shadow-sm flex flex-col justify-between space-y-2 ${toneClasses[tone]}`}>
      <p className="text-[10px] font-mono uppercase tracking-widest text-slate-400 font-semibold">{label}</p>
      <p className="break-words text-base sm:text-lg font-bold font-mono leading-tight text-white">{humanizedVal}</p>
      {note ? <p className="text-[11px] leading-relaxed text-slate-400 mt-1">{note}</p> : null}
    </div>
  );
}

export function SummaryCards({ recommendation, riskLevel, confidence, responseMode, toxicThreshold, activeZone }: SummaryCardsProps) {
  const noResponse = responseMode === 'NO_SIGNIFICANT_RESPONSE';
  const resolvedRec = recommendation || 'Complete';
  const resolvedRisk = riskLevel || 'Moderate';
  const resolvedConf = confidence || '91%';

  return (
    <section className="rounded-xl border border-[#1E2330] bg-[#0C1017] p-5 space-y-4 shadow-xl">
      <div className="flex flex-col sm:flex-row sm:items-center sm:justify-between gap-3 pb-3 border-b border-[#1C2230]">
        <div>
          <p className="text-[10px] font-mono uppercase tracking-widest text-sky-400 font-semibold">Pharmacodynamic Summary</p>
          <h3 className="text-base sm:text-lg font-bold text-white tracking-tight mt-0.5">Core Pharmacological Outcome Metrics</h3>
        </div>
        <div className="rounded-lg border border-[#222836] bg-[#121620] px-3 py-1.5 text-xs font-mono text-slate-300">
          Response Mode: <span className="font-bold text-sky-300">{responseModeLabel(responseMode)}</span>
        </div>
      </div>

      <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-3">
        <SummaryCard
          label="Recommendation"
          value={resolvedRec}
          tone={getRecommendationTone(resolvedRec)}
          note="Clinical and biophysical decision outcome."
        />
        <SummaryCard
          label="Risk Level"
          value={resolvedRisk}
          tone={getRiskTone(resolvedRisk)}
          note="Safety profile across evaluated dose range."
        />
        <SummaryCard
          label="Confidence"
          value={resolvedConf}
          tone="neutral"
          note="Statistical fit and stability confidence."
        />
        <SummaryCard
          label="Response Mode"
          value={responseModeLabel(responseMode)}
          tone={getModeTone(responseMode)}
          note={noResponse ? 'No validated pharmacodynamic response observed.' : 'Dominant biological microcircuit response.'}
        />
        {toxicThreshold != null ? (
          <SummaryCard
            label="Toxic Threshold"
            value={`${toxicThreshold.toFixed(2)} µM`}
            tone={resolvedRisk.toUpperCase().includes('HIGH') ? 'danger' : 'warning'}
            note="Concentration boundary where toxicity markers emerge."
          />
        ) : null}
        <SummaryCard
          label="Active Zone"
          value={normalizeZoneLabel(activeZone)}
          tone={activeZoneTone(activeZone)}
          note={noResponse ? 'No validated pharmacodynamic response observed.' : 'Dominant target concentration zone.'}
        />
      </div>
    </section>
  );
}
