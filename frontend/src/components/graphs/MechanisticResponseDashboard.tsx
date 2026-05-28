import { useMemo } from 'react';
import { motion } from 'framer-motion';
import { Download, FileDown } from 'lucide-react';
import type { BackendRunResponse, DrugEvaluationVisualizationData, ReportChartPoint } from '../../types';
import { formatRangeLabel } from '../../lib/drugVisualization';
import { AdvancedNeuroCharts } from './AdvancedNeuroCharts';
import { FiringRateChart } from './FiringRateChart';
import { NiiChart } from './NiiChart';
import { normalizeVisualizationData, responseModeLabel, type ResponseMode } from './chartUtils';
import { PrimaryResponseChart } from './PrimaryResponseChart';
import { ResponseZoneTimeline } from './ResponseZoneTimeline';
import { SeizureRiskChart } from './SeizureRiskChart';
import { SummaryCards } from './SummaryCards';
import { SynchronizationChart } from './SynchronizationChart';

interface MechanisticResponseDashboardProps {
  visualizationData: DrugEvaluationVisualizationData | null | undefined;
  parsedSummary: BackendRunResponse['parsed_summary'];
  reportText?: string | null;
  responseMode: string;
  chartData?: ReportChartPoint[];
  onDownloadCsv?: () => void;
  onDownloadReport?: () => void;
  onDownloadPng?: () => void;
}

function readSummaryValue(summary: Record<string, unknown> | null | undefined, key: string): string | null {
  const value = summary?.[key];

  if (typeof value === 'number' && Number.isFinite(value)) {
    return String(value);
  }

  if (typeof value !== 'string') {
    return null;
  }

  const normalized = value.trim();
  if (!normalized) {
    return null;
  }

  const lowered = normalized.toLowerCase();
  if (['null', 'undefined', 'n/a', 'na', 'none', '-', '—'].includes(lowered)) {
    return null;
  }

  return normalized;
}

export function MechanisticResponseDashboard({
  visualizationData,
  parsedSummary,
  reportText,
  responseMode,
  chartData,
  onDownloadCsv,
  onDownloadReport,
  onDownloadPng
}: MechanisticResponseDashboardProps) {
  const normalized = useMemo(
    () => normalizeVisualizationData(visualizationData, parsedSummary ?? null, reportText),
    [parsedSummary, reportText, visualizationData]
  );

  const mode = (normalized.responseMode === 'UNSPECIFIED' ? responseMode : normalized.responseMode) as ResponseMode;
  const recommendation = readSummaryValue(parsedSummary, 'recommendation');
  const riskLevel = readSummaryValue(parsedSummary, 'risk_level');
  const confidence = readSummaryValue(parsedSummary, 'confidence');
  const toxicThreshold = normalized.markers.toxicThreshold ?? null;
  const activeZone = normalized.markers.activeZone;
  const validatedRange = normalized.markers.hasValidatedTherapeuticWindow
    ? formatRangeLabel(normalized.markers.therapeuticMin, normalized.markers.therapeuticMax)
    : mode === 'NO_SIGNIFICANT_RESPONSE'
      ? 'No validated pharmacodynamic response observed'
      : 'No validated therapeutic window';

  return (
    <motion.section initial={{ opacity: 0, y: 14 }} animate={{ opacity: 1, y: 0 }} className="space-y-6">
      <div className="glass-card border border-cyan-400/10 bg-[rgba(6,12,24,0.9)] p-6 shadow-panel lg:p-7">
        <div className="flex flex-col gap-5 xl:flex-row xl:items-start xl:justify-between">
          <div className="max-w-3xl">
            <p className="text-xs uppercase tracking-[0.34em] text-cyan-300/70">Mechanistic Response Dashboard</p>
            <h3 className="mt-3 text-3xl font-semibold tracking-[-0.02em] text-white lg:text-[2.05rem] lg:leading-tight">Large, readable graphs for pharma review</h3>
            <p className="mt-4 text-sm leading-7 text-slate-400 lg:text-[0.98rem]">
              This layer explains the biological outcome without changing the engine output. It turns the same dose-response data into decision cards, response zones, and mechanistic plots for researchers, investors, and non-technical reviewers.
            </p>
          </div>

          <div className="flex flex-wrap gap-3">
            {onDownloadCsv ? (
              <button type="button" onClick={onDownloadCsv} className="inline-flex items-center gap-2 rounded-2xl border border-emerald-400/20 bg-emerald-500/10 px-4 py-2 text-sm text-emerald-100 transition hover:bg-emerald-500/20">
                <Download className="h-4 w-4" /> Download CSV
              </button>
            ) : null}
            {onDownloadReport ? (
              <button type="button" onClick={onDownloadReport} className="inline-flex items-center gap-2 rounded-2xl border border-rose-400/20 bg-rose-500/10 px-4 py-2 text-sm text-rose-100 transition hover:bg-rose-500/20">
                <FileDown className="h-4 w-4" /> Download Report
              </button>
            ) : null}
            {onDownloadPng ? (
              <button type="button" onClick={onDownloadPng} className="inline-flex items-center gap-2 rounded-2xl border border-cyan-400/20 bg-cyan-500/10 px-4 py-2 text-sm text-cyan-100 transition hover:bg-cyan-500/20">
                <Download className="h-4 w-4" /> Download Graphs as PNG
              </button>
            ) : null}
          </div>
        </div>
      </div>

      <SummaryCards
        recommendation={recommendation}
        riskLevel={riskLevel}
        confidence={confidence}
        responseMode={mode}
        toxicThreshold={toxicThreshold}
        activeZone={activeZone}
      />

      <PrimaryResponseChart data={normalized.doseResults} markers={normalized.markers} responseMode={mode} />

      <div className="grid gap-6 xl:grid-cols-2">
        <FiringRateChart data={normalized.doseResults} markers={normalized.markers} responseMode={mode} />
        <SeizureRiskChart data={normalized.doseResults} markers={normalized.markers} responseMode={mode} />
      </div>

      <div className="grid gap-6 xl:grid-cols-2">
        <SynchronizationChart data={normalized.doseResults} markers={normalized.markers} responseMode={mode} />
        <NiiChart data={normalized.doseResults} markers={normalized.markers} responseMode={mode} />
      </div>

      <ResponseZoneTimeline segments={normalized.timeline} markers={normalized.markers} responseModeLabel={responseModeLabel(mode)} />

      <AdvancedNeuroCharts data={normalized} />

      <div className="rounded-2xl border border-white/10 bg-white/5 px-5 py-4 text-sm text-slate-400">
        Zone and active-state cards are backend-authoritative. Therapeutic segments are rendered only when the analyzer validates a therapeutic window. Current range status: {validatedRange}.
        {chartData?.length ? ` Dose chart points available: ${chartData.length}.` : ''}
      </div>
    </motion.section>
  );
}
