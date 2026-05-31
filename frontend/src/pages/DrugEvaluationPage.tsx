import { useEffect, useMemo, useState } from 'react';
import { motion } from 'framer-motion';
import { DrugInputForm } from '../components/DrugInputForm';
import { LoadingEngineState } from '../components/LoadingEngineState';
import { ReportViewer } from '../components/ReportViewer';
import { StatusBadge } from '../components/StatusBadge';
import { MechanisticResponseDashboard } from '../components/graphs/MechanisticResponseDashboard';
import { buildDoseChartData } from '../lib/charts';
import { buildFallbackVisualizationData, downloadCsv, downloadSvgAsPng, downloadTextReport, getVisualizationData } from '../lib/drugVisualization';
import type { BackendRunResponse, DrugEvalRequest, DrugEvaluationVisualizationData, ReportChartPoint } from '../types';
import { useRunTask } from '../context/RunTaskContext';

function validatePayload(payload: DrugEvalRequest): string | null {
  if (!payload.drug_name.trim()) {
    return 'Invalid drug input';
  }

  const channels = ['Na', 'K', 'Ca'] as const;
  for (const channel of channels) {
    const params = payload.channels[channel];
    if (params.ic50 <= 0 || params.hill <= 0) {
      return 'Invalid drug input';
    }
  }

  if (payload.dose_range.min < 0 || payload.dose_range.max <= payload.dose_range.min || payload.dose_range.step <= 0) {
    return 'Invalid drug input';
  }

  if (payload.runs < 1 || payload.runs > 10) {
    return 'Invalid drug input';
  }

  return null;
}

function getSummaryValue(result: BackendRunResponse | null, key: string): string | null {
  const value = result?.parsed_summary?.[key];

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

function getResponseMode(result: BackendRunResponse | null): string {
  const responseMode = getSummaryValue(result, 'response_mode');
  return responseMode ? responseMode.toUpperCase() : '';
}

export function DrugEvaluationPage() {
  const { drugEvaluationState, runDrugEvaluationTask, cancelDrugEvaluationTask, setDrugEvaluationPayload } = useRunTask();

  const payload = drugEvaluationState.payload;
  const loading = drugEvaluationState.status === 'queued' || drugEvaluationState.status === 'running';
  const result = drugEvaluationState.result;
  const error = drugEvaluationState.error;
  const responseMode = getResponseMode(result);

  const [activeStep, setActiveStep] = useState(0);
  const [clientError, setClientError] = useState<string | null>(null);

  const chartData: ReportChartPoint[] = useMemo(() => {
    if (!result?.raw_report && result?.status !== 'completed') {
      return [];
    }

    return buildDoseChartData(
      payload.dose_range.min,
      payload.dose_range.max,
      payload.dose_range.step,
      getSummaryValue(result, 'effective_range'),
      getSummaryValue(result, 'risk_level'),
      getSummaryValue(result, 'toxic_threshold'),
      responseMode,
      Number(getSummaryValue(result, 'max_effect'))
    );
  }, [payload, result]);

  const visualizationData: DrugEvaluationVisualizationData = useMemo(() => {
    const fallback = buildFallbackVisualizationData(chartData, payload, responseMode, {
      toxicThreshold: getSummaryValue(result, 'toxic_threshold'),
      stabilizationRange: getSummaryValue(result, 'stabilization_range')
    });
    return getVisualizationData(result?.visualization_data, fallback);
  }, [chartData, payload, responseMode, result]);

  const run = async (nextPayload: DrugEvalRequest) => {
    const validationError = validatePayload(nextPayload);
    if (validationError) {
      setClientError(validationError);
      setDrugEvaluationPayload(nextPayload);
      return;
    }

    setClientError(null);
    setActiveStep(0);
    await runDrugEvaluationTask(nextPayload);
  };

  useEffect(() => {
    if (!loading) {
      if (drugEvaluationState.status === 'completed' || drugEvaluationState.status === 'failed') {
        setActiveStep(4);
      }
      return;
    }

    const timer = window.setInterval(() => {
      setActiveStep((step) => (step + 1) % 5);
    }, 1100);

    return () => {
      window.clearInterval(timer);
    };
  }, [loading, drugEvaluationState.status]);

  const reportText = result?.raw_report ?? '';
  const summaryAvailable = result?.status === 'completed' || Boolean(reportText);

  const exportGraphs = async () => {
    const chartIds = [
      'spp-mechanistic-primary',
      'spp-mechanistic-firing',
      'spp-mechanistic-seizure',
      'spp-mechanistic-sync',
      'spp-mechanistic-nii',
      'spp-mechanistic-voltage',
      'spp-mechanistic-raster',
      'spp-mechanistic-variance'
    ];

    let exported = 0;
    try {
      for (const chartId of chartIds) {
        const element = document.getElementById(chartId);
        if (element) {
          // Sequential export keeps browser download behavior stable.
          // eslint-disable-next-line no-await-in-loop
          await downloadSvgAsPng(element as HTMLElement, `${result?.run_id ?? 'silicon-patient'}-${chartId.replace('spp-mechanistic-', '')}.png`);
          exported += 1;
        }
      }
    } catch (err) {
      const msg = err instanceof Error ? err.message : 'Export failed';
      window.alert(`PNG export failed: ${msg}`);
      return;
    }

    if (exported === 0) {
      window.alert('No chart images found to export. Open the visualization dashboard (run a dose-eval) and try again.');
    }
  };

  return (
    <div className="space-y-6 pb-24 xl:pb-8">
      <motion.div initial={{ opacity: 0, y: 12 }} animate={{ opacity: 1, y: 0 }} className="glass-card p-6 shadow-panel">
        <div className="flex flex-col gap-4 lg:flex-row lg:items-center lg:justify-between">
          <div>
            <p className="text-xs uppercase tracking-[0.32em] text-cyan-300/70">Drug Evaluation</p>
            <h2 className="mt-2 text-3xl font-semibold text-white">Design, validate, and evaluate ion-channel pharmacology</h2>
            <p className="mt-3 max-w-3xl text-sm leading-7 text-slate-400">
              Enter the ion-channel profile and dose sweep. The backend will run the live CUDA engine and return a structured report.
            </p>
          </div>
          <StatusBadge label="Response Mode" value={responseMode || 'UNSPECIFIED'} />
        </div>

        <div className="mt-5 rounded-2xl border border-emerald-400/20 bg-emerald-500/10 px-4 py-3 text-sm text-emerald-100">
          Runtime input active: your Na/K/Ca IC50 values, hill coefficient, dose range, and runs are applied to this
          <span className="font-mono"> --dose-eval </span>
          execution.
        </div>
      </motion.div>

      {loading ? <LoadingEngineState activeStep={activeStep} onStop={() => void cancelDrugEvaluationTask()} /> : null}

      <div className="grid gap-6">
        <DrugInputForm
          value={payload}
          loading={loading}
          onChange={setDrugEvaluationPayload}
          onSubmit={run}
          error={clientError ?? error}
        />
      </div>

      {summaryAvailable ? (
        <MechanisticResponseDashboard
          visualizationData={visualizationData}
          parsedSummary={result?.parsed_summary}
          reportText={reportText}
          responseMode={responseMode}
          chartData={chartData}
          onDownloadCsv={() => downloadCsv(visualizationData.dose_results, `${result?.run_id ?? 'silicon-patient'}-dose-results.csv`)}
          onDownloadReport={() => downloadTextReport(reportText, `${result?.run_id ?? 'silicon-patient'}-report.txt`)}
          onDownloadPng={() => void exportGraphs()}
        />
      ) : null}

      {reportText ? <ReportViewer title="Drug Evaluation Report" report={reportText} /> : null}
    </div>
  );
}
