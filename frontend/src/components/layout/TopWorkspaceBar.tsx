/**
 * TopWorkspaceBar.tsx — Global Navigation Taskbar.
 * Professional scientific/medical instrumentation aesthetic (Bloomberg Terminal / Simulink / Epic Clinical).
 * High information density, restrained color usage, clean grotesk typography.
 */

import React, { useState } from 'react';
import { NavLink, Link, useLocation, useNavigate } from 'react-router-dom';
import {
  Search,
  Settings,
  LogOut,
  HelpCircle,
  LayoutDashboard,
  Sliders,
  History,
  GitCompare,
  Activity,
} from 'lucide-react';
import { useAuth } from '../../context/AuthContext';
import { useBackend } from '../../context/BackendContext';
import { SettingsModal } from './SettingsModal';

interface TopWorkspaceBarProps {
  pageTitle?: string;
  backendConnected?: boolean;
  engineOnline?: boolean;
  onOpenCommandPalette?: () => void;
  onOpenActiveJobs?: () => void;
  onToggleMobileMenu?: () => void;
}

const PAGE_NAV = [
  { to: '/app',           label: 'Overview',    icon: LayoutDashboard, end: true  },
  { to: '/app/dose-eval', label: 'Dose Lab',    icon: Sliders,         end: false },
  { to: '/app/history',   label: 'Run History', icon: History,         end: false },
  { to: '/app/compare',   label: 'Compare',     icon: GitCompare,      end: false },
] as const;

export function TopWorkspaceBar({ onOpenCommandPalette }: TopWorkspaceBarProps) {
  const { user, logout } = useAuth();
  const { backendConnected } = useBackend();
  const location         = useLocation();
  const navigate         = useNavigate();
  const [settingsOpen, setSettingsOpen] = useState(false);

  const isDashboard = location.pathname === '/app';

  const initials = user?.full_name
    ? user.full_name.split(' ').map((n) => n[0]).join('').slice(0, 2).toUpperCase()
    : 'LR';

  async function handleLogout() {
    await logout();
    navigate('/', { replace: true });
  }

  const iconBtnStyle: React.CSSProperties = {
    display: 'flex', alignItems: 'center', justifyContent: 'center',
    width: 26, height: 26, borderRadius: 3,
    background: 'transparent', border: '1px solid transparent',
    cursor: 'pointer', color: '#8B949E',
    transition: 'color 0.12s, background 0.12s, border-color 0.12s',
  };

  return (
    <>
      <header
        style={{
          position: 'sticky', top: 0, zIndex: 50,
          height: 46,
          display: 'flex', alignItems: 'center',
          padding: '0 14px', gap: 0,
          background: '#0B0D10',
          borderBottom: '1px solid #1E2228',
          flexShrink: 0,
          fontFamily: 'Inter, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif',
        }}
      >
        {/* ── Left: Brand & Station Identity ── */}
        <Link
          to="/app"
          style={{
            display: 'flex', alignItems: 'center', gap: 9,
            textDecoration: 'none', flexShrink: 0, marginRight: 18,
          }}
        >
          {/* Flat Scientific Instrumentation Glyph Badge */}
          <div
            style={{
              width: 24, height: 24, borderRadius: 3,
              background: '#161B22',
              border: '1px solid #30363D',
              display: 'flex', alignItems: 'center', justifyContent: 'center',
              flexShrink: 0,
            }}
          >
            <Activity style={{ width: 13, height: 13, color: '#58A6FF' }} />
          </div>

          <div style={{ display: 'flex', flexDirection: 'column', justifyContent: 'center' }}>
            <span
              style={{
                fontSize: 12, fontWeight: 700,
                letterSpacing: '0.05em', color: '#E6EDF3', textTransform: 'uppercase',
                lineHeight: 1.1,
              }}
            >
              SILICON PATIENT
            </span>
            <span
              style={{
                fontSize: 8.5, fontWeight: 600,
                letterSpacing: '0.08em', color: '#7D8590', textTransform: 'uppercase',
                lineHeight: 1.1, marginTop: 1.5,
              }}
            >
              NEUROPHARMACOLOGY WORKSTATION · V2.4
            </span>
          </div>
        </Link>

        {/* Vertical Divider */}
        <div style={{ width: 1, height: 18, background: '#1E2228', flexShrink: 0, marginRight: 14 }} />

        {/* ── Center: Instrumentation Navigation Tabs ── */}
        <nav style={{ display: 'flex', alignItems: 'center', gap: 3, flex: 1 }}>
          {PAGE_NAV.map((page) => {
            const Icon = page.icon;
            return (
              <NavLink
                key={page.to}
                to={page.to}
                end={page.end}
                style={({ isActive }) => ({
                  display: 'flex', alignItems: 'center', gap: 6,
                  padding: '4.5px 11px', borderRadius: 3,
                  textDecoration: 'none', whiteSpace: 'nowrap',
                  fontSize: 11.5, fontWeight: isActive ? 600 : 450,
                  color: isActive ? '#F0F6FC' : '#8B949E',
                  background: isActive ? '#161B22' : 'transparent',
                  border: isActive ? '1px solid #30363D' : '1px solid transparent',
                  borderBottom: isActive ? '2px solid #58A6FF' : '2px solid transparent',
                  transition: 'all 0.12s',
                })}
              >
                <Icon style={{ width: 13, height: 13, flexShrink: 0, opacity: 0.85 }} />
                {page.label}
              </NavLink>
            );
          })}
        </nav>

        {/* ── Right: Search · Status Telemetry · User Controls ── */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexShrink: 0 }}>

          {/* Search Trigger */}
          <button
            onClick={onOpenCommandPalette}
            style={{
              display: 'flex', alignItems: 'center', gap: 9,
              padding: '3.5px 9px', borderRadius: 3,
              background: '#0D1117', border: '1px solid #21262D',
              cursor: 'pointer', fontSize: 11, color: '#8B949E',
            }}
          >
            <Search style={{ width: 11, height: 11, color: '#7D8590' }} />
            <span>Search protocols, assays...</span>
            <kbd style={{ fontFamily: 'JetBrains Mono, monospace', fontSize: 9, padding: '1px 4px', borderRadius: 2, background: '#161B22', border: '1px solid #30363D', color: '#7D8590' }}>
              Ctrl k
            </kbd>
          </button>

          {/* Simulation status pill: shown when real job is running */}
          {isDashboard && false && (
            <div style={{ display: 'flex', alignItems: 'center', gap: 5, padding: '3px 8px', borderRadius: 3, background: 'rgba(59,185,80,0.12)', border: '1px solid rgba(46,160,67,0.35)', fontFamily: 'JetBrains Mono, monospace', fontSize: 10, fontWeight: 600, color: '#3FB950', whiteSpace: 'nowrap' }}>
              <span className="live-dot" style={{ width: 5, height: 5, background: '#3FB950' }} />
              SIM RUNNING · 00:02:48
            </div>
          )}

          {/* Backend Connection Indicator */}
          <div
            title={backendConnected ? 'ODE Engine Online' : 'Backend Disconnected'}
            style={{
              display: 'flex', alignItems: 'center', gap: 4.5,
              padding: '2.5px 7px', borderRadius: 3,
              background: '#161B22', border: '1px solid #21262D',
              fontSize: 10, fontFamily: 'JetBrains Mono, monospace',
              color: backendConnected ? '#7EE787' : '#F85149',
            }}
          >
            <span
              style={{
                width: 5, height: 5, borderRadius: '50%',
                background: backendConnected ? '#3FB950' : '#F85149',
              }}
            />
            <span style={{ fontSize: 9.5 }}>{backendConnected ? 'ONLINE' : 'OFFLINE'}</span>
          </div>

          {/* Settings */}
          <button
            onClick={() => setSettingsOpen(true)}
            title="Settings"
            style={iconBtnStyle}
            onMouseEnter={(e) => { e.currentTarget.style.color = '#F0F6FC'; e.currentTarget.style.background = '#161B22'; e.currentTarget.style.borderColor = '#30363D'; }}
            onMouseLeave={(e) => { e.currentTarget.style.color = '#8B949E'; e.currentTarget.style.background = 'transparent'; e.currentTarget.style.borderColor = 'transparent'; }}
          >
            <Settings style={{ width: 13, height: 13 }} />
          </button>

          {/* Help */}
          <button
            title="Documentation / Help"
            style={iconBtnStyle}
            onMouseEnter={(e) => { e.currentTarget.style.color = '#F0F6FC'; e.currentTarget.style.background = '#161B22'; e.currentTarget.style.borderColor = '#30363D'; }}
            onMouseLeave={(e) => { e.currentTarget.style.color = '#8B949E'; e.currentTarget.style.background = 'transparent'; e.currentTarget.style.borderColor = 'transparent'; }}
          >
            <HelpCircle style={{ width: 13, height: 13 }} />
          </button>

          {/* Vertical Divider */}
          <div style={{ width: 1, height: 16, background: '#1E2228' }} />

          {/* User Initials Avatar Badge */}
          <div
            title={user?.email ?? ''}
            style={{
              width: 24, height: 24, borderRadius: 3,
              background: '#161B22', border: '1px solid #30363D',
              display: 'flex', alignItems: 'center', justifyContent: 'center',
              fontSize: 10, fontWeight: 700, color: '#C9D1D9',
              flexShrink: 0, cursor: 'default',
            }}
          >
            {initials}
          </div>

          {/* Logout */}
          <button
            onClick={handleLogout}
            title="Logout"
            style={iconBtnStyle}
            onMouseEnter={(e) => { e.currentTarget.style.color = '#F85149'; e.currentTarget.style.background = '#161B22'; e.currentTarget.style.borderColor = '#30363D'; }}
            onMouseLeave={(e) => { e.currentTarget.style.color = '#8B949E'; e.currentTarget.style.background = 'transparent'; e.currentTarget.style.borderColor = 'transparent'; }}
          >
            <LogOut style={{ width: 12, height: 12 }} />
          </button>
        </div>
      </header>

      <SettingsModal isOpen={settingsOpen} onClose={() => setSettingsOpen(false)} />
    </>
  );
}
