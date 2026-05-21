import { createContext, useContext, useEffect, useMemo, useState } from 'react';
import { runDrugEvaluation, runValidation } from '../api/client';
import type { BackendRunResponse, DrugEvalRequest, EngineMode } from '../types';

type TaskStatus = 'idle' | 'running' | 'completed' | 'failed';

interface ValidationState {
  status: TaskStatus;
  result: BackendRunResponse | null;
  error: string | null;
}

interface DrugEvaluationState {
  status: TaskStatus;
  payload: DrugEvalRequest;
  engineMode: EngineMode;
  result: BackendRunResponse | null;
  error: string | null;
}

interface RunTaskContextValue {
  validationState: ValidationState;
  drugEvaluationState: DrugEvaluationState;
  runValidationTask: () => Promise<void>;
  runDrugEvaluationTask: (payload: DrugEvalRequest) => Promise<void>;
  setDrugEvaluationPayload: (payload: DrugEvalRequest) => void;
  setDrugEvaluationMode: (mode: EngineMode) => void;
}

const VALIDATION_STORAGE_KEY = 'spp.validation.state';
const DRUG_EVAL_STORAGE_KEY = 'spp.drug-eval.state';

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
  engineMode: 'fast',
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

const RunTaskContext = createContext<RunTaskContextValue | undefined>(undefined);

export function RunTaskProvider({ children }: { children: React.ReactNode }) {
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
      const raw = window.sessionStorage.getItem(DRUG_EVAL_STORAGE_KEY);
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
        engineMode: parsed.engineMode ?? 'fast',
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
    window.sessionStorage.setItem(DRUG_EVAL_STORAGE_KEY, JSON.stringify(drugEvaluationState));
  }, [drugEvaluationState]);

  const runValidationTask = async () => {
    setValidationState({ status: 'running', result: null, error: null });

    try {
      const response = await runValidation();
      if (response.status === 'failed') {
        const message =
          response.error === 'Engine execution timed out'
            ? 'Simulation timeout. Try Fast mode or fewer runs.'
            : response.error ?? 'Engine execution failed';
        setValidationState({ status: 'failed', result: response, error: message });
        return;
      }

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
      status: 'running',
      payload,
      result: null,
      error: null
    }));

    try {
      const response = await runDrugEvaluation(payload);
      if (response.status === 'failed') {
        setDrugEvaluationState((current) => ({
          ...current,
          status: 'failed',
          payload,
          result: response,
          error: mapEngineError(response)
        }));
        return;
      }

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

  const setDrugEvaluationMode = (mode: EngineMode) => {
    setDrugEvaluationState((current) => ({
      ...current,
      engineMode: mode,
      payload: {
        ...current.payload,
        runs: mode === 'fast' ? Math.min(current.payload.runs, 3) || 1 : Math.max(current.payload.runs, 3)
      }
    }));
  };

  const value = useMemo(
    () => ({
      validationState,
      drugEvaluationState,
      runValidationTask,
      runDrugEvaluationTask,
      setDrugEvaluationPayload,
      setDrugEvaluationMode
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
