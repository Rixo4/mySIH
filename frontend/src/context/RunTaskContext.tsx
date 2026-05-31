import { createContext, useContext, useEffect, useMemo, useRef, useState } from 'react';
import { cancelScientificJob, getRunDetail, getScientificJobStatus, runDrugEvaluation, runValidation } from '../api/client';
import type { BackendRunResponse, DrugEvalRequest } from '../types';
import { useAuth } from './AuthContext';

type TaskStatus = 'idle' | 'queued' | 'running' | 'completed' | 'failed' | 'cancelled';

interface ValidationState {
  status: TaskStatus;
  result: BackendRunResponse | null;
  error: string | null;
}

interface DrugEvaluationState {
  status: TaskStatus;
  jobId: string | null;
  payload: DrugEvalRequest;
  result: BackendRunResponse | null;
  error: string | null;
}

interface RunTaskContextValue {
  validationState: ValidationState;
  drugEvaluationState: DrugEvaluationState;
  runValidationTask: () => Promise<void>;
  runDrugEvaluationTask: (payload: DrugEvalRequest) => Promise<void>;
  cancelDrugEvaluationTask: () => Promise<void>;
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
  jobId: null,
  payload: initialDrugEvalPayload,
  result: null,
  error: null
};

class CancellationError extends Error {
  constructor() {
    super('Simulation cancelled');
    this.name = 'CancellationError';
  }
}

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

function parseDrugEvaluationState(raw: string | null): DrugEvaluationState {
  if (!raw) {
    return initialDrugEvaluationState;
  }

  const parsed = JSON.parse(raw) as Partial<DrugEvaluationState>;
  if (!parsed || typeof parsed !== 'object') {
    return initialDrugEvaluationState;
  }

  return {
    status: parsed.status ?? 'idle',
    jobId: typeof parsed.jobId === 'string' ? parsed.jobId : null,
    payload: parsed.payload ?? initialDrugEvalPayload,
    result: parsed.result ?? null,
    error: parsed.error ?? null
  };
}

async function waitForScientificJob(jobId: string, shouldStop: () => boolean): Promise<BackendRunResponse> {
  const startedAt = Date.now();
  const pollIntervalMs = 2000;
  const timeoutMs = 60 * 60 * 1000;

  while (Date.now() - startedAt < timeoutMs) {
    if (shouldStop()) {
      throw new CancellationError();
    }

    const job = await getScientificJobStatus(jobId);

    if (job.status === 'FAILED' || job.status === 'CANCELLED') {
      if (job.status === 'CANCELLED') {
        throw new CancellationError();
      }
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
  const activeDrugEvaluationJobIdRef = useRef<string | null>(null);
  const cancelledDrugEvaluationJobIdRef = useRef<string | null>(null);

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
      return parseDrugEvaluationState(window.sessionStorage.getItem(userStorageKey));
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
      setDrugEvaluationState(parseDrugEvaluationState(raw));
    } catch {
      setDrugEvaluationState(initialDrugEvaluationState);
    }
  }, [userStorageKey]);

  useEffect(() => {
    if (drugEvaluationState.status !== 'queued' && drugEvaluationState.status !== 'running') {
      return;
    }

    if (!drugEvaluationState.jobId || activeDrugEvaluationJobIdRef.current === drugEvaluationState.jobId) {
      return;
    }

    let active = true;
    const jobId = drugEvaluationState.jobId;
    activeDrugEvaluationJobIdRef.current = jobId;

    const resumeJob = async () => {
      try {
        setDrugEvaluationState((current) => ({
          ...current,
          status: 'running',
          jobId,
          error: null
        }));

        const response = await waitForScientificJob(jobId, () => cancelledDrugEvaluationJobIdRef.current === jobId);
        if (!active) {
          return;
        }

        setDrugEvaluationState((current) => ({
          ...current,
          status: 'completed',
          jobId: null,
          result: response,
          error: null
        }));
      } catch (err) {
        if (!active) {
          return;
        }

        if (err instanceof CancellationError) {
          setDrugEvaluationState((current) => ({
            ...current,
            status: 'cancelled',
            jobId: null,
            result: null,
            error: null
          }));
          return;
        }

        setDrugEvaluationState((current) => ({
          ...current,
          status: 'failed',
          jobId: null,
          result: null,
          error: err instanceof Error ? err.message : 'Backend unreachable'
        }));
      } finally {
        if (activeDrugEvaluationJobIdRef.current === jobId) {
          activeDrugEvaluationJobIdRef.current = null;
        }
      }
    };

    void resumeJob();

    return () => {
      active = false;
    };
  }, [drugEvaluationState.jobId, drugEvaluationState.status]);

  const runValidationTask = async () => {
    setValidationState({ status: 'queued', result: null, error: null });

    try {
      const submission = await runValidation();
      setValidationState((current) => ({ ...current, status: 'running' }));
      const response = await waitForScientificJob(submission.job_id, () => false);

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
      jobId: null,
      payload,
      result: null,
      error: null
    }));

    try {
      const submission = await runDrugEvaluation(payload);
      setDrugEvaluationState((current) => ({
        ...current,
        status: 'running',
        jobId: submission.job_id,
        payload,
        error: null
      }));

      activeDrugEvaluationJobIdRef.current = submission.job_id;
      cancelledDrugEvaluationJobIdRef.current = null;

      const response = await waitForScientificJob(submission.job_id, () => cancelledDrugEvaluationJobIdRef.current === submission.job_id);

      setDrugEvaluationState((current) => ({
        ...current,
        status: 'completed',
        jobId: null,
        payload,
        result: response,
        error: null
      }));
    } catch (err) {
      if (err instanceof CancellationError) {
        setDrugEvaluationState((current) => ({
          ...current,
          status: 'cancelled',
          jobId: null,
          result: null,
          error: null
        }));
        return;
      }

      setDrugEvaluationState((current) => ({
        ...current,
        status: 'failed',
        jobId: null,
        payload,
        result: null,
        error: err instanceof Error ? err.message : 'Backend unreachable'
      }));
    } finally {
      if (activeDrugEvaluationJobIdRef.current) {
        activeDrugEvaluationJobIdRef.current = null;
      }
    }
  };

  const cancelDrugEvaluationTask = async () => {
    const jobId = drugEvaluationState.jobId;
    if (!jobId) {
      return;
    }

    cancelledDrugEvaluationJobIdRef.current = jobId;
    try {
      await cancelScientificJob(jobId);
    } catch {
      // If the backend cancellation fails, still stop polling locally.
    }

    setDrugEvaluationState((current) => ({
      ...current,
      status: 'cancelled',
      jobId: null,
      result: null,
      error: null
    }));

    if (activeDrugEvaluationJobIdRef.current === jobId) {
      activeDrugEvaluationJobIdRef.current = null;
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
      cancelDrugEvaluationTask,
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
