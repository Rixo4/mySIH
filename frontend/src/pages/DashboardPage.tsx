import React, { useEffect, useMemo, useState, useCallback, useRef } from 'react';
import { getRuns, getRunDetail } from '../api/client';
import { ModernDashboardCockpit } from '../components/dashboard/ModernDashboardCockpit';
import type { RunDetailResponse, RunListItem } from '../types';
import { useAuth } from '../context/AuthContext';
import { useBackend } from '../context/BackendContext';

interface DashboardPageProps {
  backendConnected?: boolean;
}

export function DashboardPage({ backendConnected: _propConnected }: DashboardPageProps) {
  const { accessToken } = useAuth();
  const { backendConnected } = useBackend();

  const [runs,            setRuns]            = useState<RunListItem[]>([]);
  const [selectedRunId,   setSelectedRunId]   = useState<string | null>(null);
  const [selectedDetail,  setSelectedDetail]  = useState<RunDetailResponse | null>(null);
  const [latestDetails,   setLatestDetails]   = useState<RunDetailResponse[]>([]);
  const [error,           setError]           = useState<string | null>(null);
  const [loading,         setLoading]         = useState(true);

  const detailsCache = useRef<Map<string, RunDetailResponse>>(new Map());

  /* ── When backend disconnects, immediately wipe all in-memory run data ── */
  useEffect(() => {
    if (!backendConnected) {
      setRuns([]);
      setSelectedRunId(null);
      setSelectedDetail(null);
      setLatestDetails([]);
      detailsCache.current.clear();
      setError('Backend Disconnected');
      setLoading(false);
    }
  }, [backendConnected]);

  /* ── Fetch run detail by ID with caching ── */
  const fetchDetailForRun = useCallback(async (runId: string) => {
    if (!backendConnected) return null;
    if (detailsCache.current.has(runId)) {
      const cached = detailsCache.current.get(runId)!;
      setSelectedDetail(cached);
      return cached;
    }
    try {
      const detail = await getRunDetail(runId);
      if (detail) {
        detailsCache.current.set(runId, detail);
        setSelectedDetail(detail);
        return detail;
      }
    } catch {
      // Fallback
    }
    return null;
  }, [backendConnected]);

  /* ── Fetch dashboard runs list & details ── */
  const fetchDashboardData = useCallback(async (silent = false) => {
    if (!backendConnected) {
      setRuns([]);
      setSelectedRunId(null);
      setSelectedDetail(null);
      setLatestDetails([]);
      if (!silent) setLoading(false);
      return;
    }

    try {
      if (!silent) setLoading(true);
      setError(null);

      const runList = await getRuns();
      const currentRuns = runList.runs ?? [];
      setRuns(currentRuns);

      // Auto-select latest run if none selected or selected run is missing
      if (currentRuns.length > 0) {
        const activeId = selectedRunId && currentRuns.some(r => r.run_id === selectedRunId)
          ? selectedRunId
          : currentRuns[0].run_id;

        if (activeId !== selectedRunId) {
          setSelectedRunId(activeId);
        }
        await fetchDetailForRun(activeId);
      } else {
        setSelectedRunId(null);
        setSelectedDetail(null);
      }

      // Fetch first 5 run details for telemetry averaging
      const top5 = currentRuns.slice(0, 5);
      const details = await Promise.all(
        top5.map(async (r) => {
          if (detailsCache.current.has(r.run_id)) {
            return detailsCache.current.get(r.run_id)!;
          }
          try {
            const d = await getRunDetail(r.run_id);
            if (d) detailsCache.current.set(r.run_id, d);
            return d;
          } catch {
            return null;
          }
        })
      );
      setLatestDetails(details.filter((d): d is RunDetailResponse => d !== null));
    } catch (err) {
      setRuns([]);
      setSelectedRunId(null);
      setSelectedDetail(null);
      setLatestDetails([]);
      if (!silent) {
        setError(err instanceof Error ? err.message : 'Backend unreachable');
      }
    } finally {
      if (!silent) setLoading(false);
    }
  }, [backendConnected, selectedRunId, fetchDetailForRun]);

  /* ── Initial Load & Real-Time Polling ── */
  useEffect(() => {
    if (!accessToken || !backendConnected) {
      setLoading(false);
      return;
    }
    void fetchDashboardData(false);

    // Dynamic polling interval (4s)
    const interval = setInterval(() => {
      void fetchDashboardData(true);
    }, 4000);

    return () => clearInterval(interval);
  }, [accessToken, backendConnected, fetchDashboardData]);

  /* ── When user clicks or selects a different run ── */
  const handleSelectRun = useCallback((runId: string) => {
    setSelectedRunId(runId);
    void fetchDetailForRun(runId);
  }, [fetchDetailForRun]);

  /* ── Derived stats (real data only) ── */
  const stats = useMemo(() => {
    if (!backendConnected) {
      return { total: 0, completed: 0, failed: 0, running: 0 };
    }
    const total     = runs.length;
    const completed = runs.filter(r => r.status === 'completed' || r.status === 'SUCCESS').length;
    const failed    = runs.filter(r => r.status === 'failed'    || r.status === 'FAILED').length;
    const running   = runs.filter(r => r.status === 'running'   || r.status === 'queued').length;
    return { total, completed, failed, running };
  }, [backendConnected, runs]);

  const averageRuntime = useMemo(() => {
    if (!backendConnected) return null;
    const durations = latestDetails
      .map(i => i.duration_seconds)
      .filter((v): v is number => typeof v === 'number');
    if (!durations.length) return null;
    return durations.reduce((s, v) => s + v, 0) / durations.length;
  }, [backendConnected, latestDetails]);

  /* ── Render ── */
  return (
    <ModernDashboardCockpit
      runs={backendConnected ? runs : []}
      selectedRunId={backendConnected ? selectedRunId : null}
      selectedDetail={backendConnected ? selectedDetail : null}
      onSelectRun={handleSelectRun}
      latestDetails={backendConnected ? latestDetails : []}
      loading={loading}
      error={error}
      onRetry={() => void fetchDashboardData(false)}
      stats={stats}
      averageRuntime={averageRuntime}
    />
  );
}
