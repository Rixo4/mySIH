import { BadgeCheck, DatabaseZap, ShieldCheck } from 'lucide-react';
import { StatusBadge } from './StatusBadge';

interface TopbarProps {
  pageTitle: string;
  backendConnected: boolean;
  engineOnline: boolean;
  cudaEnabled?: boolean;
  validationRunning?: boolean;
}

export function Topbar({ pageTitle, backendConnected, engineOnline, cudaEnabled = true, validationRunning = false }: TopbarProps) {
  return (
    <header className="sticky top-0 z-20 border-b border-white/10 bg-midnight-950/80 backdrop-blur-xl">
      <div className="flex flex-col gap-4 px-5 py-4 xl:flex-row xl:items-center xl:justify-between xl:px-8">
        <div>
          <p className="text-xs uppercase tracking-[0.36em] text-cyan-300/80">Silicon Patient Platform</p>
          <h1 className="mt-1 text-2xl font-semibold text-white">{pageTitle}</h1>
        </div>
        <div className="flex flex-wrap items-center gap-2">
          <StatusBadge label="Backend" value={backendConnected ? 'Connected' : 'Disconnected'} />
          <StatusBadge label="Engine" value={engineOnline ? 'Online' : 'Unknown'} />
          {validationRunning ? <StatusBadge label="Validation" value="Running" /> : null}
          <span className="inline-flex items-center gap-2 rounded-full bg-emerald-500/10 px-3 py-1 text-xs font-medium text-emerald-200 ring-1 ring-emerald-400/20">
            <BadgeCheck className="h-4 w-4" /> CUDA Enabled
          </span>
          <span className="inline-flex items-center gap-2 rounded-full bg-cyan-500/10 px-3 py-1 text-xs font-medium text-cyan-200 ring-1 ring-cyan-400/20">
            <ShieldCheck className="h-4 w-4" /> Enterprise Ready
          </span>
          {cudaEnabled ? (
            <span className="inline-flex items-center gap-2 rounded-full bg-slate-500/10 px-3 py-1 text-xs font-medium text-slate-200 ring-1 ring-white/10">
              <DatabaseZap className="h-4 w-4" /> API Connected
            </span>
          ) : null}
        </div>
      </div>
    </header>
  );
}
