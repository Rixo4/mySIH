import React, { useEffect, useState } from 'react';
import { Activity, Database, Server, Cpu, RefreshCw } from 'lucide-react';
import { getHealth } from '../../api/client';
import type { HealthResponse } from '../../types';
import { SubsystemNode } from '../dashboard/SubsystemNode';

export const SystemHealthWidget: React.FC = () => {
  const [health, setHealth] = useState<HealthResponse | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const fetchHealth = () => {
    setLoading(true);
    setError(null);
    getHealth()
      .then((data) => setHealth(data))
      .catch((err) => setError(err instanceof Error ? err.message : 'Disconnected'))
      .finally(() => setLoading(false));
  };

  useEffect(() => {
    fetchHealth();
  }, []);

  return (
    <div className="glass-card p-5 border border-slate-800/80 shadow-2xl relative overflow-hidden">
      <div className="flex items-center justify-between mb-4 relative z-10">
        <div className="flex items-center gap-2.5">
          <div className="w-2 h-2 rounded-full bg-cyan-400 alert-dot" />
          <h3 className="text-xs font-mono font-bold text-slate-200 uppercase tracking-widest flex items-center gap-2">
            <Activity className="w-4 h-4 text-brand-400" />
            Infrastructure Telemetry & Subsystems
          </h3>
        </div>
        <button
          onClick={fetchHealth}
          className="p-1.5 rounded-xl text-slate-400 hover:text-white hover:bg-surface-800 border border-slate-800/80 transition-all cursor-pointer"
          title="Refresh telemetry"
        >
          <RefreshCw className={`w-3.5 h-3.5 ${loading ? 'animate-spin text-brand-400' : ''}`} />
        </button>
      </div>

      <div className="grid grid-cols-1 sm:grid-cols-3 gap-4 relative z-10">
        <SubsystemNode
          title="FastAPI REST Core"
          subtitle="Port 8000 • Active REST API"
          status={health?.status === 'ok' ? 'online' : 'offline'}
          icon={Server}
          iconColor="text-brand-400"
          badgeText="ONLINE"
        />

        <SubsystemNode
          title="CUDA 12 Biophysical Solver"
          subtitle="Native C++20 / OpenMP Engine"
          status="ready"
          icon={Cpu}
          iconColor="text-purple-400"
          badgeText="READY"
        />

        <SubsystemNode
          title="SQLite Experiment Ledger"
          subtitle="Persistent Run DB Store"
          status="active"
          icon={Database}
          iconColor="text-emerald-400"
          badgeText="ACTIVE"
        />
      </div>
    </div>
  );
};
