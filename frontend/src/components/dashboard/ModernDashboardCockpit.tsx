import React, { useState } from 'react';
import type { RunDetailResponse, RunListItem } from '../../types';
import { NeuroCanvas3D } from './NeuroCanvas3D';
import { AIAnalyzerGauge } from './AIAnalyzerGauge';
import { ReceptorRadarChart } from './ReceptorRadarChart';
import { ActionPotentialWaveform } from './ActionPotentialWaveform';
import { PhaseTrajectoryPlot } from './PhaseTrajectoryPlot';
import { ChannelBindingFibers } from './ChannelBindingFibers';
import { RecentAssaysLedger } from './RecentAssaysLedger';
import { BottomStatusStrip } from './BottomStatusStrip';
import { BrainThumbnail } from './BrainThumbnail';
import { ErrorBoundary } from '../common/ErrorBoundary';
import { useBackend } from '../../context/BackendContext';
import {
  ChevronDown,
  Download,
  Plus,
  Zap,
  Dna,
  Activity,
  Microscope,
  Cpu,
} from 'lucide-react';
import { Link } from 'react-router-dom';

interface ModernDashboardCockpitProps {
  runs: RunListItem[];
  selectedRunId?: string | null;
  selectedDetail?: RunDetailResponse | null;
  onSelectRun?: (runId: string) => void;
  latestDetails: RunDetailResponse[];
  loading: boolean;
  error: string | null;
  onRetry: () => void;
  stats: {
    total: number;
    completed: number;
    failed: number;
    running: number;
  };
  averageRuntime: number | null;
}

/* ── Subsystem tabs data ── */
const SUBSYSTEM_TABS = [
  { id: 'cortex',       label: 'Critical L5',   icon: Cpu       },
  { id: 'channels',     label: 'Ion Channels',  icon: Zap       },
  { id: 'receptors',    label: 'Receptors',     icon: Dna       },
  { id: 'microcircuit', label: 'Microcircuit',  icon: Microscope },
  { id: 'ode',          label: '1000+ Neurons', icon: Activity  },
] as const;

export function ModernDashboardCockpit({
  runs = [],
  selectedRunId,
  selectedDetail,
  onSelectRun,
  loading = false,
}: ModernDashboardCockpitProps) {
  const { backendConnected } = useBackend();
  const hasRealData = backendConnected && runs.length > 0;

  const activeRun = hasRealData
    ? (runs.find(r => r.run_id === selectedRunId) || runs[0])
    : null;

  const isSimulating = hasRealData && (activeRun?.status === 'running' || activeRun?.status === 'queued');

  /* ── Compound names from actual runs (zero fallback mock) ── */
  const compoundList = hasRealData
    ? Array.from(new Set(runs.map(r => r.drug_name || r.run_id).filter(Boolean)))
    : [];

  const currentDrugName = hasRealData
    ? (activeRun?.drug_name || activeRun?.run_id || compoundList[0] || '—')
    : 'No Active Assay';

  /* ── Local state ── */
  const [activeSubsystem, setActiveSubsystem] = useState<string>('microcircuit');
  const [drugMenuOpen,    setDrugMenuOpen]    = useState(false);

  function handleExport() {
    if (!hasRealData || !activeRun) return;
    const exportData = selectedDetail || {
      run_id: activeRun.run_id,
      drug_name: currentDrugName,
      status: activeRun.status,
      risk_level: activeRun.risk_level,
      confidence: activeRun.confidence,
      exported_at: new Date().toISOString(),
    };
    const json = JSON.stringify(exportData, null, 2);
    const a = Object.assign(document.createElement('a'), {
      href: 'data:application/json;charset=utf-8,' + encodeURIComponent(json),
      download: `silicon_patient_${(currentDrugName).toLowerCase().replace(/[^a-z0-9]/g, '_')}_export.json`,
    });
    a.click();
  }

  /* ── Compute dynamic channel binding items from input channels ── */
  const channelsPayload = hasRealData && selectedDetail
    ? (selectedDetail.input_payload?.channels as Record<string, { ic50?: number; hill?: number }> | undefined)
    : undefined;

  const naIc50 = channelsPayload?.Na?.ic50;
  const kIc50 = channelsPayload?.K?.ic50;
  const caIc50 = channelsPayload?.Ca?.ic50;

  const channelItems = channelsPayload && naIc50 !== undefined && kIc50 !== undefined && caIc50 !== undefined
    ? [
        { name: 'NMDA (GluN1/N2)', percentage: Math.round(100 / (1 + (caIc50 / 40))) },
        { name: 'GABA-A (α1β2γ2)', percentage: Math.round(100 / (1 + (kIc50 / 120))) },
        { name: 'Na+ Voltage-Gated', percentage: Math.round(100 / (1 + (naIc50 / 100))) },
        { name: 'K+ Inward Rectifier', percentage: Math.round(100 / (1 + (kIc50 / 150))) },
      ]
    : undefined;

  /* ── Compute dynamic radar levels ── */
  const radarLevels = channelsPayload && naIc50 !== undefined && kIc50 !== undefined && caIc50 !== undefined
    ? {
        na: Math.max(0.05, Math.min(1.0, 100 / (naIc50 + 50))),
        k: Math.max(0.05, Math.min(1.0, 100 / (kIc50 + 40))),
        ca: Math.max(0.05, Math.min(1.0, 100 / (caIc50 + 20))),
        nmda: 0.82,
        gaba: 0.74,
        ampa: 0.70,
      }
    : undefined;

  const parsedNii = hasRealData && typeof selectedDetail?.parsed_summary?.nii_score === 'number'
    ? selectedDetail.parsed_summary.nii_score
    : null;
  const firstDoseNii = hasRealData && typeof selectedDetail?.visualization_data?.dose_results?.[0]?.nii === 'number'
    ? selectedDetail.visualization_data.dose_results[0].nii
    : null;
  const dynamicNii = parsedNii ?? firstDoseNii;

  const parsedStability = hasRealData && typeof selectedDetail?.parsed_summary?.stability_score === 'number'
    ? selectedDetail.parsed_summary.stability_score
    : (hasRealData && typeof selectedDetail?.parsed_summary?.model_fit_r2 === 'number'
        ? Math.round(selectedDetail.parsed_summary.model_fit_r2 * 100)
        : null);

  const dynamicConfidence = hasRealData && typeof activeRun?.confidence === 'string'
    ? activeRun.confidence
    : (hasRealData && typeof selectedDetail?.confidence === 'string'
        ? selectedDetail.confidence
        : (hasRealData && typeof selectedDetail?.parsed_summary?.confidence === 'string'
            ? selectedDetail.parsed_summary.confidence
            : null));

  const dynamicRisk = hasRealData && typeof activeRun?.risk_level === 'string'
    ? activeRun.risk_level
    : (hasRealData && typeof selectedDetail?.risk_level === 'string'
        ? selectedDetail.risk_level
        : null);

  const mono = { fontFamily: 'monospace' } as const;

  return (
    <div
      className="w-full font-sans text-slate-100 flex flex-col overflow-hidden select-none"
      style={{
        height: 'calc(100vh - 52px)',
        background: '#0B0D10',
      }}
    >
      {/* ══════════════════════════════════════════════════════════
          ROW 2 — Single Combined Control & Subsystem Bar
         ══════════════════════════════════════════════════════════ */}
      <div
        style={{
          height: 38,
          display: 'grid',
          gridTemplateColumns: '1fr auto 1fr',
          alignItems: 'center',
          padding: '0 16px',
          background: '#080A0E',
          borderBottom: '1px solid #1D2127',
          flexShrink: 0,
        }}
      >
        {/* Left: status indicator + compound dropdown */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 10, justifyContent: 'flex-start' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
            <span
              className="live-dot"
              style={{
                background: hasRealData ? '#22C55E' : '#6B7280',
                boxShadow: hasRealData ? '0 0 6px #22C55E' : 'none',
              }}
            />
            <span style={{ fontFamily: 'Inter, sans-serif', fontSize: 9.5, fontWeight: 700, letterSpacing: '0.1em', color: hasRealData ? '#8896A6' : '#4B5563', textTransform: 'uppercase' }}>
              {hasRealData ? 'BIOPHYSICAL WORKSTATION' : 'WORKSTATION IDLE'}
            </span>
          </div>

          {/* Compound dropdown */}
          <div style={{ position: 'relative' }}>
            <button
              onClick={() => setDrugMenuOpen((v) => !v)}
              disabled={!hasRealData}
              style={{
                ...mono,
                display: 'flex', alignItems: 'center', gap: 6,
                padding: '3px 9px', borderRadius: 4,
                background: '#111418',
                border: '1px solid #1D2127',
                cursor: hasRealData ? 'pointer' : 'default',
                fontSize: 10.5,
                color: hasRealData ? '#D4D8DE' : '#6B7280',
              }}
            >
              {currentDrugName}
              {hasRealData && <ChevronDown style={{ width: 11, height: 11, color: '#6B7280' }} />}
            </button>

            {drugMenuOpen && hasRealData && (
              <div
                style={{
                  position: 'absolute', top: 'calc(100% + 4px)', left: 0,
                  minWidth: 220, borderRadius: 5, zIndex: 300,
                  background: '#111418',
                  border: '1px solid #252B34',
                  padding: '4px 0',
                  boxShadow: '0 10px 30px rgba(0,0,0,0.8)',
                }}
              >
                <div style={{ fontFamily: 'Inter, sans-serif', padding: '5px 12px 4px', fontSize: 9, color: '#424956', textTransform: 'uppercase', letterSpacing: '0.1em', borderBottom: '1px solid #1D2127' }}>
                  Live Runs ({runs.length})
                </div>
                {runs.map((r) => {
                  const name = r.drug_name || r.run_id;
                  const isSelected = activeRun?.run_id === r.run_id;
                  return (
                    <button
                      key={r.run_id}
                      onClick={() => {
                        onSelectRun?.(r.run_id);
                        setDrugMenuOpen(false);
                      }}
                      style={{
                        width: '100%', textAlign: 'left',
                        padding: '6px 12px', background: isSelected ? '#161A20' : 'transparent',
                        border: 'none', cursor: 'pointer',
                        fontFamily: 'Inter, sans-serif', fontSize: 11, color: isSelected ? '#38BDF8' : '#D4D8DE',
                        display: 'flex', justifyContent: 'space-between', alignItems: 'center',
                      }}
                      onMouseEnter={(e) => (e.currentTarget.style.background = '#161A20')}
                      onMouseLeave={(e) => {
                        if (!isSelected) e.currentTarget.style.background = 'transparent';
                      }}
                    >
                      <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', maxWidth: 160 }}>
                        {name}
                      </span>
                      {isSelected && <span style={{ width: 6, height: 6, borderRadius: '50%', background: '#38bdf8', display: 'inline-block' }} />}
                    </button>
                  );
                })}
              </div>
            )}
          </div>
        </div>

        {/* Center: Subsystem pills */}
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 6 }}>
          {SUBSYSTEM_TABS.map((tab) => {
            const Icon     = tab.icon;
            const isActive = activeSubsystem === tab.id;
            const tabColor =
              tab.id === 'cortex' ? '#00F5D4' :
              tab.id === 'channels' ? '#38BDF8' :
              tab.id === 'receptors' ? '#D946EF' :
              tab.id === 'ode' ? '#C084FC' : '#818CF8';

            return (
              <button
                key={tab.id}
                onClick={() => setActiveSubsystem(tab.id)}
                style={{
                  fontFamily: 'Inter, sans-serif',
                  display: 'flex', alignItems: 'center', gap: 5,
                  padding: '3px 10px', borderRadius: 4,
                  cursor: 'pointer', fontSize: 10.5,
                  fontWeight: isActive ? 700 : 500,
                  color: isActive ? '#FFFFFF' : '#8896A6',
                  background: isActive ? `${tabColor}18` : '#111418',
                  border: isActive ? `1px solid ${tabColor}88` : '1px solid #1D2127',
                  boxShadow: isActive ? `0 0 14px ${tabColor}40` : 'none',
                  transition: 'all 0.18s ease',
                  whiteSpace: 'nowrap',
                }}
                onMouseEnter={(e) => { if (!isActive) e.currentTarget.style.color = '#D4D8DE'; }}
                onMouseLeave={(e) => { if (!isActive) e.currentTarget.style.color = '#8896A6'; }}
                title={`Switch 3D Simulation Focus to ${tab.label}`}
              >
                {isActive && hasRealData ? (
                  <span
                    style={{
                      width: 5,
                      height: 5,
                      borderRadius: '50%',
                      background: tabColor,
                      boxShadow: `0 0 8px ${tabColor}`,
                    }}
                  />
                ) : null}
                <Icon style={{ width: 11, height: 11, flexShrink: 0, color: isActive ? tabColor : '#8896A6' }} />
                {tab.label}
              </button>
            );
          })}
        </div>

        {/* Right: Export + New Assay */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 6, justifyContent: 'flex-end' }}>
          <button
            onClick={handleExport}
            disabled={!hasRealData}
            style={{
              ...mono,
              display: 'flex', alignItems: 'center', gap: 5,
              padding: '4px 10px', borderRadius: 4,
              background: '#111418', border: '1px solid #1D2127',
              cursor: hasRealData ? 'pointer' : 'not-allowed',
              fontSize: 10.5,
              color: hasRealData ? '#8896A6' : '#4B5563',
              opacity: hasRealData ? 1 : 0.6,
            }}
          >
            <Download style={{ width: 11, height: 11 }} />
            Export
          </button>

          <Link
            to="/app/dose-eval"
            style={{
              ...mono,
              display: 'flex', alignItems: 'center', gap: 5,
              padding: '4px 12px', borderRadius: 4,
              background: backendConnected ? '#3B82F6' : '#1E293B',
              border: backendConnected ? '1px solid rgba(59,130,246,0.6)' : '1px solid #334155',
              boxShadow: backendConnected ? '0 0 12px rgba(59,130,246,0.35)' : 'none',
              fontSize: 10.5, fontWeight: 700,
              color: backendConnected ? '#fff' : '#94A3B8',
              textDecoration: 'none',
            }}
          >
            <Plus style={{ width: 11, height: 11 }} />
            New Assay
          </Link>
        </div>
      </div>

      {/* ══════════════════════════════════════════════════════════
          MAIN 3-COLUMN BIOPHYSICAL WORKSTATION BODY
         ══════════════════════════════════════════════════════════ */}
      <div className="flex flex-1 min-h-0 w-full overflow-hidden" style={{ padding: 6, gap: 6 }}>

        {/* ════════ LEFT COLUMN — 25% Width (3 Equal Rows) ════════ */}
        <div className="grid grid-rows-3 gap-1.5 h-full overflow-hidden shrink-0" style={{ width: '25%' }}>
          <div className="min-h-0 h-full overflow-hidden">
            <ErrorBoundary>
              <PhaseTrajectoryPlot
                niiScore={dynamicNii}
                riskLevel={dynamicRisk}
                doseResults={selectedDetail?.visualization_data?.dose_results ?? []}
                drugName={currentDrugName}
                hasActiveData={hasRealData}
              />
            </ErrorBoundary>
          </div>

          <div className="min-h-0 h-full overflow-hidden">
            <ErrorBoundary>
              <ChannelBindingFibers
                channels={channelItems}
                drugName={hasRealData ? currentDrugName : null}
                r2={hasRealData && typeof selectedDetail?.parsed_summary?.model_fit_r2 === 'number' ? selectedDetail.parsed_summary.model_fit_r2 : null}
                statusText={!hasRealData ? 'OFFLINE' : (isSimulating ? 'SIMULATING' : 'LIVE SYNC')}
                hasActiveData={hasRealData}
              />
            </ErrorBoundary>
          </div>

          <div className="min-h-0 h-full overflow-hidden">
            <ErrorBoundary>
              <RecentAssaysLedger
                runs={runs}
                selectedRunId={activeRun?.run_id}
                onSelectRun={onSelectRun}
                loading={loading}
              />
            </ErrorBoundary>
          </div>
        </div>

        {/* ════════ CENTER COLUMN — 50% Width 3D Simulation Canvas ════════ */}
        <div
          className="relative flex-1 h-full min-w-0 overflow-hidden"
          style={{
            background: '#080A0E',
            border: '1px solid #1D2127',
            borderRadius: 6,
            zIndex: 0,
          }}
        >
          <ErrorBoundary>
            <NeuroCanvas3D
              isSimulating={isSimulating}
              isRunning={hasRealData && !isSimulating}
              hasActiveData={hasRealData}
              firingRateHz={hasRealData ? (selectedDetail?.visualization_data?.dose_results?.[0]?.firing_rate ?? null) : null}
              niiScore={hasRealData && typeof dynamicNii === 'number' ? dynamicNii : null}
              gabaOccupancy={channelItems?.[1]?.percentage ?? null}
              nmdaOccupancy={channelItems?.[0]?.percentage ?? null}
              riskLevel={dynamicRisk}
              activeSubsystem={activeSubsystem}
              onSelectSubsystem={setActiveSubsystem}
            />
          </ErrorBoundary>
        </div>

        {/* ════════ RIGHT COLUMN — 25% Width Readout Stack (4 Equal Rows) ════════ */}
        <div className="grid grid-rows-4 gap-1.5 h-full overflow-hidden shrink-0" style={{ width: '25%' }}>
          {/* Card 1: Membrane Potential */}
          <div className="min-h-0 h-full overflow-hidden">
            <ErrorBoundary>
              <ActionPotentialWaveform
                voltageTrace={hasRealData ? (selectedDetail?.visualization_data?.voltage_trace ?? []) : []}
                firingRateHz={hasRealData ? (selectedDetail?.visualization_data?.dose_results?.[0]?.firing_rate ?? null) : null}
                thresholdMv={20}
                hasActiveData={hasRealData}
              />
            </ErrorBoundary>
          </div>

          {/* Card 2: Channel Balance */}
          <div className="min-h-0 h-full overflow-hidden">
            <ErrorBoundary>
              <ReceptorRadarChart
                levels={radarLevels}
                drugName={hasRealData ? currentDrugName : null}
                hasActiveData={hasRealData}
              />
            </ErrorBoundary>
          </div>

          {/* Card 3: Bio-Assay AI */}
          <div className="min-h-0 h-full overflow-hidden">
            <ErrorBoundary>
              <AIAnalyzerGauge
                score={parsedStability}
                confidence={dynamicConfidence}
                riskLevel={dynamicRisk}
                isSimulating={isSimulating}
                hasActiveData={hasRealData}
              />
            </ErrorBoundary>
          </div>

          {/* Card 4: 3D Brain & View Layers */}
          <div className="min-h-0 h-full overflow-hidden">
            <ErrorBoundary>
              <BrainThumbnail activeLayer="L5" />
            </ErrorBoundary>
          </div>
        </div>
      </div>

      {/* ══════════════════════════════════════════════════════════
          BOTTOM RESEARCH STREAM & TELEMETRY STRIP
         ══════════════════════════════════════════════════════════ */}
      <ErrorBoundary>
        <BottomStatusStrip
          runs={runs}
          selectedDetail={hasRealData ? selectedDetail : null}
          loading={loading}
          backendConnected={backendConnected}
        />
      </ErrorBoundary>
    </div>
  );
}