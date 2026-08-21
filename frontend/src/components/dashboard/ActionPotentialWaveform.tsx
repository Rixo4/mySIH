import React, { useState } from 'react';
import type { VoltageTracePoint } from '../../types';

interface ActionPotentialWaveformProps {
  firingRateHz?: number | null;
  restingPotentialMv?: number | null;
  thresholdMv?: number;
  voltageTrace?: VoltageTracePoint[];
  onThresholdChange?: (val: number) => void;
  hasActiveData?: boolean;
}

export function ActionPotentialWaveform({
  firingRateHz,
  restingPotentialMv,
  thresholdMv = 20,
  voltageTrace = [],
  onThresholdChange,
  hasActiveData = true,
}: ActionPotentialWaveformProps) {
  const [val, setVal] = useState(thresholdMv);

  const handleChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const next = Number(e.target.value);
    setVal(next);
    onThresholdChange?.(next);
  };

  const sliderPct = ((val - 5) / 35) * 100;
  const hasRealTrace = hasActiveData && voltageTrace.length > 0;

  // Calculate resting potential
  const currentResting = hasActiveData && restingPotentialMv !== null && restingPotentialMv !== undefined
    ? restingPotentialMv
    : (hasRealTrace ? voltageTrace[0].voltage : null);

  const displayFiringRate = hasActiveData && firingRateHz !== undefined && firingRateHz !== null
    ? firingRateHz
    : null;

  // Generate SVG path from real voltageTrace
  const generatePath = () => {
    if (!hasRealTrace) {
      return 'M 0 12 L 240 12'; // Flatline
    }

    const minV = -85;
    const maxV = 40;
    const pts = voltageTrace.slice(0, 120);
    const stepX = 240 / Math.max(1, pts.length - 1);

    return pts
      .map((pt, i) => {
        const x = i * stepX;
        const norm = Math.max(0, Math.min(1, (pt.voltage - minV) / (maxV - minV)));
        const y = 22 - norm * 20;
        return `${i === 0 ? 'M' : 'L'} ${x.toFixed(1)} ${y.toFixed(1)}`;
      })
      .join(' ');
  };

  const wavePath = generatePath();

  return (
    <div
      className="relative flex flex-col justify-between h-full select-none overflow-hidden"
      style={{
        background: '#111418',
        border: '1px solid #1D2127',
        borderRadius: 6,
        padding: '4px 8px',
      }}
    >
      {/* ── Header ── */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 1 }}>
        <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 10, fontWeight: 600, color: hasActiveData ? '#D4D8DE' : '#6B7280', textTransform: 'uppercase', letterSpacing: '0.05em' }}>
          Membrane Potential
        </span>
        <span style={{ fontFamily: 'JetBrains Mono, monospace', fontSize: 9.5, fontWeight: 600, color: hasActiveData && displayFiringRate !== null ? '#3B82F6' : '#6B7280' }}>
          ↯ {hasActiveData && displayFiringRate !== null ? `${displayFiringRate.toFixed(2)} Hz` : '— Hz'}
        </span>
      </div>

      {/* ── Hero metric ── */}
      <div style={{ display: 'flex', alignItems: 'baseline', justifyContent: 'center', gap: 3, margin: '0' }}>
        <span style={{ fontFamily: 'JetBrains Mono, monospace', fontSize: 20, fontWeight: 700, color: hasActiveData && currentResting !== null ? '#D4D8DE' : '#6B7280', lineHeight: 1, letterSpacing: '-0.02em' }}>
          {hasActiveData && currentResting !== null ? currentResting.toFixed(0) : '—'}
        </span>
        <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 9, color: '#6B7280' }}>mV</span>
      </div>

      {/* ── Waveform SVG ── */}
      <div style={{ position: 'relative', width: '100%', height: 18, margin: '1px 0' }}>
        <svg viewBox="0 0 240 24" style={{ width: '100%', height: '100%', overflow: 'visible' }}>
          {/* Background baseline */}
          <line x1="0" y1="12" x2="240" y2="12" stroke="#1D2127" strokeWidth="0.5" strokeDasharray={hasRealTrace ? undefined : "3,3"} />
          {/* Waveform */}
          <path
            d={wavePath}
            fill="none"
            stroke={hasRealTrace ? "#3B82F6" : "#252B34"}
            strokeWidth="1.5"
            strokeLinecap="round"
            opacity={hasRealTrace ? 0.85 : 0.4}
          />
          {/* Shadow trace */}
          {hasRealTrace && (
            <path
              d={wavePath}
              fill="none"
              stroke="#1E3A5F"
              strokeWidth="3"
              strokeLinecap="round"
              opacity="0.4"
            />
          )}
          {/* Cursor dot */}
          {hasRealTrace && (
            <circle cx="140" cy="8" r="2.5" fill="#D4D8DE" stroke="#3B82F6" strokeWidth="1" />
          )}
        </svg>
      </div>

      {/* ── Threshold slider ── */}
      <div style={{ margin: '2px 0' }}>
        <style>{`
          input[type="range"].sci-slider { -webkit-appearance: none; appearance: none; height: 2px; border-radius: 1px; outline: none; cursor: pointer; }
          input[type="range"].sci-slider::-webkit-slider-thumb { -webkit-appearance: none; width: 10px; height: 10px; border-radius: 50%; background: #D4D8DE; border: 2px solid #3B82F6; cursor: pointer; }
          input[type="range"].sci-slider::-moz-range-thumb { width: 10px; height: 10px; border-radius: 50%; background: #D4D8DE; border: 2px solid #3B82F6; cursor: pointer; }
        `}</style>
        <input
          type="range"
          min="5" max="40"
          value={val}
          onChange={handleChange}
          disabled={!hasActiveData}
          className="sci-slider"
          style={{
            width: '100%',
            background: hasActiveData
              ? `linear-gradient(to right, #3B82F6 0%, #3B82F6 ${sliderPct}%, #1C2028 ${sliderPct}%)`
              : '#161A20',
          }}
        />
      </div>

      {/* ── Footer ── */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginTop: 2, paddingTop: 3, borderTop: '1px solid #1D2127' }}>
        <span style={{ fontFamily: 'JetBrains Mono, monospace', fontSize: 8.5, color: '#424956' }}>−80 mV</span>
        <span style={{ fontFamily: 'JetBrains Mono, monospace', fontSize: 8.5, color: '#424956' }}>40 mV</span>
      </div>
    </div>
  );
}
