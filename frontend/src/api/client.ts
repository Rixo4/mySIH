import axios from 'axios';
import type {
  BackendRunResponse,
  DeleteRunResponse,
  DrugEvalRequest,
  HealthResponse,
  RunDetailResponse,
  ScientificSimulationStatusResponse,
  ScientificSimulationSubmitResponse,
  SingleSimulationRequest,
  RunsListResponse
} from '../types';

const baseURL = import.meta.env.VITE_API_BASE_URL ?? 'http://localhost:8000';

export const api = axios.create({
  baseURL,
  timeout: 900000,
  withCredentials: true,
  headers: {
    'Content-Type': 'application/json'
  }
});
// Frontend default timeout is in milliseconds. Increased to 1 hour to allow long engine runs.
api.defaults.timeout = 3600000;

let currentAccessToken: string | null = null;
let unauthorizedHandler: (() => void) | null = null;

let isRefreshing = false;
let refreshSubscribers: Array<(token: string | null) => void> = [];

function getCookie(name: string): string | null {
  if (typeof document === 'undefined') return null;
  const match = document.cookie.match(new RegExp('(?:^|; )' + name.replace(/([.$?*|{}()\[\]\\/+^])/g, '\\$1') + '=([^;]*)'));
  return match ? decodeURIComponent(match[1]) : null;
}

function subscribeTokenRefresh(cb: (token: string | null) => void) {
  refreshSubscribers.push(cb);
}

function onRefreshed(token: string | null) {
  refreshSubscribers.forEach((cb) => cb(token));
  refreshSubscribers = [];
}

async function refreshAccessToken(): Promise<string | null> {
  if (isRefreshing) {
    return new Promise((resolve) => subscribeTokenRefresh(resolve));
  }
  isRefreshing = true;
  try {
    const csrf = getCookie('spp_csrf');
    const headers: Record<string, string> = {};
    if (csrf) headers['x-csrf-token'] = csrf;
    // Use bare axios to avoid interceptor recursion
    const res = await axios.request({
      method: 'post',
      url: `${baseURL}/auth/refresh`,
      withCredentials: true,
      headers
    });
    const token = res.data?.access_token || res.data?.accessToken || null;
    setAccessToken(token);
    onRefreshed(token);
    return token;
  } catch (err) {
    onRefreshed(null);
    setAccessToken(null);
    return null;
  } finally {
    isRefreshing = false;
  }
}

export async function tryRefresh(): Promise<string | null> {
  return refreshAccessToken();
}

export function setAccessToken(token: string | null) {
  currentAccessToken = token;
}

export function setUnauthorizedHandler(handler: (() => void) | null) {
  unauthorizedHandler = handler;
}

api.interceptors.request.use((cfg) => {
  if (currentAccessToken) {
    cfg.headers = cfg.headers ?? {};
    cfg.headers.Authorization = `Bearer ${currentAccessToken}`;
  }
  return cfg;
});

api.interceptors.response.use(
  (response) => response,
  (error) => {
    const originalRequest = error.config;
    if (axios.isAxiosError(error) && error.response?.status === 401 && originalRequest && !originalRequest._retry) {
      originalRequest._retry = true;
      return refreshAccessToken().then((newToken) => {
        if (newToken) {
          originalRequest.headers = originalRequest.headers ?? {};
          originalRequest.headers.Authorization = `Bearer ${newToken}`;
          return api(originalRequest);
        }
        unauthorizedHandler?.();
        return Promise.reject(error);
      });
    }
    return Promise.reject(error);
  }
);

export async function getHealth(): Promise<HealthResponse> {
  const response = await api.get<HealthResponse>('/health');
  return response.data;
}

export interface MeResponse {
  id: number;
  full_name: string;
  email: string;
  role: string;
  company_id: number | null;
  company_name: string | null;
}

export async function getMe(): Promise<MeResponse> {
  const response = await api.get<MeResponse>('/auth/me');
  return response.data;
}

export async function runSimulation(payload?: SingleSimulationRequest): Promise<ScientificSimulationSubmitResponse> {
  const response = await api.post<ScientificSimulationSubmitResponse>('/simulate', payload ?? {});
  return response.data;
}

export async function runValidation(): Promise<ScientificSimulationSubmitResponse> {
  // Internal developer endpoint - not part of public workflows
  const response = await api.post<ScientificSimulationSubmitResponse>('/api/internal/validate');
  return response.data;
}

export async function runDrugEvaluation(payload: DrugEvalRequest): Promise<ScientificSimulationSubmitResponse> {
  const response = await api.post<ScientificSimulationSubmitResponse>('/api/dose-eval', payload);
  return response.data;
}

export async function getScientificJobStatus(jobId: string): Promise<ScientificSimulationStatusResponse> {
  const response = await api.get<ScientificSimulationStatusResponse>(`/jobs/${encodeURIComponent(jobId)}`);
  return response.data;
}

export async function cancelScientificJob(jobId: string): Promise<ScientificSimulationStatusResponse> {
  const response = await api.post<ScientificSimulationStatusResponse>(`/api/jobs/${encodeURIComponent(jobId)}/cancel`);
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

export interface SignupResponse {
  detail: string;
  email?: string;
  otp?: string;
}

export interface GenericAuthResponse {
  detail: string;
  otp?: string;
}

export async function verifyEmail(email: string, code: string): Promise<GenericAuthResponse> {
  const response = await api.post<GenericAuthResponse>('/auth/verify-email', { email, code });
  return response.data;
}

export async function resendVerificationCode(email: string): Promise<GenericAuthResponse> {
  const response = await api.post<GenericAuthResponse>('/auth/resend-verification-code', { email });
  return response.data;
}
