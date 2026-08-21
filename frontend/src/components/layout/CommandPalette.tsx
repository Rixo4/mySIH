import React, { useEffect, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { Search, LayoutDashboard, TestTube2, History, Activity, Settings, FileText, ArrowRight, X } from 'lucide-react';
import { getRuns } from '../../api/client';
import type { RunListItem } from '../../types';

interface CommandPaletteProps {
  isOpen: boolean;
  onClose: () => void;
}

export const CommandPalette: React.FC<CommandPaletteProps> = ({ isOpen, onClose }) => {
  const navigate = useNavigate();
  const [query, setQuery] = useState('');
  const [runs, setRuns] = useState<RunListItem[]>([]);
  const [loadingRuns, setLoadingRuns] = useState(false);

  useEffect(() => {
    if (isOpen) {
      setQuery('');
      setLoadingRuns(true);
      getRuns()
        .then((data) => setRuns(data.runs || []))
        .catch(() => setRuns([]))
        .finally(() => setLoadingRuns(false));
    }
  }, [isOpen]);

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && e.key === 'k') {
        e.preventDefault();
        if (isOpen) onClose();
        else {
          // Open triggered from parent or global handler
        }
      }
      if (e.key === 'Escape' && isOpen) {
        onClose();
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [isOpen, onClose]);

  if (!isOpen) return null;

  const quickNav = [
    { label: 'Dashboard', path: '/app', icon: LayoutDashboard },
    { label: 'New Evaluation', path: '/app/dose-eval', icon: TestTube2 },
    { label: 'Run History', path: '/app/history', icon: History },
  ];

  const filteredRuns = runs.filter((run) => {
    const term = query.toLowerCase();
    return (
      run.run_id.toLowerCase().includes(term) ||
      (run.drug_name && run.drug_name.toLowerCase().includes(term)) ||
      (run.risk_level && run.risk_level.toLowerCase().includes(term)) ||
      (run.recommendation && run.recommendation.toLowerCase().includes(term))
    );
  });

  const handleSelect = (path: string) => {
    navigate(path);
    onClose();
  };

  return (
    <div className="fixed inset-0 z-50 flex items-start justify-center pt-20 px-4 bg-slate-950/80 backdrop-blur-sm animate-fadeIn">
      <div
        className="w-full max-w-2xl bg-slate-900 border border-slate-700/80 rounded-2xl shadow-2xl overflow-hidden flex flex-col"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Search Header */}
        <div className="flex items-center px-4 border-b border-slate-800 bg-slate-900/90">
          <Search className="w-5 h-5 text-cyan-400 shrink-0 mr-3" />
          <input
            type="text"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
            placeholder="Search experiments, compounds, or navigate pages... (Ctrl + K)"
            className="w-full py-4 bg-transparent text-slate-100 placeholder-slate-500 text-sm focus:outline-none"
            autoFocus
          />
          <button
            onClick={onClose}
            className="p-1 rounded-lg text-slate-400 hover:text-white hover:bg-slate-800 transition-colors"
          >
            <X className="w-5 h-5" />
          </button>
        </div>

        {/* Results List */}
        <div className="max-h-[60vh] overflow-y-auto p-3 space-y-4">
          {/* Quick Navigation Section */}
          {!query && (
            <div>
              <div className="px-3 py-1 text-[11px] font-semibold tracking-wider text-slate-400 uppercase">
                Navigation
              </div>
              <div className="mt-1 space-y-1">
                {quickNav.map((item) => {
                  const Icon = item.icon;
                  return (
                    <button
                      key={item.path}
                      onClick={() => handleSelect(item.path)}
                      className="w-full flex items-center justify-between px-3 py-2.5 rounded-xl text-slate-200 hover:text-cyan-400 hover:bg-cyan-950/40 border border-transparent hover:border-cyan-800/40 transition-all text-sm group"
                    >
                      <div className="flex items-center gap-3">
                        <Icon className="w-4 h-4 text-cyan-400 group-hover:scale-110 transition-transform" />
                        <span>{item.label}</span>
                      </div>
                      <ArrowRight className="w-4 h-4 opacity-0 group-hover:opacity-100 transition-opacity" />
                    </button>
                  );
                })}
              </div>
            </div>
          )}

          {/* Experiments Section */}
          <div>
            <div className="px-3 py-1 text-[11px] font-semibold tracking-wider text-slate-400 uppercase flex items-center justify-between">
              <span>Experiments & Runs</span>
              {loadingRuns && <span className="text-xs text-slate-400 font-normal">Loading...</span>}
            </div>

            <div className="mt-1 space-y-1">
              {filteredRuns.length === 0 && !loadingRuns ? (
                <div className="px-3 py-6 text-center text-xs text-slate-400">
                  {query ? `No experiments matching "${query}"` : 'No past experiments found.'}
                </div>
              ) : (
                filteredRuns.slice(0, 8).map((run) => (
                  <button
                    key={run.run_id}
                    onClick={() => handleSelect(`/app/reports/${run.run_id}`)}
                    className="w-full flex items-center justify-between px-3 py-2.5 rounded-xl text-left hover:bg-slate-800/80 border border-slate-800/50 hover:border-slate-700 transition-all text-sm group"
                  >
                    <div className="flex items-center gap-3">
                      <FileText className="w-4 h-4 text-indigo-400 shrink-0" />
                      <div>
                        <div className="font-mono text-xs text-slate-200 font-semibold group-hover:text-cyan-300">
                          {run.run_id}
                        </div>
                        <div className="text-xs text-slate-400 flex items-center gap-2">
                          <span>{run.drug_name || 'Unnamed Compound'}</span>
                          {run.risk_level && (
                            <span className="px-1.5 py-0.5 rounded text-[10px] bg-slate-800 border border-slate-700 text-slate-300 font-medium">
                              {run.risk_level}
                            </span>
                          )}
                        </div>
                      </div>
                    </div>
                    <ArrowRight className="w-4 h-4 text-slate-500 group-hover:text-cyan-400 transition-colors" />
                  </button>
                ))
              )}
            </div>
          </div>
        </div>

        {/* Footer */}
        <div className="px-4 py-2.5 bg-slate-950/60 border-t border-slate-800 flex items-center justify-between text-xs text-slate-400">
          <div className="flex items-center gap-2">
            <kbd className="px-1.5 py-0.5 rounded bg-slate-800 border border-slate-700 text-[10px] text-slate-300">
              Esc
            </kbd>
            <span>to close</span>
          </div>
          <span>Silicon Patient Research Console</span>
        </div>
      </div>
    </div>
  );
};
