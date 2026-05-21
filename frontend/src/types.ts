export type EngineMode = 'fast' | 'accurate';
export type RunType = 'simulate' | 'dose-eval' | 'validate';

export interface HealthResponse {
  status: 'ok';
  service: string;
}

export interface ChannelInput {
  ic50: number;
  hill: number;
}

export interface DrugEvalRequest {
  drug_name: string;
  channels: {
    Na: ChannelInput;
    K: ChannelInput;
    Ca: ChannelInput;
  };
  dose_range: {
    min: number;
    max: number;
    step: number;
  };
  runs: number;
}

export interface SingleSimulationRequest {
  drug_name: string;
  channels: {
    Na: ChannelInput;
    K: ChannelInput;
    Ca: ChannelInput;
  };
  dose: number;
  mode: EngineMode;
}

export interface BackendRunResponse {
  run_id: string;
  status: string;
  report_type: RunType;
  engine_input_mode: string;
  parsed_summary: Record<string, unknown> | null;
  raw_report: string | null;
  duration_seconds: number | null;
  created_at: string;
  error?: string | null;
  stderr?: string | null;
}

export interface RunListItem {
  run_id: string;
  report_type: RunType;
  drug_name: string | null;
  status: string;
  recommendation: string | null;
  risk_level: string | null;
  confidence: string | null;
  created_at: string;
}

export interface RunsListResponse {
  runs: RunListItem[];
}

export interface DeleteRunResponse {
  run_id: string;
  deleted: boolean;
}

export interface RunDetailResponse {
  run_id: string;
  report_type: RunType;
  drug_name: string | null;
  status: string;
  engine_input_mode: string;
  recommendation: string | null;
  risk_level: string | null;
  confidence: string | null;
  raw_report: string | null;
  parsed_summary: Record<string, unknown> | null;
  input_payload: Record<string, unknown> | null;
  error_message: string | null;
  duration_seconds: number | null;
  created_at: string;
}

export interface ReportChartPoint {
  dose: number;
  effect: number;
  toxicity: number;
  risk: number;
}
