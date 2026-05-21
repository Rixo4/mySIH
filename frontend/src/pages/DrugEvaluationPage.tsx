import { useEffect, useMemo, useState } from 'react';
import { motion } from 'framer-motion';
import { AlertCircle, Rocket, Sigma } from 'lucide-react';
import { ChartPanel } from '../components/ChartPanel';
import { DrugInputForm } from '../components/DrugInputForm';
import { LoadingEngineState } from '../components/LoadingEngineState';
import { MetricCard } from '../components/MetricCard';
import { ReportViewer } from '../components/ReportViewer';
import { StatusBadge } from '../components/StatusBadge';
import { buildDoseChartData } from '../lib/charts';
import type { BackendRunResponse, DrugEvalRequest, ReportChartPoint } from '../types';
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

  if (payload.runs < 1 || payload.runs > 20) {
    return 'Invalid drug input';
  }

  return null;
}

function getSummaryValue(result: BackendRunResponse | null, key: string): string {
  const value = result?.parsed_summary?.[key];
  return typeof value === 'string' ? value : typeof value === 'number' ? String(value) : '—';
}

export function DrugEvaluationPage() {
  const {
    drugEvaluationState,
    runDrugEvaluationTask,
    setDrugEvaluationPayload,
    setDrugEvaluationMode
  } = useRunTask();

  const payload = drugEvaluationState.payload;
  const engineMode = drugEvaluationState.engineMode;
  const loading = drugEvaluationState.status === 'running';
  const result = drugEvaluationState.result;
  const error = drugEvaluationState.error;

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
      getSummaryValue(result, 'toxic_threshold')
    );
  }, [payload, result]);

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

  return (
    <div className="space-y-6 pb-24 xl:pb-8">
      <motion.div initial={{ opacity: 0, y: 12 }} animate={{ opacity: 1, y: 0 }} className="glass-card p-6 shadow-panel">
        <div className="flex flex-col gap-4 lg:flex-row lg:items-center lg:justify-between">
          <div>
            <p className="text-xs uppercase tracking-[0.32em] text-cyan-300/70">Drug Evaluation</p>
            <h2 className="mt-2 text-3xl font-semibold text-white">Design, validate, and evaluate ion-channel pharmacology</h2>
            <p className="mt-3 max-w-3xl text-sm leading-7 text-slate-400">
              Enter the ion-channel profile, dose sweep, and execution settings. The backend will run the live CUDA engine and return a structured report.
            </p>
          </div>
          <StatusBadge label="Execution Mode" value={engineMode === 'fast' ? 'FAST' : 'ACCURATE'} />
        </div>

        <div className="mt-5 rounded-2xl border border-emerald-400/20 bg-emerald-500/10 px-4 py-3 text-sm text-emerald-100">
          Runtime input active: your Na/K/Ca IC50 values, hill coefficient, dose range, and runs are applied to this
          <span className="font-mono"> --dose-eval </span>
          execution.
        </div>
      </motion.div>

      {loading ? (
        <LoadingEngineState activeStep={activeStep} />
      ) : null}

      <div className="grid gap-6 xl:grid-cols-[1.12fr_0.88fr]">
        <DrugInputForm
          value={payload}
          engineMode={engineMode}
          loading={loading}
          onModeChange={setDrugEvaluationMode}
          onChange={setDrugEvaluationPayload}
          onSubmit={run}
          error={clientError ?? error}
        />

        <div className="space-y-5">
          <div className="glass-card p-6">
            <div className="flex items-center justify-between gap-3">
              <div>
                <p className="text-xs uppercase tracking-[0.32em] text-cyan-300/70">Execution Summary</p>
                <h3 className="mt-1 text-lg font-semibold text-white">Run Output</h3>
              </div>
              <Rocket className="h-5 w-5 text-cyan-300" />
            </div>
            <div className="mt-5 grid gap-4 sm:grid-cols-2">
              <MetricCard label="Recommendation" value={getSummaryValue(result, 'recommendation')} helper="Decision returned by backend parser" icon={<Sigma className="h-5 w-5" />} />
              <MetricCard label="Risk Level" value={getSummaryValue(result, 'risk_level')} helper="Clinical risk classification" />
              <MetricCard label="Confidence" value={getSummaryValue(result, 'confidence')} helper="Decision confidence band" />
              <MetricCard label="Therapeutic Window" value={getSummaryValue(result, 'effective_range')} helper="Dose interval with desired effect" />
              <MetricCard label="Toxic Threshold" value={getSummaryValue(result, 'toxic_threshold')} helper="Highest safe threshold observed" />
              <MetricCard label="Stability Score" value={getSummaryValue(result, 'stability_score')} helper="Batch variability assessment" />
            </div>
          </div>

          {error ? (
            <div className="rounded-2xl border border-rose-400/20 bg-rose-500/10 px-5 py-4 text-sm text-rose-200">
              <div className="flex items-center gap-2 font-semibold">
                <AlertCircle className="h-4 w-4" /> {error}
              </div>
            </div>
          ) : null}

          {summaryAvailable ? <ChartPanel data={chartData} /> : null}
        </div>
      </div>

      {reportText ? <ReportViewer title="Drug Evaluation Report" report={reportText} /> : null}
    </div>
  );
}
