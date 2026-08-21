import React from 'react';
import { Beaker, FlaskConical, LayoutDashboard, History, Activity, GitCompare } from 'lucide-react';
import { NavLink } from 'react-router-dom';
import { useAuth } from '../context/AuthContext';

const primaryNavItems = [
  { to: '/app', label: 'Dashboard', icon: LayoutDashboard },
  { to: '/app/dose-eval', label: 'Drug Evaluation', icon: Beaker },
  { to: '/app/history', label: 'Run History', icon: History },
  { to: '/app/compare', label: 'Compare Runs', icon: GitCompare },
];

const secondaryNavItems = [
  { to: '/app/status', label: 'System Status', icon: Activity },
];

interface SidebarProps {
  onCloseMobile?: () => void;
}

export function Sidebar({ onCloseMobile }: SidebarProps) {
  const { user } = useAuth();
  const userLabel = user?.full_name?.trim() || user?.email?.split('@')[0] || 'Research Console';

  return (
    <aside className="flex h-full w-full flex-col border-r border-slate-800 bg-slate-950/95 backdrop-blur-xl">
      {/* Brand Header */}
      <div className="border-b border-slate-800/80 px-5 py-5 xl:px-6">
        <div className="flex items-center gap-3">
          <div className="flex h-10 w-10 items-center justify-center rounded-xl bg-gradient-to-br from-cyan-400 to-emerald-400 text-slate-950 shadow-lg shadow-cyan-500/20">
            <FlaskConical className="h-5 w-5" />
          </div>
          <div>
            <p className="text-sm font-bold text-white tracking-wide">Silicon Patient</p>
            <p className="text-[10px] font-mono uppercase tracking-widest text-cyan-400/90">{userLabel}</p>
          </div>
        </div>
      </div>

      {/* Main Nav */}
      <nav className="flex-1 px-3 py-4 space-y-6 overflow-y-auto">
        <div>
          <div className="px-3 text-[10px] font-semibold tracking-wider text-slate-500 uppercase">
            Workstation
          </div>
          <div className="mt-2 space-y-1">
            {primaryNavItems.map((item) => {
              const Icon = item.icon;
              return (
                <NavLink
                  key={item.to}
                  to={item.to}
                  end={item.to === '/app'}
                  onClick={onCloseMobile}
                  className={({ isActive }) =>
                    `flex items-center gap-3 rounded-xl px-3.5 py-2.5 text-xs font-medium transition-all ${
                      isActive
                        ? 'bg-cyan-500/15 text-cyan-200 border border-cyan-500/30 shadow-xs'
                        : 'text-slate-400 hover:bg-slate-900 hover:text-slate-200 border border-transparent'
                    }`
                  }
                >
                  <Icon className="h-4 w-4 shrink-0" />
                  {item.label}
                </NavLink>
              );
            })}
          </div>
        </div>

        <div>
          <div className="px-3 text-[10px] font-semibold tracking-wider text-slate-500 uppercase">
            System & Analytics
          </div>
          <div className="mt-2 space-y-1">
            {secondaryNavItems.map((item) => {
              const Icon = item.icon;
              return (
                <NavLink
                  key={item.to}
                  to={item.to}
                  onClick={onCloseMobile}
                  className={({ isActive }) =>
                    `flex items-center gap-3 rounded-xl px-3.5 py-2.5 text-xs font-medium transition-all ${
                      isActive
                        ? 'bg-cyan-500/15 text-cyan-200 border border-cyan-500/30 shadow-xs'
                        : 'text-slate-400 hover:bg-slate-900 hover:text-slate-200 border border-transparent'
                    }`
                  }
                >
                  <Icon className="h-4 w-4 shrink-0" />
                  {item.label}
                </NavLink>
              );
            })}
          </div>
        </div>
      </nav>

      {/* Footer Info */}
      <div className="border-t border-slate-800/80 px-5 py-4 text-[11px] text-slate-500 font-mono flex items-center justify-between">
        <span>Engine: C++20 / CUDA</span>
        <span className="w-2 h-2 rounded-full bg-emerald-500 animate-pulse"></span>
      </div>
    </aside>
  );
}
