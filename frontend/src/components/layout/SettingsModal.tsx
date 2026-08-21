import React from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { Check, Moon, Sun, X, Sliders, ShieldCheck, WifiOff, Wifi, RefreshCw, Circle } from 'lucide-react';
import { useTheme } from '../../context/ThemeContext';
import { useBackend } from '../../context/BackendContext';

interface SettingsModalProps {
  isOpen: boolean;
  onClose: () => void;
}

/* ── Mini theme preview canvas ── */
function DarkPreview() {
  return (
    <svg width="100%" height="52" viewBox="0 0 220 52" fill="none" aria-hidden="true">
      <rect width="220" height="52" fill="#0B0D10" rx="6" />
      {/* nav strip */}
      <rect x="0" y="0" width="220" height="10" fill="#111318" rx="6" />
      <circle cx="10" cy="5" r="2.5" fill="#30363D" />
      <rect x="20" y="3" width="28" height="4" rx="2" fill="#1E2330" />
      <rect x="52" y="3" width="20" height="4" rx="2" fill="#1E2330" />
      <rect x="76" y="3" width="22" height="4" rx="2" fill="#1E2330" />
      <rect x="186" y="2" width="14" height="6" rx="3" fill="#3B82F6" opacity="0.7" />
      {/* sidebar */}
      <rect x="0" y="10" width="36" height="42" fill="#0D1017" />
      <rect x="6" y="16" width="24" height="4" rx="2" fill="#1E2736" />
      <rect x="6" y="23" width="18" height="3" rx="1.5" fill="#1A1F2B" />
      <rect x="6" y="29" width="21" height="3" rx="1.5" fill="#1A1F2B" />
      <rect x="6" y="35" width="16" height="3" rx="1.5" fill="#1A1F2B" />
      {/* main panel */}
      <rect x="42" y="14" width="84" height="34" rx="4" fill="#12151C" />
      <rect x="48" y="19" width="40" height="3" rx="1.5" fill="#1E2736" />
      <rect x="48" y="25" width="72" height="2" rx="1" fill="#1A1F2B" />
      <rect x="48" y="29" width="60" height="2" rx="1" fill="#1A1F2B" />
      <rect x="48" y="35" width="28" height="6" rx="3" fill="#3B82F6" opacity="0.8" />
      {/* glow accent */}
      <rect x="132" y="14" width="82" height="34" rx="4" fill="#12151C" />
      <path d="M140 36 L160 24 L178 30 L196 20 L208 26" stroke="#6366F1" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
      <circle cx="160" cy="24" r="2" fill="#6366F1" />
      <circle cx="196" cy="20" r="2" fill="#A78BFA" />
      <rect x="140" y="38" width="18" height="2" rx="1" fill="#1E2736" />
      <rect x="162" y="38" width="14" height="2" rx="1" fill="#1E2736" />
    </svg>
  );
}

function BrightPreview() {
  return (
    <svg width="100%" height="52" viewBox="0 0 220 52" fill="none" aria-hidden="true">
      <rect width="220" height="52" fill="#F8FAFC" rx="6" />
      {/* nav strip */}
      <rect x="0" y="0" width="220" height="10" fill="#FFFFFF" rx="6" />
      <rect x="0" y="5" width="220" height="5" fill="#FFFFFF" />
      <circle cx="10" cy="5" r="2.5" fill="#E2E8F0" />
      <rect x="20" y="3" width="28" height="4" rx="2" fill="#EFF2F7" />
      <rect x="52" y="3" width="20" height="4" rx="2" fill="#EFF2F7" />
      <rect x="76" y="3" width="22" height="4" rx="2" fill="#4F46E5" opacity="0.8" />
      <rect x="186" y="2" width="14" height="6" rx="3" fill="#4F46E5" opacity="0.85" />
      <rect x="0" y="10" width="220" height="1" fill="#E2E8F0" />
      {/* sidebar */}
      <rect x="0" y="11" width="36" height="41" fill="#F1F5F9" />
      <rect x="6" y="16" width="24" height="4" rx="2" fill="#E2E8F0" />
      <rect x="6" y="23" width="18" height="3" rx="1.5" fill="#E2E8F0" />
      <rect x="6" y="29" width="21" height="3" rx="1.5" fill="#E2E8F0" />
      {/* main panel */}
      <rect x="42" y="14" width="84" height="34" rx="4" fill="#FFFFFF" />
      <rect x="42" y="14" width="84" height="34" rx="4" stroke="#E2E8F0" strokeWidth="1" />
      <rect x="48" y="19" width="40" height="3" rx="1.5" fill="#CBD5E1" />
      <rect x="48" y="25" width="72" height="2" rx="1" fill="#E2E8F0" />
      <rect x="48" y="29" width="60" height="2" rx="1" fill="#E2E8F0" />
      <rect x="48" y="35" width="28" height="6" rx="3" fill="#4F46E5" opacity="0.9" />
      {/* chart panel */}
      <rect x="132" y="14" width="82" height="34" rx="4" fill="#FFFFFF" />
      <rect x="132" y="14" width="82" height="34" rx="4" stroke="#E2E8F0" strokeWidth="1" />
      <path d="M140 36 L160 24 L178 30 L196 20 L208 26" stroke="#4F46E5" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
      <circle cx="160" cy="24" r="2" fill="#4F46E5" />
      <circle cx="196" cy="20" r="2" fill="#818CF8" />
    </svg>
  );
}

export function SettingsModal({ isOpen, onClose }: SettingsModalProps) {
  const { theme, setTheme } = useTheme();
  const { backendConnected, lastChecked, checking, recheckNow } = useBackend();

  const isDark = theme !== 'bright';

  return (
    <AnimatePresence>
      {isOpen && (
        <div className="fixed inset-0 z-[100] flex items-center justify-center p-4">
          {/* Backdrop */}
          <motion.div
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            exit={{ opacity: 0 }}
            transition={{ duration: 0.18 }}
            onClick={onClose}
            className="absolute inset-0 bg-black/75 backdrop-blur-sm"
          />

          {/* Modal Card */}
          <motion.div
            initial={{ opacity: 0, scale: 0.96, y: 16 }}
            animate={{ opacity: 1, scale: 1, y: 0 }}
            exit={{ opacity: 0, scale: 0.96, y: 8 }}
            transition={{ type: 'spring', damping: 28, stiffness: 380 }}
            style={{
              position: 'relative',
              width: '100%',
              maxWidth: 480,
              borderRadius: 16,
              border: isDark ? '1px solid #21262D' : '1px solid #E2E8F0',
              background: isDark ? '#0D1117' : '#FFFFFF',
              color: isDark ? '#E6EDF3' : '#0F172A',
              boxShadow: isDark
                ? '0 24px 64px rgba(0,0,0,0.7), 0 0 0 1px rgba(255,255,255,0.04) inset'
                : '0 24px 64px rgba(0,0,0,0.14), 0 1px 0 rgba(255,255,255,0.8) inset',
              fontFamily: 'Inter, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif',
              overflow: 'hidden',
            }}
          >
            {/* ── Top accent line ── */}
            <div style={{
              position: 'absolute', top: 0, left: 0, right: 0, height: 2,
              background: backendConnected
                ? 'linear-gradient(90deg, #22C55E 0%, #3B82F6 50%, #8B5CF6 100%)'
                : 'linear-gradient(90deg, #F85149 0%, #FB923C 100%)',
              opacity: 0.9,
            }} />

            {/* ── Header ── */}
            <div style={{
              display: 'flex', alignItems: 'center', justifyContent: 'space-between',
              padding: '20px 24px 16px',
              borderBottom: isDark ? '1px solid #21262D' : '1px solid #F1F5F9',
            }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
                {/* Icon badge */}
                <div style={{
                  width: 36, height: 36, borderRadius: 10, display: 'flex',
                  alignItems: 'center', justifyContent: 'center', flexShrink: 0,
                  background: isDark ? '#161B22' : '#F8FAFC',
                  border: isDark ? '1px solid #30363D' : '1px solid #E2E8F0',
                }}>
                  <Sliders style={{ width: 16, height: 16, color: isDark ? '#8B949E' : '#64748B' }} />
                </div>
                <div>
                  <div style={{ fontSize: 15, fontWeight: 700, letterSpacing: '-0.02em', lineHeight: 1.2 }}>
                    Workstation Preferences
                  </div>
                  <div style={{ fontSize: 11, color: isDark ? '#6B7280' : '#94A3B8', marginTop: 2 }}>
                    Theme · display · connection
                  </div>
                </div>
              </div>

              {/* Close */}
              <button
                onClick={onClose}
                title="Close"
                style={{
                  width: 28, height: 28, borderRadius: 8, border: 'none', cursor: 'pointer',
                  background: 'transparent', display: 'flex', alignItems: 'center', justifyContent: 'center',
                  color: isDark ? '#6B7280' : '#94A3B8', transition: 'color 0.1s, background 0.1s',
                }}
                onMouseEnter={e => { e.currentTarget.style.color = isDark ? '#E6EDF3' : '#0F172A'; e.currentTarget.style.background = isDark ? '#21262D' : '#F1F5F9'; }}
                onMouseLeave={e => { e.currentTarget.style.color = isDark ? '#6B7280' : '#94A3B8'; e.currentTarget.style.background = 'transparent'; }}
              >
                <X style={{ width: 15, height: 15 }} />
              </button>
            </div>

            {/* ── Body ── */}
            <div style={{ padding: '20px 24px', display: 'flex', flexDirection: 'column', gap: 20 }}>

              {/* Section: Theme */}
              <div>
                <div style={{
                  display: 'flex', alignItems: 'center', justifyContent: 'space-between',
                  marginBottom: 12,
                }}>
                  <span style={{
                    fontSize: 10, fontWeight: 700, letterSpacing: '0.08em',
                    textTransform: 'uppercase', color: isDark ? '#6B7280' : '#94A3B8',
                  }}>
                    Interface Theme
                  </span>
                  <span style={{
                    fontSize: 10, fontFamily: 'JetBrains Mono, monospace',
                    padding: '2px 8px', borderRadius: 4,
                    background: isDark ? '#161B22' : '#F1F5F9',
                    border: isDark ? '1px solid #30363D' : '1px solid #E2E8F0',
                    color: isDark ? '#8B949E' : '#64748B',
                  }}>
                    {theme === 'bright' ? 'CLINICAL_BRIGHT' : 'OBSIDIAN_DARK'}
                  </span>
                </div>

                {/* Theme cards */}
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10 }}>
                  {/* Dark card */}
                  <button
                    type="button"
                    onClick={() => setTheme('dark')}
                    style={{
                      position: 'relative', textAlign: 'left', cursor: 'pointer',
                      padding: 0, borderRadius: 10, overflow: 'hidden',
                      border: isDark && theme === 'dark'
                        ? '1.5px solid #3B82F6'
                        : isDark ? '1px solid #21262D' : '1px solid #E2E8F0',
                      background: isDark ? '#0D1117' : '#0B0D10',
                      boxShadow: theme === 'dark' ? '0 0 0 3px rgba(59,130,246,0.18)' : 'none',
                      transition: 'border-color 0.15s, box-shadow 0.15s',
                    }}
                    onMouseEnter={e => { if (theme !== 'dark') e.currentTarget.style.borderColor = isDark ? '#30363D' : '#475569'; }}
                    onMouseLeave={e => { if (theme !== 'dark') e.currentTarget.style.borderColor = isDark ? '#21262D' : '#E2E8F0'; }}
                  >
                    {/* Preview canvas */}
                    <div style={{ width: '100%', overflow: 'hidden', lineHeight: 0 }}>
                      <DarkPreview />
                    </div>
                    {/* Label row */}
                    <div style={{
                      display: 'flex', alignItems: 'center', justifyContent: 'space-between',
                      padding: '8px 10px',
                      background: isDark ? '#0D1117' : '#111318',
                      borderTop: '1px solid #21262D',
                    }}>
                      <div>
                        <div style={{ fontSize: 11.5, fontWeight: 700, color: '#E6EDF3', lineHeight: 1 }}>Obsidian Dark</div>
                        <div style={{ fontSize: 9.5, color: '#6B7280', marginTop: 2 }}>Deep cosmic · neon glows</div>
                      </div>
                      {theme === 'dark' && (
                        <div style={{
                          width: 18, height: 18, borderRadius: '50%', background: '#3B82F6',
                          display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0,
                        }}>
                          <Check style={{ width: 10, height: 10, color: '#fff', strokeWidth: 3 }} />
                        </div>
                      )}
                    </div>
                  </button>

                  {/* Bright card */}
                  <button
                    type="button"
                    onClick={() => setTheme('bright')}
                    style={{
                      position: 'relative', textAlign: 'left', cursor: 'pointer',
                      padding: 0, borderRadius: 10, overflow: 'hidden',
                      border: theme === 'bright'
                        ? '1.5px solid #4F46E5'
                        : isDark ? '1px solid #21262D' : '1px solid #E2E8F0',
                      background: '#F8FAFC',
                      boxShadow: theme === 'bright' ? '0 0 0 3px rgba(79,70,229,0.15)' : 'none',
                      transition: 'border-color 0.15s, box-shadow 0.15s',
                    }}
                    onMouseEnter={e => { if (theme !== 'bright') e.currentTarget.style.borderColor = '#CBD5E1'; }}
                    onMouseLeave={e => { if (theme !== 'bright') e.currentTarget.style.borderColor = isDark ? '#21262D' : '#E2E8F0'; }}
                  >
                    <div style={{ width: '100%', overflow: 'hidden', lineHeight: 0 }}>
                      <BrightPreview />
                    </div>
                    <div style={{
                      display: 'flex', alignItems: 'center', justifyContent: 'space-between',
                      padding: '8px 10px',
                      background: '#F8FAFC',
                      borderTop: '1px solid #E2E8F0',
                    }}>
                      <div>
                        <div style={{ fontSize: 11.5, fontWeight: 700, color: '#0F172A', lineHeight: 1 }}>Clinical Bright</div>
                        <div style={{ fontSize: 9.5, color: '#94A3B8', marginTop: 2 }}>Lab light · high contrast</div>
                      </div>
                      {theme === 'bright' && (
                        <div style={{
                          width: 18, height: 18, borderRadius: '50%', background: '#4F46E5',
                          display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0,
                        }}>
                          <Check style={{ width: 10, height: 10, color: '#fff', strokeWidth: 3 }} />
                        </div>
                      )}
                    </div>
                  </button>
                </div>
              </div>

              {/* Section: Connection */}
              <div>
                <span style={{
                  display: 'block', fontSize: 10, fontWeight: 700, letterSpacing: '0.08em',
                  textTransform: 'uppercase', color: isDark ? '#6B7280' : '#94A3B8',
                  marginBottom: 10,
                }}>
                  API Connection
                </span>

                <div style={{
                  borderRadius: 10, overflow: 'hidden',
                  border: backendConnected
                    ? isDark ? '1px solid #1A3A2A' : '1px solid #BBF7D0'
                    : isDark ? '1px solid #3A1A1A' : '1px solid #FECACA',
                }}>
                  {/* Status bar */}
                  <div style={{
                    display: 'flex', alignItems: 'center', justifyContent: 'space-between',
                    padding: '10px 14px',
                    background: backendConnected
                      ? isDark ? '#0D1F18' : '#F0FDF4'
                      : isDark ? '#1F0D0D' : '#FFF5F5',
                  }}>
                    <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
                      {/* Live dot */}
                      <div style={{ position: 'relative', width: 10, height: 10, flexShrink: 0 }}>
                        <span style={{
                          position: 'absolute', inset: 0, borderRadius: '50%',
                          background: backendConnected ? '#22C55E' : '#F85149',
                          opacity: 0.3,
                          animation: 'alert-dot 2s ease-in-out infinite',
                        }} />
                        <span style={{
                          position: 'absolute', inset: '20%', borderRadius: '50%',
                          background: backendConnected ? '#22C55E' : '#F85149',
                        }} />
                      </div>

                      <div>
                        <div style={{
                          fontSize: 12, fontWeight: 700,
                          color: backendConnected
                            ? isDark ? '#4ADE80' : '#16A34A'
                            : isDark ? '#F87171' : '#DC2626',
                        }}>
                          {backendConnected ? 'Backend Connected' : 'Backend Disconnected'}
                        </div>
                        {!backendConnected && (
                          <div style={{
                            fontSize: 10, marginTop: 1,
                            color: isDark ? '#9B4848' : '#DC2626',
                          }}>
                            Cannot reach API server — data won't load
                          </div>
                        )}
                      </div>
                    </div>

                    <button
                      onClick={recheckNow}
                      disabled={checking}
                      title="Re-check connection now"
                      style={{
                        display: 'flex', alignItems: 'center', gap: 5,
                        padding: '4px 10px', borderRadius: 6, cursor: checking ? 'default' : 'pointer',
                        border: isDark ? '1px solid #30363D' : '1px solid #E2E8F0',
                        background: isDark ? '#161B22' : '#FFFFFF',
                        color: isDark ? '#8B949E' : '#64748B',
                        fontSize: 10, fontWeight: 600,
                        opacity: checking ? 0.6 : 1,
                        transition: 'opacity 0.15s',
                      }}
                    >
                      <RefreshCw style={{ width: 10, height: 10, animation: checking ? 'spin 1s linear infinite' : 'none' }} />
                      {checking ? 'Checking…' : 'Re-check'}
                    </button>
                  </div>

                  {/* Terminal row */}
                  <div style={{
                    padding: '8px 14px',
                    background: isDark ? '#0B0D10' : '#F8FAFC',
                    borderTop: isDark ? '1px solid #21262D' : '1px solid #E2E8F0',
                    display: 'flex', alignItems: 'center', gap: 8,
                  }}>
                    <span style={{
                      fontFamily: 'JetBrains Mono, monospace', fontSize: 10,
                      color: isDark ? '#3FB950' : '#16A34A', flexShrink: 0,
                    }}>›</span>
                    <span style={{
                      fontFamily: 'JetBrains Mono, monospace', fontSize: 10,
                      color: isDark ? '#6B7280' : '#94A3B8',
                    }}>
                      {backendConnected ? 'GET /health' : 'GET /health'}{' '}
                      <span style={{ color: backendConnected ? (isDark ? '#3FB950' : '#16A34A') : (isDark ? '#F85149' : '#DC2626'), fontWeight: 700 }}>
                        {backendConnected ? '200 OK' : 'ERR_CONN'}
                      </span>
                    </span>
                    <span style={{ marginLeft: 'auto', fontFamily: 'JetBrains Mono, monospace', fontSize: 9, color: isDark ? '#424956' : '#CBD5E1' }}>
                      {lastChecked ? lastChecked.toLocaleTimeString() : '—'}
                    </span>
                  </div>
                </div>
              </div>

              {/* Section: Persistence */}
              <div style={{
                display: 'flex', alignItems: 'center', gap: 8,
                padding: '8px 12px', borderRadius: 8,
                background: isDark ? '#0D1117' : '#F8FAFC',
                border: isDark ? '1px solid #21262D' : '1px solid #F1F5F9',
              }}>
                <ShieldCheck style={{ width: 13, height: 13, color: '#22C55E', flexShrink: 0 }} />
                <span style={{ fontSize: 11, color: isDark ? '#6B7280' : '#94A3B8' }}>
                  Theme preference auto-saved to local storage
                </span>
              </div>
            </div>

            {/* ── Footer ── */}
            <div style={{
              display: 'flex', alignItems: 'center', justifyContent: 'flex-end',
              padding: '14px 24px 20px',
              borderTop: isDark ? '1px solid #21262D' : '1px solid #F1F5F9',
            }}>
              <button
                type="button"
                onClick={onClose}
                style={{
                  padding: '7px 22px', borderRadius: 8, fontWeight: 700, fontSize: 12,
                  cursor: 'pointer', border: 'none',
                  background: isDark ? '#238636' : '#4F46E5',
                  color: '#fff',
                  boxShadow: isDark ? '0 0 18px rgba(35,134,54,0.35)' : '0 2px 12px rgba(79,70,229,0.3)',
                  transition: 'opacity 0.12s, box-shadow 0.12s',
                }}
                onMouseEnter={e => { e.currentTarget.style.opacity = '0.88'; }}
                onMouseLeave={e => { e.currentTarget.style.opacity = '1'; }}
              >
                Done
              </button>
            </div>
          </motion.div>
        </div>
      )}
    </AnimatePresence>
  );
}
