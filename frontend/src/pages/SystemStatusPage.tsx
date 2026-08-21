import React, { useEffect, useState } from 'react';
import { Activity, Server, Database, Cpu, RefreshCw, CheckCircle2, AlertTriangle, Layers } from 'lucide-react';
import { getHealth, api } from '../api/client';
import type { HealthResponse } from '../types';

export function SystemStatusPage() {
  const [health, setHealth] = useState<HealthResponse | null>(null);
  const [queueStats, setQueueStats] = useState<any | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const fetchStatus = () => {
    setLoading(true);
    setError(null);
    Promise.all([
      getHealth().catch(() => null),
      api.get('/api/queue/stats').then((r) => r.data).catch(() => null),
    ])
      .then(([h, q]) => {
        setHealth(h);
        setQueueStats(q);
      })
      .catch((err) => setError(err instanceof Error ? err.message : 'Telemetry error'))
      .finally(() => setLoading(false));
  };

  useEffect(() => {
    fetchStatus();
  }, []);

  return (
    <div className="space-y-6 pb-12">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <div className="flex items-center gap-2 text-xs font-mono uppercase tracking-widest text-cyan-400 font-semibold">
            <Activity className="w-4 h-4" /> System Infrastructure & Telemetry Console
          </div>
          <h2 className="text-2xl font-bold text-white tracking-tight mt-1">System Health & Diagnostics</h2>
          <p className="text-xs text-slate-400 mt-0.5">Real-time status for FastAPI backend, SQLite database, and native C++/CUDA simulation engine</p>
        </div>

        <button
          onClick={fetchStatus}
          className="flex items-center gap-2 px-3.5 py-2 rounded-xl bg-slate-900 border border-slate-800 text-xs font-semibold text-slate-300 hover:text-white hover:bg-slate-800 transition-colors"
        >
          <RefreshCw className={`w-3.5 h-3.5 ${loading ? 'animate-spin' : ''}`} /> Refresh Status
        </button>
      </div>

      {error && (
        <div className="p-4 rounded-xl border border-rose-800/40 bg-rose-950/40 text-xs text-rose-300">
          {error}
        </div>
      )}

      {/* Subsystem Cards */}
      <div className="grid grid-cols-1 md:grid-cols-3 gap-6">
        {/* FastAPI Backend */}
        <div className="p-6 rounded-2xl border border-slate-800 bg-slate-900/60 backdrop-blur-xl shadow-xl space-y-4">
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-3">
              <div className="p-2.5 rounded-xl bg-cyan-950/80 text-cyan-400 border border-cyan-800/40">
                <Server className="w-5 h-5" />
              </div>
              <div>
                <h3 className="text-sm font-bold text-white">FastAPI Backend</h3>
                <p className="text-[10px] font-mono text-slate-500">REST API & Auth Service</p>
              </div>
            </div>
            {health?.status === 'ok' ? (
              <span className="px-2.5 py-1 rounded-full text-xs font-semibold bg-emerald-950 text-emerald-400 border border-emerald-800">
                Operational
              </span>
            ) : (
              <span className="px-2.5 py-1 rounded-full text-xs font-semibold bg-rose-950 text-rose-400 border border-rose-800">
                Unavailable
              </span>
            )}
          </div>
          <div className="space-y-2 text-xs font-mono text-slate-400 border-t border-slate-800 pt-3">
            <div className="flex justify-between"><span>Endpoint:</span><span className="text-slate-200">/health</span></div>
            <div className="flex justify-between"><span>Service Name:</span><span className="text-slate-200">{health?.service || 'Silicon Patient'}</span></div>
          </div>
        </div>

        {/* Native Engine */}
        <div className="p-6 rounded-2xl border border-slate-800 bg-slate-900/60 backdrop-blur-xl shadow-xl space-y-4">
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-3">
              <div className="p-2.5 rounded-xl bg-purple-950/80 text-purple-400 border border-purple-800/40">
                <Cpu className="w-5 h-5" />
              </div>
              <div>
                <h3 className="text-sm font-bold text-white">C++/CUDA Engine</h3>
                <p className="text-[10px] font-mono text-slate-500">Hodgkin-Huxley Core</p>
              </div>
            </div>
            <span className="px-2.5 py-1 rounded-full text-xs font-semibold bg-emerald-950 text-emerald-400 border border-emerald-800">
              Ready
            </span>
          </div>
          <div className="space-y-2 text-xs font-mono text-slate-400 border-t border-slate-800 pt-3">
            <div className="flex justify-between"><span>Compiler:</span><span className="text-slate-200">GNU C++20</span></div>
            <div className="flex justify-between"><span>Multi-threading:</span><span className="text-slate-200">OpenMP Enabled</span></div>
          </div>
        </div>

        {/* SQLite Database */}
        <div className="p-6 rounded-2xl border border-slate-800 bg-slate-900/60 backdrop-blur-xl shadow-xl space-y-4">
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-3">
              <div className="p-2.5 rounded-xl bg-emerald-950/80 text-emerald-400 border border-emerald-800/40">
                <Database className="w-5 h-5" />
              </div>
              <div>
                <h3 className="text-sm font-bold text-white">SQLite Database</h3>
                <p className="text-[10px] font-mono text-slate-500">Runs & Artifact Ledger</p>
              </div>
            </div>
            <span className="px-2.5 py-1 rounded-full text-xs font-semibold bg-emerald-950 text-emerald-400 border border-emerald-800">
              Active
            </span>
          </div>
          <div className="space-y-2 text-xs font-mono text-slate-400 border-t border-slate-800 pt-3">
            <div className="flex justify-between"><span>ORM:</span><span className="text-slate-200">SQLAlchemy 2.0</span></div>
            <div className="flex justify-between"><span>Migrations:</span><span className="text-slate-200">Alembic</span></div>
          </div>
        </div>
      </div>

      {/* Queue Statistics Card */}
      {queueStats && (
        <div className="p-6 rounded-2xl border border-slate-800 bg-slate-900/60 backdrop-blur-xl space-y-4">
          <div className="flex items-center gap-2 text-xs font-mono uppercase tracking-widest text-slate-400 font-semibold">
            <Layers className="w-4 h-4 text-cyan-400" /> Simulation Job Queue Telemetry
          </div>

          <div className="grid grid-cols-2 md:grid-cols-5 gap-3 font-mono text-xs">
            <div className="p-3.5 rounded-xl bg-slate-950/60 border border-slate-800">
              <span className="text-slate-500 text-[10px]">QUEUED JOBS</span>
              <div className="text-xl font-bold text-cyan-300 mt-1">{queueStats.queued_jobs}</div>
            </div>

            <div className="p-3.5 rounded-xl bg-slate-950/60 border border-slate-800">
              <span className="text-slate-500 text-[10px]">RUNNING JOBS</span>
              <div className="text-xl font-bold text-purple-300 mt-1">{queueStats.running_jobs}</div>
            </div>

            <div className="p-3.5 rounded-xl bg-slate-950/60 border border-slate-800">
              <span className="text-slate-500 text-[10px]">COMPLETED</span>
              <div className="text-xl font-bold text-emerald-400 mt-1">{queueStats.completed_jobs}</div>
            </div>

            <div className="p-3.5 rounded-xl bg-slate-950/60 border border-slate-800">
              <span className="text-slate-500 text-[10px]">FAILED</span>
              <div className="text-xl font-bold text-rose-400 mt-1">{queueStats.failed_jobs}</div>
            </div>

            <div className="p-3.5 rounded-xl bg-slate-950/60 border border-slate-800">
              <span className="text-slate-500 text-[10px]">CONCURRENCY MAX</span>
              <div className="text-xl font-bold text-slate-200 mt-1">{queueStats.max_concurrent_simulations}</div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
