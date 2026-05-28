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

  return (
    <ChartShell
      id="spp-mechanistic-seizure"
      title="Seizure-Risk Trend"
      subtitle="Dose vs neural instability"
      description="Higher seizure score indicates elevated neural instability. This graph is especially important for potassium-blocker / 4-AP style response modes."
      legendItems={[{ label: 'Seizure score', color: emphasisColor }, { label: 'Toxic threshold', color: '#ef4444' }]}
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
            domain={mutedMode ? [0, 100] : ['dataMin - 4', 'dataMax + 8']}
            label={{ value: 'Seizure score', angle: -90, position: 'insideLeft', fill: '#94a3b8', fontSize: 12 }}
          />
          <Tooltip content={<MechanisticTooltip labelPrefix="Dose" explanation="Higher seizure score indicates elevated neural instability." fieldLabels={{ seizure_score: 'Seizure score', dose: 'Dose', effect: 'Effect %' }} />} />
          {markers.ic50 != null ? <ReferenceLine x={markers.ic50} stroke="#22d3ee" strokeDasharray="4 4" /> : null}
          {markers.onsetDose != null ? <ReferenceLine x={markers.onsetDose} stroke="#10b981" strokeDasharray="4 4" /> : null}
          {markers.toxicThreshold != null ? <ReferenceLine x={markers.toxicThreshold} stroke="#ef4444" strokeDasharray="4 4" label="Warning threshold" /> : null}
          <Line type="monotone" dataKey="seizure_score" stroke={emphasisColor} strokeWidth={mutedMode ? 2.2 : 3} dot={{ r: mutedMode ? 2 : 2.6, fill: '#fff1f2' }} activeDot={{ r: mutedMode ? 3.5 : 5 }} name="Seizure score" />
        </LineChart>
      </ResponsiveContainer>
    </ChartShell>
  );
}
