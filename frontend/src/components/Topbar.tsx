import React from 'react';
import { Search, Play, LogIn, LogOut, UserPlus, Menu } from 'lucide-react';
import { Link, useNavigate } from 'react-router-dom';
import { StatusBadge } from './StatusBadge';
import { useAuth } from '../context/AuthContext';
import { useRunTask } from '../context/RunTaskContext';

interface TopbarProps {
  pageTitle: string;
  backendConnected: boolean;
  engineOnline: boolean;
  onOpenCommandPalette: () => void;
  onOpenActiveJobs: () => void;
  onToggleMobileMenu: () => void;
}

export function Topbar({
  pageTitle,
  backendConnected,
  engineOnline,
  onOpenCommandPalette,
  onOpenActiveJobs,
  onToggleMobileMenu,
}: TopbarProps) {
  const { accessToken, logout } = useAuth();
  const { drugEvaluationState } = useRunTask();
  const navigate = useNavigate();

  const isJobExecuting =
    drugEvaluationState.status === 'queued' || drugEvaluationState.status === 'running';

  async function handleLogout() {
    await logout();
    navigate('/', { replace: true });
  }

  return (
    <header className="sticky top-0 z-40 border-b border-white/10 bg-obsidian-950/80 backdrop-blur-2xl">
      <div className="flex items-center justify-between px-4 py-3 xl:px-8 max-w-full">
        {/* Left: Title & Mobile Toggle */}
        <div className="flex items-center gap-3">
          <button
            onClick={onToggleMobileMenu}
            className="xl:hidden p-1.5 rounded-full text-slate-400 hover:text-white bg-white/5 border border-white/10 transition-colors"
          >
            <Menu className="w-5 h-5" />
          </button>
          <div>
            <div className="flex items-center gap-2">
              <span className="w-2 h-2 rounded-full bg-violet-400 alert-dot" />
              <p className="text-[10px] uppercase tracking-widest font-mono text-violet-300 font-bold">
                Silicon Patient / NeuroSIH OS
              </p>
            </div>
            <h1 className="text-lg font-bold text-white tracking-tight font-sans">{pageTitle}</h1>
          </div>
        </div>

        {/* Center: Search Command Trigger */}
        <div className="hidden md:flex items-center flex-1 max-w-xs mx-6">
          <button
            onClick={onOpenCommandPalette}
            className="w-full flex items-center justify-between px-4 py-1.5 rounded-full bg-white/[0.04] border border-white/10 text-xs text-slate-400 hover:text-white hover:border-violet-500/40 transition-all shadow-inner"
          >
            <div className="flex items-center gap-2">
              <Search className="w-3.5 h-3.5 text-violet-400" />
              <span>Search commands & runs...</span>
            </div>
            <kbd className="px-2 py-0.5 rounded-full bg-white/10 text-[9px] text-slate-300 font-mono border border-white/10">
              Ctrl K
            </kbd>
          </button>
        </div>

        {/* Right: Resq.io Metric Badges & Auth */}
        <div className="flex items-center gap-3">
          {/* Active Simulation Pill */}
          {isJobExecuting && (
            <button
              onClick={onOpenActiveJobs}
              className="flex items-center gap-2 px-3 py-1 rounded-full bg-violet-600/30 border border-violet-400/40 text-violet-200 text-xs font-semibold animate-pulse shadow-[0_0_15px_rgba(139,92,246,0.4)]"
            >
              <Play className="w-3 h-3 text-violet-300 fill-violet-300" />
              <span>1 Simulation Running</span>
            </button>
          )}

          {/* System Telemetry Badges */}
          <div className="hidden xl:flex items-center gap-2">
            <StatusBadge label="Backend" value={backendConnected ? 'Connected' : 'Disconnected'} />
            <StatusBadge label="Engine" value={engineOnline ? 'Online' : 'Unknown'} />
          </div>

          {/* User Profile Pill & Logout */}
          {accessToken ? (
            <div className="flex items-center gap-2">
              {/* Real user badge */}
              <button
                type="button"
                onClick={handleLogout}
                className="p-1.5 rounded-full bg-white/5 border border-white/10 text-slate-300 hover:text-white hover:bg-rose-900/40 hover:border-rose-400/40 transition-all cursor-pointer"
                title="Logout"
              >
                <LogOut className="h-4 w-4" />
              </button>
            </div>
          ) : (
            <div className="flex items-center gap-2">
              <Link
                to="/login"
                className="text-xs font-medium text-slate-300 hover:text-white px-3 py-1.5 transition-colors"
              >
                Login
              </Link>
              <Link
                to="/signup"
                className="btn-primary text-xs py-1.5 px-4 font-sans rounded-full"
              >
                Sign up
              </Link>
            </div>
          )}
        </div>
      </div>
    </header>
  );
}
