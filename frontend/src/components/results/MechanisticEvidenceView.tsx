import React from 'react';
import { Sparkles, Activity, ShieldCheck, Zap } from 'lucide-react';
import type { DrugEvaluationVisualizationData } from '../../types';
import { humanizeEnum } from '../../lib/format';

interface MechanisticEvidenceViewProps {
  visualizationData: DrugEvaluationVisualizationData;
  parsedSummary?: Record<string, unknown> | null;
}

export const MechanisticEvidenceView: React.FC<MechanisticEvidenceViewProps> = ({
  visualizationData,
  parsedSummary
}) => {
  const responseMode = humanizeEnum((parsedSummary?.response_mode as string) || visualizationData.response_mode || 'Inhibition');
  const activeZone = humanizeEnum((parsedSummary?.active_zone as string) || visualizationData.active_zone || 'Neutral Zone');
  const primaryMechanism = (parsedSummary?.primary_mechanism as string) || 'Electrophysiological Channel Blockade';

  return (
    <div className="space-y-4 font-sans">
      {/* Primary Signature Card */}
      <div className="p-5 rounded-xl border border-[#1E2330] bg-[#0C1017] shadow-xl space-y-4">
        <div className="flex items-center gap-2 text-[10px] font-mono uppercase tracking-widest text-sky-400 font-semibold">
          <Sparkles className="w-4 h-4" /> Mechanistic Evidence & Emergence Dynamics
        </div>

        <div className="grid grid-cols-1 md:grid-cols-3 gap-3">
          <div className="p-3.5 rounded-lg bg-[#11151E] border border-[#1E2330] space-y-1">
            <span className="text-[10px] font-mono uppercase text-slate-400">Response Mode</span>
            <div className="text-base font-bold text-sky-300 font-mono">{responseMode}</div>
            <p className="text-[11px] text-slate-400">Biological network classification</p>
          </div>

          <div className="p-3.5 rounded-lg bg-[#11151E] border border-[#1E2330] space-y-1">
            <span className="text-[10px] font-mono uppercase text-slate-400">Active Zone Signature</span>
            <div className="text-base font-bold text-purple-300 font-mono">{activeZone}</div>
            <p className="text-[11px] text-slate-400">Emergent dynamic zone</p>
          </div>

          <div className="p-3.5 rounded-lg bg-[#11151E] border border-[#1E2330] space-y-1">
            <span className="text-[10px] font-mono uppercase text-slate-400">Dominant Driver</span>
            <div className="text-xs font-bold text-slate-200 truncate mt-1">{primaryMechanism}</div>
            <p className="text-[11px] text-slate-400">Primary molecular mechanism</p>
          </div>
        </div>
      </div>

      {/* Detected Biological Signatures List */}
      <div className="p-5 rounded-xl border border-[#1E2330] bg-[#0C1017] shadow-xl space-y-3">
        <div className="flex items-center justify-between">
          <h3 className="text-xs font-bold text-slate-100 uppercase tracking-wider font-mono">
            Detected Biological Emergence Signatures
          </h3>
          <span className="text-[10px] font-mono text-slate-400">
            {visualizationData.dose_results.length} Dose Concentrations
          </span>
        </div>

        <div className="space-y-2.5">
          {visualizationData.dose_results.length > 0 ? (
            visualizationData.dose_results.map((point) => {
              const stateLabel = humanizeEnum(point.biological_state || 'Observed State');
              const isDangerous = (point.biological_state || '').toUpperCase().includes('DANGEROUS') || (point.biological_state || '').toUpperCase().includes('HYPEREXCITABLE');
              const isEffective = (point.biological_state || '').toUpperCase().includes('EFFECTIVE') || (point.biological_state || '').toUpperCase().includes('STABILIZING');

              return (
                <div
                  key={point.dose}
                  className="p-3.5 rounded-lg bg-[#11151E] border border-[#1E2330] flex flex-col sm:flex-row sm:items-center justify-between gap-3"
                >
                  <div className="flex items-center gap-3">
                    <div className="px-2.5 py-1 rounded bg-[#181D28] text-sky-400 border border-[#252B38] font-mono text-xs font-bold shrink-0">
                      {point.dose.toFixed(1)} µM
                    </div>
                    <div>
                      <div className="flex items-center gap-2">
                        <span className="text-xs font-bold text-white">{stateLabel}</span>
                        <span className={`w-1.5 h-1.5 rounded-full ${
                          isDangerous ? 'bg-rose-400' : isEffective ? 'bg-emerald-400' : 'bg-slate-400'
                        }`} />
                      </div>
                      <p className="text-[11px] text-slate-400 mt-0.5">
                        {isDangerous
                          ? 'Hyperexcitable network state with elevated seizure discharge risk.'
                          : isEffective
                          ? 'Controlled microcircuit suppression within therapeutic boundaries.'
                          : 'Sub-therapeutic exposure maintaining baseline electrophysiology.'}
                      </p>
                    </div>
                  </div>

                  <div className="flex items-center gap-3 text-xs font-mono text-slate-300 shrink-0 bg-[#0C1017] px-3 py-1.5 rounded-md border border-[#1E2330]">
                    <span>Firing: <strong className="text-white">{point.firing_rate?.toFixed(1) ?? '—'} Hz</strong></span>
                    <span className="text-slate-600">|</span>
                    <span>Sync: <strong className="text-sky-400">{point.sync?.toFixed(2) ?? '—'}</strong></span>
                    <span className="text-slate-600">|</span>
                    <span>NII: <strong className="text-purple-400">{point.nii?.toFixed(3) ?? '—'}</strong></span>
                  </div>
                </div>
              );
            })
          ) : (
            <div className="p-4 text-xs text-slate-400 text-center">
              No detailed biological signatures extracted for this run.
            </div>
          )}
        </div>
      </div>
    </div>
  );
};
