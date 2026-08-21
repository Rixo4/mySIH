import React, { useEffect, useState } from 'react';
import { NavLink, useLocation } from 'react-router-dom';
import {
  Beaker,
  ChevronRight,
  FlaskConical,
  GitCompare,
  History,
  LayoutDashboard,
  Pin,
  PinOff,
  X,
} from 'lucide-react';
import { useAuth } from '../../context/AuthContext';

interface NavItem {
  to: string;
  label: string;
  icon: React.ElementType;
}

const WORKSTATION_ITEMS: NavItem[] = [
  { to: '/app',           label: 'Dashboard',       icon: LayoutDashboard },
  { to: '/app/dose-eval', label: 'Drug Evaluation', icon: Beaker },
  { to: '/app/history',   label: 'Run History',     icon: History },
  { to: '/app/compare',   label: 'Compare Runs',    icon: GitCompare },
];

interface AdaptiveNavRailProps {
  isMobileOpen: boolean;
  onCloseMobile: () => void;
}

export function AdaptiveNavRail({ isMobileOpen, onCloseMobile }: AdaptiveNavRailProps) {
  const { user } = useAuth();
  const location = useLocation();

  // Pin state from localStorage
  const [isPinned, setIsPinned] = useState<boolean>(() => {
    try {
      return localStorage.getItem('siliconPatient.navPinned') === 'true';
    } catch {
      return false;
    }
  });

  // Hover state (edge activation corridor or mouse inside rail)
  const [isHovered, setIsHovered] = useState(false);

  const isExpanded = isPinned || isHovered;

  const togglePin = () => {
    const next = !isPinned;
    setIsPinned(next);
    try {
      localStorage.setItem('siliconPatient.navPinned', String(next));
    } catch {
      // ignore
    }
  };

  // Close hover state when route changes or ESC key pressed
  useEffect(() => {
    setIsHovered(false);
  }, [location.pathname]);

  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape' && !isPinned) {
        setIsHovered(false);
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [isPinned]);

  const userLabel = user?.full_name?.trim() || user?.email?.split('@')[0] || 'Lead Researcher';

  return (
    <>
      {/* ── 1. Edge-hover activation zone (0–24px from left edge) ── */}
      <div
        className="hidden lg:block fixed left-0 top-0 bottom-0 w-6 z-40 pointer-events-auto"
        onPointerEnter={() => setIsHovered(true)}
        aria-hidden="true"
      />

      {/* ── 2. Desktop Adaptive Rail ── */}
      <aside
        onPointerEnter={() => setIsHovered(true)}
        onPointerLeave={() => setIsHovered(false)}
        aria-label="Workstation Navigation"
        aria-expanded={isExpanded}
        className={`hidden lg:flex fixed left-0 top-0 bottom-0 z-50 flex-col bg-slate-950/95 backdrop-blur-2xl border-r border-indigo-500/15 shadow-[0_0_50px_rgba(0,0,20,0.8)] transition-all duration-300 ease-[cubic-bezier(0.16,1,0.3,1)] select-none text-slate-100 ${
          isExpanded ? 'w-64' : 'w-16'
        }`}
      >
        {/* Header / Brand */}
        <div className="flex items-center justify-between h-16 px-4 border-b border-white/[0.06] overflow-hidden shrink-0">
          <div className="flex items-center gap-3 min-w-0">
            <div className="flex h-9 w-9 shrink-0 items-center justify-center rounded-xl bg-gradient-to-br from-cyan-400 to-indigo-600 text-slate-950 shadow-lg shadow-cyan-500/25">
              <FlaskConical className="h-5 w-5 text-slate-950" />
            </div>
            <div
              className={`flex flex-col transition-all duration-200 ${
                isExpanded ? 'opacity-100 translate-x-0' : 'opacity-0 -translate-x-3 pointer-events-none'
              }`}
            >
              <span className="text-xs font-bold text-white tracking-wide truncate">
                Silicon Patient
              </span>
              <span className="text-[9px] font-mono uppercase tracking-widest text-cyan-400/90 truncate">
                {userLabel}
              </span>
            </div>
          </div>

          {/* Pin Button (Visible when expanded) */}
          <button
            onClick={togglePin}
            title={isPinned ? 'Unpin navigation' : 'Pin navigation expanded'}
            className={`p-1.5 rounded-lg text-slate-400 hover:text-white hover:bg-slate-800/80 transition-all cursor-pointer ${
              isExpanded ? 'opacity-100 scale-100' : 'opacity-0 scale-75 pointer-events-none'
            }`}
          >
            {isPinned ? (
              <PinOff className="w-3.5 h-3.5 text-cyan-400" />
            ) : (
              <Pin className="w-3.5 h-3.5 text-slate-500 hover:text-slate-300" />
            )}
          </button>
        </div>

        {/* Navigation Items */}
        <nav className="flex-1 py-4 space-y-6 overflow-y-auto overflow-x-hidden">
          {/* Section 1: Workstation */}
          <div>
            <div
              className={`px-4 text-[9px] font-mono font-bold tracking-[0.2em] text-indigo-400/60 uppercase transition-opacity duration-200 mb-2 ${
                isExpanded ? 'opacity-100' : 'opacity-0'
              }`}
            >
              Research Navigation
            </div>
            <div className="space-y-1 px-2">
              {WORKSTATION_ITEMS.map((item) => (
                <NavRailLink key={item.to} item={item} isExpanded={isExpanded} />
              ))}
            </div>
          </div>
        </nav>

        {/* Rail Footer */}
        <div className="h-12 border-t border-white/[0.06] px-4 flex items-center justify-between shrink-0 text-[10px] font-mono text-slate-500 overflow-hidden">
          <div className="flex items-center gap-2">
            <span className="w-2 h-2 rounded-full bg-emerald-400 alert-dot" />
            <span
              className={`transition-opacity duration-200 whitespace-nowrap text-[9.5px] ${
                isExpanded ? 'opacity-100' : 'opacity-0'
              }`}
            >
              Cortical Model Active
            </span>
          </div>
          {isExpanded && (
            <span className="text-[9px] text-slate-600 font-mono">v1.2.0</span>
          )}
        </div>
      </aside>

      {/* ── 3. Mobile Navigation Drawer Overlay ── */}
      {isMobileOpen && (
        <div className="fixed inset-0 z-50 flex lg:hidden bg-slate-950/80 backdrop-blur-md animate-fadeIn">
          <div className="w-72 h-full bg-slate-950 border-r border-slate-800 shadow-2xl flex flex-col relative">
            <button
              onClick={onCloseMobile}
              className="absolute top-4 right-4 p-1.5 rounded-xl bg-slate-900 border border-slate-800 text-slate-400 hover:text-white cursor-pointer"
            >
              <X className="w-5 h-5" />
            </button>

            {/* Brand */}
            <div className="p-5 border-b border-slate-800 flex items-center gap-3">
              <div className="flex h-10 w-10 items-center justify-center rounded-xl bg-gradient-to-br from-cyan-400 to-indigo-600 text-slate-950 shadow-lg shadow-cyan-500/20">
                <FlaskConical className="h-5 w-5 text-slate-950" />
              </div>
              <div>
                <p className="text-sm font-bold text-white tracking-wide">Silicon Patient</p>
                <p className="text-[10px] font-mono uppercase tracking-widest text-cyan-400">{userLabel}</p>
              </div>
            </div>

            {/* Mobile Nav Links */}
            <nav className="flex-1 p-4 space-y-6 overflow-y-auto">
              <div>
                <div className="px-2 text-[10px] font-mono font-bold tracking-wider text-slate-500 uppercase mb-2">
                  Research Navigation
                </div>
                <div className="space-y-1">
                  {WORKSTATION_ITEMS.map((item) => (
                    <NavRailLink key={item.to} item={item} isExpanded={true} onClick={onCloseMobile} />
                  ))}
                </div>
              </div>
            </nav>
          </div>
          <div className="flex-1" onClick={onCloseMobile} />
        </div>
      )}
    </>
  );
}

function NavRailLink({
  item,
  isExpanded,
  onClick,
}: {
  item: NavItem;
  isExpanded: boolean;
  onClick?: () => void;
}) {
  const Icon = item.icon;

  return (
    <NavLink
      to={item.to}
      end={item.to === '/app'}
      onClick={onClick}
      className={({ isActive }) =>
        `group relative flex items-center h-10 rounded-xl transition-all duration-200 ${
          isActive
            ? 'bg-gradient-to-r from-indigo-500/20 to-cyan-500/10 text-cyan-200 border-l-2 border-cyan-400 font-semibold shadow-[inset_0_0_12px_rgba(6,182,212,0.12)]'
            : 'text-slate-400 hover:text-slate-100 hover:bg-slate-900/60 border-l-2 border-transparent'
        }`
      }
    >
      {({ isActive }) => (
        <>
          <div className="w-12 h-10 flex items-center justify-center shrink-0">
            <Icon
              className={`w-4 h-4 transition-transform duration-200 group-hover:scale-110 ${
                isActive ? 'text-cyan-400 filter drop-shadow-[0_0_8px_rgba(34,211,238,0.5)]' : 'text-slate-400 group-hover:text-slate-200'
              }`}
            />
          </div>

          <span
            className={`text-xs whitespace-nowrap transition-all duration-200 ${
              isExpanded ? 'opacity-100 translate-x-0' : 'opacity-0 -translate-x-2 pointer-events-none'
            }`}
          >
            {item.label}
          </span>

          {isActive && isExpanded && (
            <ChevronRight className="w-3.5 h-3.5 text-cyan-400/80 ml-auto mr-3 shrink-0" />
          )}
        </>
      )}
    </NavLink>
  );
}
