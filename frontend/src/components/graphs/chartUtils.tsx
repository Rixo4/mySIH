import type {
  DrugEvaluationVisualizationData,
  RasterSpikePoint,
  TimelineSegment,
  VoltageTracePoint
} from '../../types';

export type ResponseMode = 'SUPPRESSIVE_RESPONSE' | 'EXCITATORY_RESPONSE' | 'STABILIZING_RESPONSE' | 'NO_SIGNIFICANT_RESPONSE' | 'TOXIC_INSTABILITY' | 'UNSPECIFIED';

export interface NormalizedDoseResult {
  dose: number;
  effect: number;
  firing_rate: number;
  seizure_score: number;
  sync: number;
  nii: number;
  toxicity_score: number;
  variance: number;
  response_mode: ResponseMode;
  biological_state: string;
  ic50_na?: number;
  ic50_k?: number;
  ic50_ca?: number;
  active_zone?: string;
}

export interface NormalizedMarkers {
  ic50?: number;
  ic50Label?: string;
  onsetDose?: number;
  toxicThreshold?: number;
  therapeuticMin?: number;
  therapeuticMax?: number;
  activeZone: string;
  hasValidatedTherapeuticWindow: boolean;
  toxicityObserved: boolean;
  lowResponseVisualMode: boolean;
  channelIc50s: {
    Na?: number;
    K?: number;
    Ca?: number;
  };
}

export interface NormalizedVisualizationData {
  responseMode: ResponseMode;
  doseResults: NormalizedDoseResult[];
  voltageTrace: VoltageTracePoint[];
  rasterSpikes: RasterSpikePoint[];
  timeline: TimelineSegment[];
  markers: NormalizedMarkers;
}

interface SummaryLike {
  recommendation?: unknown;
  riskLevel?: unknown;
  confidence?: unknown;
  response_mode?: unknown;
  responseMode?: unknown;
  active_zone?: unknown;
  activeZone?: unknown;
  max_effect?: unknown;
  maxEffect?: unknown;
  response_strength?: unknown;
  responseStrength?: unknown;
  [key: string]: unknown;
}

interface MechanisticTooltipProps {
  active?: boolean;
  label?: string | number;
  payload?: Array<{ dataKey?: string | number; name?: string; value?: unknown; color?: string }>;
  explanation: string;
  labelPrefix?: string;
  fieldLabels?: Record<string, string>;
  suffixByKey?: Record<string, string>;
}

const RESPONSE_MODE_LABELS: Record<ResponseMode, string> = {
  SUPPRESSIVE_RESPONSE: 'Suppression',
  EXCITATORY_RESPONSE: 'Excitability',
  STABILIZING_RESPONSE: 'Stabilization',
  NO_SIGNIFICANT_RESPONSE: 'No Significant Response',
  UNSPECIFIED: 'Response'
};

export function normalizeResponseMode(value: unknown): ResponseMode {
  const normalized = typeof value === 'string' ? value.trim().toUpperCase() : '';
  if (normalized === 'STANDARD_RESPONSE' || normalized === 'NO_SIGNIFICANT_RESPONSE') {
    return 'NO_SIGNIFICANT_RESPONSE';
  }
  if (normalized === 'SUPPRESSIVE_RESPONSE' || normalized === 'EXCITATORY_RESPONSE' || normalized === 'STABILIZING_RESPONSE' || normalized === 'TOXIC_INSTABILITY') {
    return normalized;
  }
  return 'UNSPECIFIED';
}

export function responseModeLabel(mode: ResponseMode): string {
  return RESPONSE_MODE_LABELS[mode];
}

export function safeNumber(value: unknown, fallback = Number.NaN): number {
  const number = typeof value === 'number' ? value : typeof value === 'string' ? Number(value) : Number.NaN;
  return Number.isFinite(number) ? number : fallback;
}

export function extractNumber(value: unknown): number | null {
  if (typeof value === 'number' && Number.isFinite(value)) {
    return value;
  }

  if (typeof value !== 'string') {
    return null;
  }

  const match = value.match(/[-+]?\d*\.?\d+/);
  if (!match) {
    return null;
  }

  const number = Number(match[0]);
  return Number.isFinite(number) ? number : null;
}

export function extractRange(value: unknown): { min: number; max: number } | null {
  if (typeof value !== 'string') {
    return null;
  }

  const match = value.match(/([-+]?\d*\.?\d+)\s*(?:-|to|–)\s*([-+]?\d*\.?\d+)/i);
  if (!match) {
    const single = extractNumber(value);
    return single == null ? null : { min: single, max: single };
  }

  const min = Number(match[1]);
  const max = Number(match[2]);
  if (!Number.isFinite(min) || !Number.isFinite(max)) {
    return null;
  }

  return min <= max ? { min, max } : { min: max, max: min };
}

export function formatNumber(value: number | null | undefined, digits = 2): string {
  if (value == null || !Number.isFinite(value)) {
    return '—';
  }
  return value.toFixed(digits);
}

export function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

export function formatDoseLabel(value: number | null | undefined): string {
  if (value == null || !Number.isFinite(value)) {
    return '—';
  }
  return `${value.toFixed(2)}`;
}

function readSummaryText(summary: SummaryLike | null | undefined, keys: string[]): string | null {
  if (!summary) {
    return null;
  }

  for (const key of keys) {
    const value = summary[key];
    if (typeof value === 'number' && Number.isFinite(value)) {
      return String(value);
    }
    if (typeof value === 'string') {
      const normalized = value.trim();
      if (normalized && !['null', 'undefined', 'n/a', 'na', 'none', '-', '—'].includes(normalized.toLowerCase())) {
        return normalized;
      }
    }
  }

  return null;
}

function parseSummaryActiveZone(summary: SummaryLike | null | undefined): string | null {
  const text = readSummaryText(summary, ['active_zone', 'activeZone']);
  return text ? text.toUpperCase() : null;
}

function parseMaxEffect(summary: SummaryLike | null | undefined): number | null {
  return extractNumber(readSummaryText(summary, ['max_effect', 'maxEffect']));
}

function getDoseResults(raw: Partial<DrugEvaluationVisualizationData> | null | undefined): unknown[] {
  const doseResults = raw?.dose_results;
  return Array.isArray(doseResults) ? doseResults : [];
}

function readFirstDosePoint(doseResults: NormalizedDoseResult[]): NormalizedDoseResult | null {
  return doseResults.length > 0 ? doseResults[0] : null;
}

function readLastDosePoint(doseResults: NormalizedDoseResult[]): NormalizedDoseResult | null {
  return doseResults.length > 0 ? doseResults[doseResults.length - 1] : null;
}

function dominantChannel(mode: ResponseMode): 'Na' | 'K' | 'Ca' {
  if (mode === 'EXCITATORY_RESPONSE') {
    return 'K';
  }
  if (mode === 'STABILIZING_RESPONSE') {
    return 'Ca';
  }
  return 'Na';
}

function getChannelIc50(point: NormalizedDoseResult | null, channel: 'Na' | 'K' | 'Ca'): number | undefined {
  if (!point) {
    return undefined;
  }

  if (channel === 'Na') return point.ic50_na;
  if (channel === 'K') return point.ic50_k;
  return point.ic50_ca;
}

export function normalizeDoseResults(raw: unknown): NormalizedDoseResult[] {
  const source = Array.isArray(raw) ? raw : [];

  return source.map((entry, index) => {
    const record = entry as Record<string, unknown>;
    const fallbackDose = Number(index);
    const dose = extractNumber(record.dose ?? record.dose_mg ?? record.concentration ?? record.x ?? record.value) ?? fallbackDose;
    const responseMode = normalizeResponseMode(record.response_mode ?? record.responseMode);

    return {
      dose,
      effect: extractNumber(record.effect ?? record.effect_pct ?? record.effectPercent ?? record.response ?? record.response_pct) ?? 0,
      firing_rate: extractNumber(record.firing_rate ?? record.firingRate ?? record.spike_rate ?? record.spikeRate) ?? 0,
      seizure_score: extractNumber(record.seizure_score ?? record.seizureScore ?? record.seizureRisk ?? record.seizure ?? record.risk) ?? 0,
      sync: extractNumber(record.sync ?? record.synchronization ?? record.synchronization_index ?? record.sync_index) ?? 0,
      nii: extractNumber(record.nii ?? record.neural_instability_index ?? record.instability_index ?? record.instability) ?? 0,
      toxicity_score: extractNumber(record.toxicity_score ?? record.toxicityScore ?? record.toxicity ?? record.toxicity_risk) ?? 0,
      variance: extractNumber(record.variance ?? record.std_dev ?? record.standard_deviation ?? record.stability_variance) ?? 0,
      response_mode: responseMode,
      biological_state: typeof record.biological_state === 'string'
        ? record.biological_state
        : typeof record.biologicalState === 'string'
          ? record.biologicalState
          : 'UNKNOWN',
      ic50_na: extractNumber(record.ic50_na ?? record.ic50Na),
      ic50_k: extractNumber(record.ic50_k ?? record.ic50K),
      ic50_ca: extractNumber(record.ic50_ca ?? record.ic50Ca),
      active_zone: typeof record.active_zone === 'string'
        ? record.active_zone
        : typeof record.activeZone === 'string'
          ? record.activeZone
          : undefined
    };
  });
}

function normalizeTimelineSegments(rawTimeline: unknown): TimelineSegment[] {
  const source = Array.isArray(rawTimeline) ? rawTimeline : [];
  return source
    .map((entry) => {
      const record = entry as Record<string, unknown>;
      const label = typeof record.label === 'string' ? record.label : 'Zone';
      const state = typeof record.state === 'string' ? record.state : 'UNKNOWN_ZONE';
      const from = extractNumber(record.from) ?? Number.NaN;
      const to = extractNumber(record.to) ?? Number.NaN;
      const color = typeof record.color === 'string' ? record.color : '#64748b';
      return { label, state, from, to, color };
    })
    .filter((segment) => Number.isFinite(segment.from) && Number.isFinite(segment.to) && segment.to > segment.from);
}

function normalizeZone(value: unknown): { start: number; end: number } | null {
  if (!value || typeof value !== 'object') {
    return null;
  }

  const record = value as Record<string, unknown>;
  const start = extractNumber(record.start);
  const end = extractNumber(record.end);
  if (start == null || end == null || !Number.isFinite(start) || !Number.isFinite(end) || end <= start) {
    return null;
  }
  return { start, end };
}

function deriveDominantIc50(mode: ResponseMode, point: NormalizedDoseResult | null): { value?: number; label: string } {
  const channel = dominantChannel(mode);
  const value = getChannelIc50(point, channel);
  if (value != null) {
    return { value, label: `${channel} IC50` };
  }

  const fallbackValue = point?.ic50_na ?? point?.ic50_k ?? point?.ic50_ca;
  return { value: fallbackValue, label: 'IC50' };
}

function normalizeTracePoints(raw: unknown): VoltageTracePoint[] {
  const source = Array.isArray(raw) ? raw : [];
  return source
    .map((entry) => {
      const record = entry as Record<string, unknown>;
      const time = extractNumber(record.time ?? record.t ?? record.timestamp) ?? 0;
      const voltage = extractNumber(record.voltage ?? record.mV ?? record.membrane_voltage ?? record.value) ?? 0;
      return { time, voltage };
    })
    .filter((point) => Number.isFinite(point.time) && Number.isFinite(point.voltage));
}

function normalizeRasterPoints(raw: unknown): RasterSpikePoint[] {
  const source = Array.isArray(raw) ? raw : [];
  return source
    .map((entry) => {
      const record = entry as Record<string, unknown>;
      const neuronId = extractNumber(record.neuron_id ?? record.neuronId ?? record.cell_id ?? record.cellId) ?? 0;
      const spikeTime = extractNumber(record.spike_time ?? record.spikeTime ?? record.time) ?? 0;
      return { neuron_id: neuronId, spike_time: spikeTime };
    })
    .filter((point) => Number.isFinite(point.neuron_id) && Number.isFinite(point.spike_time));
}

export function normalizeVisualizationData(
  raw: Partial<DrugEvaluationVisualizationData> | null | undefined,
  summary: SummaryLike | null | undefined,
  reportText?: string | null
): NormalizedVisualizationData {
  const rawRecord = (raw ?? {}) as Record<string, unknown>;
  const doseResults = normalizeDoseResults(getDoseResults(raw));
  const firstPoint = readFirstDosePoint(doseResults);
  void readLastDosePoint(doseResults);
  const parsedResponseMode = normalizeResponseMode(summary?.response_mode ?? summary?.responseMode ?? firstPoint?.response_mode);

  const parsedActiveZone = parseSummaryActiveZone(summary);
  const responseMode = parsedResponseMode;

  const dominant = deriveDominantIc50(responseMode, firstPoint);
  const zones = rawRecord.zones ?? (rawRecord.zone_contract ? {
    ineffective: normalizeZone((rawRecord.zone_contract as Record<string, unknown>).ineffective_zone),
    therapeutic: normalizeZone((rawRecord.zone_contract as Record<string, unknown>).therapeutic_zone),
    over_suppression: normalizeZone((rawRecord.zone_contract as Record<string, unknown>).over_suppression_zone),
    excitatory: normalizeZone((rawRecord.zone_contract as Record<string, unknown>).severe_excitability_zone),
    toxic: normalizeZone((rawRecord.zone_contract as Record<string, unknown>).saturated_stabilization_zone)
  } : null);

  const thresholds = (rawRecord.thresholds as { onset?: number | null; toxic?: number | null; saturation?: number | null } | undefined) ?? null;
  const toxicityObserved = Boolean(rawRecord.toxicity_observed ?? Boolean((zones as { toxic?: unknown } | null)?.toxic ?? (zones as { over_suppression?: unknown } | null)?.over_suppression ?? (zones as { excitatory?: unknown } | null)?.excitatory));
  const lowResponse = responseMode === 'NO_SIGNIFICANT_RESPONSE';

  const hasValidatedTherapeuticWindow = Boolean(
    (zones as { therapeutic?: unknown } | null)?.therapeutic &&
    thresholds &&
    !lowResponse
  );

  const activeZone = (typeof rawRecord.active_zone === 'string' ? rawRecord.active_zone : null) ?? parsedActiveZone ?? 'NONE';

  const timeline = normalizeTimelineSegments(raw?.classification_timeline);
  const filteredTimeline = lowResponse
    ? timeline.filter((segment) => segment.state === 'INEFFECTIVE_ZONE')
    : timeline;

  const zoneFilter = (key: 'ineffective' | 'therapeutic' | 'over_suppression' | 'excitatory' | 'toxic') => (zones as Record<string, { start: number; end: number } | null> | null)?.[key] ?? null;

  const toxicThreshold = toxicityObserved ? thresholds?.toxic ?? null : null;
  const onsetDose = lowResponse ? null : thresholds?.onset ?? null;
  const therapeuticMin = zoneFilter('therapeutic')?.start ?? null;
  const therapeuticMax = zoneFilter('therapeutic')?.end ?? null;

  const normalizedMarkers: NormalizedMarkers = {
    ic50: dominant.value,
    ic50Label: dominant.label,
    onsetDose,
    toxicThreshold,
    therapeuticMin: hasValidatedTherapeuticWindow ? therapeuticMin ?? undefined : undefined,
    therapeuticMax: hasValidatedTherapeuticWindow ? therapeuticMax ?? undefined : undefined,
    activeZone,
    hasValidatedTherapeuticWindow,
    toxicityObserved,
    lowResponseVisualMode: lowResponse,
    channelIc50s: {
      Na: firstPoint?.ic50_na,
      K: firstPoint?.ic50_k,
      Ca: firstPoint?.ic50_ca
    }
  };

  return {
    responseMode,
    doseResults,
    voltageTrace: normalizeTracePoints(raw?.voltage_trace),
    rasterSpikes: normalizeRasterPoints(raw?.raster_spikes),
    timeline: filteredTimeline,
    markers: normalizedMarkers
  };
}

export function getResponseModeCopy(mode: ResponseMode) {
  switch (mode) {
    case 'EXCITATORY_RESPONSE':
      return {
        title: 'Excitability / Seizure-Risk Response Across Dose',
        explanation: 'As potassium-channel block increases, excitability and seizure-risk markers rise.',
        axisLabel: 'Effect %',
        legendItems: [
          { label: 'Excitability signal', color: '#f59e0b' },
          { label: 'IC50 marker', color: '#22d3ee' },
          { label: 'Onset dose', color: '#10b981' },
          { label: 'Toxic threshold', color: '#ef4444' }
        ]
      };
    case 'STABILIZING_RESPONSE':
      return {
        title: 'Network Stabilization Response Across Dose',
        explanation: 'As calcium-channel block increases, synchronization and instability markers decrease.',
        axisLabel: 'Effect %',
        legendItems: [
          { label: 'Stabilization signal', color: '#06b6d4' },
          { label: 'IC50 marker', color: '#22d3ee' },
          { label: 'Onset dose', color: '#10b981' },
          { label: 'Toxic threshold', color: '#ef4444' }
        ]
      };
    case 'SUPPRESSIVE_RESPONSE':
      return {
        title: 'Suppression Response Across Dose',
        explanation: 'As sodium-channel block increases, firing activity falls and the response eventually saturates.',
        axisLabel: 'Effect %',
        legendItems: [
          { label: 'Suppression signal', color: '#22c55e' },
          { label: 'IC50 marker', color: '#22d3ee' },
          { label: 'Onset dose', color: '#10b981' },
          { label: 'Toxic threshold', color: '#ef4444' }
        ]
      };
    case 'NO_SIGNIFICANT_RESPONSE':
      return {
        title: 'Low Biological Response Across Dose',
        explanation: 'No validated pharmacodynamic response observed. The plotted values stay on the true biological scale without visual amplification.',
        axisLabel: 'Effect %',
        legendItems: [
          { label: 'Low-response signal', color: '#94a3b8' },
          { label: 'IC50 marker', color: '#22d3ee' },
          { label: 'Onset dose', color: '#10b981' },
          { label: 'Toxic threshold', color: '#ef4444' }
        ]
      };
    default:
      return {
        title: 'Dose Response Across Dose',
        explanation: 'The sweep shows how the drug changes response magnitude across the tested dose range.',
        axisLabel: 'Effect %',
        legendItems: [
          { label: 'Response signal', color: '#38bdf8' },
          { label: 'IC50 marker', color: '#22d3ee' },
          { label: 'Onset dose', color: '#10b981' },
          { label: 'Toxic threshold', color: '#ef4444' }
        ]
      };
  }
}

export function getRecommendationTone(value?: string | null): 'safe' | 'warning' | 'danger' | 'neutral' {
  const normalized = (value ?? '').trim().toUpperCase();
  if (normalized.includes('PROMISING') || normalized.includes('CANDIDATE') || normalized.includes('SUPPORTED')) {
    return 'safe';
  }
  if (normalized.includes('CAUTION') || normalized.includes('LIMITED') || normalized.includes('MARGINAL')) {
    return 'warning';
  }
  if (normalized.includes('NOT RECOMMENDED') || normalized.includes('REJECT') || normalized.includes('UNACCEPTABLE')) {
    return 'danger';
  }
  if (normalized.includes('LOW')) {
    return 'safe';
  }
  return 'neutral';
}

export function getRiskTone(value?: string | null): 'safe' | 'warning' | 'danger' | 'neutral' {
  const normalized = (value ?? '').trim().toUpperCase();
  if (normalized.includes('LOW')) {
    return 'safe';
  }
  if (normalized.includes('MODERATE')) {
    return 'warning';
  }
  if (normalized.includes('HIGH')) {
    return 'danger';
  }
  return 'neutral';
}

export function getModeTone(mode: ResponseMode): 'safe' | 'warning' | 'danger' | 'neutral' {
  if (mode === 'EXCITATORY_RESPONSE') {
    return 'warning';
  }
  if (mode === 'STABILIZING_RESPONSE' || mode === 'SUPPRESSIVE_RESPONSE') {
    return 'safe';
  }
  return 'neutral';
}

export function formatChartSeriesLabel(key: string): string {
  const normalized = key.replace(/_/g, ' ');
  return normalized
    .replace(/\b(sync|nii|ic50)\b/gi, (word) => word.toUpperCase())
    .replace(/\b(rate\b)/gi, 'Rate')
    .replace(/\bscore\b/gi, 'Score')
    .replace(/\bvariance\b/gi, 'Variance')
    .replace(/\bvoltage\b/gi, 'Voltage')
    .replace(/\bseizure\b/gi, 'Seizure');
}

export function MechanisticTooltip({ active, label, payload, explanation, labelPrefix = 'Dose', fieldLabels, suffixByKey }: MechanisticTooltipProps) {
  if (!active || !payload?.length) {
    return null;
  }

  return (
    <div className="max-w-xs rounded-2xl border border-white/10 bg-slate-950/95 px-4 py-3 text-xs text-slate-100 shadow-2xl backdrop-blur">
      <p className="text-[10px] uppercase tracking-[0.3em] text-cyan-300/70">{typeof label === 'number' ? `${labelPrefix} ${formatDoseLabel(label)}` : String(label ?? 'Value')}</p>
      <p className="mt-2 text-sm leading-6 text-slate-300">{explanation}</p>
      <div className="mt-3 space-y-2">
        {payload
          .filter((item) => item && item.value != null)
          .map((item) => {
            const key = typeof item.dataKey === 'string'
              ? item.dataKey
              : typeof item.name === 'string'
                ? item.name
                : 'value';
            const labelText = fieldLabels?.[key] ?? formatChartSeriesLabel(key);
            const numeric = typeof item.value === 'number' ? item.value : extractNumber(item.value);
            const rawText = numeric == null ? String(item.value) : formatNumber(numeric, numeric < 1 && numeric > 0 ? 3 : 2);
            const suffix = suffixByKey?.[key] ?? '';

            return (
              <div key={`${key}-${labelText}`} className="flex items-center justify-between gap-4">
                <div className="flex items-center gap-2 text-slate-300">
                  <span className="h-2.5 w-2.5 rounded-full" style={{ backgroundColor: item.color ?? '#22d3ee' }} />
                  <span>{labelText}</span>
                </div>
                <span className="font-medium text-white">{rawText}{suffix}</span>
              </div>
            );
          })}
      </div>
    </div>
  );
}
