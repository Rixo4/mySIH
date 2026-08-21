import React from 'react';
import { X, Play, ArrowLeft, ShieldAlert, CheckCircle2 } from 'lucide-react';
import type { DrugEvalRequest } from '../../types';

interface ExperimentReviewModalProps {
  isOpen: boolean;
  onClose: () => void;
  onConfirm: () => void;
  payload: DrugEvalRequest;
  loading: boolean;
}

export const ExperimentReviewModal: React.FC<ExperimentReviewModalProps> = ({
  isOpen,
  onClose,
  onConfirm,
  payload,
  loading
}) => {
  if (!isOpen) return null;

  const totalDosePoints =
    payload.dose_range.step > 0
      ? Math.floor((payload.dose_range.max - payload.dose_range.min) / payload.dose_range.step) + 1
      : 0;

  const isLongWorkload = totalDosePoints * payload.runs > 25;

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-slate-950/80 backdrop-blur-md animate-fadeIn">
      <div
        className="w-full max-w-xl bg-slate-900 border border-slate-700/80 rounded-2xl shadow-2xl overflow-hidden flex flex-col"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="p-5 border-b border-slate-800 flex items-center justify-between bg-slate-950/60">
          <div>
            <h3 className="text-base font-bold text-slate-100">Review Experiment Configuration</h3>
            <p className="text-xs text-slate-400 mt-0.5">Pre-flight validation & workload assessment</p>
          </div>
          <button
            onClick={onClose}
            disabled={loading}
            className="p-1.5 rounded-lg text-slate-400 hover:text-white hover:bg-slate-800 transition-colors"
          >
            <X className="w-5 h-5" />
          </button>
        </div>

        {/* Content */}
        <div className="p-6 space-y-5 overflow-y-auto max-h-[70vh]">
          {/* Identity & Range Overview */}
          <div className="grid grid-cols-2 gap-4 p-4 rounded-xl bg-slate-950/50 border border-slate-800">
            <div>
              <span className="text-[11px] font-mono uppercase text-slate-500">Compound Name</span>
              <p className="text-sm font-bold text-cyan-300 font-mono mt-0.5">{payload.drug_name}</p>
            </div>
            <div>
              <span className="text-[11px] font-mono uppercase text-slate-500">Total Simulation Runs</span>
              <p className="text-sm font-bold text-slate-200 mt-0.5">
                {payload.runs} iterations ({totalDosePoints} dose steps)
              </p>
            </div>
            <div>
              <span className="text-[11px] font-mono uppercase text-slate-500">Dose Concentration Range</span>
              <p className="text-sm font-medium text-slate-300 mt-0.5">
                {payload.dose_range.min} µM → {payload.dose_range.max} µM
              </p>
            </div>
            <div>
              <span className="text-[11px] font-mono uppercase text-slate-500">Step Increment</span>
              <p className="text-sm font-medium text-slate-300 mt-0.5">{payload.dose_range.step} µM</p>
            </div>
          </div>

          {/* Ion Channel Binding Summary */}
          <div>
            <h4 className="text-xs font-semibold uppercase tracking-wider text-slate-400 font-mono mb-2">
              Ion Channel Binding Profiles (IC50 & Hill)
            </h4>
            <div className="grid grid-cols-3 gap-3">
              {(['Na', 'K', 'Ca'] as const).map((ch) => (
                <div key={ch} className="p-3 rounded-xl bg-slate-950/40 border border-slate-800">
                  <div className="text-xs font-bold text-cyan-400 font-mono">{ch} Channel</div>
                  <div className="text-xs text-slate-300 mt-1">IC50: {payload.channels[ch].ic50} µM</div>
                  <div className="text-[11px] text-slate-400">Hill: {payload.channels[ch].hill}</div>
                </div>
              ))}
            </div>
          </div>

          {/* Workload Alert */}
          {isLongWorkload ? (
            <div className="p-3.5 rounded-xl bg-amber-950/40 border border-amber-800/40 flex items-start gap-3 text-amber-200 text-xs">
              <ShieldAlert className="w-5 h-5 text-amber-400 shrink-0 mt-0.5" />
              <div>
                <span className="font-semibold">Potentially High Workload</span>
                <p className="mt-0.5 opacity-90">
                  This configuration requires {totalDosePoints * payload.runs} biophysical simulation runs. Execution will run asynchronously on the native C++/CUDA engine.
                </p>
              </div>
            </div>
          ) : (
            <div className="p-3 rounded-xl bg-emerald-950/40 border border-emerald-800/40 flex items-center gap-2.5 text-emerald-200 text-xs">
              <CheckCircle2 className="w-4 h-4 text-emerald-400 shrink-0" />
              <span>Configuration valid and ready for execution.</span>
            </div>
          )}
        </div>

        {/* Footer Actions */}
        <div className="p-5 border-t border-slate-800 bg-slate-950/80 flex items-center justify-between">
          <button
            onClick={onClose}
            disabled={loading}
            className="flex items-center gap-2 px-4 py-2 rounded-xl text-xs font-semibold text-slate-300 hover:text-white bg-slate-800/60 hover:bg-slate-800 transition-colors"
          >
            <ArrowLeft className="w-4 h-4" /> Edit Parameters
          </button>

          <button
            onClick={onConfirm}
            disabled={loading}
            className="flex items-center gap-2 px-5 py-2.5 rounded-xl text-xs font-bold text-slate-950 bg-gradient-to-r from-cyan-400 to-emerald-400 hover:from-cyan-300 hover:to-emerald-300 shadow-lg shadow-cyan-500/20 transition-all disabled:opacity-50"
          >
            <Play className="w-4 h-4 fill-slate-950" /> Start Simulation
          </button>
        </div>
      </div>
    </div>
  );
};
