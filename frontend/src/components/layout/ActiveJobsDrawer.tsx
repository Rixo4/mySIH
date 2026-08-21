import React from 'react';
import { useNavigate } from 'react-router-dom';
import { Loader2, X, AlertCircle, CheckCircle2, Play, Square } from 'lucide-react';
import { useRunTask } from '../../context/RunTaskContext';

interface ActiveJobsDrawerProps {
  isOpen: boolean;
  onClose: () => void;
}

export const ActiveJobsDrawer: React.FC<ActiveJobsDrawerProps> = ({ isOpen, onClose }) => {
  const navigate = useNavigate();
  const { drugEvaluationState, cancelDrugEvaluationTask } = useRunTask();

  if (!isOpen) return null;

  const isExecuting =
    drugEvaluationState.status === 'queued' || drugEvaluationState.status === 'running';

  const handleOpenResults = () => {
    if (drugEvaluationState.result?.run_id) {
      navigate(`/app/reports/${drugEvaluationState.result.run_id}`);
      onClose();
    }
  };

  return (
    <div className="fixed inset-0 z-50 flex justify-end bg-slate-950/60 backdrop-blur-xs animate-fadeIn">
      <div
        className="w-full max-w-md bg-slate-900 border-l border-slate-800 shadow-2xl h-full flex flex-col"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="p-4 border-b border-slate-800 flex items-center justify-between bg-slate-950/40">
          <div className="flex items-center gap-2">
            <Play className="w-4 h-4 text-cyan-400" />
            <h3 className="text-sm font-semibold text-slate-100">Active Simulations</h3>
          </div>
          <button
            onClick={onClose}
            className="p-1 rounded-lg text-slate-400 hover:text-white hover:bg-slate-800 transition-colors"
          >
            <X className="w-5 h-5" />
          </button>
        </div>

        {/* Content */}
        <div className="flex-1 overflow-y-auto p-4 space-y-4">
          {/* Active Job Card */}
          <div className="p-4 rounded-xl border border-slate-800 bg-slate-950/50 space-y-3">
            <div className="flex items-start justify-between">
              <div>
                <span className="text-xs font-mono text-cyan-400 font-semibold">
                  {drugEvaluationState.payload.drug_name}
                </span>
                <p className="text-xs text-slate-400 mt-0.5">
                  Dose: {drugEvaluationState.payload.dose_range.min} - {drugEvaluationState.payload.dose_range.max} µM ({drugEvaluationState.payload.runs} runs)
                </p>
              </div>

              {/* Status Badge */}
              <div className="flex items-center gap-1.5 px-2.5 py-1 rounded-full text-xs font-medium bg-slate-800 text-slate-300 border border-slate-700">
                {isExecuting ? (
                  <>
                    <Loader2 className="w-3.5 h-3.5 text-cyan-400 animate-spin" />
                    <span className="capitalize">{drugEvaluationState.status}</span>
                  </>
                ) : drugEvaluationState.status === 'completed' ? (
                  <>
                    <CheckCircle2 className="w-3.5 h-3.5 text-emerald-400" />
                    <span>Completed</span>
                  </>
                ) : (
                  <>
                    <AlertCircle className="w-3.5 h-3.5 text-amber-400" />
                    <span className="capitalize">{drugEvaluationState.status}</span>
                  </>
                )}
              </div>
            </div>

            {/* Error Message */}
            {drugEvaluationState.error && (
              <div className="p-2.5 rounded-lg bg-rose-950/50 border border-rose-800/40 text-xs text-rose-300">
                {drugEvaluationState.error}
              </div>
            )}

            {/* Actions */}
            <div className="flex items-center justify-end gap-2 pt-2 border-t border-slate-800/60">
              {isExecuting && (
                <button
                  onClick={() => void cancelDrugEvaluationTask()}
                  className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-medium bg-rose-950/60 hover:bg-rose-900 border border-rose-800/50 text-rose-200 transition-colors"
                >
                  <Square className="w-3 h-3 fill-rose-300" />
                  Cancel Job
                </button>
              )}

              {drugEvaluationState.status === 'completed' && drugEvaluationState.result?.run_id && (
                <button
                  onClick={handleOpenResults}
                  className="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-semibold bg-cyan-600 hover:bg-cyan-500 text-white transition-colors"
                >
                  View Report
                </button>
              )}
            </div>
          </div>
        </div>

        {/* Footer */}
        <div className="p-4 border-t border-slate-800 bg-slate-950/60 text-xs text-slate-500 text-center">
          Jobs are executed asynchronously on the native C++/CUDA engine.
        </div>
      </div>
    </div>
  );
};
