import React from 'react';
import { CheckCircle2, AlertTriangle, RefreshCw, LucideIcon } from 'lucide-react';

interface SubsystemNodeProps {
  title: string;
  subtitle: string;
  status: 'online' | 'ready' | 'active' | 'offline';
  icon: LucideIcon;
  iconColor: string;
  badgeText: string;
}

export function SubsystemNode({
  title,
  subtitle,
  status,
  icon: Icon,
  iconColor,
  badgeText,
}: SubsystemNodeProps) {
  const isHealthy = status !== 'offline';

  return (
    <div className="glass-card p-4 rounded-2xl border border-slate-800/80 hover:border-brand-500/40 transition-all group flex items-center justify-between shadow-lg">
      <div className="flex items-center gap-3">
        <div className={`w-10 h-10 rounded-xl bg-surface-900 border border-slate-800 flex items-center justify-center ${iconColor} group-hover:scale-105 transition-transform`}>
          <Icon className="w-5 h-5" />
        </div>
        <div>
          <div className="text-xs font-bold text-white font-sans">{title}</div>
          <div className="text-[10px] font-mono text-slate-400 mt-0.5">{subtitle}</div>
        </div>
      </div>

      {isHealthy ? (
        <div className="flex items-center gap-1.5 text-[10px] font-mono font-bold uppercase tracking-wider text-emerald-400 bg-emerald-500/10 border border-emerald-500/30 px-3 py-1 rounded-full">
          <span className="w-1.5 h-1.5 rounded-full bg-emerald-400 alert-dot" />
          {badgeText}
        </div>
      ) : (
        <div className="flex items-center gap-1.5 text-[10px] font-mono font-bold uppercase tracking-wider text-rose-400 bg-rose-500/10 border border-rose-500/30 px-3 py-1 rounded-full">
          <AlertTriangle className="w-3 h-3" />
          Offline
        </div>
      )}
    </div>
  );
}
