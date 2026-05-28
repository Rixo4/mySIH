import { useMemo } from 'react';
import {
  Area,
  AreaChart,
  CartesianGrid,
  ComposedChart,
  Legend,
  Line,
  LineChart,
  ReferenceArea,
  ReferenceLine,
  ResponsiveContainer,
  Scatter,
  ScatterChart,
  Tooltip,
  XAxis,
  YAxis
} from 'recharts';
import { motion } from 'framer-motion';
import type { DrugEvaluationVisualizationData } from '../types';
import { formatRangeLabel } from '../lib/drugVisualization';

interface DashboardProps {
  visualizationData: DrugEvaluationVisualizationData;
  responseMode: string;
  summary: {
    recommendation?: string | null;
    riskLevel?: string | null;
    confidence?: string | null;
    toxicThreshold?: string | null;
    therapeuticRange?: string | null;
    stabilizationRange?: string | null;
  };
}

function SummaryCard({ label, value, tone = 'neutral' }: { label: string; value?: string | null; tone?: 'neutral' | 'safe' | 'warning' | 'danger' }) {
  const toneClasses = {
    neutral: 'border-white/10 bg-white/5 text-slate-200',
    safe: 'border-emerald-400/20 bg-emerald-500/10 text-emerald-100',
    warning: 'border-amber-400/20 bg-amber-500/10 text-amber-100',
    danger: 'border-rose-400/20 bg-rose-500/10 text-rose-100'
  } as const;

  return (
    <div className={`min-h-[112px] rounded-3xl border px-5 py-5 shadow-[0_0_0_1px_rgba(255,255,255,0.02)] ${toneClasses[tone]}`}>
      <p className="text-[11px] uppercase tracking-[0.3em] opacity-70">{label}</p>
      <p className="mt-3 break-words text-lg font-semibold leading-tight xl:text-xl">{value ?? '—'}</p>
    </div>
  );
}

function ChartCard({ title, subtitle, chartId, children }: { title: string; subtitle: string; chartId: string; children: React.ReactNode }) {
  return (
    <div className="glass-card overflow-hidden border border-white/10 bg-[rgba(6,12,24,0.86)]">
      <div className="border-b border-white/10 px-6 py-5">
        <p className="text-[11px] uppercase tracking-[0.34em] text-cyan-300/75">{subtitle}</p>
        <h4 className="mt-2 text-xl font-semibold tracking-[-0.01em] text-white">{title}</h4>
      </div>
      <div id={chartId} className="h-[26rem] px-3 py-5 sm:h-[30rem] lg:h-[32rem]">
        {children}
      </div>
    </div>
  );
}

export function DrugEvaluationVisualizationDashboard({ visualizationData, responseMode, summary }: DashboardProps) {
  const doseResults = useMemo(() => visualizationData.dose_results, [visualizationData.dose_results]);
  const responseTone = responseMode === 'EXCITATORY_RESPONSE' ? 'warning' : responseMode === 'STABILIZING_RESPONSE' ? 'safe' : 'neutral';
  const mutedMode = responseMode === 'NO_SIGNIFICANT_RESPONSE';
  const therapeuticRange = summary.therapeuticRange ?? summary.stabilizationRange;

  const voltageMin = Math.min(...visualizationData.voltage_trace.map((point) => point.voltage)) - 6;
  const voltageMax = Math.max(...visualizationData.voltage_trace.map((point) => point.voltage)) + 6;

  return (
    <motion.section initial={{ opacity: 0, y: 14 }} animate={{ opacity: 1, y: 0 }} className="space-y-6">
      <div className="glass-card border border-cyan-400/10 bg-[rgba(6,12,24,0.9)] p-6 shadow-panel lg:p-7">
        <div className="grid gap-6 xl:grid-cols-[0.95fr_1.05fr] xl:items-start">
          <div className="max-w-2xl">
            <p className="text-xs uppercase tracking-[0.34em] text-cyan-300/70">Drug Evaluation Visualization Dashboard</p>
            <h3 className="mt-3 text-3xl font-semibold tracking-[-0.02em] text-white lg:text-[2.05rem] lg:leading-tight">Pharma-grade response interpretation</h3>
            <p className="mt-4 max-w-2xl text-sm leading-7 text-slate-400 lg:text-[0.98rem]">
              The graph system translates the parsed dose-evaluation run into interpretable pharmacology, network stability, and seizure-risk views without changing the engine output.
            </p>
          </div>
          <div className="grid gap-4 sm:grid-cols-2 xl:grid-cols-3">
            <SummaryCard label="Recommendation" value={summary.recommendation} tone={responseTone} />
            <SummaryCard label="Risk Level" value={summary.riskLevel} tone={responseTone} />
            <SummaryCard label="Confidence" value={summary.confidence} />
            <SummaryCard label="Toxic Threshold" value={summary.toxicThreshold} tone="warning" />
            <SummaryCard label="Therapeutic Range" value={therapeuticRange ?? formatRangeLabel(visualizationData.reference_points?.therapeutic_min, visualizationData.reference_points?.therapeutic_max)} tone="safe" />
            <SummaryCard label="Response Mode" value={responseMode || 'UNSPECIFIED'} tone={responseTone} />
          </div>
        </div>
      </div>

      <section className="grid gap-6 xl:grid-cols-2">
        <ChartCard title="Dose vs Effect %" subtitle="Primary pharmacology" chartId="spp-chart-effect">
          <ResponsiveContainer width="100%" height="100%">
            <ComposedChart data={doseResults} margin={{ top: 18, right: 28, left: 8, bottom: 18 }}>
              <defs>
                <linearGradient id="dose-effect-gradient" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="5%" stopColor="#38bdf8" stopOpacity={0.42} />
                  <stop offset="95%" stopColor="#38bdf8" stopOpacity={0.02} />
                </linearGradient>
              </defs>
              <CartesianGrid strokeDasharray="3 3" stroke="rgba(148,163,184,0.14)" />
              <XAxis dataKey="dose" stroke="#d1d5db" tick={{ fontSize: 13, fill: '#d1d5db' }} tickLine={{ stroke: 'rgba(255,255,255,0.15)' }} axisLine={{ stroke: 'rgba(255,255,255,0.18)' }} />
              <YAxis stroke="#d1d5db" tick={{ fontSize: 13, fill: '#d1d5db' }} domain={[0, 100]} tickLine={{ stroke: 'rgba(255,255,255,0.15)' }} axisLine={{ stroke: 'rgba(255,255,255,0.18)' }} />
              <Tooltip contentStyle={{ background: '#07111f', border: '1px solid rgba(255,255,255,0.08)', borderRadius: 16 }} />
              <Legend wrapperStyle={{ fontSize: 13, color: '#e2e8f0' }} />
              {visualizationData.reference_points?.ic50 != null ? <ReferenceLine x={visualizationData.reference_points.ic50} stroke="#22d3ee" strokeDasharray="4 4" label="IC50" /> : null}
              {visualizationData.reference_points?.toxic_threshold != null ? <ReferenceLine x={visualizationData.reference_points.toxic_threshold} stroke="#f97316" strokeDasharray="4 4" label="Toxic Threshold" /> : null}
              {visualizationData.reference_points?.therapeutic_min != null && visualizationData.reference_points?.therapeutic_max != null ? (
                <ReferenceArea x1={visualizationData.reference_points.therapeutic_min} x2={visualizationData.reference_points.therapeutic_max} fill="#10b981" fillOpacity={0.08} label="Therapeutic Zone" />
              ) : null}
              <Area type="monotone" dataKey="effect" stroke="#38bdf8" fill="url(#dose-effect-gradient)" name="Effect %" />
            </ComposedChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Dose vs Firing Rate" subtitle="Neuronal dynamics" chartId="spp-chart-firing">
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={doseResults} margin={{ top: 18, right: 28, left: 8, bottom: 18 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="rgba(148,163,184,0.14)" />
              <XAxis dataKey="dose" stroke="#d1d5db" tick={{ fontSize: 13, fill: '#d1d5db' }} tickLine={{ stroke: 'rgba(255,255,255,0.15)' }} axisLine={{ stroke: 'rgba(255,255,255,0.18)' }} />
              <YAxis stroke="#d1d5db" tick={{ fontSize: 13, fill: '#d1d5db' }} tickLine={{ stroke: 'rgba(255,255,255,0.15)' }} axisLine={{ stroke: 'rgba(255,255,255,0.18)' }} />
              <Tooltip contentStyle={{ background: '#07111f', border: '1px solid rgba(255,255,255,0.08)', borderRadius: 16 }} />
              <Legend wrapperStyle={{ fontSize: 13, color: '#e2e8f0' }} />
              <Line type="monotone" dataKey="firing_rate" stroke={mutedMode ? '#94a3b8' : responseMode === 'SUPPRESSIVE_RESPONSE' ? '#22c55e' : '#f59e0b'} strokeWidth={mutedMode ? 2 : 2.5} dot={false} name="Firing Rate" />
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Dose vs Seizure Risk" subtitle="Safety signal" chartId="spp-chart-risk">
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={doseResults} margin={{ top: 18, right: 28, left: 8, bottom: 18 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="rgba(148,163,184,0.14)" />
              <XAxis dataKey="dose" stroke="#d1d5db" tick={{ fontSize: 13, fill: '#d1d5db' }} tickLine={{ stroke: 'rgba(255,255,255,0.15)' }} axisLine={{ stroke: 'rgba(255,255,255,0.18)' }} />
              <YAxis stroke="#d1d5db" tick={{ fontSize: 13, fill: '#d1d5db' }} tickLine={{ stroke: 'rgba(255,255,255,0.15)' }} axisLine={{ stroke: 'rgba(255,255,255,0.18)' }} />
              <Tooltip contentStyle={{ background: '#07111f', border: '1px solid rgba(255,255,255,0.08)', borderRadius: 16 }} />
              <Legend wrapperStyle={{ fontSize: 13, color: '#e2e8f0' }} />
              <Line type="monotone" dataKey="seizure_score" stroke="#fb7185" strokeWidth={2.5} dot={false} name="Seizure Score" />
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Dose vs Synchronization" subtitle="Network stability" chartId="spp-chart-sync">
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={doseResults} margin={{ top: 18, right: 28, left: 8, bottom: 18 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="rgba(148,163,184,0.14)" />
              <XAxis dataKey="dose" stroke="#d1d5db" tick={{ fontSize: 13, fill: '#d1d5db' }} tickLine={{ stroke: 'rgba(255,255,255,0.15)' }} axisLine={{ stroke: 'rgba(255,255,255,0.18)' }} />
              <YAxis stroke="#d1d5db" tick={{ fontSize: 13, fill: '#d1d5db' }} domain={[0, 1]} tickLine={{ stroke: 'rgba(255,255,255,0.15)' }} axisLine={{ stroke: 'rgba(255,255,255,0.18)' }} />
              <Tooltip contentStyle={{ background: '#07111f', border: '1px solid rgba(255,255,255,0.08)', borderRadius: 16 }} />
              <Legend wrapperStyle={{ fontSize: 13, color: '#e2e8f0' }} />
              <Line type="monotone" dataKey="sync" stroke="#06b6d4" strokeWidth={2.5} dot={false} name="Synchronization Index" />
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Dose vs NII" subtitle="Neural instability" chartId="spp-chart-nii">
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={doseResults} margin={{ top: 18, right: 28, left: 8, bottom: 18 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="rgba(148,163,184,0.14)" />
              <XAxis dataKey="dose" stroke="#d1d5db" tick={{ fontSize: 13, fill: '#d1d5db' }} tickLine={{ stroke: 'rgba(255,255,255,0.15)' }} axisLine={{ stroke: 'rgba(255,255,255,0.18)' }} />
              <YAxis stroke="#d1d5db" tick={{ fontSize: 13, fill: '#d1d5db' }} tickLine={{ stroke: 'rgba(255,255,255,0.15)' }} axisLine={{ stroke: 'rgba(255,255,255,0.18)' }} />
              <Tooltip contentStyle={{ background: '#07111f', border: '1px solid rgba(255,255,255,0.08)', borderRadius: 16 }} />
              <Legend wrapperStyle={{ fontSize: 13, color: '#e2e8f0' }} />
              <Line type="monotone" dataKey="nii" stroke="#c084fc" strokeWidth={2.5} dot={false} name="Neural Instability Index" />
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Membrane Voltage Trace" subtitle="Advanced neuroscience" chartId="spp-chart-voltage">
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={visualizationData.voltage_trace} margin={{ top: 18, right: 28, left: 8, bottom: 18 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="rgba(148,163,184,0.14)" />
              <XAxis dataKey="time" stroke="#d1d5db" tick={{ fontSize: 13, fill: '#d1d5db' }} tickLine={{ stroke: 'rgba(255,255,255,0.15)' }} axisLine={{ stroke: 'rgba(255,255,255,0.18)' }} />
              <YAxis stroke="#d1d5db" tick={{ fontSize: 13, fill: '#d1d5db' }} domain={[voltageMin, voltageMax]} tickLine={{ stroke: 'rgba(255,255,255,0.15)' }} axisLine={{ stroke: 'rgba(255,255,255,0.18)' }} />
              <Tooltip contentStyle={{ background: '#07111f', border: '1px solid rgba(255,255,255,0.08)', borderRadius: 16 }} />
              <Legend wrapperStyle={{ fontSize: 13, color: '#e2e8f0' }} />
              <ReferenceLine y={-55} stroke="#f59e0b" strokeDasharray="4 4" label="Spike Threshold" />
              <Line type="monotone" dataKey="voltage" stroke="#e879f9" strokeWidth={2.4} dot={false} name="Membrane Voltage" />
            </LineChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Spike Raster Plot" subtitle="Population activity" chartId="spp-chart-raster">
          <ResponsiveContainer width="100%" height="100%">
            <ScatterChart margin={{ top: 18, right: 28, left: 8, bottom: 18 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="rgba(148,163,184,0.14)" />
              <XAxis type="number" dataKey="spike_time" stroke="#d1d5db" tick={{ fontSize: 13, fill: '#d1d5db' }} tickLine={{ stroke: 'rgba(255,255,255,0.15)' }} axisLine={{ stroke: 'rgba(255,255,255,0.18)' }} />
              <YAxis type="number" dataKey="neuron_id" stroke="#d1d5db" tick={{ fontSize: 13, fill: '#d1d5db' }} domain={[0, 20]} tickLine={{ stroke: 'rgba(255,255,255,0.15)' }} axisLine={{ stroke: 'rgba(255,255,255,0.18)' }} />
              <Tooltip contentStyle={{ background: '#07111f', border: '1px solid rgba(255,255,255,0.08)', borderRadius: 16 }} />
              <Legend wrapperStyle={{ fontSize: 13, color: '#e2e8f0' }} />
              <Scatter data={visualizationData.raster_spikes} fill="#38bdf8" name="Neuron Spikes" />
            </ScatterChart>
          </ResponsiveContainer>
        </ChartCard>

        <ChartCard title="Stability / Variance" subtitle="Reproducibility confidence" chartId="spp-chart-variance">
          <ResponsiveContainer width="100%" height="100%">
            <AreaChart data={doseResults} margin={{ top: 18, right: 28, left: 8, bottom: 18 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="rgba(148,163,184,0.14)" />
              <XAxis dataKey="dose" stroke="#d1d5db" tick={{ fontSize: 13, fill: '#d1d5db' }} tickLine={{ stroke: 'rgba(255,255,255,0.15)' }} axisLine={{ stroke: 'rgba(255,255,255,0.18)' }} />
              <YAxis stroke="#d1d5db" tick={{ fontSize: 13, fill: '#d1d5db' }} tickLine={{ stroke: 'rgba(255,255,255,0.15)' }} axisLine={{ stroke: 'rgba(255,255,255,0.18)' }} />
              <Tooltip contentStyle={{ background: '#07111f', border: '1px solid rgba(255,255,255,0.08)', borderRadius: 16 }} />
              <Legend wrapperStyle={{ fontSize: 13, color: '#e2e8f0' }} />
              <Area type="monotone" dataKey="variance" stroke="#60a5fa" fill="#60a5fa" fillOpacity={0.22} name="Variance" />
            </AreaChart>
          </ResponsiveContainer>
        </ChartCard>
      </section>

      <div className="glass-card border border-white/10 bg-[rgba(6,12,24,0.86)] p-5">
        <div className="flex flex-col gap-4 lg:flex-row lg:items-center lg:justify-between">
          <div>
            <p className="text-xs uppercase tracking-[0.3em] text-cyan-300/70">Classification Timeline</p>
            <h4 className="mt-1 text-lg font-semibold text-white">Dose-state segmentation</h4>
          </div>
          <p className="text-sm text-slate-400">Therapeutic span: {formatRangeLabel(visualizationData.reference_points?.therapeutic_min, visualizationData.reference_points?.therapeutic_max)} interpreted against the full sweep</p>
        </div>
        <div className="mt-5 flex overflow-hidden rounded-3xl border border-white/10 bg-slate-950/70 p-2">
          {visualizationData.classification_timeline.map((segment) => {
            const width = Math.max(0.08, segment.to - segment.from);
            return (
              <div
                key={segment.state}
                className="min-h-24 rounded-2xl px-4 py-4 text-sm text-white shadow-lg"
                style={{ background: segment.color, flex: width }}
              >
                <div className="font-semibold">{segment.label}</div>
                <div className="mt-1 text-xs opacity-80">{segment.from.toFixed(2)} - {segment.to.toFixed(2)}</div>
              </div>
            );
          })}
        </div>
      </div>
    </motion.section>
  );
}