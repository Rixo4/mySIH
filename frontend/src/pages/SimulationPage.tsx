import { useState, useEffect } from 'react';
import { motion } from 'framer-motion';
import { Activity, AlertCircle } from 'lucide-react';
import { getRunDetail, getScientificJobStatus, runSimulation } from '../api/client';
import { LoadingEngineState } from '../components/LoadingEngineState';
import { MetricCard } from '../components/MetricCard';
import { ReportViewer } from '../components/ReportViewer';
import { StatusBadge } from '../components/StatusBadge';
import { SingleSimulationForm } from '../components/SingleSimulationForm';
import type { BackendRunResponse, SingleSimulationRequest } from '../types';

const SIMULATION_STORAGE_KEY = 'spp.simulation.state';

interface SimulationState {
  payload: SingleSimulationRequest;
  result: BackendRunResponse | null;
  loading: boolean;
  error: string | null;
}

const defaultPayload: SingleSimulationRequest = {
  drug_name: 'Test-Na-Modulator',
  channels: {
    Na: { ic50: 200, hill: 3.2 },
    K: { ic50: 8, hill: 3.2 },
    Ca: { ic50: 1000, hill: 3.2 }
  },
  dose: 10,
  mode: 'accurate'
};

function summaryValue(result: BackendRunResponse | null, key: string): string {
  const value = result?.parsed_summary?.[key];
  return typeof value === 'string' ? value : typeof value === 'number' ? String(value) : '—';
}

function mapRunDetailToBackendResponse(detail: Awaited<ReturnType<typeof getRunDetail>>): BackendRunResponse {
  return {
    run_id: detail.run_id,
    status: detail.status,
    report_type: detail.report_type,
    engine_input_mode: detail.engine_input_mode,
    parsed_summary: detail.parsed_summary,
    visualization_data: detail.visualization_data ?? null,
    raw_report: detail.raw_report,
    duration_seconds: detail.duration_seconds,
    created_at: detail.created_at,
    error: detail.error_message,
    stderr: null
  };
}

async function waitForScientificJob(jobId: string): Promise<BackendRunResponse> {
  const startedAt = Date.now();
  const pollIntervalMs = 2000;
  const timeoutMs = 60 * 60 * 1000;

  while (Date.now() - startedAt < timeoutMs) {
    const job = await getScientificJobStatus(jobId);
    if (job.status === 'FAILED' || job.status === 'CANCELLED') {
      throw new Error(job.error_message ?? 'Simulation failed');
    }

    if (job.status === 'COMPLETED') {
      if (!job.result_run_id) {
        throw new Error('Completed job did not return a run record');
      }
      const detail = await getRunDetail(job.result_run_id);
      return mapRunDetailToBackendResponse(detail);
    }

    await new Promise((resolve) => window.setTimeout(resolve, pollIntervalMs));
  }

  throw new Error('Simulation timeout. Try fewer runs.');
}

export function SimulationPage() {
  const [payload, setPayloadState] = useState<SingleSimulationRequest>(() => {
    try {
      const stored = window.sessionStorage.getItem(SIMULATION_STORAGE_KEY);
      if (!stored) return defaultPayload;
      const parsed = JSON.parse(stored) as Partial<SimulationState>;
      return (parsed.payload as SingleSimulationRequest) || defaultPayload;
    } catch {
      return defaultPayload;
    }
  });

  const [result, setResultState] = useState<BackendRunResponse | null>(() => {
    try {
      const stored = window.sessionStorage.getItem(SIMULATION_STORAGE_KEY);
      if (!stored) return null;
      const parsed = JSON.parse(stored) as Partial<SimulationState>;
      return (parsed.result as BackendRunResponse) || null;
    } catch {
      return null;
    }
  });

  const [loading, setLoadingState] = useState(false);
  const [activeStep, setActiveStep] = useState(0);
  const [error, setErrorState] = useState<string | null>(() => {
    try {
      const stored = window.sessionStorage.getItem(SIMULATION_STORAGE_KEY);
      if (!stored) return null;
      const parsed = JSON.parse(stored) as Partial<SimulationState>;
      return (parsed.error as string) || null;
    } catch {
      return null;
    }
  });

  // Persist state to sessionStorage whenever any state changes
  useEffect(() => {
    const state: SimulationState = {
      payload,
      result,
      loading,
      error
    };
    window.sessionStorage.setItem(SIMULATION_STORAGE_KEY, JSON.stringify(state));
  }, [payload, result, loading, error]);

  const setPayload = (next: SingleSimulationRequest) => {
    setPayloadState(next);
  };

  const setResult = (next: BackendRunResponse | null) => {
    setResultState(next);
  };

  const setLoading = (next: boolean) => {
    setLoadingState(next);
  };

  const setError = (next: string | null) => {
    setErrorState(next);
  };

  const startSimulation = async (nextPayload: SingleSimulationRequest) => {
    if (!nextPayload.drug_name.trim()) {
      setError('Invalid drug input');
      return;
    }

    const channels = ['Na', 'K', 'Ca'] as const;
    for (const channel of channels) {
      const params = nextPayload.channels[channel];
      if (params.ic50 <= 0 || params.hill <= 0) {
        setError('Invalid drug input');
        return;
      }
    }

    if (nextPayload.dose < 0) {
      setError('Invalid drug input');
      return;
    }

    setLoading(true);
    setError(null);
    setResult(null);
    setActiveStep(0);

    const timer = window.setInterval(() => {
      setActiveStep((step) => (step + 1) % 5);
    }, 1100);

    try {
      const submission = await runSimulation(nextPayload);
      const response = await waitForScientificJob(submission.job_id);
      setResult(response);
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Backend unreachable');
    } finally {
      window.clearInterval(timer);
      setLoading(false);
      setActiveStep(4);
    }
  };

  return (
    <div className="space-y-6 pb-24 xl:pb-8">
      <motion.div initial={{ opacity: 0, y: 12 }} animate={{ opacity: 1, y: 0 }} className="glass-card p-6">
        <div className="flex flex-col gap-4">
          <div>
            <p className="text-xs uppercase tracking-[0.32em] text-cyan-300/70">Single Dose Simulation</p>
            <h2 className="mt-2 text-3xl font-semibold text-white">Run a single-dose neural simulation on the CUDA engine</h2>
            <p className="mt-3 max-w-3xl text-sm leading-7 text-slate-400">
              Configure one drug profile and one dose input, then execute a single run for neural activity, seizure risk, and toxicity assessment.
            </p>
          </div>
          <SingleSimulationForm value={payload} onChange={setPayload} onSubmit={startSimulation} loading={loading} error={error} />
        </div>
      </motion.div>

      {loading ? <LoadingEngineState title="Running single-dose simulation..." subtext="The engine is simulating a single configured dose and deriving the report." activeStep={activeStep} /> : null}

      {!payload.drug_name.trim() && !loading ? (
        <div className="rounded-2xl border border-amber-400/20 bg-amber-500/10 px-5 py-4 text-sm text-amber-200">
          Provide a valid drug input profile to start simulation.
        </div>
      ) : null}

      {error ? (
        <div className="rounded-2xl border border-rose-400/20 bg-rose-500/10 px-5 py-4 text-sm text-rose-200">
          <div className="flex items-center gap-2 font-semibold">
            <AlertCircle className="h-4 w-4" /> {error}
          </div>
        </div>
      ) : null}

      {result ? (
        <div className="space-y-5">
          <StatusBadge label="Simulation Status" value={result.status === 'completed' ? 'Completed' : 'Failed'} />
          <div className="grid gap-4 md:grid-cols-2 xl:grid-cols-3">
            <MetricCard label="Firing Rate" value={`${summaryValue(result, 'firing_rate')} Hz`} />
            <MetricCard label="Synchronization" value={summaryValue(result, 'synchronization')} />
            <MetricCard label="ISI Variability" value={summaryValue(result, 'isi_variability')} />
            <MetricCard label="Brain State" value={summaryValue(result, 'brain_state')} />
            <MetricCard label="Seizure Risk" value={summaryValue(result, 'seizure_risk')} />
            <MetricCard label="Toxicity Risk" value={summaryValue(result, 'toxicity_risk')} />
          </div>
          <div className="glass-card p-6">
            <p className="text-xs uppercase tracking-[0.32em] text-cyan-300/70">Final Recommendation</p>
            <h3 className="mt-2 text-2xl font-semibold text-white">{summaryValue(result, 'recommendation')}</h3>
          </div>
          {result.raw_report ? <ReportViewer title="Single Dose Simulation Report" report={result.raw_report} /> : null}
        </div>
      ) : null}
    </div>
  );
}
