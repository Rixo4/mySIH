import React from 'react';
import { ChevronRight, CheckCircle2, AlertTriangle, AlertCircle, Clock } from 'lucide-react';
import { Link } from 'react-router-dom';
import type { RunListItem } from '../../types';

interface RecentAssaysLedgerProps {
  runs: RunListItem[];
  selectedRunId?: string | null;
  onSelectRun?: (runId: string) => void;
  loading?: boolean;
}

export function RecentAssaysLedger({
  runs = [],
  selectedRunId,
  onSelectRun,
  loading = false,
}: RecentAssaysLedgerProps) {
  const displayRuns = runs.slice(0, 3);

  const formatDate = (isoStr: string) => {
    try {
      const d = new Date(isoStr);
      if (isNaN(d.getTime())) return isoStr;
      return d.toLocaleDateString('en-GB', { day: '2-digit', month: '2-digit', year: 'numeric' });
    } catch {
      return isoStr;
    }
  };

  const getStatusChip = (status?: string | null, riskLevel?: string | null) => {
    const s = (status || '').toUpperCase();
    const r = (riskLevel || '').toUpperCase();

    if (s === 'RUNNING' || s === 'QUEUED') {
      return {
        label: s,
        className: 'chip-info',
        icon: <Clock style={{ width: 12, height: 12, color: '#38BDF8' }} />,
      };
    }
    if (s === 'FAILED') {
      return {
        label: 'FAILED',
        className: 'chip-danger',
        icon: <AlertCircle style={{ width: 12, height: 12, color: '#EF4444' }} />,
      };
    }
    if (r === 'HIGH_RISK' || r === 'CRITICAL' || r === 'DANGER') {
      return {
        label: 'DANGER',
        className: 'chip-danger',
        icon: <AlertCircle style={{ width: 12, height: 12, color: '#EF4444' }} />,
      };
    }
    if (r === 'MODERATE' || r === 'WARNING' || r === 'CAUTION') {
      return {
        label: 'WARNING',
        className: 'chip-warning',
        icon: <AlertTriangle style={{ width: 12, height: 12, color: '#F59E0B' }} />,
      };
    }
    return {
      label: 'SAFE',
      className: 'chip-safe',
      icon: <CheckCircle2 style={{ width: 12, height: 12, color: '#22C55E' }} />,
    };
  };

  return (
    <div
      className="relative flex flex-col justify-between h-full select-none overflow-hidden"
      style={{ background: '#111418', border: '1px solid #1D2127', borderRadius: 6, padding: '4px 8px' }}
    >
      {/* ── Header ── */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 2 }}>
        <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 10, fontWeight: 600, color: '#6B7280', textTransform: 'uppercase', letterSpacing: '0.05em' }}>
          Recent Assays
        </span>
        <Link
          to="/app/history"
          style={{ display: 'flex', alignItems: 'center', gap: 2, fontFamily: 'Inter, sans-serif', fontSize: 9, fontWeight: 500, color: '#3B82F6', textDecoration: 'none' }}
        >
          All ({runs.length})
          <ChevronRight style={{ width: 9, height: 9 }} />
        </Link>
      </div>

      {/* ── Assay rows ── */}
      <div style={{ display: 'flex', flexDirection: 'column', flex: 1, justifyContent: 'space-between', gap: 2 }}>
        {loading && displayRuns.length === 0 ? (
          <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', height: '100%', fontFamily: 'Inter, sans-serif', fontSize: 9.5, color: '#6B7280' }}>
            Loading assays...
          </div>
        ) : displayRuns.length === 0 ? (
          <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', height: '100%', fontFamily: 'Inter, sans-serif', fontSize: 9.5, color: '#6B7280' }}>
            No assay runs recorded yet
          </div>
        ) : (
          displayRuns.map((run) => {
            const isSelected = selectedRunId === run.run_id;
            const chip = getStatusChip(run.status, run.risk_level);
            const displayName = run.drug_name || run.run_id;

            return (
              <div
                key={run.run_id}
                onClick={() => onSelectRun?.(run.run_id)}
                style={{
                  display: 'flex', alignItems: 'center', justifyContent: 'space-between',
                  padding: '3px 6px', borderRadius: 4, cursor: 'pointer',
                  background: isSelected ? '#1A2230' : '#161A20',
                  border: isSelected ? '1px solid #38BDF8' : '1px solid #1D2127',
                  boxShadow: isSelected ? '0 0 6px rgba(56,189,248,0.2)' : 'none',
                  transition: 'border-color 0.14s, background 0.14s',
                }}
                onMouseEnter={(e) => {
                  if (!isSelected) (e.currentTarget as HTMLElement).style.borderColor = '#252B34';
                }}
                onMouseLeave={(e) => {
                  if (!isSelected) (e.currentTarget as HTMLElement).style.borderColor = '#1D2127';
                }}
              >
                {/* Left: name + date */}
                <div style={{ minWidth: 0 }}>
                  <div style={{ fontFamily: 'JetBrains Mono, monospace', fontSize: 9.5, fontWeight: isSelected ? 700 : 500, color: isSelected ? '#38BDF8' : '#D4D8DE', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', maxWidth: 120 }}>
                    {displayName}
                  </div>
                  <div style={{ fontFamily: 'Inter, sans-serif', fontSize: 8, color: '#6B7280' }}>
                    {formatDate(run.created_at)}
                  </div>
                </div>
                {/* Right: chip + status icon */}
                <div style={{ display: 'flex', alignItems: 'center', gap: 4, flexShrink: 0 }}>
                  <span className={chip.className} style={{ fontSize: 8, padding: '1px 4px' }}>{chip.label}</span>
                  {chip.icon}
                </div>
              </div>
            );
          })
        )}
      </div>
    </div>
  );
}
