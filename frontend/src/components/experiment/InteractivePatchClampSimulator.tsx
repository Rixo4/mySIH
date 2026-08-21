import React, { useState, useMemo } from 'react';
import { Activity, Zap, Layers } from 'lucide-react';
import type { DrugEvalRequest } from '../../types';

interface InteractivePatchClampSimulatorProps {
  payload?: DrugEvalRequest;
  value?: DrugEvalRequest;
  isSimulating?: boolean;
}

export function InteractivePatchClampSimulator({
  payload: payloadProp,
  value: valProp,
  isSimulating: _isSimulating = false,
}: InteractivePatchClampSimulatorProps) {
  const safePayload = payloadProp ?? valProp;
  const [hoveredDose, setHoveredDose] = useState<number | null>(null);

  const channels = safePayload?.channels;
  const doseRange = safePayload?.dose_range ?? { min: 0, max: 20, step: 2 };

  const hasConfiguredChannels = !!channels && (
    (channels.Na?.ic50 ?? 0) > 0 ||
    (channels.K?.ic50 ?? 0) > 0 ||
    (channels.Ca?.ic50 ?? 0) > 0
  );

  // Compute Hill-Langmuir binding curve points across the dose range:
  // Fractional Block = 1 / (1 + (IC50 / Dose)^Hill) * 100%
  const curveData = useMemo(() => {
    if (!hasConfiguredChannels) return [];

    const min = Math.max(0.1, doseRange.min);
    const max = Math.max(min + 1, doseRange.max);
    const numPoints = 40;
    const points: Array<{ dose: number; naBlock: number; kBlock: number; caBlock: number }> = [];

    for (let i = 0; i <= numPoints; i++) {
      const dose = min + (i / numPoints) * (max - min);

      const naIc50 = channels?.Na?.ic50 ?? 0;
      const naHill = Math.max(0.1, channels?.Na?.hill ?? 1.0);
      const naBlock = naIc50 > 0 ? (1 / (1 + Math.pow(naIc50 / dose, naHill))) * 100 : 0;

      const kIc50 = channels?.K?.ic50 ?? 0;
      const kHill = Math.max(0.1, channels?.K?.hill ?? 1.0);
      const kBlock = kIc50 > 0 ? (1 / (1 + Math.pow(kIc50 / dose, kHill))) * 100 : 0;

      const caIc50 = channels?.Ca?.ic50 ?? 0;
      const caHill = Math.max(0.1, channels?.Ca?.hill ?? 1.0);
      const caBlock = caIc50 > 0 ? (1 / (1 + Math.pow(caIc50 / dose, caHill))) * 100 : 0;

      points.push({ dose, naBlock, kBlock, caBlock });
    }

    return points;
  }, [channels, doseRange, hasConfiguredChannels]);

  const minDose = doseRange.min;
  const maxDose = Math.max(minDose + 1, doseRange.max);
  const activeDose = hoveredDose !== null ? hoveredDose : (minDose + maxDose) / 2;

  const activeMetrics = useMemo(() => {
    if (!hasConfiguredChannels) {
      return { na: 0, k: 0, ca: 0 };
    }

    const naIc50 = channels?.Na?.ic50 ?? 0;
    const naHill = Math.max(0.1, channels?.Na?.hill ?? 1.0);
    const naBlock = naIc50 > 0 ? (1 / (1 + Math.pow(naIc50 / activeDose, naHill))) * 100 : 0;

    const kIc50 = channels?.K?.ic50 ?? 0;
    const kHill = Math.max(0.1, channels?.K?.hill ?? 1.0);
    const kBlock = kIc50 > 0 ? (1 / (1 + Math.pow(kIc50 / activeDose, kHill))) * 100 : 0;

    const caIc50 = channels?.Ca?.ic50 ?? 0;
    const caHill = Math.max(0.1, channels?.Ca?.hill ?? 1.0);
    const caBlock = caIc50 > 0 ? (1 / (1 + Math.pow(caIc50 / activeDose, caHill))) * 100 : 0;

    return {
      na: Math.round(naBlock),
      k: Math.round(kBlock),
      ca: Math.round(caBlock),
    };
  }, [channels, activeDose, hasConfiguredChannels]);

  const svgWidth = 440;
  const svgHeight = 220;
  const padding = { top: 20, right: 20, bottom: 35, left: 35 };
  const graphWidth = svgWidth - padding.left - padding.right;
  const graphHeight = svgHeight - padding.top - padding.bottom;

  const getX = (dose: number) => padding.left + ((dose - minDose) / (maxDose - minDose || 1)) * graphWidth;
  const getY = (blockPct: number) => padding.top + graphHeight - (blockPct / 100) * graphHeight;

  const naPath = curveData.map((d, i) => `${i === 0 ? 'M' : 'L'} ${getX(d.dose)} ${getY(d.naBlock)}`).join(' ');
  const kPath = curveData.map((d, i) => `${i === 0 ? 'M' : 'L'} ${getX(d.dose)} ${getY(d.kBlock)}`).join(' ');
  const caPath = curveData.map((d, i) => `${i === 0 ? 'M' : 'L'} ${getX(d.dose)} ${getY(d.caBlock)}`).join(' ');

  const activeX = getX(activeDose);

  return (
    <div className="flex flex-col justify-between h-full space-y-2.5 font-sans text-slate-100">
      {/* Header */}
      <div className="flex items-center justify-between pb-2 border-b border-slate-800/80">
        <div className="flex items-center gap-2">
          <Activity className="w-3.5 h-3.5 text-sky-400" />
          <h3 className="text-xs font-bold uppercase tracking-wider text-slate-200">
            Hill-Langmuir Gating Kinetics
          </h3>
        </div>
        <span className={`text-[9px] font-mono font-bold px-2 py-0.5 rounded border ${
          hasConfiguredChannels
            ? 'text-emerald-400 bg-emerald-500/10 border-emerald-500/30'
            : 'text-slate-500 bg-[#111120] border-slate-800'
        }`}>
          {hasConfiguredChannels ? 'LIVE KINETICS PREVIEW' : 'STANDBY · AWAITING INPUT'}
        </span>
      </div>

      {/* Hill Kinetics SVG Curve Display */}
      <div className="relative w-full rounded-xl bg-[#111120] border border-slate-800 p-2">
        <svg
          viewBox={`0 0 ${svgWidth} ${svgHeight}`}
          className="w-full h-40 select-none cursor-crosshair overflow-visible"
          onMouseMove={(e) => {
            if (!hasConfiguredChannels) return;
            const rect = e.currentTarget.getBoundingClientRect();
            const mouseX = e.clientX - rect.left;
            const normalizedX = Math.max(
              0,
              Math.min(
                1,
                (mouseX - (padding.left / svgWidth) * rect.width) /
                  ((graphWidth / svgWidth) * rect.width)
              )
            );
            const calculatedDose = minDose + normalizedX * (maxDose - minDose);
            setHoveredDose(Number(calculatedDose.toFixed(1)));
          }}
          onMouseLeave={() => setHoveredDose(null)}
        >
          {/* Grid lines */}
          {[0, 25, 50, 75, 100].map((pct) => (
            <g key={pct}>
              <line
                x1={padding.left}
                y1={getY(pct)}
                x2={padding.left + graphWidth}
                y2={getY(pct)}
                stroke="#1e293b"
                strokeWidth="1"
                strokeDasharray="2 3"
              />
              <text
                x={padding.left - 6}
                y={getY(pct) + 3}
                fill="#64748b"
                fontSize="8"
                fontFamily="JetBrains Mono"
                textAnchor="end"
              >
                {pct}%
              </text>
            </g>
          ))}

          {/* X Axis Ticks */}
          <text
            x={padding.left}
            y={padding.top + graphHeight + 16}
            fill="#64748b"
            fontSize="8"
            fontFamily="JetBrains Mono"
            textAnchor="middle"
          >
            {minDose.toFixed(1)} nM
          </text>
          <text
            x={padding.left + graphWidth / 2}
            y={padding.top + graphHeight + 16}
            fill="#64748b"
            fontSize="8"
            fontFamily="JetBrains Mono"
            textAnchor="middle"
          >
            {((minDose + maxDose) / 2).toFixed(1)} nM
          </text>
          <text
            x={padding.left + graphWidth}
            y={padding.top + graphHeight + 16}
            fill="#64748b"
            fontSize="8"
            fontFamily="JetBrains Mono"
            textAnchor="middle"
          >
            {maxDose.toFixed(1)} nM
          </text>

          {/* Curves when configured */}
          {hasConfiguredChannels ? (
            <>
              {(channels?.Na?.ic50 ?? 0) > 0 && (
                <path d={naPath} fill="none" stroke="#38bdf8" strokeWidth="1.5" strokeLinecap="round" />
              )}
              {(channels?.K?.ic50 ?? 0) > 0 && (
                <path d={kPath} fill="none" stroke="#388bfd" strokeWidth="2.0" strokeLinecap="round" />
              )}
              {(channels?.Ca?.ic50 ?? 0) > 0 && (
                <path d={caPath} fill="none" stroke="#10b981" strokeWidth="1.5" strokeLinecap="round" />
              )}

              {/* Active Dose Scrubber Line */}
              <line
                x1={activeX}
                y1={padding.top}
                x2={activeX}
                y2={padding.top + graphHeight}
                stroke="#ef4444"
                strokeWidth="1"
                strokeDasharray="2,2"
              />

              {/* Active Intersection Dot on K+ curve */}
              {(channels?.K?.ic50 ?? 0) > 0 && (
                <circle
                  cx={activeX}
                  cy={getY(activeMetrics.k)}
                  r="4"
                  fill="#ffffff"
                  stroke="#388bfd"
                  strokeWidth="2"
                />
              )}
            </>
          ) : (
            /* Standby Placeholder */
            <text
              x={svgWidth / 2}
              y={svgHeight / 2}
              textAnchor="middle"
              fill="#475569"
              fontSize="10"
              fontFamily="Inter, sans-serif"
            >
              Configure channel IC50 or select a preset to compute live Hill curve
            </text>
          )}
        </svg>
      </div>

      {/* Live Calculated Channel Occupancy Chips */}
      <div className="space-y-1.5">
        <div className="flex items-center justify-between text-[10px] font-mono text-slate-400">
          <span>
            Scrubber Concentration:{' '}
            <strong className="text-white">
              {hasConfiguredChannels ? `${activeDose.toFixed(1)} nM` : '—'}
            </strong>
          </span>
          <span className="text-blue-400">{hasConfiguredChannels ? 'Hover graph to scrub' : 'Awaiting input'}</span>
        </div>

        <div className="grid grid-cols-3 gap-2">
          {/* Na+ Readout */}
          <div className="p-2 rounded-xl bg-[#111120] border border-slate-800 text-center">
            <div className="text-[9px] font-mono text-sky-400 font-bold uppercase">Na⁺ Block</div>
            <div className="text-sm font-mono font-black text-white mt-0.5">
              {hasConfiguredChannels && (channels?.Na?.ic50 ?? 0) > 0 ? `${activeMetrics.na}%` : '—'}
            </div>
          </div>

          {/* K+ Readout */}
          <div className="p-2 rounded-xl bg-[#111120] border border-slate-800 text-center">
            <div className="text-[9px] font-mono text-blue-400 font-bold uppercase">K⁺ Block</div>
            <div className="text-sm font-mono font-black text-white mt-0.5">
              {hasConfiguredChannels && (channels?.K?.ic50 ?? 0) > 0 ? `${activeMetrics.k}%` : '—'}
            </div>
          </div>

          {/* Ca2+ Readout */}
          <div className="p-2 rounded-xl bg-[#111120] border border-slate-800 text-center">
            <div className="text-[9px] font-mono text-emerald-400 font-bold uppercase">Ca²⁺ Block</div>
            <div className="text-sm font-mono font-black text-white mt-0.5">
              {hasConfiguredChannels && (channels?.Ca?.ic50 ?? 0) > 0 ? `${activeMetrics.ca}%` : '—'}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
