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

  const legendItems = [
    { label: 'Synchronization Index', color: lineColor },
    ...(markers.ic50 != null ? [{ label: 'IC50 Marker', color: '#22d3ee' }] : []),
    ...(markers.onsetDose != null ? [{ label: 'Onset Dose', color: '#10b981' }] : []),
    ...(markers.toxicThreshold != null ? [{ label: 'Toxic Threshold', color: '#ef4444' }] : [])
  ];

  return (
    <ChartShell
      id="spp-mechanistic-sync"
      title="Network Synchronization"
      subtitle="Dose vs neural population coupling"
      description="Measures collective population phase-locking. Elevated values signal hypersynchrony; controlled decreases indicate therapeutic circuit stabilization."
      legendItems={legendItems}
    >
      <ResponsiveContainer width="100%" height="100%">
        <LineChart data={data} margin={{ top: 24, right: 28, left: 8, bottom: 18 }}>
          <XAxis
            dataKey="dose"
            stroke="#cbd5e1"
            tick={{ fontSize: 12, fill: '#cbd5e1' }}
            tickLine={{ stroke: 'rgba(255,255,255,0.16)' }}
            axisLine={{ stroke: 'rgba(255,255,255,0.18)' }}
            label={{ value: 'Dose (µM)', position: 'insideBottom', offset: -8, fill: '#94a3b8', fontSize: 12 }}
          />
          <YAxis
            stroke="#cbd5e1"
            tick={{ fontSize: 12, fill: '#cbd5e1' }}
            tickLine={{ stroke: 'rgba(255,255,255,0.16)' }}
            axisLine={{ stroke: 'rgba(255,255,255,0.18)' }}
            domain={[0, 1]}
            tickFormatter={(val: number) => val.toFixed(2)}
            label={{ value: 'Sync Index', angle: -90, position: 'insideLeft', fill: '#94a3b8', fontSize: 12 }}
          />
          <Tooltip
            content={
              <MechanisticTooltip
                labelPrefix="Dose"
                explanation="Values closer to 1.0 indicate high phase-locking across neurons; lower values indicate independent firing."
                fieldLabels={{ sync: 'Sync Index', dose: 'Dose (µM)' }}
              />
            }
          />
          {markers.ic50 != null ? <ReferenceLine x={markers.ic50} stroke="#22d3ee" strokeDasharray="4 4" strokeWidth={1.5} /> : null}
          {markers.onsetDose != null ? <ReferenceLine x={markers.onsetDose} stroke="#10b981" strokeDasharray="4 4" strokeWidth={1.5} /> : null}
          {markers.toxicThreshold != null ? <ReferenceLine x={markers.toxicThreshold} stroke="#ef4444" strokeDasharray="4 4" strokeWidth={1.5} /> : null}
          <Line
            type="monotone"
            dataKey="sync"
            stroke={lineColor}
            strokeWidth={mutedMode ? 2.2 : 3}
            dot={{ r: mutedMode ? 2 : 2.6, fill: '#ecfeff' }}
            activeDot={{ r: mutedMode ? 3.5 : 5 }}
            name="Synchronization index"
          />
        </LineChart>
      </ResponsiveContainer>
    </ChartShell>
  );
}
