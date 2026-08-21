import { Line, LineChart, ReferenceLine, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';
import { ChartShell } from './ChartShell';
import type { NormalizedDoseResult, NormalizedMarkers, ResponseMode } from './chartUtils';
import { MechanisticTooltip } from './chartUtils';

interface FiringRateChartProps {
  data: NormalizedDoseResult[];
  markers: NormalizedMarkers;
  responseMode: ResponseMode;
}

export function FiringRateChart({ data, markers, responseMode }: FiringRateChartProps) {
  const mutedMode = markers.lowResponseVisualMode || responseMode === 'NO_SIGNIFICANT_RESPONSE';
  const lineColor = responseMode === 'NO_SIGNIFICANT_RESPONSE' ? '#94a3b8' : responseMode === 'EXCITATORY_RESPONSE' ? '#f59e0b' : responseMode === 'STABILIZING_RESPONSE' ? '#06b6d4' : '#22c55e';
  const domain: [number | string, number | string] = mutedMode ? [0, 28] : ['dataMin - 2', 'dataMax + 4'];

  const legendItems = [
    { label: 'Firing rate (Hz)', color: lineColor },
    ...(markers.ic50 != null ? [{ label: 'IC50 Marker', color: '#22d3ee' }] : []),
    ...(markers.onsetDose != null ? [{ label: 'Onset dose', color: '#10b981' }] : []),
    ...(markers.toxicThreshold != null ? [{ label: 'Toxic threshold', color: '#ef4444' }] : [])
  ];

  return (
    <ChartShell
      id="spp-mechanistic-firing"
      title="Firing Rate Change"
      subtitle="Dose vs neuronal spike frequency"
      description="Shows how compound exposure alters action potential generation. Sodium blockers suppress firing; potassium blockers elevate excitability; calcium blockers modulate network pacing."
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
            domain={domain}
            tickFormatter={(val: number) => `${Math.round(val)} Hz`}
            label={{ value: 'Firing rate (Hz)', angle: -90, position: 'insideLeft', fill: '#94a3b8', fontSize: 12 }}
          />
          <Tooltip
            content={
              <MechanisticTooltip
                labelPrefix="Dose"
                explanation="Higher firing rate indicates elevated spike generation; lower firing rate indicates microcircuit suppression."
                fieldLabels={{ firing_rate: 'Firing rate (Hz)', dose: 'Dose (µM)', effect: 'Effect %' }}
              />
            }
          />
          {markers.ic50 != null ? <ReferenceLine x={markers.ic50} stroke="#22d3ee" strokeDasharray="4 4" strokeWidth={1.5} /> : null}
          {markers.onsetDose != null ? <ReferenceLine x={markers.onsetDose} stroke="#10b981" strokeDasharray="4 4" strokeWidth={1.5} /> : null}
          {markers.toxicThreshold != null ? <ReferenceLine x={markers.toxicThreshold} stroke="#ef4444" strokeDasharray="4 4" strokeWidth={1.5} /> : null}
          <Line
            type="monotone"
            dataKey="firing_rate"
            stroke={lineColor}
            strokeWidth={mutedMode ? 2.2 : 3}
            dot={{ r: mutedMode ? 2 : 2.6, fill: '#f8fafc' }}
            activeDot={{ r: mutedMode ? 3.5 : 5 }}
            name="Firing rate"
          />
        </LineChart>
      </ResponsiveContainer>
    </ChartShell>
  );
}
