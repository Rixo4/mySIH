import React, { useState } from 'react';
import { motion } from 'framer-motion';
import { Activity, Brain, Layers, Maximize2, RefreshCw, Zap } from 'lucide-react';

interface CorticalNetworkVisualizerProps {
  latestDrugName?: string;
  recommendation?: string;
  riskLevel?: string;
}

export function CorticalNetworkVisualizer({
  latestDrugName = 'Reference Compound',
  recommendation = 'EVALUATED',
  riskLevel = 'LOW_RISK',
}: CorticalNetworkVisualizerProps) {
  const [activeTab, setActiveTab] = useState<'network' | 'channels' | 'receptors'>('network');
  const [zoomLevel, setZoomLevel] = useState(1);

  // Nodes for Cortical Microcircuit (Pyramidal + Interneurons)
  const nodes = [
    { id: 'n1', cx: 200, cy: 120, label: 'PYR-1 (Excitatory)', type: 'excitatory', color: '#3b82f6' },
    { id: 'n2', cx: 320, cy: 90, label: 'INT-1 (GABAergic)', type: 'inhibitory', color: '#10b981' },
    { id: 'n3', cx: 440, cy: 130, label: 'PYR-2 (Excitatory)', type: 'excitatory', color: '#3b82f6' },
    { id: 'n4', cx: 160, cy: 230, label: 'INT-2 (Fast Spiking)', type: 'inhibitory', color: '#10b981' },
    { id: 'n5', cx: 300, cy: 220, label: 'PYR-Core', type: 'core', color: '#6366f1' },
    { id: 'n6', cx: 460, cy: 240, label: 'PYR-3 (Excitatory)', type: 'excitatory', color: '#3b82f6' },
    { id: 'n7', cx: 220, cy: 330, label: 'INT-3 (Somatostatin)', type: 'inhibitory', color: '#10b981' },
    { id: 'n8', cx: 380, cy: 330, label: 'PYR-4 (Excitatory)', type: 'excitatory', color: '#3b82f6' },
  ];

  // Connections (Synapses)
  const connections = [
    { from: nodes[0], to: nodes[1] },
    { from: nodes[1], to: nodes[4] },
    { from: nodes[0], to: nodes[4] },
    { from: nodes[2], to: nodes[4] },
    { from: nodes[3], to: nodes[4] },
    { from: nodes[4], to: nodes[5] },
    { from: nodes[4], to: nodes[6] },
    { from: nodes[6], to: nodes[7] },
    { from: nodes[5], to: nodes[7] },
    { from: nodes[2], to: nodes[5] },
  ];

  return (
    <div className="glass-card glow-ring rounded-3xl p-6 border border-slate-800/80 shadow-2xl relative overflow-hidden flex flex-col justify-between min-h-[480px]">
      {/* Background Radial Glow */}
      <div className="absolute inset-0 bg-grid opacity-50 pointer-events-none" />
      <div className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-80 h-80 bg-brand-500/10 rounded-full blur-3xl pointer-events-none" />

      {/* Top Header Bar */}
      <div className="flex items-center justify-between relative z-10 pb-4 border-b border-slate-800/80">
        <div className="flex items-center gap-3">
          <div className="w-9 h-9 rounded-xl bg-brand-500/15 border border-brand-500/30 flex items-center justify-center text-brand-400">
            <Brain className="w-5 h-5" />
          </div>
          <div>
            <div className="flex items-center gap-2">
              <span className="section-label">Central Biophysical Workstation</span>
              <span className="px-2 py-0.5 rounded-full bg-emerald-500/10 border border-emerald-500/30 text-[9px] font-mono font-bold text-emerald-400 uppercase tracking-wider flex items-center gap-1">
                <span className="w-1.5 h-1.5 rounded-full bg-emerald-400 alert-dot" />
                Live Microcircuit
              </span>
            </div>
            <h3 className="text-base font-bold text-white font-sans mt-0.5">
              Cortical Network & Ion Sweep Kinetics
            </h3>
          </div>
        </div>

        {/* View Controls */}
        <div className="flex items-center gap-2">
          <div className="bg-surface-900/80 border border-slate-800/80 rounded-xl p-1 flex gap-1 text-xs font-mono">
            <button
              onClick={() => setActiveTab('network')}
              className={`px-3 py-1 rounded-lg transition-all cursor-pointer ${
                activeTab === 'network' ? 'bg-brand-500 text-white font-bold' : 'text-slate-400 hover:text-white'
              }`}
            >
              Microcircuit
            </button>
            <button
              onClick={() => setActiveTab('channels')}
              className={`px-3 py-1 rounded-lg transition-all cursor-pointer ${
                activeTab === 'channels' ? 'bg-brand-500 text-white font-bold' : 'text-slate-400 hover:text-white'
              }`}
            >
              Ion Channels
            </button>
            <button
              onClick={() => setActiveTab('receptors')}
              className={`px-3 py-1 rounded-lg transition-all cursor-pointer ${
                activeTab === 'receptors' ? 'bg-brand-500 text-white font-bold' : 'text-slate-400 hover:text-white'
              }`}
            >
              Receptors
            </button>
          </div>
        </div>
      </div>

      {/* Main Interactive Canvas Container */}
      <div className="relative my-4 flex-1 flex items-center justify-center min-h-[300px] z-10 overflow-hidden rounded-2xl border border-slate-800/60 bg-surface-950/60 backdrop-blur-md">
        <svg className="w-full h-full min-h-[320px]" viewBox="0 0 600 400">
          <defs>
            <linearGradient id="synapsesGrad" x1="0%" y1="0%" x2="100%" y2="100%">
              <stop offset="0%" stopColor="#2563eb" stopOpacity="0.6" />
              <stop offset="100%" stopColor="#10b981" stopOpacity="0.6" />
            </linearGradient>
            <filter id="glow" x="-20%" y="-20%" width="140%" height="140%">
              <feGaussianBlur stdDeviation="3" result="blur" />
              <feComposite in="SourceGraphic" in2="blur" operator="over" />
            </filter>
          </defs>

          {/* Connection Lines (Synaptic Pathways) */}
          {connections.map((c, i) => (
            <g key={i}>
              <line
                x1={c.from.cx}
                y1={c.from.cy}
                x2={c.to.cx}
                y2={c.to.cy}
                stroke="url(#synapsesGrad)"
                strokeWidth="1.5"
                strokeDasharray="4 4"
                className="opacity-70"
              />
              {/* Animated Synaptic Impulse Particle */}
              <circle r="3" fill="#60a5fa" filter="url(#glow)">
                <animateMotion
                  path={`M${c.from.cx},${c.from.cy} L${c.to.cx},${c.to.cy}`}
                  dur={`${2 + (i % 3)}s`}
                  repeatCount="indefinite"
                />
              </circle>
            </g>
          ))}

          {/* Contextual Callout Lines to Badges */}
          {/* Callout 1: Na+ Channel */}
          <line x1="200" y1="120" x2="80" y2="70" stroke="#3b82f6" strokeWidth="1" strokeDasharray="2 2" opacity="0.6" />
          <circle cx="80" cy="70" r="3" fill="#3b82f6" />

          {/* Callout 2: GABA-A Inhibitory */}
          <line x1="320" y1="90" x2="510" y2="70" stroke="#10b981" strokeWidth="1" strokeDasharray="2 2" opacity="0.6" />
          <circle cx="510" cy="70" r="3" fill="#10b981" />

          {/* Callout 3: NMDA Receptor */}
          <line x1="300" y1="220" x2="510" y2="350" stroke="#6366f1" strokeWidth="1" strokeDasharray="2 2" opacity="0.6" />
          <circle cx="510" cy="350" r="3" fill="#6366f1" />

          {/* Neural Nodes */}
          {nodes.map((node) => (
            <g key={node.id} className="cursor-pointer group">
              {/* Outer Glow Ring */}
              <circle
                cx={node.cx}
                cy={node.cy}
                r={node.type === 'core' ? 18 : 12}
                fill={node.color}
                fillOpacity="0.15"
                stroke={node.color}
                strokeWidth="1.5"
                className="group-hover:scale-125 transition-transform"
                filter="url(#glow)"
              />
              {/* Core Node Pulse */}
              <circle
                cx={node.cx}
                cy={node.cy}
                r={node.type === 'core' ? 8 : 5}
                fill={node.color}
              />
              {/* Node Text Label */}
              <text
                x={node.cx}
                y={node.cy + (node.type === 'core' ? 26 : 20)}
                textAnchor="middle"
                fill="#94a3b8"
                fontSize="9"
                fontFamily="JetBrains Mono, monospace"
                fontWeight="600"
              >
                {node.label}
              </text>
            </g>
          ))}
        </svg>

        {/* Floating Contextual Callout Overlay Badges */}
        <div className="absolute top-3 left-3 glass-card p-2.5 rounded-xl border border-blue-500/30 bg-surface-900/80 backdrop-blur-md">
          <div className="text-[10px] font-mono font-bold text-blue-400 uppercase tracking-wider flex items-center gap-1">
            <Zap className="w-3 h-3" /> Na+ Voltage-Gated
          </div>
          <div className="text-[11px] font-bold text-white mt-0.5">Depolarization Block: 42%</div>
        </div>

        <div className="absolute top-3 right-3 glass-card p-2.5 rounded-xl border border-emerald-500/30 bg-surface-900/80 backdrop-blur-md">
          <div className="text-[10px] font-mono font-bold text-emerald-400 uppercase tracking-wider flex items-center gap-1">
            <Activity className="w-3 h-3" /> GABA-A Receptor
          </div>
          <div className="text-[11px] font-bold text-white mt-0.5">Inhibitory Current: +28%</div>
        </div>

        <div className="absolute bottom-3 right-3 glass-card p-2.5 rounded-xl border border-indigo-500/30 bg-surface-900/80 backdrop-blur-md">
          <div className="text-[10px] font-mono font-bold text-indigo-400 uppercase tracking-wider flex items-center gap-1">
            <Brain className="w-3 h-3" /> NMDA / AMPA Ratio
          </div>
          <div className="text-[11px] font-bold text-white mt-0.5">E/I Balance: 0.72 (Stable)</div>
        </div>
      </div>

      {/* Footer Metrics Banner */}
      <div className="relative z-10 pt-3 border-t border-slate-800/80 flex items-center justify-between flex-wrap gap-4 text-xs font-mono text-slate-400">
        <div className="flex items-center gap-4">
          <span className="flex items-center gap-1.5">
            <span className="w-2 h-2 rounded-full bg-blue-500" />
            Excitatory: 80%
          </span>
          <span className="flex items-center gap-1.5">
            <span className="w-2 h-2 rounded-full bg-emerald-500" />
            Inhibitory: 20%
          </span>
        </div>

        <div className="flex items-center gap-2">
          <span className="text-[10px] uppercase text-slate-500 font-bold">Active Drug:</span>
          <span className="text-white font-bold">{latestDrugName}</span>
        </div>
      </div>
    </div>
  );
}
