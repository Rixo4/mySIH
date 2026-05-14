import type { ChangeEvent, FormEvent } from 'react';
import type { ChannelInput, EngineMode, SingleSimulationRequest } from '../types';

interface SingleSimulationFormProps {
  value: SingleSimulationRequest;
  loading?: boolean;
  error?: string | null;
  onChange: (next: SingleSimulationRequest) => void;
  onSubmit: (payload: SingleSimulationRequest) => void;
}

function updateChannel(
  value: SingleSimulationRequest,
  channel: 'Na' | 'K' | 'Ca',
  key: keyof ChannelInput,
  nextValue: number
): SingleSimulationRequest {
  return {
    ...value,
    channels: {
      ...value.channels,
      [channel]: {
        ...value.channels[channel],
        [key]: nextValue
      }
    }
  };
}

export function SingleSimulationForm({ value, loading = false, error, onChange, onSubmit }: SingleSimulationFormProps) {
  const submit = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    onSubmit(value);
  };

  const handleDrugName = (event: ChangeEvent<HTMLInputElement>) => {
    onChange({ ...value, drug_name: event.target.value });
  };

  const handleDose = (event: ChangeEvent<HTMLInputElement>) => {
    onChange({ ...value, dose: Number(event.target.value) });
  };

  const setMode = (mode: EngineMode) => {
    onChange({ ...value, mode });
  };

  return (
    <form onSubmit={submit} className="space-y-6">
      {error ? (
        <div className="rounded-2xl border border-rose-400/20 bg-rose-500/10 px-4 py-3 text-sm text-rose-200">
          {error}
        </div>
      ) : null}

      <section className="glass-card p-6">
        <h3 className="text-lg font-semibold text-white">Drug Identity</h3>
        <div className="mt-4 grid gap-4">
          <label className="space-y-2 text-sm">
            <span className="text-slate-300">Drug Name</span>
            <input
              value={value.drug_name}
              onChange={handleDrugName}
              className="w-full rounded-2xl border border-white/10 bg-slate-950/70 px-4 py-3 text-white outline-none transition placeholder:text-slate-500 focus:border-cyan-400/40 focus:ring-2 focus:ring-cyan-400/10"
              placeholder="Test-Na-Modulator"
            />
          </label>
        </div>
      </section>

      <section className="glass-card p-6">
        <h3 className="text-lg font-semibold text-white">Ion Channel Parameters</h3>
        <div className="mt-5 grid gap-6 xl:grid-cols-3">
          {(['Na', 'K', 'Ca'] as const).map((channel) => (
            <div key={channel} className="rounded-2xl border border-white/10 bg-slate-950/45 p-4">
              <h4 className="text-sm font-semibold uppercase tracking-[0.28em] text-cyan-200">{channel} Channel</h4>
              <div className="mt-4 grid gap-3">
                {(['ic50', 'hill'] as const).map((field) => (
                  <label key={field} className="space-y-2 text-sm">
                    <span className="text-slate-300">{field.toUpperCase()}</span>
                    <input
                      type="number"
                      step="0.01"
                      min="0"
                      value={value.channels[channel][field]}
                      onChange={(event) =>
                        onChange(updateChannel(value, channel, field, Number(event.target.value)))
                      }
                      className="w-full rounded-xl border border-white/10 bg-slate-950/70 px-4 py-3 text-white outline-none transition placeholder:text-slate-500 focus:border-cyan-400/40 focus:ring-2 focus:ring-cyan-400/10"
                    />
                  </label>
                ))}
              </div>
            </div>
          ))}
        </div>
      </section>

      <section className="glass-card p-6">
        <h3 className="text-lg font-semibold text-white">Single Dose Settings</h3>
        <div className="mt-4 grid gap-4 md:grid-cols-2">
          <label className="space-y-2 text-sm">
            <span className="text-slate-300">Input Dose</span>
            <input
              type="number"
              min="0"
              step="0.1"
              value={value.dose}
              onChange={handleDose}
              className="w-full rounded-2xl border border-white/10 bg-slate-950/70 px-4 py-3 text-white outline-none transition focus:border-cyan-400/40 focus:ring-2 focus:ring-cyan-400/10"
            />
          </label>

          <div className="space-y-2 text-sm">
            <span className="text-slate-300">Mode</span>
            <div className="grid grid-cols-2 gap-3">
              {(['fast', 'accurate'] as const).map((mode) => (
                <button
                  key={mode}
                  type="button"
                  onClick={() => setMode(mode)}
                  className={`rounded-2xl border px-4 py-3 text-left transition ${
                    value.mode === mode
                      ? 'border-cyan-400/40 bg-cyan-500/10 text-cyan-100'
                      : 'border-white/10 bg-slate-950/60 text-slate-300 hover:border-cyan-400/20 hover:bg-white/5'
                  }`}
                >
                  <p className="font-medium capitalize">{mode}</p>
                  <p className="mt-1 text-xs text-slate-400">{mode === 'fast' ? 'Lower-latency run' : 'Full-quality run'}</p>
                </button>
              ))}
            </div>
          </div>
        </div>
      </section>

      <div className="flex flex-col gap-3 md:flex-row md:items-center md:justify-between">
        <p className="text-sm text-slate-400">
          Validation enforced: drug name required, IC50 and Hill must be positive, dose must be non-negative.
        </p>
        <button
          type="submit"
          disabled={loading}
          className="inline-flex items-center justify-center rounded-2xl bg-gradient-to-r from-cyan-500 to-emerald-500 px-5 py-3 text-sm font-semibold text-midnight-950 shadow-glow transition hover:scale-[1.01] disabled:cursor-not-allowed disabled:opacity-60"
        >
          {loading ? 'Running Simulation...' : 'Run Single Dose Simulation'}
        </button>
      </div>
    </form>
  );
}
