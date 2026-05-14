import { useEffect, useMemo, useState } from 'react';
import { BrowserRouter, Navigate, Outlet, Route, Routes, useLocation } from 'react-router-dom';
import { AppLayout } from './components/AppLayout';
import { getHealth } from './api/client';
import { DashboardPage } from './pages/DashboardPage';
import { DrugEvaluationPage } from './pages/DrugEvaluationPage';
import { SimulationPage } from './pages/SimulationPage';
import { ValidationPage } from './pages/ValidationPage';
import { RunHistoryPage } from './pages/RunHistoryPage';
import { ReportDetailPage } from './pages/ReportDetailPage';
import { RunTaskProvider, useRunTask } from './context/RunTaskContext';

const titleMap: Array<{ prefix: string; title: string }> = [
  { prefix: '/dose-eval', title: 'Drug Evaluation' },
  { prefix: '/simulation', title: 'Single Simulation' },
  { prefix: '/validation', title: 'Biological Validation' },
  { prefix: '/history', title: 'Run History' },
  { prefix: '/reports', title: 'Report Detail' },
  { prefix: '/', title: 'Dashboard' }
];

function Shell({ backendConnected }: { backendConnected: boolean }) {
  const location = useLocation();
  const { validationState } = useRunTask();
  const pageTitle = useMemo(() => {
    const found = titleMap.find((item) => item.prefix !== '/' && location.pathname.startsWith(item.prefix));
    return found?.title ?? 'Dashboard';
  }, [location.pathname]);

  return (
    <AppLayout
      pageTitle={pageTitle}
      backendConnected={backendConnected}
      engineOnline={backendConnected}
      validationRunning={validationState.status === 'running'}
    >
      <Outlet />
    </AppLayout>
  );
}

export default function App() {
  const [backendConnected, setBackendConnected] = useState(false);

  useEffect(() => {
    let active = true;
    getHealth()
      .then(() => {
        if (active) setBackendConnected(true);
      })
      .catch(() => {
        if (active) setBackendConnected(false);
      });

    return () => {
      active = false;
    };
  }, []);

  return (
    <RunTaskProvider>
      <BrowserRouter>
        <Routes>
          <Route element={<Shell backendConnected={backendConnected} />}>
            <Route path="/" element={<DashboardPage backendConnected={backendConnected} />} />
            <Route path="/dose-eval" element={<DrugEvaluationPage />} />
            <Route path="/simulation" element={<SimulationPage />} />
            <Route path="/validation" element={<ValidationPage />} />
            <Route path="/history" element={<RunHistoryPage />} />
            <Route path="/reports/:runId" element={<ReportDetailPage />} />
          </Route>
          <Route path="*" element={<Navigate to="/" replace />} />
        </Routes>
      </BrowserRouter>
    </RunTaskProvider>
  );
}
