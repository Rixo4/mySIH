import { useState } from 'react';
import { Area, AreaChart, CartesianGrid, ReferenceLine, ResponsiveContainer, Scatter, ScatterChart, Tooltip, XAxis, YAxis } from 'recharts';
import { ChevronDown, ChevronUp } from 'lucide-react';
import { ChartShell } from './ChartShell';
import type { NormalizedDoseResult, NormalizedMarkers, NormalizedVisualizationData } from './chartUtils';
import { MechanisticTooltip, formatDoseLabel } from './chartUtils';

interface AdvancedNeuroChartsProps {
  data: NormalizedVisualizationData;
}

function PlaceholderCard({ message, title }: { message: string; title: string }) {
  return (
    <div className="glass-card overflow-hidden border border-white/10 bg-[rgba(6,12,24,0.88)] shadow-panel">
      <div className="border-b border-white/10 px-6 py-5">
        <p className="text-[11px] uppercase tracking-[0.34em] text-cyan-300/75">{title}</p>
        <h4 className="mt-2 text-xl font-semibold tracking-[-0.01em] text-white">{title}</h4>
      </div>
      <div className="flex min-h-[360px] items-center justify-center px-6 py-6 text-sm leading-7 text-slate-400">{message}</div>
    </div>
  );
}

function VoltageTraceCard({ voltageTrace, markers }: { voltageTrace: NormalizedVisualizationData['voltageTrace']; markers: NormalizedMarkers }) {
  if (!voltageTrace.length) {
    return <PlaceholderCard title="Voltage Trace" message="Voltage trace data not available for this run." />;
  }

  const minVoltage = Math.min(...voltageTrace.map((point) => point.voltage)) - 5;
  const maxVoltage = Math.max(...voltageTrace.map((point) => point.voltage)) + 5;

  return (
    <ChartShell
      id="spp-mechanistic-voltage"
      title="Voltage Trace"
      subtitle="Advanced neuroscience"
      description="Shows the membrane voltage behavior across the run. Spiking near threshold usually tracks higher excitability."
      legendItems={[{ label: 'Membrane voltage', color: '#e879f9' }, { label: 'Spike threshold', color: '#f59e0b' }]}
      chartHeightClassName="h-[360px] lg:h-[380px]"
    >
      <ResponsiveContainer width="100%" height="100%">
        <AreaChart data={voltageTrace} margin={{ top: 24, right: 28, left: 8, bottom: 18 }}>
          <CartesianGrid strokeDasharray="3 3" stroke="rgba(148,163,184,0.14)" />
          <XAxis
            dataKey="time"
            stroke="#cbd5e1"
            tick={{ fontSize: 12, fill: '#cbd5e1' }}
            tickLine={{ stroke: 'rgba(255,255,255,0.16)' }}
            axisLine={{ stroke: 'rgba(255,255,255,0.18)' }}
            label={{ value: 'Time', position: 'insideBottom', offset: -8, fill: '#94a3b8', fontSize: 12 }}
          />
          <YAxis
            stroke="#cbd5e1"
            tick={{ fontSize: 12, fill: '#cbd5e1' }}
            domain={[minVoltage, maxVoltage]}
            tickLine={{ stroke: 'rgba(255,255,255,0.16)' }}
            axisLine={{ stroke: 'rgba(255,255,255,0.18)' }}
            label={{ value: 'Voltage (mV)', angle: -90, position: 'insideLeft', fill: '#94a3b8', fontSize: 12 }}
          />
          <Tooltip content={<MechanisticTooltip labelPrefix="Time" explanation="Membrane voltage trace across the run. Depolarizing spikes or a flattened trace can indicate instability or suppression." fieldLabels={{ voltage: 'Voltage', time: 'Time' }} suffixByKey={{ voltage: ' mV' }} />} />
          <ReferenceLine y={-55} stroke="#f59e0b" strokeDasharray="4 4" label="Spike threshold" />
          {markers.toxicThreshold != null ? <ReferenceLine x={markers.toxicThreshold} stroke="#ef4444" strokeDasharray="4 4" /> : null}
          <Area type="monotone" dataKey="voltage" stroke="#e879f9" fill="#e879f9" fillOpacity={0.18} strokeWidth={2.8} dot={false} name="Membrane voltage" />
        </AreaChart>
      </ResponsiveContainer>
    </ChartShell>
  );
}

function RasterCard({ rasterSpikes }: { rasterSpikes: NormalizedVisualizationData['rasterSpikes'] }) {
  if (!rasterSpikes.length) {
    return <PlaceholderCard title="Spike Raster Plot" message="Spike raster data not available for this run." />;
  }

  const maxNeuron = Math.max(...rasterSpikes.map((point) => point.neuron_id), 1);

  return (
    <ChartShell
      id="spp-mechanistic-raster"
      title="Spike Raster Plot"
      subtitle="Population activity"
      description="Each dot marks one spike event. Dense rows suggest highly synchronized or elevated firing activity."
      legendItems={[{ label: 'Spike event', color: '#38bdf8' }]}
      chartHeightClassName="h-[360px] lg:h-[380px]"
    >
      <ResponsiveContainer width="100%" height="100%">
        <ScatterChart margin={{ top: 24, right: 28, left: 8, bottom: 18 }}>
          <CartesianGrid strokeDasharray="3 3" stroke="rgba(148,163,184,0.14)" />
          <XAxis
            type="number"
            dataKey="spike_time"
            stroke="#cbd5e1"
            tick={{ fontSize: 12, fill: '#cbd5e1' }}
            tickLine={{ stroke: 'rgba(255,255,255,0.16)' }}
            axisLine={{ stroke: 'rgba(255,255,255,0.18)' }}
            label={{ value: 'Time', position: 'insideBottom', offset: -8, fill: '#94a3b8', fontSize: 12 }}
          />
          <YAxis
            type="number"
            dataKey="neuron_id"
            stroke="#cbd5e1"
            tick={{ fontSize: 12, fill: '#cbd5e1' }}
            domain={[1, maxNeuron]}
            tickLine={{ stroke: 'rgba(255,255,255,0.16)' }}
            axisLine={{ stroke: 'rgba(255,255,255,0.18)' }}
            label={{ value: 'Neuron ID', angle: -90, position: 'insideLeft', fill: '#94a3b8', fontSize: 12 }}
          />
          <Tooltip content={<MechanisticTooltip labelPrefix="Spike time" explanation="Spike timing and density can reveal whether the network is synchronized or fragmented." fieldLabels={{ spike_time: 'Spike time', neuron_id: 'Neuron ID' }} />} />
          <Scatter data={rasterSpikes} fill="#38bdf8" name="Spike event" />
        </ScatterChart>
      </ResponsiveContainer>
    </ChartShell>
  );
}

function VarianceCard({ data, markers }: { data: NormalizedDoseResult[]; markers: NormalizedMarkers }) {
  if (!data.length) {
    return <PlaceholderCard title="Stability / Variance" message="Stability and variance data not available for this run." />;
  }

  return (
    <ChartShell
      id="spp-mechanistic-variance"
      title="Stability / Variance"
      subtitle="Reproducibility confidence"
      description="Lower variance suggests a steadier response and a more reproducible dose sweep."
      legendItems={[{ label: 'Variance', color: '#60a5fa' }]}
      chartHeightClassName="h-[360px] lg:h-[380px]"
    >
      <ResponsiveContainer width="100%" height="100%">
        <AreaChart data={data} margin={{ top: 24, right: 28, left: 8, bottom: 18 }}>
          <CartesianGrid strokeDasharray="3 3" stroke="rgba(148,163,184,0.14)" />
          <XAxis
            dataKey="dose"
            stroke="#cbd5e1"
            tick={{ fontSize: 12, fill: '#cbd5e1' }}
            tickLine={{ stroke: 'rgba(255,255,255,0.16)' }}
            axisLine={{ stroke: 'rgba(255,255,255,0.18)' }}
            label={{ value: 'Dose', position: 'insideBottom', offset: -8, fill: '#94a3b8', fontSize: 12 }}
          />
          <YAxis
            stroke="#cbd5e1"
            tick={{ fontSize: 12, fill: '#cbd5e1' }}
            tickLine={{ stroke: 'rgba(255,255,255,0.16)' }}
            axisLine={{ stroke: 'rgba(255,255,255,0.18)' }}
            label={{ value: 'Variance', angle: -90, position: 'insideLeft', fill: '#94a3b8', fontSize: 12 }}
          />
          <Tooltip content={<MechanisticTooltip labelPrefix="Dose" explanation="Lower variance suggests a steadier response and a more reproducible dose sweep." fieldLabels={{ variance: 'Variance', dose: 'Dose' }} />} />
          {markers.toxicThreshold != null ? <ReferenceLine x={markers.toxicThreshold} stroke="#ef4444" strokeDasharray="4 4" /> : null}
          <Area type="monotone" dataKey="variance" stroke="#60a5fa" fill="#60a5fa" fillOpacity={0.22} strokeWidth={2.4} dot={false} name="Variance" />
        </AreaChart>
      </ResponsiveContainer>
    </ChartShell>
  );
}

export function AdvancedNeuroCharts({ data }: AdvancedNeuroChartsProps) {
  const [open, setOpen] = useState(false);

  return (
    <section className="glass-card border border-white/10 bg-[rgba(6,12,24,0.86)] p-5 shadow-panel">
      <button
        type="button"
        onClick={() => setOpen((current) => !current)}
        className="flex w-full items-center justify-between gap-4 rounded-[1.35rem] border border-white/10 bg-white/5 px-5 py-4 text-left transition hover:border-cyan-400/25 hover:bg-cyan-500/10"
      >
        <div>
          <p className="text-[11px] uppercase tracking-[0.3em] text-cyan-300/70">Advanced details</p>
          <h4 className="mt-1 text-lg font-semibold text-white">Voltage trace, raster, and stability plots</h4>
          <p className="mt-1 text-sm text-slate-400">These charts are hidden by default so the main story stays readable.</p>
        </div>
        <span className="inline-flex h-10 w-10 items-center justify-center rounded-full border border-white/10 bg-slate-950/70 text-white">
          {open ? <ChevronUp className="h-5 w-5" /> : <ChevronDown className="h-5 w-5" />}
        </span>
      </button>

      {open ? (
        <div className="mt-5 grid gap-6 xl:grid-cols-3">
          <VoltageTraceCard voltageTrace={data.voltageTrace} markers={data.markers} />
          <RasterCard rasterSpikes={data.rasterSpikes} />
          <VarianceCard data={data.doseResults} markers={data.markers} />
        </div>
      ) : null}
    </section>
  );
}
