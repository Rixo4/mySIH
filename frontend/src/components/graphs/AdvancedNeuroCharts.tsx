import { useState } from 'react';
import { ChevronDown, ChevronUp } from 'lucide-react';
import { Area, AreaChart, Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';
import { ChartShell } from './ChartShell';
import type { NormalizedVisualizationData } from './chartUtils';
import { MechanisticTooltip } from './chartUtils';
import type { VoltageTracePoint, RasterSpikePoint } from '../../types';

interface AdvancedNeuroChartsProps {
  data: NormalizedVisualizationData;
}

function VoltageTraceCard({ voltageTrace }: { voltageTrace: VoltageTracePoint[]; markers: NormalizedVisualizationData['markers'] }) {
  const minVoltage = Math.floor(Math.min(...voltageTrace.map((point) => point.voltage), -85));
  const maxVoltage = Math.ceil(Math.max(...voltageTrace.map((point) => point.voltage), 45));

  return (
    <ChartShell
      id="spp-advanced-voltage-trace"
      title="Membrane Voltage Trace"
      subtitle="Time vs membrane potential"
      description="Somatosensory action potential waveform over time under active compound exposure."
      legendItems={[{ label: 'Membrane potential (mV)', color: '#38bdf8' }]}
      chartHeightClassName="h-[280px]"
    >
      <ResponsiveContainer width="100%" height="100%">
        <LineChart data={voltageTrace} margin={{ top: 18, right: 24, left: 4, bottom: 12 }}>
          <XAxis
            dataKey="time"
            stroke="#cbd5e1"
            tick={{ fontSize: 11, fill: '#cbd5e1' }}
            tickLine={{ stroke: 'rgba(255,255,255,0.16)' }}
            axisLine={{ stroke: 'rgba(255,255,255,0.18)' }}
            label={{ value: 'Time (ms)', position: 'insideBottom', offset: -6, fill: '#94a3b8', fontSize: 11 }}
          />
          <YAxis
            stroke="#cbd5e1"
            tick={{ fontSize: 11, fill: '#cbd5e1' }}
            tickLine={{ stroke: 'rgba(255,255,255,0.16)' }}
            axisLine={{ stroke: 'rgba(255,255,255,0.18)' }}
            domain={[minVoltage, maxVoltage]}
            tickFormatter={(val: number) => `${Math.round(val)} mV`}
            label={{ value: 'Voltage (mV)', angle: -90, position: 'insideLeft', fill: '#94a3b8', fontSize: 11 }}
          />
          <Tooltip content={<MechanisticTooltip labelPrefix="Time" explanation="Shows single-neuron action potential waveform and repolarization dynamics." fieldLabels={{ voltage: 'Voltage (mV)', time: 'Time (ms)' }} />} />
          <Line type="monotone" dataKey="voltage" stroke="#38bdf8" strokeWidth={2} dot={false} name="Voltage (mV)" />
        </LineChart>
      </ResponsiveContainer>
    </ChartShell>
  );
}

function RasterCard({ rasterSpikes }: { rasterSpikes: RasterSpikePoint[] }) {
  const maxNeuron = Math.max(...rasterSpikes.map((point) => point.neuron_id), 20);

  return (
    <ChartShell
      id="spp-advanced-raster"
      title="Population Raster Plot"
      subtitle="Neuron ID vs spike timing"
      description="Spike event raster across cortical neuron populations, visualizing synchrony patterns."
      legendItems={[{ label: 'Spike event', color: '#f43f5e' }]}
      chartHeightClassName="h-[280px]"
    >
      <ResponsiveContainer width="100%" height="100%">
        <LineChart data={rasterSpikes} margin={{ top: 18, right: 24, left: 4, bottom: 12 }}>
          <XAxis
            dataKey="spike_time"
            stroke="#cbd5e1"
            tick={{ fontSize: 11, fill: '#cbd5e1' }}
            tickLine={{ stroke: 'rgba(255,255,255,0.16)' }}
            axisLine={{ stroke: 'rgba(255,255,255,0.18)' }}
            label={{ value: 'Time (ms)', position: 'insideBottom', offset: -6, fill: '#94a3b8', fontSize: 11 }}
          />
          <YAxis
            dataKey="neuron_id"
            stroke="#cbd5e1"
            tick={{ fontSize: 11, fill: '#cbd5e1' }}
            tickLine={{ stroke: 'rgba(255,255,255,0.16)' }}
            axisLine={{ stroke: 'rgba(255,255,255,0.18)' }}
            domain={[1, maxNeuron]}
            label={{ value: 'Neuron ID', angle: -90, position: 'insideLeft', fill: '#94a3b8', fontSize: 11 }}
          />
          <Tooltip content={<MechanisticTooltip labelPrefix="Spike time" explanation="Spike timing across population reveals network synchronization." fieldLabels={{ spike_time: 'Spike time (ms)', neuron_id: 'Neuron ID' }} />} />
          <Line type="linear" dataKey="neuron_id" stroke="#f43f5e" strokeWidth={0} dot={{ r: 2.2, fill: '#f43f5e' }} name="Spike" />
        </LineChart>
      </ResponsiveContainer>
    </ChartShell>
  );
}

function VarianceCard({ data }: { data: NormalizedVisualizationData }) {
  return (
    <ChartShell
      id="spp-advanced-variance"
      title="Inter-Trial Variance"
      subtitle="Dose vs variability"
      description="Run-to-run variability metric across simulation repeats for each tested concentration."
      legendItems={[{ label: 'Variance', color: '#60a5fa' }]}
      chartHeightClassName="h-[280px]"
    >
      <ResponsiveContainer width="100%" height="100%">
        <AreaChart data={data.doseResults} margin={{ top: 18, right: 24, left: 4, bottom: 12 }}>
          <XAxis
            dataKey="dose"
            stroke="#cbd5e1"
            tick={{ fontSize: 11, fill: '#cbd5e1' }}
            tickLine={{ stroke: 'rgba(255,255,255,0.16)' }}
            axisLine={{ stroke: 'rgba(255,255,255,0.18)' }}
            label={{ value: 'Dose (µM)', position: 'insideBottom', offset: -6, fill: '#94a3b8', fontSize: 11 }}
          />
          <YAxis
            stroke="#cbd5e1"
            tick={{ fontSize: 11, fill: '#cbd5e1' }}
            tickLine={{ stroke: 'rgba(255,255,255,0.16)' }}
            axisLine={{ stroke: 'rgba(255,255,255,0.18)' }}
            label={{ value: 'Variance', angle: -90, position: 'insideLeft', fill: '#94a3b8', fontSize: 11 }}
          />
          <Tooltip content={<MechanisticTooltip labelPrefix="Dose" explanation="Lower variance indicates consistent response reproducibility." fieldLabels={{ variance: 'Variance', dose: 'Dose (µM)' }} />} />
          <Area type="monotone" dataKey="variance" stroke="#60a5fa" fill="#60a5fa" fillOpacity={0.22} strokeWidth={2} dot={false} name="Variance" />
        </AreaChart>
      </ResponsiveContainer>
    </ChartShell>
  );
}

export function AdvancedNeuroCharts({ data }: AdvancedNeuroChartsProps) {
  const [open, setOpen] = useState(false);

  return (
    <section className="rounded-xl border border-[#1E2330] bg-[#0C1017] p-5 shadow-xl">
      <button
        type="button"
        onClick={() => setOpen((current) => !current)}
        className="flex w-full items-center justify-between gap-4 rounded-lg border border-[#222836] bg-[#11151E] px-4 py-3 text-left transition hover:border-sky-500/30 hover:bg-[#151A24] cursor-pointer"
      >
        <div>
          <p className="text-[10px] font-mono uppercase tracking-widest text-sky-400 font-semibold">Supplementary Biophysical Traces</p>
          <h4 className="text-sm font-bold text-white mt-0.5">Membrane Voltage, Population Raster & Trial Variance</h4>
          <p className="text-xs text-slate-400 mt-0.5">Detailed single-cell trajectories and microcircuit spike raster timing.</p>
        </div>
        <span className="inline-flex h-8 w-8 items-center justify-center rounded-lg border border-[#222836] bg-[#0D1017] text-slate-300">
          {open ? <ChevronUp className="h-4 w-4" /> : <ChevronDown className="h-4 w-4" />}
        </span>
      </button>

      {open ? (
        <div className="mt-4 grid gap-4 lg:grid-cols-3">
          <VoltageTraceCard voltageTrace={data.voltageTrace} markers={data.markers} />
          <RasterCard rasterSpikes={data.rasterSpikes} />
          <VarianceCard data={data} />
        </div>
      ) : null}
    </section>
  );
}
