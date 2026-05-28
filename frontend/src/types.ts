export type EngineMode = 'accurate';
export type RunType = 'simulate' | 'dose-eval' | 'validate';
export type ScientificJobStatus = 'QUEUED' | 'RUNNING' | 'COMPLETED' | 'FAILED' | 'CANCELLED';

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
  visualization_data?: DrugEvaluationVisualizationData | null;
  raw_report: string | null;
  duration_seconds: number | null;
  created_at: string;
  error?: string | null;
  stderr?: string | null;
}

export interface ScientificSimulationSubmitResponse {
  job_id: string;
  status: 'QUEUED';
}

export interface ScientificSimulationStatusResponse {
  job_id: string;
  status: ScientificJobStatus;
  progress: number;
  created_at: string;
  started_at: string | null;
  completed_at: string | null;
  result_run_id: string | null;
  error_message: string | null;
  runtime_seconds: number | null;
  queue_latency_seconds: number | null;
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
  visualization_data?: DrugEvaluationVisualizationData | null;
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

export interface DoseResultPoint {
  dose: number;
  effect: number;
  firing_rate: number;
  sync: number;
  nii: number;
  seizure_score: number;
  toxicity_score: number;
  variance: number;
  response_mode: string;
  biological_state: string;
  active_zone?: string;
  ic50_na?: number;
  ic50_k?: number;
  ic50_ca?: number;
}

export interface VoltageTracePoint {
  time: number;
  voltage: number;
}

export interface RasterSpikePoint {
  neuron_id: number;
  spike_time: number;
}

export interface TimelineSegment {
  label: string;
  state: string;
  from: number;
  to: number;
  color: string;
}

export interface VisualizationZone {
  start: number;
  end: number;
}

export interface VisualizationZones {
  ineffective: VisualizationZone | null;
  therapeutic: VisualizationZone | null;
  over_suppression: VisualizationZone | null;
  excitatory: VisualizationZone | null;
  toxic: VisualizationZone | null;
}

export interface VisualizationThresholds {
  onset: number | null;
  toxic: number | null;
  saturation: number | null;
}

export interface DrugEvaluationVisualizationData {
  response_mode?: string;
  active_zone?: string;
  toxicity_observed?: boolean;
  zones?: VisualizationZones;
  thresholds?: VisualizationThresholds;
  dose_results: DoseResultPoint[];
  voltage_trace: VoltageTracePoint[];
  raster_spikes: RasterSpikePoint[];
  classification_timeline: TimelineSegment[];
  reference_points?: {
    ic50?: number;
    toxic_threshold?: number;
    therapeutic_min?: number;
    therapeutic_max?: number;
    onset_dose?: number;
    active_zone?: string;
    has_valid_therapeutic_window?: boolean;
  };
}
