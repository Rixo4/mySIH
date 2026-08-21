import React from 'react';
import { Activity } from 'lucide-react';
import type { DoseResultPoint } from '../../types';

interface PhaseTrajectoryPlotProps {
  niiScore?: number | null;
  syncDelta?: number | null;
  riskLevel?: string | null;
  doseResults?: DoseResultPoint[];
  drugName?: string | null;
  hasActiveData?: boolean;
}

export function PhaseTrajectoryPlot({
  niiScore: propNiiScore,
  syncDelta: propSyncDelta,
  riskLevel,
  doseResults = [],
  drugName: _drugName,
  hasActiveData = true,
}: PhaseTrajectoryPlotProps) {
  const hasRealPoints = hasActiveData && doseResults.length > 0;
  const hasRealNii = hasActiveData && typeof propNiiScore === 'number' && !isNaN(propNiiScore);

  // Derive NII and Sync from real dose results if available
  const avgNii = hasRealPoints
    ? doseResults.reduce((acc, d) => acc + (d.nii ?? 0), 0) / doseResults.length
    : (hasRealNii ? propNiiScore! : null);

  const calculatedSyncDelta = hasRealPoints && doseResults.length >= 2
    ? Math.abs(doseResults[doseResults.length - 1].sync - doseResults[0].sync)
    : (hasActiveData && typeof propSyncDelta === 'number' ? propSyncDelta : null);

  const displayNii = hasRealNii
    ? propNiiScore!
    : (avgNii !== null ? avgNii : null);

  const normalizedRisk = (riskLevel || '').toUpperCase();
  const getRiskChip = () => {
    if (!hasActiveData || !riskLevel) {
      return { label: 'STANDBY', className: 'chip-neutral', isConvergent: null };
    }
    if (normalizedRisk === 'HIGH_RISK' || normalizedRisk === 'CRITICAL' || normalizedRisk === 'DANGER') {
      return { label: 'CRITICAL', className: 'chip-danger', isConvergent: false };
    }
    if (normalizedRisk === 'MODERATE' || normalizedRisk === 'WARNING' || normalizedRisk === 'CAUTION') {
      return { label: 'AT RISK', className: 'chip-warning', isConvergent: false };
    }
    return { label: 'STABLE', className: 'chip-safe', isConvergent: true };
  };

  const riskBadge = getRiskChip();

  // Project real dose points to SVG coordinate grid (cx = 120, cy = 60, scale = 70)
  const cx = 120;
  const cy = 60;
  const scale = 70;

  const realPlotPoints = hasRealPoints
    ? doseResults.map((pt, idx) => {
        const nx = (pt.sync - 0.5) * 2;
        const ny = (pt.nii - 0.5) * 2;
        const px = Math.max(35, Math.min(205, cx + nx * scale));
        const py = Math.max(18, Math.min(102, cy - ny * scale));
        const isCore = idx === 0 || idx === doseResults.length - 1;
        return {
          x: px,
          y: py,
          r: isCore ? 3.2 : 2.0,
          col: idx === 0 ? '#ffffff' : (nx > 0.3 ? '#38bdf8' : '#00f5d4'),
          op: 0.9,
        };
      })
    : [];

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
        <div style={{ display: 'flex', alignItems: 'center', gap: 5 }}>
          <Activity style={{ width: 11, height: 11, color: hasActiveData ? '#38bdf8' : '#6B7280', flexShrink: 0 }} />
          <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 10, fontWeight: 600, color: hasActiveData ? '#D4D8DE' : '#6B7280', textTransform: 'uppercase', letterSpacing: '0.05em' }}>
            Phase Space Trajectory
          </span>
        </div>
        <span className={riskBadge.className} style={{ fontSize: 9, padding: '1px 5px' }}>{riskBadge.label}</span>
      </div>

      {/* Sub-header */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 1 }}>
        <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 9, color: '#6B7280' }}>Instability vs Sync</span>
        <span style={{ fontFamily: 'JetBrains Mono, monospace', fontSize: 9.5, color: '#6B7280' }}>
          NII: <strong style={{ color: hasActiveData && displayNii !== null ? '#D4D8DE' : '#6B7280' }}>
            {hasActiveData && displayNii !== null ? displayNii.toFixed(3) : '—'}
          </strong>
        </span>
      </div>

      {/* ── 2D Coordinate Grid ── */}
      <div
        className="relative w-full flex-1 flex items-center justify-center min-h-0 overflow-hidden"
        style={{ background: '#0B0D10', border: '1px solid #1D2127', borderRadius: 4 }}
      >
        <svg viewBox="0 0 240 120" className="w-full h-full" style={{ maxHeight: '100%' }}>
          <defs>
            <linearGradient id="phase-flare-grad" x1="0%" y1="50%" x2="100%" y2="50%">
              <stop offset="0%" stopColor="#0284c7" stopOpacity="0" />
              <stop offset="20%" stopColor="#0ea5e9" stopOpacity="0.4" />
              <stop offset="45%" stopColor="#38bdf8" stopOpacity="0.9" />
              <stop offset="50%" stopColor="#ffffff" stopOpacity="1" />
              <stop offset="55%" stopColor="#38bdf8" stopOpacity="0.9" />
              <stop offset="80%" stopColor="#0ea5e9" stopOpacity="0.4" />
              <stop offset="100%" stopColor="#0284c7" stopOpacity="0" />
            </linearGradient>

            <radialGradient id="phase-core-glow" cx="50%" cy="50%" r="50%">
              <stop offset="0%" stopColor="#ffffff" stopOpacity="0.95" />
              <stop offset="25%" stopColor="#38bdf8" stopOpacity="0.75" />
              <stop offset="55%" stopColor="#0284c7" stopOpacity="0.35" />
              <stop offset="100%" stopColor="#0369a1" stopOpacity="0" />
            </radialGradient>

            <filter id="phase-node-glow" x="-30%" y="-30%" width="160%" height="160%">
              <feGaussianBlur stdDeviation="1.8" result="blur" />
              <feMerge>
                <feMergeNode in="blur" />
                <feMergeNode in="SourceGraphic" />
              </feMerge>
            </filter>
          </defs>

          {/* Coordinate axes */}
          <line x1="28" y1="60" x2="212" y2="60" stroke="#1D2127" strokeWidth="1" strokeDasharray="3,3" />
          <line x1="120" y1="14" x2="120" y2="106" stroke="#1D2127" strokeWidth="1" strokeDasharray="3,3" />

          {/* Tick marks on axes */}
          <line x1="120" y1="20" x2="123" y2="20" stroke="#252B34" strokeWidth="1" />
          <line x1="120" y1="100" x2="123" y2="100" stroke="#252B34" strokeWidth="1" />
          <line x1="40" y1="60" x2="40" y2="63" stroke="#252B34" strokeWidth="1" />
          <line x1="200" y1="60" x2="200" y2="63" stroke="#252B34" strokeWidth="1" />

          {/* Axis Numerical Labels */}
          <text x="18" y="24" fill="#424956" fontSize="7.5" fontFamily="'JetBrains Mono', monospace">1.0</text>
          <text x="18" y="63" fill="#424956" fontSize="7.5" fontFamily="'JetBrains Mono', monospace">0</text>
          <text x="14" y="102" fill="#424956" fontSize="7.5" fontFamily="'JetBrains Mono', monospace">-1.0</text>
          <text x="36" y="113" fill="#424956" fontSize="7.5" fontFamily="'JetBrains Mono', monospace">-1.0</text>
          <text x="117" y="113" fill="#424956" fontSize="7.5" fontFamily="'JetBrains Mono', monospace">0</text>
          <text x="196" y="113" fill="#424956" fontSize="7.5" fontFamily="'JetBrains Mono', monospace">1.0</text>

          {/* Render trajectory and particles ONLY when real data exists */}
          {hasActiveData && realPlotPoints.length > 0 && (
            <>
              <ellipse cx="120" cy="60" rx="65" ry="14" fill="url(#phase-core-glow)" />
              <rect
                x="35"
                y="59"
                width="170"
                height="2"
                fill="url(#phase-flare-grad)"
                filter="url(#phase-node-glow)"
              />
              {realPlotPoints.length >= 2 && (
                <polyline
                  points={realPlotPoints.map(p => `${p.x.toFixed(1)},${p.y.toFixed(1)}`).join(' ')}
                  fill="none"
                  stroke="#38BDF8"
                  strokeWidth="1.2"
                  strokeDasharray="2,2"
                  opacity="0.6"
                />
              )}
              {realPlotPoints.map((pt, i) => (
                <circle
                  key={i}
                  cx={pt.x}
                  cy={pt.y}
                  r={pt.r}
                  fill={pt.col}
                  opacity={pt.op}
                  filter="url(#phase-node-glow)"
                />
              ))}
            </>
          )}

          {/* Empty state label when disconnected */}
          {(!hasActiveData || realPlotPoints.length === 0) && (
            <text x="120" y="64" textAnchor="middle" fill="#4B5563" fontSize="8.5" fontFamily="'Inter', sans-serif">
              STANDBY · NO TELEMETRY
            </text>
          )}
        </svg>
      </div>

      {/* ── Footer ── */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginTop: 2 }}>
        <span style={{ fontFamily: 'JetBrains Mono, monospace', fontSize: 9.5, color: '#6B7280' }}>
          ΔSync: {hasActiveData && calculatedSyncDelta !== null ? `${(calculatedSyncDelta * 1000).toFixed(2)}e-3` : '—'}
        </span>
        <span style={{
          fontFamily: 'JetBrains Mono, monospace',
          fontSize: 9.5,
          color: hasActiveData && riskBadge.isConvergent !== null ? (riskBadge.isConvergent ? '#22C55E' : '#EF4444') : '#6B7280',
          fontWeight: 600
        }}>
          {hasActiveData && riskBadge.isConvergent !== null ? (riskBadge.isConvergent ? 'Convergent' : 'Divergent') : 'Standby'}
        </span>
      </div>
    </div>
  );
}
