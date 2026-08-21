import React, { useState } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { Activity, ArrowRight, Menu, Settings, X, Sun, Moon } from 'lucide-react';
import { Link, NavLink } from 'react-router-dom';
import { useTheme } from '../../context/ThemeContext';
import { SettingsModal } from '../layout/SettingsModal';

export function Navbar() {
  const [mobileMenuOpen, setMobileMenuOpen] = useState(false);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const { theme, toggleTheme } = useTheme();

  const navLinks = [
    { label: 'Solutions', to: '/' },
    { label: 'Features', to: '/features' },
    { label: 'How It Works', to: '/how-it-works' },
    { label: 'Showcase', to: '/showcase' },
  ];

  return (
    <>
      <header className="fixed top-4 left-0 right-0 z-50 px-4 sm:px-6 lg:px-8 max-w-7xl mx-auto pointer-events-none">
        <div
          className={`pointer-events-auto relative flex items-center justify-between h-14 px-6 rounded-full border backdrop-blur-2xl shadow-2xl transition-all duration-300 ${
            theme === 'bright'
              ? 'bg-white/90 border-slate-200 text-slate-900 shadow-slate-200/80'
              : 'bg-slate-950/85 border-white/10 text-white'
          }`}
        >
          {/* Brand Logo */}
          <Link to="/" className="flex items-center gap-2.5 group shrink-0">
            <div
              className={`relative flex items-center justify-center w-8 h-8 rounded-full border shadow-sm group-hover:scale-105 transition-all ${
                theme === 'bright'
                  ? 'bg-blue-100 border-blue-300 text-blue-600'
                  : 'bg-blue-600/30 border-blue-400/40 text-blue-300 shadow-[0_0_12px_rgba(59,130,246,0.4)]'
              }`}
            >
              <Activity className="w-4 h-4 group-hover:rotate-12 transition-transform" />
            </div>
            <span className="text-sm font-bold tracking-tight font-sans">
              Silicon<span className={theme === 'bright' ? 'text-blue-600' : 'text-blue-400'}>Patient</span>
            </span>
          </Link>

          {/* Desktop Nav Links */}
          <nav
            className={`hidden md:flex items-center gap-1.5 px-4 py-1.5 rounded-full border ${
              theme === 'bright'
                ? 'bg-slate-100 border-slate-200'
                : 'bg-white/[0.03] border-white/[0.06]'
            }`}
          >
            {navLinks.map((link) => (
              <NavLink
                key={link.label}
                to={link.to}
                end={link.to === '/'}
                className={({ isActive }) =>
                  `text-xs font-medium transition-all px-3.5 py-1.5 rounded-full font-sans ${
                    isActive
                      ? theme === 'bright'
                        ? 'text-white bg-blue-600 font-semibold shadow-sm'
                        : 'text-white bg-blue-600/35 border border-blue-400/40 shadow-[0_0_12px_rgba(59,130,246,0.35)] font-semibold'
                      : theme === 'bright'
                      ? 'text-slate-600 hover:text-slate-900 hover:bg-slate-200/70'
                      : 'text-slate-300 hover:text-white hover:bg-white/[0.06]'
                  }`
                }
              >
                {link.label}
              </NavLink>
            ))}
          </nav>

          {/* Desktop CTA Action Buttons & Settings Toggle */}
          <div className="hidden md:flex items-center gap-2.5 shrink-0">
            {/* Quick Settings Icon Button */}
            <button
              onClick={() => setSettingsOpen(true)}
              className={`p-2 rounded-full border transition-colors cursor-pointer ${
                theme === 'bright'
                  ? 'text-slate-600 hover:text-slate-900 hover:bg-slate-100 border-slate-200'
                  : 'text-slate-400 hover:text-white hover:bg-white/5 border-white/5'
              }`}
              title="Interface Theme Preferences"
            >
              <Settings className="w-4 h-4" />
            </button>

            <Link
              to="/login"
              className={`text-xs font-semibold px-3 py-1.5 transition-colors ${
                theme === 'bright' ? 'text-slate-700 hover:text-slate-900' : 'text-slate-300 hover:text-white'
              }`}
            >
              Log in
            </Link>
            <Link
              to="/app"
              className="flex items-center gap-1.5 px-4 py-2 rounded-full bg-blue-600 hover:bg-blue-500 text-white font-bold text-xs shadow-[0_0_15px_rgba(37,99,235,0.4)] transition-all hover:scale-105"
            >
              <span>Launch App</span>
              <ArrowRight className="w-3 h-3" />
            </Link>
          </div>

          {/* Mobile Hamburger Toggle */}
          <button
            onClick={() => setMobileMenuOpen(!mobileMenuOpen)}
            className={`md:hidden p-2 rounded-full border transition-colors cursor-pointer ${
              theme === 'bright'
                ? 'text-slate-700 hover:text-slate-900 bg-slate-100 border-slate-200'
                : 'text-slate-400 hover:text-white bg-white/[0.05] border-white/10'
            }`}
            aria-label="Toggle Navigation Menu"
          >
            {mobileMenuOpen ? <X className="w-4 h-4" /> : <Menu className="w-4 h-4" />}
          </button>
        </div>

        {/* Mobile Drawer Menu */}
        <AnimatePresence>
          {mobileMenuOpen && (
            <motion.div
              initial={{ opacity: 0, scale: 0.95, y: -10 }}
              animate={{ opacity: 1, scale: 1, y: 8 }}
              exit={{ opacity: 0, scale: 0.95, y: -10 }}
              className={`md:hidden pointer-events-auto rounded-3xl p-5 mt-2 border shadow-2xl backdrop-blur-2xl overflow-hidden ${
                theme === 'bright'
                  ? 'bg-white/95 border-slate-200 text-slate-900 shadow-slate-300'
                  : 'bg-slate-950/95 border-white/15 text-white'
              }`}
            >
              <nav className="flex flex-col space-y-2">
                {navLinks.map((link) => (
                  <NavLink
                    key={link.label}
                    to={link.to}
                    end={link.to === '/'}
                    onClick={() => setMobileMenuOpen(false)}
                    className={({ isActive }) =>
                      `text-sm font-medium py-2 px-4 rounded-xl transition-colors ${
                        isActive
                          ? 'text-white bg-blue-600 font-bold'
                          : theme === 'bright'
                          ? 'text-slate-700 hover:text-slate-900 hover:bg-slate-100'
                          : 'text-slate-300 hover:text-white hover:bg-white/5'
                      }`
                    }
                  >
                    {link.label}
                  </NavLink>
                ))}
              </nav>

              <div className="pt-4 mt-3 border-t border-inherit flex flex-col gap-2.5">
                <button
                  onClick={() => {
                    setMobileMenuOpen(false);
                    setSettingsOpen(true);
                  }}
                  className={`flex items-center justify-between text-xs font-semibold py-2.5 px-4 rounded-xl border ${
                    theme === 'bright'
                      ? 'bg-slate-50 border-slate-200 text-slate-700'
                      : 'bg-white/5 border-white/10 text-slate-300'
                  }`}
                >
                  <span className="flex items-center gap-2">
                    <Settings className="w-4 h-4" />
                    <span>Display Theme ({theme === 'bright' ? 'Bright' : 'Dark'})</span>
                  </span>
                  <span>Change</span>
                </button>

                <Link
                  to="/login"
                  onClick={() => setMobileMenuOpen(false)}
                  className={`text-xs font-semibold text-center py-2 rounded-xl border transition-colors ${
                    theme === 'bright'
                      ? 'border-slate-200 text-slate-800 hover:bg-slate-50'
                      : 'border-white/10 text-slate-300 hover:text-white hover:bg-white/5'
                  }`}
                >
                  Log in
                </Link>

                <Link
                  to="/app"
                  onClick={() => setMobileMenuOpen(false)}
                  className="flex items-center justify-center gap-2 py-2.5 rounded-xl bg-blue-600 text-white font-bold text-xs shadow-lg shadow-blue-500/25"
                >
                  <span>Launch App</span>
                  <ArrowRight className="w-3.5 h-3.5" />
                </Link>
              </div>
            </motion.div>
          )}
        </AnimatePresence>
      </header>

      {/* Settings Modal */}
      <SettingsModal isOpen={settingsOpen} onClose={() => setSettingsOpen(false)} />
    </>
  );
}
