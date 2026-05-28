import type { ChangeEvent, FormEvent } from 'react';
import type { ChannelInput, DrugEvalRequest } from '../types';

interface DrugInputFormProps {
  value: DrugEvalRequest;
  loading?: boolean;
  onChange: (next: DrugEvalRequest) => void;
  onSubmit: (payload: DrugEvalRequest) => void;
  error?: string | null;
}

function updateChannel(value: DrugEvalRequest, channel: 'Na' | 'K' | 'Ca', key: keyof ChannelInput, nextValue: number): DrugEvalRequest {
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

export function DrugInputForm({ value, loading = false, onChange, onSubmit, error }: DrugInputFormProps) {
  const submit = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    onSubmit(value);
  };

  const handleNumber = (
    field: keyof DrugEvalRequest['dose_range'] | 'runs',
    nextValue: string
  ) => {
    const numberValue = nextValue === '' ? 0 : Number(nextValue);
    if (field === 'runs') {
      onChange({ ...value, runs: numberValue });
      return;
    }

    onChange({
      ...value,
      dose_range: {
        ...value.dose_range,
        [field]: numberValue
      }
    });
  };

  const handleDrugName = (event: ChangeEvent<HTMLInputElement>) => {
    onChange({ ...value, drug_name: event.target.value });
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
              placeholder="Test-K-Blocker"
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
        <h3 className="text-lg font-semibold text-white">Dose Sweep</h3>
        <div className="mt-4 grid gap-4 md:grid-cols-3">
          <label className="space-y-2 text-sm">
            <span className="text-slate-300">Minimum Dose</span>
            <input
              type="number"
              min="0"
              step="0.1"
              value={value.dose_range.min}
              onChange={(event) => handleNumber('min', event.target.value)}
              className="w-full rounded-2xl border border-white/10 bg-slate-950/70 px-4 py-3 text-white outline-none transition focus:border-cyan-400/40 focus:ring-2 focus:ring-cyan-400/10"
            />
          </label>
          <label className="space-y-2 text-sm">
            <span className="text-slate-300">Maximum Dose</span>
            <input
              type="number"
              min="0"
              step="0.1"
              value={value.dose_range.max}
              onChange={(event) => handleNumber('max', event.target.value)}
              className="w-full rounded-2xl border border-white/10 bg-slate-950/70 px-4 py-3 text-white outline-none transition focus:border-cyan-400/40 focus:ring-2 focus:ring-cyan-400/10"
            />
          </label>
          <label className="space-y-2 text-sm">
            <span className="text-slate-300">Step Size</span>
            <input
              type="number"
              min="0.1"
              step="0.1"
              value={value.dose_range.step}
              onChange={(event) => handleNumber('step', event.target.value)}
              className="w-full rounded-2xl border border-white/10 bg-slate-950/70 px-4 py-3 text-white outline-none transition focus:border-cyan-400/40 focus:ring-2 focus:ring-cyan-400/10"
            />
          </label>
        </div>
      </section>

      <section className="glass-card p-6">
        <h3 className="text-lg font-semibold text-white">Execution Settings</h3>
        <div className="mt-4 grid gap-4">
          <label className="space-y-2 text-sm">
            <span className="text-slate-300">Number of Runs</span>
            <input
              type="number"
              min="1"
              max="20"
              step="1"
              value={value.runs}
              onChange={(event) => handleNumber('runs', event.target.value)}
              className="w-full rounded-2xl border border-white/10 bg-slate-950/70 px-4 py-3 text-white outline-none transition focus:border-cyan-400/40 focus:ring-2 focus:ring-cyan-400/10"
            />
          </label>
        </div>
      </section>

      <div className="flex flex-col gap-3 md:flex-row md:items-center md:justify-between">
        <p className="text-sm text-slate-400">
          Validation rules enforced before execution: IC50 &gt; 0, Hill &gt; 0, dose range valid, runs between 1 and 20.
        </p>
        <button
          type="submit"
          disabled={loading}
          className="inline-flex items-center justify-center rounded-2xl bg-gradient-to-r from-cyan-500 to-emerald-500 px-5 py-3 text-sm font-semibold text-midnight-950 shadow-glow transition hover:scale-[1.01] disabled:cursor-not-allowed disabled:opacity-60"
        >
          {loading ? 'Running Drug Evaluation...' : 'Run Drug Evaluation'}
        </button>
      </div>
    </form>
  );
}
