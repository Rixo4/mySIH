import type { ReportChartPoint } from '../types';

export function buildDoseChartData(
  minDose: number,
  maxDose: number,
  step: number,
  effectiveRangeText?: string | null,
  riskLevel?: string | null,
  toxicityThresholdText?: string | null,
  responseMode?: string | null,
  maxEffect?: number | null
): ReportChartPoint[] {
  const points: ReportChartPoint[] = [];
  const safeStep = step > 0 ? step : 1;
  const count = Math.max(1, Math.round((maxDose - minDose) / safeStep));

  const effectiveRangeMatch = effectiveRangeText?.match(/([0-9.]+)\s*-\s*([0-9.]+)/);
  const effectiveMin = effectiveRangeMatch ? Number(effectiveRangeMatch[1]) : minDose + (maxDose - minDose) * 0.55;
  const effectiveMax = effectiveRangeMatch ? Number(effectiveRangeMatch[2]) : maxDose;
  const toxMatch = toxicityThresholdText?.match(/([0-9.]+)/);
  const toxicDose = toxMatch ? Number(toxMatch[1]) : maxDose + 4;
  const highRisk = (riskLevel ?? '').toUpperCase().includes('HIGH');
  const normalizedMode = (responseMode ?? '').trim().toUpperCase();
  const noSignificantResponse = normalizedMode === 'NO_SIGNIFICANT_RESPONSE' || (maxEffect != null && maxEffect < 20);
  const peakEffect = noSignificantResponse ? Math.max(4, Math.min(18, maxEffect ?? 14)) : 14 + 78;

  for (let i = 0; i <= count; i += 1) {
    const dose = Number((minDose + i * safeStep).toFixed(2));
    const midpoint = (minDose + maxDose) / 2;
    const sigmoid = 1 / (1 + Math.exp(-0.42 * (dose - midpoint)));
    const effect = noSignificantResponse ? Math.max(0, Math.min(20, peakEffect * sigmoid)) : Math.max(0, Math.min(100, 14 + sigmoid * 78));
    const toxicityCurve = Math.max(0, Math.min(100, dose >= toxicDose ? 70 + (dose - toxicDose) * 4 : dose > effectiveMax ? 14 + (dose - effectiveMax) * 3 : 8 + (dose / (maxDose || 1)) * 10));
    const risk = highRisk
      ? Math.min(100, toxicityCurve * 0.72 + effect * 0.18)
      : Math.min(100, noSignificantResponse ? effect * 0.12 + toxicityCurve * 0.28 : effect * 0.24 + toxicityCurve * 0.44);

    points.push({
      dose,
      effect: Number(effect.toFixed(1)),
      toxicity: Number(toxicityCurve.toFixed(1)),
      risk: Number(risk.toFixed(1))
    });
  }

  return points;
}
