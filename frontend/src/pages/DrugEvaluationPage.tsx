import React, { useEffect, useMemo, useState } from 'react';
import { useSearchParams, useNavigate } from 'react-router-dom';
import { motion } from 'framer-motion';
import {
  TestTube2,
  Beaker,
  RotateCcw,
  Play,
  CheckCircle2,
  ShieldAlert,
  FileText,
  Activity,
  Zap,
  Sliders,
  Cpu,
  Layers,
  FlaskConical,
  Eye,
  LineChart,
  Dna,
} from 'lucide-react';
import { DrugInputForm } from '../components/DrugInputForm';
import { InteractivePatchClampSimulator } from '../components/experiment/InteractivePatchClampSimulator';
import { ExperimentReviewModal } from '../components/experiment/ExperimentReviewModal';
import { SimulationExecutionModal } from '../components/experiment/SimulationExecutionModal';
import { MechanisticResponseDashboard } from '../components/graphs/MechanisticResponseDashboard';
import { ReportViewer } from '../components/ReportViewer';
import { StatusBadge } from '../components/StatusBadge';
import { buildDoseChartData } from '../lib/charts';
import {
  buildFallbackVisualizationData,
  downloadCsv,
  downloadSvgAsPng,
  downloadTextReport,
  getVisualizationData,
} from '../lib/drugVisualization';
import type { BackendRunResponse, DrugEvalRequest, DrugEvaluationVisualizationData, ReportChartPoint } from '../types';
import { useRunTask } from '../context/RunTaskContext';
import { useToast } from '../context/ToastContext';
import { useBackend } from '../context/BackendContext';
import { getRunDetail } from '../api/client';

import { ErrorBoundary } from '../components/common/ErrorBoundary';

// Preset profiles for standard research benchmarks
const PRESET_PROFILES: Array<{
  label: string;
  category: string;
  payload: DrugEvalRequest;
}> = [
  {
    label: 'Ketamine (NMDA Antagonist)',
    category: 'Anesthetic / Antidepressant',
    payload: {
      drug_name: 'Ketamine-Like-Profile',
      channels: {
        Na: { ic50: 250, hill: 2.0 },
        K: { ic50: 180, hill: 2.0 },
        Ca: { ic50: 30, hill: 2.5 },
      },
      dose_range: { min: 0, max: 50, step: 5 },
      runs: 3,
    },
  },
  {
    label: 'Diazepam (GABA-A Modulator)',
    category: 'Anxiolytic / Sedative',
    payload: {
      drug_name: 'Diazepam-Like-Profile',
      channels: {
        Na: { ic50: 500, hill: 1.5 },
        K: { ic50: 400, hill: 1.5 },
        Ca: { ic50: 100, hill: 2.0 },
      },
      dose_range: { min: 0, max: 30, step: 3 },
      runs: 3,
    },
  },
  {
    label: 'Selective K+ Channel Blocker',
    category: 'Experimental Antiarrhythmic',
    payload: {
      drug_name: 'Test-K-Blocker',
      channels: {
        Na: { ic50: 200, hill: 3.2 },
        K: { ic50: 8, hill: 3.2 },
        Ca: { ic50: 1000, hill: 3.2 },
      },
      dose_range: { min: 0, max: 20, step: 2 },
      runs: 3,
    },
  },
];

function validatePayload(payload: DrugEvalRequest): string | null {
  if (!payload || !payload.drug_name || !payload.drug_name.trim()) {
    return 'Drug name is required';
  }
  if (!payload.dose_range || payload.dose_range.min < 0 || payload.dose_range.max <= payload.dose_range.min) {
    return 'Dose range maximum must be greater than minimum';
  }
  if (payload.dose_range.step <= 0) {
    return 'Dose step must be greater than zero';
  }
  return null;
}

export function DrugEvaluationPage() {
  const [searchParams] = useSearchParams();
  const navigate = useNavigate();
  const { addToast } = useToast();

  const {
    drugEvaluationState,
    setDrugEvaluationPayload,
    runDrugEvaluationTask,
  } = useRunTask();
  const { backendConnected } = useBackend();

  const payload: DrugEvalRequest = drugEvaluationState?.payload ?? {
    drug_name: '',
    channels: {
      Na: { ic50: 0, hill: 1.0 },
      K: { ic50: 0, hill: 1.0 },
      Ca: { ic50: 0, hill: 1.0 },
    },
    dose_range: { min: 0, max: 20, step: 2 },
    runs: 3,
  };

  const [activeTab, setActiveTab] = useState<'visualize' | 'report'>('visualize');
  const [isReviewOpen, setIsReviewOpen] = useState(false);
  const [isExecutionOpen, setIsExecutionOpen] = useState(false);
  const [clientError, setClientError] = useState<string | null>(null);

  const result: BackendRunResponse | null = drugEvaluationState.result;

  useEffect(() => {
    const cloneId = searchParams.get('clone');
    if (cloneId) {
      getRunDetail(cloneId)
        .then((detail) => {
          if (detail.input_payload) {
            setDrugEvaluationPayload(detail.input_payload as unknown as DrugEvalRequest);
            addToast({
              type: 'info',
              title: 'Assay Cloned',
              message: `Loaded parameters from run ${cloneId.substring(0, 8)}`,
            });
          }
        })
        .catch(() => {
          addToast({
            type: 'warning',
            title: 'Clone Failed',
            message: 'Could not load configuration from original run',
          });
        });
    }
  }, [searchParams, setDrugEvaluationPayload, addToast]);

  const handleReviewTrigger = (nextPayload: DrugEvalRequest) => {
    const validationError = validatePayload(nextPayload);
    if (validationError) {
      setClientError(validationError);
      addToast({ type: 'error', title: 'Validation Error', message: validationError });
      return;
    }

    setClientError(null);
    setDrugEvaluationPayload(nextPayload);
    setIsReviewOpen(true);
  };

  const handleConfirmRun = async () => {
    setIsReviewOpen(false);
    setIsExecutionOpen(true);
    addToast({
      type: 'info',
      title: 'Simulation Enqueued',
      message: `Running ${payload.drug_name} simulation on C++/CUDA engine...`,
    });
    await runDrugEvaluationTask(payload);
  };

  const loadPreset = (preset: DrugEvalRequest) => {
    setDrugEvaluationPayload(preset);
    addToast({
      type: 'success',
      title: 'Preset Loaded',
      message: `Applied ${preset.drug_name} ion channel matrix`,
    });
  };

  const chartData: ReportChartPoint[] = useMemo(() => {
    if (!result?.raw_report && result?.status !== 'completed') {
      return [];
    }
    return buildDoseChartData(
      payload.dose_range.min,
      payload.dose_range.max,
      payload.dose_range.step,
      result?.parsed_summary?.effective_range as string,
      result?.parsed_summary?.risk_level as string,
      result?.parsed_summary?.toxic_threshold as string,
      (result?.parsed_summary?.response_mode as string)?.toUpperCase() || '',
      Number(result?.parsed_summary?.max_effect) || 0
    );
  }, [payload, result]);

  const visualizationData: DrugEvaluationVisualizationData = useMemo(() => {
    const fallback = buildFallbackVisualizationData(
      chartData,
      payload,
      (result?.parsed_summary?.response_mode as string) || '',
      {
        toxicThreshold: result?.parsed_summary?.toxic_threshold as string,
        stabilizationRange: result?.parsed_summary?.stabilization_range as string,
      }
    );
    return getVisualizationData(result?.visualization_data, fallback);
  }, [chartData, payload, result]);

  return (
    <div className="relative min-h-screen p-3 sm:p-5 space-y-3.5 pb-28 font-sans text-slate-100 bg-[#08080f] overflow-hidden">

      {/* ── Background Atmosphere Layers ── */}
      <div
        className="absolute inset-0 pointer-events-none opacity-20"
        style={{
          backgroundImage: 'radial-gradient(circle at 1px 1px, rgba(37, 99, 235, 0.15) 1px, transparent 0)',
          backgroundSize: '36px 36px',
        }}
      />
      <div
        className="absolute inset-0 pointer-events-none"
        style={{
          background: 'radial-gradient(ellipse 80% 45% at 50% 0%, rgba(30, 64, 175, 0.18) 0%, transparent 60%)',
        }}
      />

      {/* ── SECTION 1: Instrumentation Header Panel (TradeWise Glow & Neural Backdrop) ── */}
      <div className="relative z-10 rounded-2xl border border-slate-800/80 bg-[#0d0d18] p-4 sm:p-5 shadow-xl overflow-hidden">
        {/* Neural Network Ambient Glow Backdrop */}
        <div
          className="absolute inset-0 pointer-events-none opacity-35"
          style={{
            background: 'radial-gradient(circle at 60% 50%, rgba(99, 102, 241, 0.18) 0%, rgba(56, 189, 248, 0.08) 35%, transparent 70%)',
          }}
        />
        <div className="absolute right-1/3 top-1/2 -translate-y-1/2 w-48 h-48 bg-purple-600/10 rounded-full blur-3xl pointer-events-none" />
        <div className="absolute right-1/4 top-1/2 -translate-y-1/2 w-36 h-36 bg-blue-500/15 rounded-full blur-2xl pointer-events-none" />

        <div className="relative z-10 flex flex-col lg:flex-row lg:items-center lg:justify-between gap-4">
          <div className="space-y-1.5">
            {/* Badges Row */}
            <div className="flex items-center gap-2">
              <span className="text-[10px] font-mono font-bold px-2.5 py-0.5 rounded bg-[#111120] text-slate-300 border border-slate-800">
                DOSE LAB v2.4
              </span>
              <span className={`flex items-center gap-1.5 text-[10px] font-mono font-bold ${backendConnected ? 'text-emerald-400' : 'text-rose-400'}`}>
                <span className={`w-2 h-2 rounded-full ${backendConnected ? 'bg-emerald-400 shadow-[0_0_8px_#34d399] animate-pulse' : 'bg-rose-400'}`} />
                {backendConnected ? 'ODE SOLVER READY' : 'BACKEND DISCONNECTED'}
              </span>
            </div>

            {/* Title & Description */}
            <h1 className="text-lg sm:text-xl font-bold tracking-tight text-white">
              In-Silico Dose-Response & Ion Channel Lab
            </h1>
            <p className="text-xs text-slate-400 max-w-2xl leading-relaxed">
              Configure binding kinetics (IC50, Hill coefficient) across gated channels. Live Hill dynamics compute simultaneously on the right console.
            </p>
          </div>

          {/* Right Telemetry Pods Container */}
          <div className="bg-[#0a0a14]/90 border border-slate-800/80 p-2 sm:p-2.5 rounded-2xl flex flex-wrap items-center gap-3 shadow-xl backdrop-blur-md shrink-0">
            {/* Pod 1: Response Mode */}
            <div className="p-2 sm:p-2.5 rounded-xl bg-[#111120]/90 border border-slate-800/80 min-w-[110px] space-y-0.5">
              <div className="flex items-center gap-1.5 text-[9px] font-sans uppercase font-bold text-sky-400">
                <Sliders size={11} className="text-sky-400" />
                <span>Response Mode</span>
              </div>
              <div className="text-xs font-mono font-black text-white">
                {(result?.parsed_summary?.response_mode as string)?.toUpperCase() || 'STANDBY'}
              </div>
            </div>

            {/* Pod 2: Target Specimen */}
            <div className="p-2 sm:p-2.5 rounded-xl bg-[#111120]/90 border border-slate-800/80 min-w-[125px] space-y-0.5">
              <div className="flex items-center gap-1.5 text-[9px] font-sans uppercase font-bold text-purple-400">
                <Dna size={11} className="text-purple-400" />
                <span>Target Specimen</span>
              </div>
              <div className="text-xs font-mono font-bold text-purple-300">
                L5 Pyramidal Soma
              </div>
            </div>

            {/* Pod 3: Model Dynamics */}
            <div className="p-2 sm:p-2.5 rounded-xl bg-[#111120]/90 border border-slate-800/80 min-w-[120px] space-y-0.5">
              <div className="flex items-center gap-1.5 text-[9px] font-sans uppercase font-bold text-blue-400">
                <Cpu size={11} className="text-sky-400" />
                <span>Model Dynamics</span>
              </div>
              <div className="text-xs font-mono font-bold text-sky-400">
                Hodgkin-Huxley
              </div>
            </div>
          </div>
        </div>

        {/* Preset Profiles Bar */}
        <div className="relative z-10 mt-3 pt-3 border-t border-slate-800/80 flex flex-wrap items-center gap-2">
          <span className="text-[11px] font-mono text-slate-400 font-medium">
            Standard Presets:
          </span>
          {PRESET_PROFILES.map((preset) => (
            <button
              key={preset.label}
              onClick={() => loadPreset(preset.payload)}
              className="text-xs px-2.5 py-1 rounded-lg bg-[#111120] border border-slate-800 hover:border-blue-500/50 hover:bg-blue-500/10 text-slate-300 hover:text-white transition-all cursor-pointer font-sans"
            >
              {preset.label}
            </button>
          ))}
          <button
            onClick={() => addToast({ type: 'info', title: 'Save Preset', message: 'Current channel matrix saved to active local profile.' })}
            className="text-xs px-2.5 py-1 rounded-lg bg-blue-500/10 border border-blue-500/30 hover:border-blue-400 text-blue-300 hover:text-white transition-all cursor-pointer font-sans flex items-center gap-1"
          >
            <span>+ Save Preset</span>
          </button>
        </div>
      </div>

      {/* ── SECTION 2: Split Lab Console (Form on Left, Live Simulator on Right) ── */}
      <div className="relative z-10 grid grid-cols-1 lg:grid-cols-12 gap-3.5 items-start">
        {/* Left Column (6 Cols): Parameter Input Form */}
        <div className="lg:col-span-6 rounded-2xl border border-slate-800/80 bg-[#0d0d18] p-3 sm:p-3.5 shadow-xl">
          <ErrorBoundary>
            <DrugInputForm
              payload={payload}
              loading={drugEvaluationState?.status === 'running'}
              onChange={setDrugEvaluationPayload}
              onSubmit={handleReviewTrigger}
              backendConnected={backendConnected}
            />
          </ErrorBoundary>
        </div>

        {/* Right Column (6 Cols): Real-Time Visualizer */}
        <div className="lg:col-span-6 rounded-2xl border border-slate-800/80 bg-[#0d0d18] p-3 sm:p-3.5 shadow-xl">
          <ErrorBoundary>
            <InteractivePatchClampSimulator
              payload={payload}
              isSimulating={drugEvaluationState?.status === 'running'}
            />
          </ErrorBoundary>
        </div>
      </div>

      {/* ── SECTION 3: Post-Simulation Mechanistic Results Dashboard ── */}
      {result && result.status === 'completed' && (
        <div className="space-y-4 pt-2">
          {/* Tabs bar (Level 2) */}
          <div className="flex items-center justify-between pb-2 border-b border-[#2a2e37]">
            <div className="flex items-center gap-1.5 p-0.5 rounded-lg bg-[#1a1d24] border border-[#2a2e37]">
              <button
                onClick={() => setActiveTab('visualize')}
                className={`px-3 py-1 rounded-md text-xs transition-all cursor-pointer ${
                  activeTab === 'visualize'
                    ? 'bg-[#22262e] text-white border border-[#388bfd] font-semibold'
                    : 'text-slate-400 hover:text-white border border-transparent'
                }`}
              >
                Telemetry & Mechanistic Plots
              </button>
              <button
                onClick={() => setActiveTab('report')}
                className={`px-3 py-1 rounded-md text-xs transition-all cursor-pointer ${
                  activeTab === 'report'
                    ? 'bg-[#22262e] text-white border border-[#388bfd] font-semibold'
                    : 'text-slate-400 hover:text-white border border-transparent'
                }`}
              >
                Regulatory Audit Report
              </button>
            </div>

            <span className="text-[10px] font-mono text-emerald-400 font-medium">
              SIMULATION COMPLETED
            </span>
          </div>

          <ErrorBoundary>
            {activeTab === 'visualize' ? (
              <MechanisticResponseDashboard
                visualizationData={visualizationData}
                parsedSummary={result?.parsed_summary}
                responseMode={(result?.parsed_summary?.response_mode as string) || 'inhibition'}
              />
            ) : (
              <ReportViewer
                report={result.raw_report ?? ''}
              />
            )}
          </ErrorBoundary>
        </div>
      )}

      {/* Review Modal */}
      <ExperimentReviewModal
        isOpen={isReviewOpen}
        onClose={() => setIsReviewOpen(false)}
        onConfirm={handleConfirmRun}
        payload={payload}
        loading={drugEvaluationState.status === 'running' || drugEvaluationState.status === 'queued'}
      />

      {/* Execution Progress Modal */}
      <SimulationExecutionModal
        isOpen={isExecutionOpen}
        onClose={() => setIsExecutionOpen(false)}
      />
    </div>
  );
}
