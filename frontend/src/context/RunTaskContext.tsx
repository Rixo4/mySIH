import { createContext, useContext, useEffect, useMemo, useState } from 'react';
import { getRunDetail, getScientificJobStatus, runDrugEvaluation, runValidation } from '../api/client';
import type { BackendRunResponse, DrugEvalRequest } from '../types';
import { useAuth } from './AuthContext';

type TaskStatus = 'idle' | 'queued' | 'running' | 'completed' | 'failed';

interface ValidationState {
  status: TaskStatus;
  result: BackendRunResponse | null;
  error: string | null;
}

interface DrugEvaluationState {
  status: TaskStatus;
  payload: DrugEvalRequest;
  result: BackendRunResponse | null;
  error: string | null;
}

interface RunTaskContextValue {
  validationState: ValidationState;
  drugEvaluationState: DrugEvaluationState;
  runValidationTask: () => Promise<void>;
  runDrugEvaluationTask: (payload: DrugEvalRequest) => Promise<void>;
  setDrugEvaluationPayload: (payload: DrugEvalRequest) => void;
}

const VALIDATION_STORAGE_KEY = 'spp.validation.state';
const DRUG_EVAL_STORAGE_KEY_PREFIX = 'spp.drug-eval.state';

const initialState: ValidationState = {
  status: 'idle',
  result: null,
  error: null
};

const initialDrugEvalPayload: DrugEvalRequest = {
  drug_name: 'Test-K-Blocker',
  channels: {
    Na: { ic50: 200, hill: 3.2 },
    K: { ic50: 8, hill: 3.2 },
    Ca: { ic50: 1000, hill: 3.2 }
  },
  dose_range: {
    min: 0,
    max: 20,
    step: 2
  },
  runs: 3
};

const initialDrugEvaluationState: DrugEvaluationState = {
  status: 'idle',
  payload: initialDrugEvalPayload,
  result: null,
  error: null
};

function mapEngineError(response: BackendRunResponse): string {
  if (response.error === 'Engine execution timed out') {
    return 'Simulation timeout. Try Fast mode or fewer runs.';
  }
  if (response.error === 'Engine executable not found') {
    return 'Backend unreachable';
  }
  return response.error ?? 'Engine execution failed';
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

const RunTaskContext = createContext<RunTaskContextValue | undefined>(undefined);

export function RunTaskProvider({ children }: { children: React.ReactNode }) {
  const { user } = useAuth();
  const userStorageKey = user?.id != null ? `${DRUG_EVAL_STORAGE_KEY_PREFIX}.${user.id}` : `${DRUG_EVAL_STORAGE_KEY_PREFIX}.anonymous`;

  const [validationState, setValidationState] = useState<ValidationState>(() => {
    try {
      const raw = window.sessionStorage.getItem(VALIDATION_STORAGE_KEY);
      if (!raw) {
        return initialState;
      }
      const parsed = JSON.parse(raw) as ValidationState;
      if (!parsed || typeof parsed !== 'object') {
        return initialState;
      }
      return {
        status: parsed.status ?? 'idle',
        result: parsed.result ?? null,
        error: parsed.error ?? null
      };
    } catch {
      return initialState;
    }
  });

  const [drugEvaluationState, setDrugEvaluationState] = useState<DrugEvaluationState>(() => {
    try {
      const raw = window.sessionStorage.getItem(userStorageKey);
      if (!raw) {
        return initialDrugEvaluationState;
      }
      const parsed = JSON.parse(raw) as Partial<DrugEvaluationState>;
      if (!parsed || typeof parsed !== 'object') {
        return initialDrugEvaluationState;
      }

      const payload = parsed.payload ?? initialDrugEvalPayload;
      return {
        status: parsed.status ?? 'idle',
        payload,
        result: parsed.result ?? null,
        error: parsed.error ?? null
      };
    } catch {
      return initialDrugEvaluationState;
    }
  });

  useEffect(() => {
    window.sessionStorage.setItem(VALIDATION_STORAGE_KEY, JSON.stringify(validationState));
  }, [validationState]);

  useEffect(() => {
    window.sessionStorage.setItem(userStorageKey, JSON.stringify(drugEvaluationState));
  }, [drugEvaluationState, userStorageKey]);

  useEffect(() => {
    try {
      const raw = window.sessionStorage.getItem(userStorageKey);
      if (!raw) {
        setDrugEvaluationState(initialDrugEvaluationState);
        return;
      }
      const parsed = JSON.parse(raw) as Partial<DrugEvaluationState>;
      setDrugEvaluationState({
        status: parsed.status ?? 'idle',
        payload: parsed.payload ?? initialDrugEvalPayload,
        result: parsed.result ?? null,
        error: parsed.error ?? null
      });
    } catch {
      setDrugEvaluationState(initialDrugEvaluationState);
    }
  }, [userStorageKey]);

  const runValidationTask = async () => {
    setValidationState({ status: 'queued', result: null, error: null });

    try {
      const submission = await runValidation();
      setValidationState((current) => ({ ...current, status: 'running' }));
      const response = await waitForScientificJob(submission.job_id);

      setValidationState({ status: 'completed', result: response, error: null });
    } catch (err) {
      setValidationState({
        status: 'failed',
        result: null,
        error: err instanceof Error ? err.message : 'Backend unreachable'
      });
    }
  };

  const runDrugEvaluationTask = async (payload: DrugEvalRequest) => {
    setDrugEvaluationState((current) => ({
      ...current,
      status: 'queued',
      payload,
      result: null,
      error: null
    }));

    try {
      const submission = await runDrugEvaluation(payload);
      setDrugEvaluationState((current) => ({
        ...current,
        status: 'running'
      }));

      const response = await waitForScientificJob(submission.job_id);

      setDrugEvaluationState((current) => ({
        ...current,
        status: 'completed',
        payload,
        result: response,
        error: null
      }));
    } catch (err) {
      setDrugEvaluationState((current) => ({
        ...current,
        status: 'failed',
        payload,
        result: null,
        error: err instanceof Error ? err.message : 'Backend unreachable'
      }));
    }
  };

  const setDrugEvaluationPayload = (payload: DrugEvalRequest) => {
    setDrugEvaluationState((current) => ({
      ...current,
      payload
    }));
  };

  const value = useMemo(
    () => ({
      validationState,
      drugEvaluationState,
      runValidationTask,
      runDrugEvaluationTask,
      setDrugEvaluationPayload
    }),
    [validationState, drugEvaluationState]
  );

  return <RunTaskContext.Provider value={value}>{children}</RunTaskContext.Provider>;
}

export function useRunTask() {
  const context = useContext(RunTaskContext);
  if (!context) {
    throw new Error('useRunTask must be used inside RunTaskProvider');
  }
  return context;
}
