import React, { createContext, useContext, useEffect, useState, type ReactNode } from 'react';
import { getHealth } from '../api/client';

interface BackendContextValue {
  backendConnected: boolean;
  lastChecked: Date | null;
  checking: boolean;
  recheckNow: () => void;
}

const BackendContext = createContext<BackendContextValue | undefined>(undefined);

const POLL_INTERVAL_MS = 15_000; // check every 15 seconds

export function BackendProvider({ children }: { children: ReactNode }) {
  const [backendConnected, setBackendConnected] = useState(false);
  const [lastChecked, setLastChecked] = useState<Date | null>(null);
  const [checking, setChecking] = useState(false);

  async function check() {
    setChecking(true);
    try {
      await getHealth();
      setBackendConnected(true);
    } catch {
      setBackendConnected(false);
    } finally {
      setLastChecked(new Date());
      setChecking(false);
    }
  }

  useEffect(() => {
    void check();
    const id = setInterval(() => void check(), POLL_INTERVAL_MS);
    return () => clearInterval(id);
  }, []);

  return (
    <BackendContext.Provider value={{ backendConnected, lastChecked, checking, recheckNow: check }}>
      {children}
    </BackendContext.Provider>
  );
}

export function useBackend() {
  const ctx = useContext(BackendContext);
  if (!ctx) throw new Error('useBackend must be used within BackendProvider');
  return ctx;
}
