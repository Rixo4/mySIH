import { Line, LineChart, ReferenceLine, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';
import { ChartShell } from './ChartShell';
import type { NormalizedDoseResult, NormalizedMarkers, ResponseMode } from './chartUtils';
import { MechanisticTooltip } from './chartUtils';

interface SynchronizationChartProps {
  data: NormalizedDoseResult[];
  markers: NormalizedMarkers;
  responseMode: ResponseMode;
}

export function SynchronizationChart({ data, markers, responseMode }: SynchronizationChartProps) {
  const mutedMode = markers.lowResponseVisualMode || responseMode === 'NO_SIGNIFICANT_RESPONSE';
  const lineColor = responseMode === 'NO_SIGNIFICANT_RESPONSE' ? '#94a3b8' : responseMode === 'STABILIZING_RESPONSE' ? '#06b6d4' : '#38bdf8';

  return (
    <ChartShell
      id="spp-mechanistic-sync"
      title="Network Synchronization"
      subtitle="Dose vs coupling"
      description="Increase means stronger seizure-like coupling; decrease means improved stability. Calcium blockers should push this curve down when the response is stabilizing."
      legendItems={[{ label: 'Synchronization index', color: lineColor }]}
    >
      <ResponsiveContainer width="100%" height="100%">
        <LineChart data={data} margin={{ top: 24, right: 28, left: 8, bottom: 18 }}>
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
            domain={[0, 1]}
            label={{ value: 'Synchronization index', angle: -90, position: 'insideLeft', fill: '#94a3b8', fontSize: 12 }}
          />
          <Tooltip content={<MechanisticTooltip labelPrefix="Dose" explanation="Increase means higher seizure-like coupling; decrease means stabilization." fieldLabels={{ sync: 'Synchronization index', dose: 'Dose' }} />} />
          {markers.ic50 != null ? <ReferenceLine x={markers.ic50} stroke="#22d3ee" strokeDasharray="4 4" /> : null}
          {markers.onsetDose != null ? <ReferenceLine x={markers.onsetDose} stroke="#10b981" strokeDasharray="4 4" /> : null}
          {markers.toxicThreshold != null ? <ReferenceLine x={markers.toxicThreshold} stroke="#ef4444" strokeDasharray="4 4" /> : null}
          <Line type="monotone" dataKey="sync" stroke={lineColor} strokeWidth={mutedMode ? 2.2 : 3} dot={{ r: mutedMode ? 2 : 2.6, fill: '#ecfeff' }} activeDot={{ r: mutedMode ? 3.5 : 5 }} name="Synchronization index" />
        </LineChart>
      </ResponsiveContainer>
    </ChartShell>
  );
}
