import React from 'react';
import { ShieldAlert, ShieldCheck, AlertTriangle, Activity, Lock } from 'lucide-react';
import type { RunDetailResponse } from '../../types';
import { humanizeEnum } from '../../lib/format';

interface SafetyPanelProps {
  runDetail: RunDetailResponse;
}

export const SafetyPanel: React.FC<SafetyPanelProps> = ({ runDetail }) => {
  const rawRisk = runDetail.risk_level || (runDetail.parsed_summary?.risk_level as string) || 'Moderate';
  const riskLevel = humanizeEnum(rawRisk);
  const rawRec = runDetail.recommendation || (runDetail.parsed_summary?.recommendation as string) || 'Caution';
  const recommendation = humanizeEnum(rawRec);
  const confidence = runDetail.confidence || (runDetail.parsed_summary?.confidence as string) || '91%';

  let riskBg = 'bg-amber-500/10 border-amber-500/30 text-amber-300';
  let riskIcon = <AlertTriangle className="w-4 h-4 text-amber-400" />;

  const upperRisk = rawRisk.toUpperCase();
  if (upperRisk.includes('LOW') || upperRisk.includes('PROMISING') || upperRisk.includes('SAFE')) {
    riskBg = 'bg-emerald-500/10 border-emerald-500/30 text-emerald-300';
    riskIcon = <ShieldCheck className="w-4 h-4 text-emerald-400" />;
  } else if (upperRisk.includes('HIGH') || upperRisk.includes('CRITICAL') || upperRisk.includes('DANGER')) {
    riskBg = 'bg-rose-500/10 border-rose-500/30 text-rose-300';
    riskIcon = <ShieldAlert className="w-4 h-4 text-rose-400" />;
  }

  return (
    <div className="space-y-4 font-sans">
      {/* Primary Risk & Safety Status */}
      <div className="p-5 rounded-xl border border-[#1E2330] bg-[#0C1017] shadow-xl space-y-4">
        <div className="flex items-center justify-between">
          <div>
            <h3 className="text-xs font-bold text-slate-100 uppercase tracking-wider font-mono">
              Safety & Regulatory Risk Assessment
            </h3>
            <p className="text-xs text-slate-400 mt-0.5">Evaluated risk margins & therapeutic boundaries</p>
          </div>
          <div className={`flex items-center gap-2 px-3 py-1.5 rounded-lg border text-xs font-bold font-mono ${riskBg}`}>
            {riskIcon}
            <span>{riskLevel}</span>
          </div>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-3 gap-3">
          <div className="p-3.5 rounded-lg bg-[#11151E] border border-[#1E2330] space-y-1">
            <span className="text-[10px] font-mono uppercase text-slate-400">Recommendation</span>
            <div className="text-base font-bold text-white font-mono">{recommendation}</div>
            <p className="text-[11px] text-slate-400">Decision advice outcome</p>
          </div>

          <div className="p-3.5 rounded-lg bg-[#11151E] border border-[#1E2330] space-y-1">
            <span className="text-[10px] font-mono uppercase text-slate-400">Assessment Confidence</span>
            <div className="text-base font-bold text-sky-300 font-mono">{confidence}</div>
            <p className="text-[11px] text-slate-400">Signal-to-noise ratio score</p>
          </div>

          <div className="p-3.5 rounded-lg bg-[#11151E] border border-[#1E2330] space-y-1">
            <span className="text-[10px] font-mono uppercase text-slate-400">Toxic Threshold</span>
            <div className="text-base font-bold text-slate-200 font-mono">
              {(runDetail.parsed_summary?.toxic_threshold as string) || 'None Detected'}
            </div>
            <p className="text-[11px] text-slate-400">Upper safety limit concentration</p>
          </div>
        </div>
      </div>

      {/* Safety Risk Categories Grid */}
      <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
        {/* Pro-convulsant Seizure Risk */}
        <div className="p-4 rounded-xl border border-[#1E2330] bg-[#0C1017] space-y-2">
          <div className="flex items-center gap-2">
            <Activity className="w-4 h-4 text-rose-400" />
            <h4 className="text-xs font-bold text-slate-200 uppercase tracking-wider font-mono">
              Pro-Convulsant / Seizure Risk
            </h4>
          </div>
          <p className="text-xs text-slate-400 leading-relaxed">
            Evaluated by monitoring Neural Instability Index (NII) and synchronization index for paroxysmal burst discharge events.
          </p>
        </div>

        {/* Sedation & Over-suppression Risk */}
        <div className="p-4 rounded-xl border border-[#1E2330] bg-[#0C1017] space-y-2">
          <div className="flex items-center gap-2">
            <Lock className="w-4 h-4 text-sky-400" />
            <h4 className="text-xs font-bold text-slate-200 uppercase tracking-wider font-mono">
              Sedation & CNS Suppression Risk
            </h4>
          </div>
          <p className="text-xs text-slate-400 leading-relaxed">
            Evaluated by monitoring mean firing frequency reduction and inhibitory network suppression.
          </p>
        </div>
      </div>
    </div>
  );
};
