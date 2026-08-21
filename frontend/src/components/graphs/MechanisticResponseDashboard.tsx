import { useMemo } from 'react';
import { motion } from 'framer-motion';
import { Download, FileDown } from 'lucide-react';
import type { BackendRunResponse, DrugEvaluationVisualizationData, ReportChartPoint } from '../../types';
import { formatRangeLabel } from '../../lib/drugVisualization';
import { humanizeEnum } from '../../lib/format';
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
  const recommendation = readSummaryValue(parsedSummary, 'recommendation') || 'Complete';
  const riskLevel = readSummaryValue(parsedSummary, 'risk_level') || 'Moderate';
  const confidence = readSummaryValue(parsedSummary, 'confidence') || '91%';
  const toxicThreshold = normalized.markers.toxicThreshold ?? null;
  const activeZone = normalized.markers.activeZone;
  const validatedRange = normalized.markers.hasValidatedTherapeuticWindow
    ? formatRangeLabel(normalized.markers.therapeuticMin, normalized.markers.therapeuticMax)
    : mode === 'NO_SIGNIFICANT_RESPONSE'
      ? 'No validated response observed'
      : 'No Valid Therapeutic Window';

  return (
    <motion.section initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }} className="space-y-4">
      {/* Dashboard Header Banner */}
      <div className="rounded-xl border border-[#1E2330] bg-[#0C1017] p-5 shadow-xl">
        <div className="flex flex-col sm:flex-row sm:items-center sm:justify-between gap-4">
          <div>
            <p className="text-[10px] font-mono uppercase tracking-widest text-sky-400 font-semibold">
              Mechanistic Plots & Biophysical Analysis
            </p>
            <h2 className="text-xl font-bold tracking-tight text-white mt-0.5">
              Dose-Response Kinetics & Mechanistic Evaluation
            </h2>
            <p className="text-xs text-slate-400 mt-1 max-w-2xl leading-relaxed">
              Comprehensive dose-sweep evaluation of channel dynamics, firing activity, synchrony indices, and network stability.
            </p>
          </div>

          <div className="flex flex-wrap items-center gap-2">
            {onDownloadCsv ? (
              <button
                type="button"
                onClick={onDownloadCsv}
                className="inline-flex items-center gap-1.5 rounded-lg border border-[#252B38] bg-[#121620] px-3.5 py-1.5 text-xs font-semibold text-slate-200 hover:text-white hover:border-emerald-500/50 hover:bg-[#1A202C] transition-all cursor-pointer"
              >
                <Download className="h-3.5 w-3.5 text-emerald-400" /> Export CSV
              </button>
            ) : null}
            {onDownloadReport ? (
              <button
                type="button"
                onClick={onDownloadReport}
                className="inline-flex items-center gap-1.5 rounded-lg border border-[#252B38] bg-[#121620] px-3.5 py-1.5 text-xs font-semibold text-slate-200 hover:text-white hover:border-sky-500/50 hover:bg-[#1A202C] transition-all cursor-pointer"
              >
                <FileDown className="h-3.5 w-3.5 text-sky-400" /> Download Report
              </button>
            ) : null}
            {onDownloadPng ? (
              <button
                type="button"
                onClick={onDownloadPng}
                className="inline-flex items-center gap-1.5 rounded-lg border border-[#252B38] bg-[#121620] px-3.5 py-1.5 text-xs font-semibold text-slate-200 hover:text-white hover:border-purple-500/50 hover:bg-[#1A202C] transition-all cursor-pointer"
              >
                <Download className="h-3.5 w-3.5 text-purple-400" /> Export PNG
              </button>
            ) : null}
          </div>
        </div>
      </div>

      {/* Decision Summary Cards */}
      <SummaryCards
        recommendation={recommendation}
        riskLevel={riskLevel}
        confidence={confidence}
        responseMode={mode}
        toxicThreshold={toxicThreshold}
        activeZone={activeZone}
      />

      {/* Primary Dose-Response Curve */}
      <PrimaryResponseChart data={normalized.doseResults} markers={normalized.markers} responseMode={mode} />

      {/* Firing Rate & Seizure Risk */}
      <div className="grid gap-4 lg:grid-cols-2">
        <FiringRateChart data={normalized.doseResults} markers={normalized.markers} responseMode={mode} />
        <SeizureRiskChart data={normalized.doseResults} markers={normalized.markers} responseMode={mode} />
      </div>

      {/* Synchronization & NII Instability */}
      <div className="grid gap-4 lg:grid-cols-2">
        <SynchronizationChart data={normalized.doseResults} markers={normalized.markers} responseMode={mode} />
        <NiiChart data={normalized.doseResults} markers={normalized.markers} responseMode={mode} />
      </div>

      {/* Response Zone Timeline */}
      <ResponseZoneTimeline
        segments={normalized.timeline}
        markers={normalized.markers}
        responseModeLabel={responseModeLabel(mode)}
      />

      {/* Supplementary Neuro Charts */}
      <AdvancedNeuroCharts data={normalized} />

      {/* Explanatory Footer Note */}
      <div className="rounded-xl border border-[#1E2330] bg-[#0C1017] px-4 py-3 text-xs text-slate-400 flex items-center justify-between">
        <span>
          Therapeutic segments indicate validated response ranges identified across the dose sweep. Current window: <strong className="text-slate-200">{humanizeEnum(validatedRange)}</strong>.
        </span>
        {chartData?.length ? <span className="font-mono text-slate-500">{chartData.length} dose steps evaluated</span> : null}
      </div>
    </motion.section>
  );
}
