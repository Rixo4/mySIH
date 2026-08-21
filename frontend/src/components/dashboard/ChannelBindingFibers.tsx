import React from 'react';

export interface ChannelBindingItem {
  name: string;
  percentage: number;
}

interface ChannelBindingFibersProps {
  channels?: ChannelBindingItem[];
  drugName?: string | null;
  r2?: number | null;
  statusText?: string;
  hasActiveData?: boolean;
}

export function ChannelBindingFibers({
  channels = [],
  drugName,
  r2,
  statusText = 'LIVE SYNC',
  hasActiveData = true,
}: ChannelBindingFibersProps) {
  const BAR_GRADIENTS = [
    { grad: 'linear-gradient(90deg, #0284c7 0%, #38bdf8 100%)', tipColor: '#38bdf8', glow: 'rgba(56,189,248,0.5)' },
    { grad: 'linear-gradient(90deg, #059669 0%, #00f5d4 100%)', tipColor: '#00f5d4', glow: 'rgba(0,245,212,0.5)' },
    { grad: 'linear-gradient(90deg, #3b82f6 0%, #a855f7 100%)', tipColor: '#a855f7', glow: 'rgba(168,85,247,0.5)' },
    { grad: 'linear-gradient(90deg, #0284c7 0%, #38bdf8 100%)', tipColor: '#38bdf8', glow: 'rgba(56,189,248,0.5)' },
  ];

  const standardChannelLabels = [
    'NMDA (GluN1/N2)',
    'GABA-A (α1β2γ2)',
    'Na+ Voltage-Gated',
    'K+ Inward Rectifier',
  ];

  const displayChannels = hasActiveData && channels.length > 0
    ? channels
    : standardChannelLabels.map((name) => ({ name, percentage: 0 }));

  return (
    <div
      className="relative flex flex-col justify-between h-full select-none overflow-hidden"
      style={{ background: '#111418', border: '1px solid #1D2127', borderRadius: 6, padding: '4px 8px' }}
    >
      {/* ── Header ── */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 0 }}>
        <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 10, fontWeight: 600, color: hasActiveData ? '#38bdf8' : '#6B7280', textTransform: 'uppercase', letterSpacing: '0.05em' }}>
          Target Binding Matrix
        </span>
        {/* Live-sync indicator */}
        <span style={{ display: 'flex', alignItems: 'center', gap: 3.5, fontFamily: 'JetBrains Mono, monospace', fontSize: 8, fontWeight: 600, letterSpacing: '0.05em', textTransform: 'uppercase', color: hasActiveData ? '#38bdf8' : '#6B7280' }}>
          <span
            className="live-dot"
            style={{
              background: hasActiveData ? '#38bdf8' : '#6B7280',
              boxShadow: hasActiveData ? '0 0 5px #38bdf8' : 'none',
              width: 4, height: 4,
            }}
          />
          {statusText}
        </span>
      </div>

      <p style={{ fontFamily: 'Inter, sans-serif', fontSize: 8.5, color: '#6B7280', marginBottom: 1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
        Occupancy · {hasActiveData && drugName ? drugName : '—'}
      </p>

      {/* ── Progress bars ── */}
      <div style={{ display: 'flex', flexDirection: 'column', flex: 1, justifyContent: 'space-between', gap: 1 }}>
        {displayChannels.map((item, idx) => {
          const pct = hasActiveData && channels.length > 0
            ? Math.max(0, Math.min(100, Math.round(item.percentage)))
            : 0;
          const styleSpec = BAR_GRADIENTS[idx % BAR_GRADIENTS.length];
          return (
            <div key={item.name} style={{ display: 'flex', flexDirection: 'column', gap: 1.5 }}>
              <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', lineHeight: 1 }}>
                <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 9, color: hasActiveData ? '#9CA3AF' : '#6B7280' }}>{item.name}</span>
                <span style={{ fontFamily: 'JetBrains Mono, monospace', fontSize: 9, fontWeight: 600, color: hasActiveData && channels.length > 0 ? '#F1F5F9' : '#6B7280' }}>
                  {hasActiveData && channels.length > 0 ? `${pct}%` : '—'}
                </span>
              </div>
              {/* Track */}
              <div style={{ position: 'relative', height: 3, width: '100%', borderRadius: 2, background: '#161A20', overflow: 'visible' }}>
                {hasActiveData && pct > 0 && (
                  <div
                    style={{
                      position: 'relative',
                      height: '100%',
                      width: `${pct}%`,
                      borderRadius: 2,
                      background: styleSpec.grad,
                      boxShadow: `0 0 6px ${styleSpec.glow}`,
                    }}
                  >
                    {/* Glowing Tip Point */}
                    <div
                      style={{
                        position: 'absolute',
                        right: -1.5,
                        top: '50%',
                        transform: 'translateY(-50%)',
                        width: 4,
                        height: 4,
                        borderRadius: '50%',
                        background: styleSpec.tipColor,
                        boxShadow: `0 0 5px ${styleSpec.tipColor}`,
                      }}
                    />
                  </div>
                )}
              </div>
            </div>
          );
        })}
      </div>

      {/* ── Footer ── */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginTop: 2, paddingTop: 2, borderTop: '1px solid #1D2127', lineHeight: 1 }}>
        <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 9, color: '#6B7280' }}>Confidence</span>
        <span style={{ fontFamily: 'JetBrains Mono, monospace', fontSize: 9, fontWeight: 600, color: '#6B7280' }}>
          R² = {hasActiveData && r2 !== null && r2 !== undefined ? r2.toFixed(3) : '—'}
        </span>
      </div>
    </div>
  );
}
