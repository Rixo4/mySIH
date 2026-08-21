import React, { type ChangeEvent, type FormEvent } from 'react';
import { Play, Sliders, Info, Zap, Activity } from 'lucide-react';
import type { ChannelInput, DrugEvalRequest } from '../types';

interface DrugInputFormProps {
  value?: DrugEvalRequest;
  payload?: DrugEvalRequest;
  loading?: boolean;
  onChange: (next: DrugEvalRequest) => void;
  onSubmit: (payload: DrugEvalRequest) => void;
  error?: string | null;
  backendConnected?: boolean;
}

const EMPTY_PAYLOAD: DrugEvalRequest = {
  drug_name: '',
  channels: {
    Na: { ic50: 0, hill: 1.0 },
    K: { ic50: 0, hill: 1.0 },
    Ca: { ic50: 0, hill: 1.0 },
  },
  dose_range: {
    min: 0,
    max: 20,
    step: 2,
  },
  runs: 3,
};

function updateChannel(
  current: DrugEvalRequest,
  channel: 'Na' | 'K' | 'Ca',
  key: keyof ChannelInput,
  nextValue: number
): DrugEvalRequest {
  return {
    ...current,
    channels: {
      ...current.channels,
      [channel]: {
        ...(current.channels?.[channel] ?? { ic50: 0, hill: 1.0 }),
        [key]: nextValue,
      },
    },
  };
}

const CHANNEL_CONFIGS = [
  {
    id: 'Na' as const,
    title: 'Na⁺ Channel',
    sub: 'Voltage-Gated (Na_v)',
    desc: 'Action potential upstroke velocity & firing threshold.',
    maxIc50: 1000,
    theme: {
      card: 'bg-gradient-to-b from-sky-950/30 via-[#111120] to-[#0d0d18] border-sky-500/35 hover:border-sky-400/60 shadow-[0_0_15px_rgba(56,189,248,0.08)]',
      title: 'text-sky-400',
      sub: 'text-sky-400/70',
      badge: 'text-sky-300 bg-sky-950/70 border-sky-800/60',
      inputFocus: 'focus:border-sky-400',
      accent: 'text-sky-400',
    },
  },
  {
    id: 'K' as const,
    title: 'K⁺ Channel',
    sub: 'Inward Rectifier (K_v)',
    desc: 'Membrane repolarization, refractory period & damping.',
    maxIc50: 1000,
    theme: {
      card: 'bg-gradient-to-b from-indigo-950/30 via-[#111120] to-[#0d0d18] border-indigo-500/35 hover:border-indigo-400/60 shadow-[0_0_15px_rgba(99,102,241,0.08)]',
      title: 'text-indigo-400',
      sub: 'text-indigo-400/70',
      badge: 'text-indigo-300 bg-indigo-950/70 border-indigo-800/60',
      inputFocus: 'focus:border-indigo-400',
      accent: 'text-indigo-400',
    },
  },
  {
    id: 'Ca' as const,
    title: 'Ca²⁺ Channel',
    sub: 'L-Type Influx (Ca_v)',
    desc: 'Dendritic calcium spikes & burst synchrony.',
    maxIc50: 1000,
    theme: {
      card: 'bg-gradient-to-b from-emerald-950/30 via-[#111120] to-[#0d0d18] border-emerald-500/35 hover:border-emerald-400/60 shadow-[0_0_15px_rgba(16,185,129,0.08)]',
      title: 'text-emerald-400',
      sub: 'text-emerald-400/70',
      badge: 'text-emerald-300 bg-emerald-950/70 border-emerald-800/60',
      inputFocus: 'focus:border-emerald-400',
      accent: 'text-emerald-400',
    },
  },
];

export function DrugInputForm({
  value: valProp,
  payload: payloadProp,
  loading = false,
  onChange,
  onSubmit,
  error,
  backendConnected = true,
}: DrugInputFormProps) {
  const safeValue: DrugEvalRequest = valProp ?? payloadProp ?? EMPTY_PAYLOAD;

  const submit = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    if (!backendConnected) return;
    onSubmit(safeValue);
  };

  const handleNumber = (field: keyof DrugEvalRequest['dose_range'] | 'runs', nextValue: string) => {
    const numberValue = nextValue === '' ? 0 : Number(nextValue);
    if (field === 'runs') {
      onChange({ ...safeValue, runs: Math.max(1, Math.min(10, numberValue)) });
      return;
    }

    onChange({
      ...safeValue,
      dose_range: {
        ...(safeValue.dose_range ?? { min: 0, max: 20, step: 2 }),
        [field]: numberValue,
      },
    });
  };

  const channels = safeValue.channels ?? {
    Na: { ic50: 0, hill: 1.0 },
    K: { ic50: 0, hill: 1.0 },
    Ca: { ic50: 0, hill: 1.0 },
  };

  const doseRange = safeValue.dose_range ?? { min: 0, max: 20, step: 2 };

  return (
    <form onSubmit={submit} className="space-y-2.5 font-sans text-slate-100">
      {/* Form Header */}
      <div className="flex items-center justify-between pb-2 border-b border-slate-800/80">
        <div className="flex items-center gap-2">
          <Sliders className="w-3.5 h-3.5 text-sky-400" />
          <h3 className="text-xs font-bold uppercase tracking-wider text-slate-200">
            Compound Assay Parameters
          </h3>
        </div>
        <span className="text-[10px] font-mono text-slate-500">IN-SILICO CONFIG</span>
      </div>

      {/* Row 1: Drug Name & Repetitions (Horizontal Side-by-Side) */}
      <div className="grid grid-cols-1 sm:grid-cols-12 gap-2">
        <div className="sm:col-span-9 space-y-1">
          <label className="text-[10px] font-medium text-slate-400 flex items-center justify-between">
            <span>Compound / Candidate Identifier</span>
            <span className="text-[9px] font-mono text-slate-500">REQUIRED</span>
          </label>
          <input
            type="text"
            value={safeValue.drug_name || ''}
            onChange={(e: ChangeEvent<HTMLInputElement>) =>
              onChange({ ...safeValue, drug_name: e.target.value })
            }
            placeholder="e.g. Test-K-Blocker, Diazepam, Ketamine-HCl"
            className="w-full px-2.5 py-1.5 rounded-lg bg-[#111120] border border-slate-800 text-xs font-mono text-white placeholder:text-slate-600 focus:outline-none focus:border-blue-500 transition-colors"
            required
          />
        </div>

        <div className="sm:col-span-3 space-y-1">
          <label className="text-[10px] font-medium text-slate-400 block">
            Trials
          </label>
          <input
            type="number"
            min="1"
            max="10"
            value={safeValue.runs ?? 3}
            onChange={(e) => handleNumber('runs', e.target.value)}
            className="w-full px-2.5 py-1.5 rounded-lg bg-[#111120] border border-slate-800 text-xs font-mono text-white focus:outline-none focus:border-blue-500 text-center"
          />
        </div>
      </div>

      {/* Row 2: Ion Channel Affinity Matrix (HORIZONTALLY ALIGNED IN 3 COLUMNS WITH THEMED PALETTE) */}
      <div className="space-y-1.5 pt-0.5">
        <div className="flex items-center justify-between">
          <span className="text-[10px] font-bold text-slate-300 uppercase tracking-wider">
            Ion Channel IC50 & Hill Affinities
          </span>
          <span className="text-[9px] font-mono text-slate-500">Hill-Langmuir Model</span>
        </div>

        {/* 3 Horizontal Channel Columns */}
        <div className="grid grid-cols-1 sm:grid-cols-3 gap-2">
          {CHANNEL_CONFIGS.map((cfg) => {
            const chData = channels[cfg.id] ?? { ic50: 0, hill: 1.0 };
            return (
              <div
                key={cfg.id}
                className={`p-2 sm:p-2.5 rounded-xl border transition-all space-y-1.5 ${cfg.theme.card}`}
              >
                <div className="flex items-center justify-between border-b border-slate-800/60 pb-1">
                  <div>
                    <div className={`text-[11px] font-bold leading-none ${cfg.theme.title}`}>{cfg.title}</div>
                    <div className={`text-[9px] font-mono mt-0.5 ${cfg.theme.sub}`}>{cfg.sub}</div>
                  </div>
                  <div className="text-right">
                    <span className={`text-[10px] font-mono font-bold px-1.5 py-0.5 rounded border ${cfg.theme.badge}`}>
                      {chData.ic50 > 0 ? `${chData.ic50} nM` : '—'}
                    </span>
                  </div>
                </div>

                <div className="grid grid-cols-2 gap-1.5">
                  <div>
                    <label className="text-[9px] font-mono text-slate-400 block mb-0.5">
                      IC50 (nM)
                    </label>
                    <input
                      type="number"
                      min="0"
                      step="0.1"
                      placeholder="0"
                      value={chData.ic50 === 0 ? '' : chData.ic50}
                      onChange={(e) =>
                        onChange(
                          updateChannel(safeValue, cfg.id, 'ic50', Math.max(0, Number(e.target.value)))
                        )
                      }
                      className={`w-full px-2 py-1 rounded-md bg-[#08080f]/90 border border-slate-800 text-[11px] font-mono text-white placeholder:text-slate-600 focus:outline-none ${cfg.theme.inputFocus}`}
                    />
                  </div>

                  <div>
                    <label className="text-[9px] font-mono text-slate-400 block mb-0.5">
                      Hill (n)
                    </label>
                    <input
                      type="number"
                      min="0.1"
                      max="10"
                      step="0.1"
                      placeholder="1.0"
                      value={chData.hill || ''}
                      onChange={(e) =>
                        onChange(
                          updateChannel(safeValue, cfg.id, 'hill', Math.max(0.1, Number(e.target.value)))
                        )
                      }
                      className={`w-full px-2 py-1 rounded-md bg-[#08080f]/90 border border-slate-800 text-[11px] font-mono text-white focus:outline-none ${cfg.theme.inputFocus}`}
                    />
                  </div>
                </div>
              </div>
            );
          })}
        </div>
      </div>

      {/* Row 3: Concentration Sweep Range (Horizontal Row with Themed Palette) */}
      <div className="p-2 sm:p-2.5 rounded-xl bg-gradient-to-b from-blue-950/20 via-[#111120] to-[#0d0d18] border border-blue-500/30 hover:border-blue-400/50 shadow-[0_0_12px_rgba(59,130,246,0.06)] space-y-1">
        <div className="flex items-center justify-between text-[10px] font-mono">
          <span className="font-bold text-blue-300 uppercase tracking-wider">
            Concentration Sweep Protocol
          </span>
          <span className="text-blue-400 font-bold px-1.5 py-0.2 rounded bg-blue-950/60 border border-blue-800/60">
            {doseRange.min} – {doseRange.max} nM (Δ {doseRange.step} nM)
          </span>
        </div>

        <div className="grid grid-cols-3 gap-2">
          <div>
            <label className="text-[9px] font-mono text-slate-400 block mb-0.5">Min (nM)</label>
            <input
              type="number"
              min="0"
              value={doseRange.min}
              onChange={(e) => handleNumber('min', e.target.value)}
              className="w-full px-2 py-1 rounded-md bg-[#08080f]/90 border border-slate-800 text-xs font-mono text-white focus:outline-none focus:border-blue-400"
            />
          </div>

          <div>
            <label className="text-[9px] font-mono text-slate-400 block mb-0.5">Max (nM)</label>
            <input
              type="number"
              min="1"
              value={doseRange.max}
              onChange={(e) => handleNumber('max', e.target.value)}
              className="w-full px-2 py-1 rounded-md bg-[#08080f]/90 border border-slate-800 text-xs font-mono text-white focus:outline-none focus:border-blue-400"
            />
          </div>

          <div>
            <label className="text-[9px] font-mono text-slate-400 block mb-0.5">Step (nM)</label>
            <input
              type="number"
              min="0.1"
              step="0.5"
              value={doseRange.step}
              onChange={(e) => handleNumber('step', e.target.value)}
              className="w-full px-2 py-1 rounded-md bg-[#08080f]/90 border border-slate-800 text-xs font-mono text-white focus:outline-none focus:border-blue-400"
            />
          </div>
        </div>
      </div>

      {/* Error Message */}
      {error && (
        <div className="p-2 rounded-lg bg-rose-500/10 border border-rose-500/30 text-rose-400 text-xs font-mono">
          {error}
        </div>
      )}

      {/* Submit Button */}
      <button
        type="submit"
        disabled={loading || !backendConnected}
        className={`w-full py-2 rounded-xl flex items-center justify-center gap-2 text-xs font-bold transition-all cursor-pointer ${
          !backendConnected
            ? 'bg-slate-800 text-slate-500 border border-slate-700 cursor-not-allowed'
            : 'bg-blue-600 hover:bg-blue-500 text-white shadow-[0_0_15px_rgba(37,99,235,0.35)] active:scale-[0.99]'
        }`}
      >
        <Play className="w-3.5 h-3.5 fill-current" />
        <span>
          {!backendConnected
            ? 'Backend Disconnected · Solver Unavailable'
            : (loading ? 'Executing Solver Sweep...' : 'Review & Execute Simulation')}
        </span>
      </button>
    </form>
  );
}
