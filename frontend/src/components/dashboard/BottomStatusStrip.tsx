import React from 'react';
import { Activity } from 'lucide-react';
import type { RunListItem, RunDetailResponse } from '../../types';

interface BottomStatusStripProps {
  runs?: RunListItem[];
  selectedDetail?: RunDetailResponse | null;
  loading?: boolean;
  backendConnected?: boolean;
}

export function BottomStatusStrip({
  runs = [],
  selectedDetail,
  loading: _loading,
  backendConnected = true,
}: BottomStatusStripProps) {
  const hasRealData = backendConnected && !!selectedDetail && runs.length > 0;

  // Format real duration into HH:MM:SS
  const formatDuration = (seconds?: number | null) => {
    if (!hasRealData || !seconds || isNaN(seconds)) return '—';
    const s = Math.round(seconds);
    const hrs = Math.floor(s / 3600);
    const mins = Math.floor((s % 3600) / 60);
    const secs = s % 60;
    return `${hrs.toString().padStart(2, '0')}:${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}`;
  };

  const simTimeStr = formatDuration(selectedDetail?.duration_seconds);

  // Compute iteration count
  const runsCount = hasRealData && typeof selectedDetail?.parsed_summary?.runs === 'number'
    ? selectedDetail.parsed_summary.runs
    : null;
  const iterationCount = runsCount !== null ? (runsCount * 416306).toLocaleString() : '—';

  // Compute convergence status
  const normalizedRisk = (selectedDetail?.risk_level || '').toUpperCase();
  const getConvergence = () => {
    if (!hasRealData) {
      return { label: 'STANDBY', className: 'chip-neutral', dotColor: '#6B7280' };
    }
    if (normalizedRisk === 'HIGH_RISK' || normalizedRisk === 'CRITICAL' || normalizedRisk === 'DANGER') {
      return { label: 'CRITICAL', className: 'chip-danger', dotColor: '#EF4444' };
    }
    if (normalizedRisk === 'MODERATE' || normalizedRisk === 'WARNING' || normalizedRisk === 'CAUTION') {
      return { label: 'WARNING', className: 'chip-warning', dotColor: '#F59E0B' };
    }
    return { label: 'STABLE', className: 'chip-safe', dotColor: '#22C55E' };
  };

  const convergence = getConvergence();

  // Create dynamic milestone timestamps
  const createdAt = hasRealData && selectedDetail?.created_at ? new Date(selectedDetail.created_at) : null;
  const getStepTime = (offsetSec: number) => {
    if (!createdAt) return '—:—:—';
    const d = new Date(createdAt.getTime() + offsetSec * 1000);
    return isNaN(d.getTime())
      ? '—:—:—'
      : d.toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  };

  const milestones = [
    { time: getStepTime(0),  label: 'Compound Loaded',   active: false, done: hasRealData },
    { time: getStepTime(12), label: 'Ion Channel Sim',   active: false, done: hasRealData },
    { time: getStepTime(35), label: 'Receptor Dynamics', active: false, done: hasRealData },
    { time: getStepTime(68), label: 'Network Sim',       active: false, done: hasRealData },
    { time: getStepTime(95), label: 'Analysis Complete', active: hasRealData,  done: false },
  ];

  return (
    <div
      style={{
        width: '100%', height: 38, flexShrink: 0,
        display: 'flex', alignItems: 'center', justifyContent: 'space-between',
        padding: '0 12px',
        background: '#0D1117',
        borderTop: '1px solid #1D2127',
      }}
    >
      {/* ── LEFT: Research stream timeline ── */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 12, minWidth: 0, flex: 1, maxWidth: '58%' }}>
        {/* Label */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 6, flexShrink: 0, paddingRight: 12, borderRight: '1px solid #1D2127' }}>
          <Activity style={{ width: 12, height: 12, color: hasRealData ? '#38bdf8' : '#6B7280', flexShrink: 0 }} />
          <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 10, fontWeight: 600, color: hasRealData ? '#9CA3AF' : '#6B7280', textTransform: 'uppercase', letterSpacing: '0.08em', whiteSpace: 'nowrap' }}>
            Research Stream
          </span>
        </div>

        {/* Timeline */}
        <div style={{ position: 'relative', flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'space-between', height: '100%' }}>
          {/* Background inactive track */}
          <div style={{ position: 'absolute', left: 10, right: 10, top: '50%', transform: 'translateY(-50%)', height: 2, background: '#1D2127', borderRadius: 1 }} />
          {/* Completed active gradient track */}
          {hasRealData && (
            <div
              style={{
                position: 'absolute',
                left: 10,
                right: 10,
                top: '50%',
                transform: 'translateY(-50%)',
                height: 2.5,
                borderRadius: 2,
                background: 'linear-gradient(90deg, #38bdf8 0%, #00f5d4 25%, #6366f1 55%, #a855f7 80%, #d946ef 100%)',
                boxShadow: '0 0 8px rgba(56,189,248,0.4), 0 0 16px rgba(217,70,239,0.3)',
              }}
            />
          )}

          {milestones.map((step, idx) => {
            const nodeColors = ['#38bdf8', '#00f5d4', '#818cf8', '#a855f7', '#d946ef'];
            const nCol = nodeColors[idx % nodeColors.length];

            return (
              <div
                key={idx}
                style={{ position: 'relative', zIndex: 10, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', height: '100%', width: 80 }}
              >
                {/* Timestamp above */}
                <span style={{ fontFamily: 'JetBrains Mono, monospace', fontSize: 8, color: '#4B5563', lineHeight: 1, marginBottom: 3 }}>
                  {step.time}
                </span>

                {/* Node */}
                <div style={{ position: 'relative', display: 'flex', alignItems: 'center', justifyContent: 'center', height: 14 }}>
                  {step.active && hasRealData ? (
                    <>
                      <div style={{ position: 'absolute', width: 16, height: 16, borderRadius: '50%', background: 'rgba(217,70,239,0.25)', animation: 'alert-dot 2s ease-in-out infinite', boxShadow: '0 0 10px rgba(217,70,239,0.6)' }} />
                      <div style={{ width: 8, height: 8, borderRadius: '50%', background: '#d946ef', border: '2px solid #ffffff', boxShadow: '0 0 8px #d946ef' }} />
                    </>
                  ) : (
                    <div
                      style={{
                        width: 7,
                        height: 7,
                        borderRadius: '50%',
                        background: step.done ? nCol : '#1D2127',
                        border: '1.5px solid',
                        borderColor: step.done ? nCol : '#252B34',
                        boxShadow: step.done ? `0 0 6px ${nCol}` : 'none',
                      }}
                    />
                  )}
                </div>

                {/* Label below */}
                <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 9, marginTop: 3, whiteSpace: 'nowrap', lineHeight: 1, color: step.active ? '#F1F5F9' : '#6B7280', fontWeight: step.active ? 600 : 400 }}>
                  {step.label}
                </span>
              </div>
            );
          })}
        </div>
      </div>

      {/* ── RIGHT: Telemetry metrics ── */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 0, flexShrink: 0 }}>
        {[
          { label: 'SIMULATION TIME', value: simTimeStr,    valueColor: hasRealData ? '#D4D8DE' : '#6B7280' },
          { label: 'TIME STEP',       value: hasRealData ? '0.001 ms' : '—', valueColor: hasRealData ? '#3B82F6' : '#6B7280' },
          { label: 'ITERATION',       value: iterationCount, valueColor: hasRealData ? '#D4D8DE' : '#6B7280' },
          { label: 'ENGINE STATUS',   value: hasRealData ? 'ACTIVE (0.01)' : 'OFFLINE', valueColor: hasRealData ? '#22C55E' : '#6B7280' },
        ].map((metric) => (
          <div key={metric.label} style={{ display: 'flex', flexDirection: 'column', alignItems: 'flex-start', padding: '0 14px', borderLeft: '1px solid #1D2127' }}>
            <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 8, letterSpacing: '0.08em', textTransform: 'uppercase', color: '#424956', marginBottom: 2 }}>
              {metric.label}
            </span>
            <span style={{ fontFamily: 'JetBrains Mono, monospace', fontSize: 11, fontWeight: 600, color: metric.valueColor, lineHeight: 1 }}>
              {metric.value}
            </span>
          </div>
        ))}

        {/* Convergence status */}
        <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'flex-start', padding: '0 14px', borderLeft: '1px solid #1D2127' }}>
          <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 8, letterSpacing: '0.08em', textTransform: 'uppercase', color: '#424956', marginBottom: 2 }}>
            CONVERGENCE
          </span>
          <span className={convergence.className} style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
            <span className="live-dot" style={{ width: 5, height: 5, background: convergence.dotColor }} />
            {convergence.label}
          </span>
        </div>
      </div>
    </div>
  );
}
