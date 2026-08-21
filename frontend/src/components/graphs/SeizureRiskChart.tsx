import { Line, LineChart, ReferenceLine, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';
import { ChartShell } from './ChartShell';
import type { NormalizedDoseResult, NormalizedMarkers, ResponseMode } from './chartUtils';
import { MechanisticTooltip } from './chartUtils';

interface SeizureRiskChartProps {
  data: NormalizedDoseResult[];
  markers: NormalizedMarkers;
  responseMode: ResponseMode;
}

export function SeizureRiskChart({ data, markers, responseMode }: SeizureRiskChartProps) {
  const mutedMode = markers.lowResponseVisualMode || responseMode === 'NO_SIGNIFICANT_RESPONSE';
  const emphasisColor = responseMode === 'NO_SIGNIFICANT_RESPONSE' ? '#94a3b8' : responseMode === 'EXCITATORY_RESPONSE' ? '#ef4444' : '#fb7185';

  const legendItems = [
    { label: 'Seizure Risk Score', color: emphasisColor },
    ...(markers.ic50 != null ? [{ label: 'IC50 Marker', color: '#22d3ee' }] : []),
    ...(markers.onsetDose != null ? [{ label: 'Onset Dose', color: '#10b981' }] : []),
    ...(markers.toxicThreshold != null ? [{ label: 'Toxic Threshold', color: '#ef4444' }] : [])
  ];

  return (
    <ChartShell
      id="spp-mechanistic-seizure"
      title="Seizure-Risk Trend"
      subtitle="Dose vs pro-convulsant liability"
      description="Higher seizure risk score indicates elevated neural instability and paroxysmal discharge likelihood, particularly critical during potassium-channel blockade."
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
            domain={mutedMode ? [0, 100] : ['dataMin - 4', 'dataMax + 8']}
            tickFormatter={(val: number) => Math.round(val).toString()}
            label={{ value: 'Risk Score', angle: -90, position: 'insideLeft', fill: '#94a3b8', fontSize: 12 }}
          />
          <Tooltip
            content={
              <MechanisticTooltip
                labelPrefix="Dose"
                explanation="Higher seizure risk score indicates elevated neural instability and paroxysmal burst discharge."
                fieldLabels={{ seizure_score: 'Risk Score', dose: 'Dose (µM)', effect: 'Effect %' }}
              />
            }
          />
          {markers.ic50 != null ? <ReferenceLine x={markers.ic50} stroke="#22d3ee" strokeDasharray="4 4" strokeWidth={1.5} /> : null}
          {markers.onsetDose != null ? <ReferenceLine x={markers.onsetDose} stroke="#10b981" strokeDasharray="4 4" strokeWidth={1.5} /> : null}
          {markers.toxicThreshold != null ? <ReferenceLine x={markers.toxicThreshold} stroke="#ef4444" strokeDasharray="4 4" strokeWidth={1.5} /> : null}
          <Line
            type="monotone"
            dataKey="seizure_score"
            stroke={emphasisColor}
            strokeWidth={mutedMode ? 2.2 : 3}
            dot={{ r: mutedMode ? 2 : 2.6, fill: '#fff1f2' }}
            activeDot={{ r: mutedMode ? 3.5 : 5 }}
            name="Seizure score"
          />
        </LineChart>
      </ResponsiveContainer>
    </ChartShell>
  );
}
