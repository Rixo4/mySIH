import { Beaker, History, LayoutDashboard } from 'lucide-react';
import { NavLink } from 'react-router-dom';

const navItems = [
  { to: '/app', label: 'Home', icon: LayoutDashboard },
  { to: '/app/dose-eval', label: 'Drug', icon: Beaker },
  { to: '/app/history', label: 'Runs', icon: History }
];

export function MobileNav() {
  return (
    <nav className="grid grid-cols-5 gap-1 border-t border-white/10 bg-[rgba(4,10,22,0.97)] px-2 py-2 backdrop-blur-xl xl:hidden">
      {navItems.map((item) => {
        const Icon = item.icon;
        return (
          <NavLink
            key={item.to}
            to={item.to}
            end={item.to === '/app'}
            className={({ isActive }) =>
              `flex flex-col items-center gap-1 rounded-2xl px-1 py-2 text-[11px] font-medium transition ${
                isActive ? 'bg-cyan-500/15 text-cyan-100' : 'text-slate-400'
              }`
            }
          >
            <Icon className="h-4 w-4" />
            {item.label}
          </NavLink>
        );
      })}
    </nav>
  );
}
