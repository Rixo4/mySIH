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

  return (
    <ChartShell
      id="spp-mechanistic-nii"
      title="Neural Instability Index"
      subtitle="Dose vs instability"
      description="NII summarizes neural instability using firing, synchronization, and variability signals. A lower curve indicates a safer and more stable response."
      legendItems={[{ label: 'NII', color: lineColor }]}
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
            domain={[0, 'dataMax + 0.1']}
            label={{ value: 'NII', angle: -90, position: 'insideLeft', fill: '#94a3b8', fontSize: 12 }}
          />
          <Tooltip content={<MechanisticTooltip labelPrefix="Dose" explanation="NII summarizes neural instability using firing, synchronization, and variability signals." fieldLabels={{ nii: 'NII', dose: 'Dose' }} />} />
          {markers.ic50 != null ? <ReferenceLine x={markers.ic50} stroke="#22d3ee" strokeDasharray="4 4" /> : null}
          {markers.onsetDose != null ? <ReferenceLine x={markers.onsetDose} stroke="#10b981" strokeDasharray="4 4" /> : null}
          {markers.toxicThreshold != null ? <ReferenceLine x={markers.toxicThreshold} stroke="#ef4444" strokeDasharray="4 4" /> : null}
          <Line type="monotone" dataKey="nii" stroke={lineColor} strokeWidth={mutedMode ? 2.2 : 3} dot={{ r: mutedMode ? 2 : 2.6, fill: '#f5f3ff' }} activeDot={{ r: mutedMode ? 3.5 : 5 }} name="NII" />
        </LineChart>
      </ResponsiveContainer>
    </ChartShell>
  );
}
