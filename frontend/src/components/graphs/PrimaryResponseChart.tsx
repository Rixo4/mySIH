import { Area, ComposedChart, ReferenceArea, ReferenceLine, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts';
import { ChartShell } from './ChartShell';
import type { NormalizedDoseResult, NormalizedMarkers, ResponseMode } from './chartUtils';
import { getResponseModeCopy, MechanisticTooltip } from './chartUtils';

interface PrimaryResponseChartProps {
  data: NormalizedDoseResult[];
  markers: NormalizedMarkers;
  responseMode: ResponseMode;
}

export function PrimaryResponseChart({ data, markers, responseMode }: PrimaryResponseChartProps) {
  const copy = getResponseModeCopy(responseMode);
  const hasValidatedTherapeuticWindow = markers.hasValidatedTherapeuticWindow;
  const mutedMode = responseMode === 'NO_SIGNIFICANT_RESPONSE';
  const primaryColor = mutedMode ? '#94a3b8' : responseMode === 'EXCITATORY_RESPONSE' ? '#f59e0b' : responseMode === 'STABILIZING_RESPONSE' ? '#06b6d4' : '#22c55e';
  const fillOpacity = mutedMode ? 0.08 : 0.18;

  // Dynamically assemble legend items matching what is actually rendered
  const legendItems = [
    { label: `${copy.title.split(' ')[0]} Signal`, color: primaryColor },
    ...(markers.ic50 != null ? [{ label: `IC50 (${markers.ic50Label || 'Marker'})`, color: '#22d3ee' }] : []),
    ...(markers.onsetDose != null ? [{ label: 'Onset Dose', color: '#10b981' }] : []),
    ...(markers.toxicThreshold != null ? [{ label: 'Toxic Threshold', color: '#ef4444' }] : [])
  ];

  return (
    <ChartShell
      id="spp-mechanistic-primary"
      title={copy.title}
      subtitle="Primary mechanistic response"
      description={copy.explanation}
      legendItems={legendItems}
      chartHeightClassName="h-[360px] lg:h-[400px]"
    >
      <ResponsiveContainer width="100%" height="100%">
        <ComposedChart data={data} margin={{ top: 24, right: 28, left: 8, bottom: 18 }}>
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
            domain={[0, 100]}
            tickFormatter={(val: number) => `${Math.round(val)}%`}
            label={{ value: copy.axisLabel, angle: -90, position: 'insideLeft', fill: '#94a3b8', fontSize: 12 }}
          />
          <Tooltip
            content={
              <MechanisticTooltip
                labelPrefix="Dose"
                explanation={copy.explanation}
                fieldLabels={{
                  effect: 'Effect %',
                  dose: 'Dose (µM)',
                  firing_rate: 'Firing rate (Hz)',
                  seizure_score: 'Seizure score',
                  sync: 'Synchronization',
                  nii: 'NII Score',
                  toxicity_score: 'Toxicity score'
                }}
                suffixByKey={{ effect: '%', seizure_score: '', toxicity_score: '', sync: '', nii: '' }}
              />
            }
            cursor={{ stroke: 'rgba(148,163,184,0.35)', strokeWidth: 1 }}
          />
          {data.length > 0 ? (
            <>
              <ReferenceArea
                x1={data[0].dose}
                x2={markers.onsetDose ?? markers.therapeuticMin ?? data[0].dose}
                fill="#475569"
                fillOpacity={0.08}
                strokeOpacity={0}
              />
              {responseMode === 'EXCITATORY_RESPONSE' ? (
                <>
                  <ReferenceArea
                    x1={markers.onsetDose ?? data[0].dose}
                    x2={markers.toxicThreshold ?? data[data.length - 1].dose}
                    fill="#f59e0b"
                    fillOpacity={0.09}
                    strokeOpacity={0}
                  />
                  <ReferenceArea
                    x1={markers.toxicThreshold ?? data[data.length - 1].dose}
                    x2={data[data.length - 1].dose}
                    fill="#ef4444"
                    fillOpacity={0.12}
                    strokeOpacity={0}
                  />
                </>
              ) : responseMode === 'STABILIZING_RESPONSE' ? (
                <>
                  <ReferenceArea
                    x1={markers.onsetDose ?? data[0].dose}
                    x2={markers.toxicThreshold ?? data[data.length - 1].dose}
                    fill="#06b6d4"
                    fillOpacity={0.09}
                    strokeOpacity={0}
                  />
                  <ReferenceArea
                    x1={markers.toxicThreshold ?? data[data.length - 1].dose}
                    x2={data[data.length - 1].dose}
                    fill="#8b5cf6"
                    fillOpacity={0.11}
                    strokeOpacity={0}
                  />
                </>
              ) : (
                <>
                  <ReferenceArea
                    x1={markers.onsetDose ?? data[0].dose}
                    x2={markers.toxicThreshold ?? data[data.length - 1].dose}
                    fill={hasValidatedTherapeuticWindow ? '#10b981' : '#64748b'}
                    fillOpacity={0.09}
                    strokeOpacity={0}
                  />
                  <ReferenceArea
                    x1={markers.toxicThreshold ?? data[data.length - 1].dose}
                    x2={data[data.length - 1].dose}
                    fill="#6366f1"
                    fillOpacity={0.11}
                    strokeOpacity={0}
                  />
                </>
              )}
            </>
          ) : null}
          {markers.ic50 != null ? (
            <ReferenceLine x={markers.ic50} stroke="#22d3ee" strokeDasharray="4 4" strokeWidth={1.5} />
          ) : null}
          {markers.onsetDose != null ? (
            <ReferenceLine x={markers.onsetDose} stroke="#10b981" strokeDasharray="4 4" strokeWidth={1.5} />
          ) : null}
          {markers.toxicThreshold != null ? (
            <ReferenceLine x={markers.toxicThreshold} stroke="#ef4444" strokeDasharray="4 4" strokeWidth={1.5} />
          ) : null}
          <Area
            type="monotone"
            dataKey="effect"
            stroke={primaryColor}
            fillOpacity={fillOpacity}
            fill={primaryColor}
            strokeWidth={mutedMode ? 2 : 3}
            dot={{ r: mutedMode ? 1.8 : 2.4, fill: '#e2e8f0' }}
            activeDot={{ r: mutedMode ? 3.5 : 5 }}
            name="Effect %"
          />
        </ComposedChart>
      </ResponsiveContainer>
    </ChartShell>
  );
}
