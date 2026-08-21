import React, { useState } from 'react';
import { Maximize2, X } from 'lucide-react';

export interface ReceptorLevels {
  na?: number;
  k?: number;
  ca?: number;
  nmda?: number;
  gaba?: number;
  ampa?: number;
}

interface ReceptorRadarChartProps {
  levels?: ReceptorLevels;
  drugName?: string | null;
  hasActiveData?: boolean;
}

const AXES = [
  { label: 'Na+',    key: 'na',   angle: -90 },
  { label: 'K+',     key: 'k',    angle: -30 },
  { label: 'Ca2+',   key: 'ca',   angle:  30 },
  { label: 'NMDA',   key: 'nmda', angle:  90 },
  { label: 'GABA-A', key: 'gaba', angle: 150 },
  { label: 'AMPA',   key: 'ampa', angle: 210 },
];

export function ReceptorRadarChart({ levels, drugName: _drugName, hasActiveData = true }: ReceptorRadarChartProps) {
  const [isZoomed, setIsZoomed] = useState(false);
  const hasRealLevels = hasActiveData && levels !== undefined;

  const activeLevels = {
    na: hasRealLevels ? (levels.na ?? 0) : 0,
    k: hasRealLevels ? (levels.k ?? 0) : 0,
    ca: hasRealLevels ? (levels.ca ?? 0) : 0,
    nmda: hasRealLevels ? (levels.nmda ?? 0) : 0,
    gaba: hasRealLevels ? (levels.gaba ?? 0) : 0,
    ampa: hasRealLevels ? (levels.ampa ?? 0) : 0,
  };

  // Compact card geometry: maxR = 36, cx = 70, cy = 48
  const cx = 70, cy = 48, maxR = 36;
  const pointCoords = AXES.map((axis) => {
    const val = activeLevels[axis.key as keyof typeof activeLevels];
    const r   = hasRealLevels ? Math.max(0.05, Math.min(1.0, val)) * maxR : 0;
    const rad = (axis.angle * Math.PI) / 180;
    return { x: cx + r * Math.cos(rad), y: cy + r * Math.sin(rad) };
  });
  const polygonPoints = pointCoords.map((p) => `${p.x.toFixed(1)},${p.y.toFixed(1)}`).join(' ');

  // Zoomed geometry: maxR = 95, cx = 150, cy = 130
  const zCx = 150, zCy = 130, zMaxR = 95;
  const zPoints = AXES.map((axis) => {
    const val = activeLevels[axis.key as keyof typeof activeLevels];
    const r   = hasRealLevels ? Math.max(0.05, Math.min(1.0, val)) * zMaxR : 0;
    const rad = (axis.angle * Math.PI) / 180;
    return { x: zCx + r * Math.cos(rad), y: zCy + r * Math.sin(rad), label: axis.label, val };
  });
  const zPolygonPoints = zPoints.map((p) => `${p.x.toFixed(1)},${p.y.toFixed(1)}`).join(' ');

  return (
    <>
      <div
        onClick={() => { if (hasRealLevels) setIsZoomed(true); }}
        className={`relative flex flex-col justify-between h-full select-none overflow-hidden ${hasRealLevels ? 'cursor-pointer group' : ''}`}
        style={{
          background: '#111418',
          border: '1px solid #1D2127',
          borderRadius: 6,
          padding: '4px 8px',
          transition: 'border-color 0.15s, background 0.15s',
        }}
        title={hasRealLevels ? "Click to zoom Channel Balance" : undefined}
      >
        {/* ── Header ── */}
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 1 }}>
          <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 10, fontWeight: 600, color: hasRealLevels ? '#D4D8DE' : '#6B7280', textTransform: 'uppercase', letterSpacing: '0.05em' }}>
            Channel Balance
          </span>
          <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
            <span className="chip-neutral" style={{ fontSize: 8.5, padding: '1px 5px' }}>{hasRealLevels ? 'POLAR MAP' : 'STANDBY'}</span>
            {hasRealLevels && <Maximize2 className="w-2.5 h-2.5 text-slate-500 opacity-0 group-hover:opacity-100 transition-opacity" />}
          </div>
        </div>

        {/* ── Maximized Main Radar Diagram ── */}
        <div style={{ position: 'relative', width: '100%', flex: 1, minHeight: 0, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
          <svg viewBox="0 0 140 96" style={{ width: '100%', height: '100%', maxHeight: 85, maxWidth: 150 }}>
            {/* Concentric guide polygons */}
            {[0.35, 0.7, 1.0].map((scale, i) => {
              const pts = AXES.map((axis) => {
                const r = maxR * scale;
                const rad = (axis.angle * Math.PI) / 180;
                return `${(cx + r * Math.cos(rad)).toFixed(1)},${(cy + r * Math.sin(rad)).toFixed(1)}`;
              });
              return (
                <polygon
                  key={i}
                  points={pts.join(' ')}
                  fill="none"
                  stroke={i === 2 ? '#252B34' : '#1D2127'}
                  strokeWidth={i === 2 ? 0.75 : 0.5}
                  strokeDasharray={i === 2 ? undefined : '2,2'}
                />
              );
            })}

            {/* Radial spokes */}
            {AXES.map((axis, idx) => {
              const rad = (axis.angle * Math.PI) / 180;
              return (
                <line
                  key={idx}
                  x1={cx} y1={cy}
                  x2={cx + maxR * Math.cos(rad)}
                  y2={cy + maxR * Math.sin(rad)}
                  stroke="#1D2127" strokeWidth="0.5"
                />
              );
            })}

            {/* Axis labels */}
            {AXES.map((axis, idx) => {
              const rad = (axis.angle * Math.PI) / 180;
              const lx = cx + (maxR + 9) * Math.cos(rad);
              const ly = cy + (maxR + 7) * Math.sin(rad);
              return (
                <text key={idx} x={lx} y={ly} textAnchor="middle" dominantBaseline="middle"
                  fill={hasRealLevels ? "#818cf8" : "#4B5563"} fontSize="7" fontFamily="'JetBrains Mono', monospace"
                >
                  {axis.label}
                </text>
              );
            })}

            {/* Data polygon ONLY if real levels exist */}
            {hasRealLevels && (
              <>
                <polygon
                  points={polygonPoints}
                  fill="rgba(59,130,246,0.15)"
                  stroke="#3B82F6"
                  strokeWidth="1.2"
                />
                {pointCoords.map((p, idx) => (
                  <circle key={idx} cx={p.x} cy={p.y} r="2" fill="#D4D8DE" stroke="#3B82F6" strokeWidth="0.75" />
                ))}
              </>
            )}

            {!hasRealLevels && (
              <circle cx={cx} cy={cy} r="2" fill="#4B5563" />
            )}
          </svg>
        </div>
      </div>

      {/* ── Click-to-Zoom Modal ── */}
      {isZoomed && hasRealLevels && (
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
            <div className="flex items-center justify-between pb-3 border-b border-[#1D2127] mb-3">
              <div className="flex items-center gap-2">
                <span className="font-sans text-sm font-semibold text-slate-200 uppercase tracking-wide">
                  Channel Balance — Polar Map
                </span>
                <span className="chip-neutral">POLAR MAP</span>
              </div>
              <button
                onClick={() => setIsZoomed(false)}
                className="p-1 rounded text-slate-400 hover:text-white hover:bg-slate-800 transition-colors"
              >
                <X className="w-4 h-4" />
              </button>
            </div>

            {/* Zoomed SVG */}
            <div className="flex items-center justify-center my-3">
              <svg viewBox="0 0 300 260" className="w-72 h-64">
                {[0.25, 0.5, 0.75, 1.0].map((scale, i) => {
                  const pts = AXES.map((axis) => {
                    const r = zMaxR * scale;
                    const rad = (axis.angle * Math.PI) / 180;
                    return `${(zCx + r * Math.cos(rad)).toFixed(1)},${(zCy + r * Math.sin(rad)).toFixed(1)}`;
                  });
                  return (
                    <polygon
                      key={i}
                      points={pts.join(' ')}
                      fill="none"
                      stroke={i === 3 ? '#384252' : '#1D2127'}
                      strokeWidth={i === 3 ? 1 : 0.6}
                      strokeDasharray={i === 3 ? undefined : '3,3'}
                    />
                  );
                })}

                {AXES.map((axis, idx) => {
                  const rad = (axis.angle * Math.PI) / 180;
                  return (
                    <line
                      key={idx}
                      x1={zCx} y1={zCy}
                      x2={zCx + zMaxR * Math.cos(rad)}
                      y2={zCy + zMaxR * Math.sin(rad)}
                      stroke="#1D2127" strokeWidth="1"
                    />
                  );
                })}

                {zPoints.map((p, idx) => {
                  const rad = (AXES[idx].angle * Math.PI) / 180;
                  const lx = zCx + (zMaxR + 18) * Math.cos(rad);
                  const ly = zCy + (zMaxR + 14) * Math.sin(rad);
                  return (
                    <g key={idx}>
                      <text x={lx} y={ly - 4} textAnchor="middle" fill="#38bdf8" fontSize="10" fontFamily="'JetBrains Mono', monospace" fontWeight="bold">
                        {p.label}
                      </text>
                      <text x={lx} y={ly + 8} textAnchor="middle" fill="#94a3b8" fontSize="9" fontFamily="'JetBrains Mono', monospace">
                        {Math.round(p.val * 100)}%
                      </text>
                    </g>
                  );
                })}

                <polygon
                  points={zPolygonPoints}
                  fill="rgba(59,130,246,0.2)"
                  stroke="#3B82F6"
                  strokeWidth="2"
                />

                {zPoints.map((p, idx) => (
                  <circle key={idx} cx={p.x} cy={p.y} r="4" fill="#ffffff" stroke="#3B82F6" strokeWidth="1.5" />
                ))}
              </svg>
            </div>

            <div className="pt-3 border-t border-[#1D2127] text-center">
              <span className="font-sans text-xs font-medium text-slate-300">
                6-Axis Kinetics Matrix
              </span>
              <p className="font-mono text-[10px] text-slate-500 mt-1">
                Polar distribution of active receptor ion channel open probabilities and kinetics states.
              </p>
            </div>
          </div>
        </div>
      )}
    </>
  );
}
