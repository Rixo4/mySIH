import { Line, LineChart, ReferenceLine, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';
import { ChartShell } from './ChartShell';
import type { NormalizedDoseResult, NormalizedMarkers, ResponseMode } from './chartUtils';
import { MechanisticTooltip } from './chartUtils';

interface NiiChartProps {
  data: NormalizedDoseResult[];
  markers: NormalizedMarkers;
  responseMode: ResponseMode;
}

export function NiiChart({ data, markers, responseMode }: NiiChartProps) {
  const mutedMode = markers.lowResponseVisualMode || responseMode === 'NO_SIGNIFICANT_RESPONSE';
  const lineColor = responseMode === 'NO_SIGNIFICANT_RESPONSE' ? '#94a3b8' : responseMode === 'STABILIZING_RESPONSE' ? '#8b5cf6' : '#c084fc';

  // Construct legend items that strictly match elements present on the graph
  const legendItems = [
    { label: 'Neural Instability Index (NII)', color: lineColor },
    ...(markers.ic50 != null ? [{ label: 'IC50 Marker', color: '#22d3ee' }] : []),
    ...(markers.onsetDose != null ? [{ label: 'Onset Dose', color: '#10b981' }] : []),
    ...(markers.toxicThreshold != null ? [{ label: 'Toxic Threshold', color: '#ef4444' }] : [])
  ];

  return (
    <ChartShell
      id="spp-mechanistic-nii"
      title="Neural Instability Index (NII)"
      subtitle="Dose vs microcircuit stability"
      description="NII quantifies cortical instability derived from firing rates, network synchrony, and inter-spike variability. Lower values indicate safer, more stable dynamics."
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
            domain={[0, (dataMax: number) => Math.max(0.1, Number((dataMax * 1.25 || 0.1).toFixed(2)))]}
            tickFormatter={(val: number) => Number(val.toFixed(3)).toString()}
            label={{ value: 'NII Score', angle: -90, position: 'insideLeft', fill: '#94a3b8', fontSize: 12 }}
          />
          <Tooltip
            content={
              <MechanisticTooltip
                labelPrefix="Dose"
                explanation="NII summarizes neural instability using firing, synchronization, and variability signals."
                fieldLabels={{ nii: 'NII Score', dose: 'Dose (µM)' }}
              />
            }
          />
          {markers.ic50 != null ? <ReferenceLine x={markers.ic50} stroke="#22d3ee" strokeDasharray="4 4" strokeWidth={1.5} /> : null}
          {markers.onsetDose != null ? <ReferenceLine x={markers.onsetDose} stroke="#10b981" strokeDasharray="4 4" strokeWidth={1.5} /> : null}
          {markers.toxicThreshold != null ? <ReferenceLine x={markers.toxicThreshold} stroke="#ef4444" strokeDasharray="4 4" strokeWidth={1.5} /> : null}
          <Line
            type="monotone"
            dataKey="nii"
            stroke={lineColor}
            strokeWidth={mutedMode ? 2.2 : 3}
            dot={{ r: mutedMode ? 2 : 2.6, fill: '#f5f3ff' }}
            activeDot={{ r: mutedMode ? 3.5 : 5 }}
            name="NII Score"
          />
        </LineChart>
      </ResponsiveContainer>
    </ChartShell>
  );
}
