import { Suspense, lazy, useEffect, useMemo, useState } from 'react';
import { BrowserRouter, Navigate, Outlet, Route, Routes, useLocation } from 'react-router-dom';
import { AppLayout } from './components/AppLayout';
import { getHealth } from './api/client';
import { ProtectedRoute } from './components/ProtectedRoute';
import { RunTaskProvider, useRunTask } from './context/RunTaskContext';
import { AuthProvider, useAuth } from './context/AuthContext';

const LandingPage = lazy(() => import('./pages/LandingPage').then((module) => ({ default: module.LandingPage })));
const DashboardPage = lazy(() => import('./pages/DashboardPage').then((module) => ({ default: module.DashboardPage })));
const LoginPage = lazy(() => import('./pages/LoginPage').then((module) => ({ default: module.LoginPage })));
const SignupPage = lazy(() => import('./pages/SignupPage').then((module) => ({ default: module.SignupPage })));
const VerifyEmailPage = lazy(() => import('./pages/VerifyEmailPage').then((module) => ({ default: module.VerifyEmailPage })));
const DrugEvaluationPage = lazy(() => import('./pages/DrugEvaluationPage').then((module) => ({ default: module.DrugEvaluationPage })));
const RunHistoryPage = lazy(() => import('./pages/RunHistoryPage').then((module) => ({ default: module.RunHistoryPage })));
const ReportDetailPage = lazy(() => import('./pages/ReportDetailPage').then((module) => ({ default: module.ReportDetailPage })));

const titleMap: Array<{ prefix: string; title: string }> = [
  { prefix: '/app/dose-eval', title: 'Drug Evaluation' },
  // Validation UI is an internal benchmark and removed from public navigation.
  { prefix: '/app/history', title: 'Run History' },
  { prefix: '/app/reports', title: 'Report Detail' },
  { prefix: '/app', title: 'Dashboard' }
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
      <Suspense
        fallback={
          <div className="flex min-h-[40vh] items-center justify-center text-sm text-slate-400">
            Loading section...
          </div>
        }
      >
        <Outlet />
      </Suspense>
    </AppLayout>
  );
}

function PublicOnlyRoute({ children }: { children: JSX.Element }) {
  const { accessToken } = useAuth();
  if (accessToken) {
    return <Navigate to="/app" replace />;
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
    <AuthProvider>
      <RunTaskProvider>
        <BrowserRouter>
          <Suspense
            fallback={
              <div className="flex min-h-screen items-center justify-center bg-midnight-950 text-sm text-slate-400">
                Loading app...
              </div>
            }
          >
            <Routes>
              <Route path="/" element={<LandingPage />} />
              <Route path="/app" element={<ProtectedRoute><Shell backendConnected={backendConnected} /></ProtectedRoute>}>
                <Route index element={<DashboardPage backendConnected={backendConnected} />} />
                <Route path="dose-eval" element={<DrugEvaluationPage />} />
                <Route path="reports/:runId" element={<ReportDetailPage />} />
                <Route path="history" element={<RunHistoryPage />} />
              </Route>
              <Route path="/login" element={<PublicOnlyRoute><LoginPage /></PublicOnlyRoute>} />
              <Route path="/signup" element={<PublicOnlyRoute><SignupPage /></PublicOnlyRoute>} />
              <Route path="/verify-email" element={<PublicOnlyRoute><VerifyEmailPage /></PublicOnlyRoute>} />
              <Route path="*" element={<Navigate to="/app" replace />} />
            </Routes>
          </Suspense>
        </BrowserRouter>
      </RunTaskProvider>
    </AuthProvider>
  );
}
