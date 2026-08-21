import React, { Suspense, lazy, useMemo } from 'react';
import { BrowserRouter, Navigate, Outlet, Route, Routes, useLocation } from 'react-router-dom';
import { AppLayout } from './components/AppLayout';
import { ProtectedRoute } from './components/ProtectedRoute';
import { RunTaskProvider, useRunTask } from './context/RunTaskContext';
import { AuthProvider, useAuth } from './context/AuthContext';
import { ToastProvider } from './context/ToastContext';
import { ThemeProvider } from './context/ThemeContext';
import { BackendProvider, useBackend } from './context/BackendContext';

import { ScrollToTop } from './components/common/ScrollToTop';

const HomePage = lazy(() => import('./pages/public/HomePage').then((m) => ({ default: m.HomePage })));
const FeaturesPage = lazy(() => import('./pages/public/FeaturesPage').then((m) => ({ default: m.FeaturesPage })));
const HowItWorksPage = lazy(() => import('./pages/public/HowItWorksPage').then((m) => ({ default: m.HowItWorksPage })));
const ShowcasePage = lazy(() => import('./pages/public/ShowcasePage').then((m) => ({ default: m.ShowcasePage })));
const DashboardPage = lazy(() => import('./pages/DashboardPage').then((m) => ({ default: m.DashboardPage })));
const LoginPage = lazy(() => import('./pages/LoginPage').then((m) => ({ default: m.LoginPage })));
const SignupPage = lazy(() => import('./pages/SignupPage').then((m) => ({ default: m.SignupPage })));
const VerifyEmailPage = lazy(() => import('./pages/VerifyEmailPage').then((m) => ({ default: m.VerifyEmailPage })));
const DrugEvaluationPage = lazy(() => import('./pages/DrugEvaluationPage').then((m) => ({ default: m.DrugEvaluationPage })));
const RunHistoryPage = lazy(() => import('./pages/RunHistoryPage').then((m) => ({ default: m.RunHistoryPage })));
const ReportDetailPage = lazy(() => import('./pages/ReportDetailPage').then((m) => ({ default: m.ReportDetailPage })));
const RunComparePage = lazy(() => import('./pages/RunComparePage').then((m) => ({ default: m.RunComparePage })));
const SystemStatusPage = lazy(() => import('./pages/SystemStatusPage').then((m) => ({ default: m.SystemStatusPage })));

const titleMap: Array<{ prefix: string; title: string }> = [
  { prefix: '/app/dose-eval', title: 'Drug Evaluation' },
  { prefix: '/app/history', title: 'Run History' },
  { prefix: '/app/reports', title: 'Report Detail' },
  { prefix: '/app/compare', title: 'Run Comparison' },
  { prefix: '/app/status', title: 'System Diagnostics' },
  { prefix: '/app', title: 'Research Dashboard' },
];

function Shell() {
  const location = useLocation();
  const { validationState } = useRunTask();
  const { backendConnected } = useBackend();

  const pageTitle = useMemo(() => {
    const found = titleMap.find((item) => item.prefix !== '/' && location.pathname.startsWith(item.prefix));
    return found?.title ?? 'Research Dashboard';
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
          <div className="flex min-h-[40vh] items-center justify-center text-xs text-slate-400 font-mono">
            Loading section module...
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

  return (
    <BackendProvider>
    <ThemeProvider>
      <AuthProvider>
        <ToastProvider>
          <RunTaskProvider>
            <BrowserRouter>
              <ScrollToTop />
              <Suspense
                fallback={
                  <div className="flex min-h-screen items-center justify-center bg-slate-950 text-xs font-mono text-slate-400">
                    Initializing Silicon Patient Workstation...
                  </div>
                }
              >
                <Routes>
                  <Route path="/" element={<HomePage />} />
                  <Route path="/features" element={<FeaturesPage />} />
                  <Route path="/how-it-works" element={<HowItWorksPage />} />
                  <Route path="/showcase" element={<ShowcasePage />} />
                  <Route
                    path="/app"
                    element={
                      <ProtectedRoute>
                        <Shell />
                      </ProtectedRoute>
                    }
                  >
                    <Route index element={<DashboardPage backendConnected={false} />} />
                    <Route path="dose-eval" element={<DrugEvaluationPage />} />
                    <Route path="reports/:runId" element={<ReportDetailPage />} />
                    <Route path="history" element={<RunHistoryPage />} />
                    <Route path="compare" element={<RunComparePage />} />
                    <Route path="status" element={<SystemStatusPage />} />
                  </Route>
                  <Route path="/login" element={<PublicOnlyRoute><LoginPage /></PublicOnlyRoute>} />
                  <Route path="/signup" element={<PublicOnlyRoute><SignupPage /></PublicOnlyRoute>} />
                  <Route path="/verify-email" element={<PublicOnlyRoute><VerifyEmailPage /></PublicOnlyRoute>} />
                  <Route path="*" element={<Navigate to="/" replace />} />
                </Routes>
              </Suspense>
            </BrowserRouter>
          </RunTaskProvider>
        </ToastProvider>
      </AuthProvider>
    </ThemeProvider>
    </BackendProvider>
  );
}
