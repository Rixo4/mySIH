import axios from 'axios';
import type {
  BackendRunResponse,
  DeleteRunResponse,
  DrugEvalRequest,
  HealthResponse,
  RunDetailResponse,
  SingleSimulationRequest,
  RunsListResponse
} from '../types';

const baseURL = import.meta.env.VITE_API_BASE_URL ?? 'http://localhost:8000';

export const api = axios.create({
  baseURL,
  timeout: 900000,
  headers: {
    'Content-Type': 'application/json'
  }
});
// Frontend default timeout is in milliseconds. Increased to 1 hour to allow long engine runs.
api.defaults.timeout = 3600000;

let currentAccessToken: string | null = null;
export function setAccessToken(token: string | null) {
  currentAccessToken = token;
}

api.interceptors.request.use((cfg) => {
  if (currentAccessToken) {
    cfg.headers = cfg.headers ?? {};
    cfg.headers.Authorization = `Bearer ${currentAccessToken}`;
  }
  return cfg;
});

export async function getHealth(): Promise<HealthResponse> {
  const response = await api.get<HealthResponse>('/health');
  return response.data;
}

export async function runSimulation(payload?: SingleSimulationRequest): Promise<BackendRunResponse> {
  const response = await api.post<BackendRunResponse>('/api/simulate', payload ?? {});
  return response.data;
}

export async function runValidation(): Promise<BackendRunResponse> {
  // Internal developer endpoint - not part of public workflows
  const response = await api.post<BackendRunResponse>('/api/internal/validate');
  return response.data;
}

export async function runDrugEvaluation(payload: DrugEvalRequest): Promise<BackendRunResponse> {
  const response = await api.post<BackendRunResponse>('/api/dose-eval', payload);
  return response.data;
}

export async function getRuns(): Promise<RunsListResponse> {
  const response = await api.get<RunsListResponse>('/api/runs');
  return response.data;
}

export async function getRunDetail(runId: string): Promise<RunDetailResponse> {
  const response = await api.get<RunDetailResponse>(`/api/runs/${encodeURIComponent(runId)}`);
  return response.data;
}

export async function getRunReport(runId: string): Promise<string> {
  const response = await api.get<string>(`/api/runs/${encodeURIComponent(runId)}/report`, {
    responseType: 'text'
  });
  return response.data;
}

export async function deleteRun(runId: string): Promise<DeleteRunResponse> {
  const response = await api.delete<DeleteRunResponse>(`/api/runs/${encodeURIComponent(runId)}`);
  return response.data;
}
