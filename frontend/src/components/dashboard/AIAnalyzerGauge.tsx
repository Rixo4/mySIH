import React, { useState } from 'react';
import { ShieldCheck, Maximize2, X } from 'lucide-react';

interface AIAnalyzerGaugeProps {
  score?: number | null;
  riskLevel?: string | null;
  confidence?: string | null;
  isSimulating?: boolean;
  hasActiveData?: boolean;
}

export function AIAnalyzerGauge({
  score,
  riskLevel,
  confidence,
  isSimulating = false,
  hasActiveData = true,
}: AIAnalyzerGaugeProps) {
  const [isZoomed, setIsZoomed] = useState(false);
  const hasRealScore = hasActiveData && score !== undefined && score !== null;

  const displayScore = hasRealScore ? Math.round(score) : null;
  const normalizedRisk = (riskLevel || '').toUpperCase();

  const getChip = () => {
    if (!hasActiveData || !riskLevel) {
      return { label: 'STANDBY', className: 'chip-neutral', color: '#6B7280' };
    }
    if (normalizedRisk === 'HIGH_RISK' || normalizedRisk === 'CRITICAL' || normalizedRisk === 'DANGER') {
      return { label: confidence || 'HIGH RISK', className: 'chip-danger', color: '#EF4444' };
    }
    if (normalizedRisk === 'MODERATE' || normalizedRisk === 'WARNING' || normalizedRisk === 'CAUTION') {
      return { label: confidence || 'MODERATE CONFIDENCE', className: 'chip-warning', color: '#F59E0B' };
    }
    return { label: confidence || 'HIGH CONFIDENCE', className: 'chip-safe', color: '#00f5d4' };
  };

  const chip = getChip();

  // Card geometry: radius = 33
  const radius = 33;
  const circumference = 2 * Math.PI * radius;
  const strokeDashoffset = hasRealScore
    ? circumference - (displayScore! / 100) * circumference
    : circumference;

  // Zoomed geometry: radius = 70
  const zRadius = 70;
  const zCircumference = 2 * Math.PI * zRadius;
  const zStrokeDashoffset = hasRealScore
    ? zCircumference - (displayScore! / 100) * zCircumference
    : zCircumference;

  return (
    <>
      <div
        onClick={() => { if (hasRealScore) setIsZoomed(true); }}
        className={`relative flex flex-col justify-between h-full select-none overflow-hidden ${hasRealScore ? 'cursor-pointer group' : ''}`}
        style={{
          background: '#111418',
          border: '1px solid #1D2127',
          borderRadius: 6,
          padding: '4px 8px',
          transition: 'border-color 0.15s, background 0.15s',
        }}
        title={hasRealScore ? "Click to zoom Bio-Assay AI" : undefined}
      >
        {/* ── Header ── */}
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 1 }}>
          <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 10, fontWeight: 600, color: hasActiveData ? '#D4D8DE' : '#6B7280', textTransform: 'uppercase', letterSpacing: '0.05em' }}>
            Bio-Assay AI
          </span>
          <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
            <span className={chip.className} style={{ fontSize: 8.5, padding: '1px 5px' }}>{chip.label}</span>
            {hasRealScore && <Maximize2 className="w-2.5 h-2.5 text-slate-500 opacity-0 group-hover:opacity-100 transition-opacity" />}
          </div>
        </div>

        {/* ── Circular Gauge ── */}
        <div style={{ position: 'relative', width: '100%', flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center' }}>
          <div style={{ position: 'relative', width: 62, height: 62, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
            <svg
              style={{ width: '100%', height: '100%', transform: 'rotate(-90deg)' }}
              viewBox="0 0 80 80"
            >
              {/* Background track */}
              <circle cx="40" cy="40" r={radius} stroke="#182026" strokeWidth="5.5" fill="transparent" />
              {/* Outer tick ring */}
              <circle cx="40" cy="40" r={radius + 4} stroke="#252D36" strokeWidth="0.5" fill="transparent" strokeDasharray="2,3" />
              {/* Value arc */}
              {hasRealScore && (
                <circle
                  cx="40" cy="40" r={radius}
                  stroke={chip.color}
                  strokeWidth="5"
                  strokeDasharray={circumference}
                  strokeDashoffset={strokeDashoffset}
                  strokeLinecap="round"
                  fill="transparent"
                  style={{ filter: `drop-shadow(0 0 6px ${chip.color}99)` }}
                />
              )}
            </svg>

            {/* Centered percentage text inside gauge */}
            <div style={{ position: 'absolute', inset: 0, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
              <span style={{ fontFamily: 'JetBrains Mono, monospace', fontSize: 17, fontWeight: 700, color: hasRealScore ? '#F1F5F9' : '#6B7280', lineHeight: 1, letterSpacing: '-0.02em' }}>
                {hasRealScore ? `${displayScore}%` : '—'}
              </span>
            </div>
          </div>

          {/* Subtitle label */}
          <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 8.5, color: '#6B7280', marginTop: 1, textAlign: 'center', whiteSpace: 'nowrap' }}>
            {!hasActiveData ? 'Standby · No Assay Loaded' : (isSimulating ? 'Evaluating Simulation...' : 'Model-in-Monitor Stability')}
          </span>
        </div>
      </div>

      {/* ── Click-to-Zoom Modal ── */}
      {isZoomed && hasRealScore && (
        <div
          className="fixed inset-0 z-50 flex items-center justify-center p-4"
          style={{ background: 'rgba(0, 0, 0, 0.8)', backdropFilter: 'blur(8px)' }}
          onClick={() => setIsZoomed(false)}
        >
          <div
            className="relative flex flex-col p-5 rounded-lg max-w-md w-full"
            style={{
              background: '#111418',
              border: '1px solid #252B34',
              boxShadow: '0 20px 50px rgba(0,0,0,0.8)',
            }}
            onClick={(e) => e.stopPropagation()}
          >
            {/* Modal Header */}
            <div className="flex items-center justify-between pb-3 border-b border-[#1D2127] mb-4">
              <div className="flex items-center gap-2">
                <span className="font-sans text-sm font-semibold text-slate-200 uppercase tracking-wide">
                  Bio-Assay AI — Model Analysis
                </span>
                <span className="chip-safe flex items-center gap-1">
                  <ShieldCheck className="w-3 h-3" />
                  HIGH CONFIDENCE
                </span>
              </div>
              <button
                onClick={() => setIsZoomed(false)}
                className="p-1 rounded text-slate-400 hover:text-white hover:bg-slate-800 transition-colors"
              >
                <X className="w-4 h-4" />
              </button>
            </div>

            {/* Zoomed Circular Gauge */}
            <div className="flex flex-col items-center justify-center my-4">
              <div className="relative w-44 h-44 flex items-center justify-center">
                <svg
                  style={{ width: '100%', height: '100%', transform: 'rotate(-90deg)' }}
                  viewBox="0 0 160 160"
                >
                  <circle cx="80" cy="80" r={zRadius} stroke="#1C2028" strokeWidth="10" fill="transparent" />
                  <circle cx="80" cy="80" r={zRadius + 8} stroke="#252D36" strokeWidth="1" fill="transparent" strokeDasharray="3,4" />
                  <circle
                    cx="80" cy="80" r={zRadius}
                    stroke="#22C55E"
                    strokeWidth="9"
                    strokeDasharray={zCircumference}
                    strokeDashoffset={zStrokeDashoffset}
                    strokeLinecap="round"
                    fill="transparent"
                  />
                </svg>

                <div className="absolute inset-0 flex flex-col items-center justify-center">
                  <span className="font-mono text-4xl font-bold text-slate-100 tracking-tight leading-none">
                    {displayScore}%
                  </span>
                  <span className="font-mono text-[10px] text-emerald-400 font-semibold mt-1 uppercase tracking-wider">
                    Score
                  </span>
                </div>
              </div>
            </div>

            <div className="pt-3 border-t border-[#1D2127] text-center">
              <span className="font-sans text-xs font-semibold text-slate-300">
                Model-in-Monitor Stability
              </span>
              <p className="font-mono text-[10px] text-slate-500 mt-1">
                Real-time convergence and stability index calculated across current biophysical parameter tensors.
              </p>
            </div>
          </div>
        </div>
      )}
    </>
  );
}
