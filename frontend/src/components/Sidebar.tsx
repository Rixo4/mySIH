import { Activity, Beaker, FlaskConical, LayoutDashboard, History, Microscope } from 'lucide-react';
import { NavLink } from 'react-router-dom';

const navItems = [
  { to: '/', label: 'Dashboard', icon: LayoutDashboard },
  { to: '/dose-eval', label: 'Drug Evaluation', icon: Beaker },
  { to: '/simulation', label: 'Simulation', icon: Activity },
  { to: '/validation', label: 'Validation', icon: Microscope },
  { to: '/history', label: 'Run History', icon: History }
];

export function Sidebar() {
  return (
    <aside className="flex h-full w-full flex-col border-r border-white/10 bg-[rgba(4,10,22,0.94)] backdrop-blur-xl xl:w-72">
      <div className="border-b border-white/10 px-5 py-6 xl:px-6">
        <div className="flex items-center gap-3">
          <div className="flex h-11 w-11 items-center justify-center rounded-2xl bg-gradient-to-br from-cyan-400 to-emerald-400 text-midnight-950 shadow-glow">
            <FlaskConical className="h-6 w-6" />
          </div>
          <div>
            <p className="text-sm font-semibold text-white">Silicon Patient</p>
            <p className="text-xs uppercase tracking-[0.32em] text-slate-400">Pharma AI Console</p>
          </div>
        </div>
      </div>

      <nav className="flex-1 px-3 py-4 xl:px-4">
        <div className="space-y-1">
          {navItems.map((item) => {
            const Icon = item.icon;
            return (
              <NavLink
                key={item.to}
                to={item.to}
                end={item.to === '/'}
                className={({ isActive }) =>
                  `flex items-center gap-3 rounded-2xl px-4 py-3 text-sm font-medium transition ${
                    isActive
                      ? 'bg-cyan-500/15 text-cyan-100 ring-1 ring-cyan-400/20'
                      : 'text-slate-300 hover:bg-white/5 hover:text-white'
                  }`
                }
              >
                <Icon className="h-4 w-4" />
                {item.label}
              </NavLink>
            );
          })}
        </div>
      </nav>

      <div className="border-t border-white/10 px-5 py-4 text-xs text-slate-400 xl:px-6">
        GPU-accelerated neural drug evaluation engine
      </div>
    </aside>
  );
}
