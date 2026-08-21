import React, { useEffect, useState } from 'react';
import { Loader2, Square, AlertCircle, CheckCircle2, FileText } from 'lucide-react';
import { useRunTask } from '../../context/RunTaskContext';
import { useNavigate } from 'react-router-dom';

interface SimulationExecutionModalProps {
  isOpen: boolean;
  onClose: () => void;
}

export const SimulationExecutionModal: React.FC<SimulationExecutionModalProps> = ({
  isOpen,
  onClose
}) => {
  const { drugEvaluationState, cancelDrugEvaluationTask } = useRunTask();
  const navigate = useNavigate();
  const [elapsedSeconds, setElapsedSeconds] = useState(0);

  useEffect(() => {
    let interval: number | undefined;
    if (
      isOpen &&
      (drugEvaluationState.status === 'queued' || drugEvaluationState.status === 'running')
    ) {
      setElapsedSeconds(0);
      interval = window.setInterval(() => {
        setElapsedSeconds((prev) => prev + 1);
      }, 1000);
    }
    return () => {
      if (interval) clearInterval(interval);
    };
  }, [isOpen, drugEvaluationState.status]);

  if (!isOpen) return null;

  const isCompleted = drugEvaluationState.status === 'completed';
  const isFailed = drugEvaluationState.status === 'failed';
  const isCancelled = drugEvaluationState.status === 'cancelled';
  const runId = drugEvaluationState.result?.run_id;

  const handleViewReport = () => {
    if (runId) {
      navigate(`/app/reports/${runId}`);
      onClose();
    }
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-slate-950/80 backdrop-blur-md animate-fadeIn">
      <div
        className="w-full max-w-lg bg-slate-900 border border-slate-700/80 rounded-2xl shadow-2xl overflow-hidden flex flex-col p-6 space-y-6"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="flex items-center justify-between border-b border-slate-800 pb-4">
          <div>
            <h3 className="text-base font-bold text-slate-100">
              Native Engine Simulation Status
            </h3>
            <p className="text-xs font-mono text-cyan-400 mt-0.5">
              Compound: {drugEvaluationState.payload.drug_name}
            </p>
          </div>

          <div className="px-3 py-1 rounded-full text-xs font-semibold bg-slate-800 text-slate-300 border border-slate-700">
            {elapsedSeconds}s elapsed
          </div>
        </div>

        {/* Status Indicator */}
        <div className="flex flex-col items-center justify-center py-6 space-y-4 text-center">
          {!isCompleted && !isFailed && !isCancelled ? (
            <>
              <div className="relative flex items-center justify-center w-16 h-16 rounded-full bg-cyan-950/80 border border-cyan-500/40">
                <Loader2 className="w-8 h-8 text-cyan-400 animate-spin" />
              </div>
              <div>
                <span className="text-sm font-semibold text-slate-200 capitalize">
                  {drugEvaluationState.status === 'queued'
                    ? 'Queued in Simulation Engine Pipeline...'
                    : 'Executing C++/CUDA Biophysical Simulation...'}
                </span>
                <p className="text-xs text-slate-400 mt-1 max-w-xs mx-auto">
                  Solving ion channel conductances, transmitter pool kinetics, and network dynamics.
                </p>
              </div>
            </>
          ) : isCompleted ? (
            <>
              <div className="flex items-center justify-center w-16 h-16 rounded-full bg-emerald-950/80 border border-emerald-500/40">
                <CheckCircle2 className="w-8 h-8 text-emerald-400" />
              </div>
              <div>
                <span className="text-sm font-semibold text-emerald-300">
                  Simulation Execution Completed Successfully!
                </span>
                <p className="text-xs text-slate-400 mt-1">
                  Run ID: <span className="font-mono text-cyan-400">{runId}</span>
                </p>
              </div>
            </>
          ) : (
            <>
              <div className="flex items-center justify-center w-16 h-16 rounded-full bg-rose-950/80 border border-rose-500/40">
                <AlertCircle className="w-8 h-8 text-rose-400" />
              </div>
              <div>
                <span className="text-sm font-semibold text-rose-300">
                  {isCancelled ? 'Simulation Cancelled' : 'Simulation Execution Failed'}
                </span>
                {drugEvaluationState.error && (
                  <p className="text-xs text-rose-400 mt-2 p-2.5 rounded-lg bg-rose-950/60 border border-rose-800/40 max-w-sm">
                    {drugEvaluationState.error}
                  </p>
                )}
              </div>
            </>
          )}
        </div>

        {/* Footer Controls */}
        <div className="flex items-center justify-between pt-4 border-t border-slate-800">
          {!isCompleted && !isFailed && !isCancelled ? (
            <button
              onClick={() => void cancelDrugEvaluationTask()}
              className="flex items-center gap-1.5 px-4 py-2 rounded-xl text-xs font-semibold bg-rose-950/80 hover:bg-rose-900 border border-rose-800/60 text-rose-200 transition-colors"
            >
              <Square className="w-3.5 h-3.5 fill-rose-300" /> Cancel Execution
            </button>
          ) : (
            <button
              onClick={onClose}
              className="px-4 py-2 rounded-xl text-xs font-semibold bg-slate-800 hover:bg-slate-700 text-slate-300 transition-colors"
            >
              Close
            </button>
          )}

          {isCompleted && (
            <button
              onClick={handleViewReport}
              className="flex items-center gap-2 px-5 py-2 rounded-xl text-xs font-bold text-slate-950 bg-gradient-to-r from-cyan-400 to-emerald-400 hover:from-cyan-300 hover:to-emerald-300 shadow-lg shadow-cyan-500/20 transition-all"
            >
              <FileText className="w-4 h-4" /> View Full Report & Analysis
            </button>
          )}
        </div>
      </div>
    </div>
  );
};
