import { useEffect, useMemo, useState } from 'react';
import { BrowserRouter, Navigate, Outlet, Route, Routes, useLocation } from 'react-router-dom';
import { AppLayout } from './components/AppLayout';
import { getHealth } from './api/client';
import { DashboardPage } from './pages/DashboardPage';
import { LoginPage } from './pages/LoginPage';
import { SignupPage } from './pages/SignupPage';
import { VerifyEmailPage } from './pages/VerifyEmailPage';
import { DrugEvaluationPage } from './pages/DrugEvaluationPage';
import { SimulationPage } from './pages/SimulationPage';
import { RunHistoryPage } from './pages/RunHistoryPage';
import { ReportDetailPage } from './pages/ReportDetailPage';
import { ProtectedRoute } from './components/ProtectedRoute';
import { RunTaskProvider, useRunTask } from './context/RunTaskContext';
import { AuthProvider, useAuth } from './context/AuthContext';

const titleMap: Array<{ prefix: string; title: string }> = [
  { prefix: '/dose-eval', title: 'Drug Evaluation' },
  { prefix: '/simulation', title: 'Single Simulation' },
  // Validation UI is an internal benchmark and removed from public navigation.
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

function PublicOnlyRoute({ children }: { children: JSX.Element }) {
  const { accessToken } = useAuth();
  if (accessToken) {
    return <Navigate to="/" replace />;
  }
  return children;
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
      <AuthProvider>
        <BrowserRouter>
          <Routes>
            <Route element={<Shell backendConnected={backendConnected} />}>
              <Route path="/" element={<ProtectedRoute><DashboardPage backendConnected={backendConnected} /></ProtectedRoute>} />
              <Route path="/dose-eval" element={<ProtectedRoute><DrugEvaluationPage /></ProtectedRoute>} />
              <Route path="/simulation" element={<ProtectedRoute><SimulationPage /></ProtectedRoute>} />
              <Route path="/reports/:runId" element={<ProtectedRoute><ReportDetailPage /></ProtectedRoute>} />
              <Route path="/history" element={<ProtectedRoute><RunHistoryPage /></ProtectedRoute>} />
            </Route>
            <Route path="/login" element={<PublicOnlyRoute><LoginPage /></PublicOnlyRoute>} />
            <Route path="/signup" element={<PublicOnlyRoute><SignupPage /></PublicOnlyRoute>} />
            <Route path="/verify-email" element={<PublicOnlyRoute><VerifyEmailPage /></PublicOnlyRoute>} />
            <Route path="*" element={<Navigate to="/login" replace />} />
          </Routes>
        </BrowserRouter>
      </AuthProvider>
    </RunTaskProvider>
  );
}
