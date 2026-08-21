import React, { useMemo, useState } from 'react';
import { Link, useLocation, useNavigate } from 'react-router-dom';
import { useAuth } from '../context/AuthContext';

type VerifyLocationState = {
  email?: string;
  message?: string;
  otp?: string;
};

export function VerifyEmailPage() {
  const location = useLocation();
  const nav = useNavigate();
  const auth = useAuth();
  const state = (location.state ?? {}) as VerifyLocationState;
  const [email, setEmail] = useState(state.email ?? '');
  const [code, setCode] = useState(state.otp ?? '');
  const [submitting, setSubmitting] = useState(false);
  const [resending, setResending] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState(state.message ?? 'Enter the 6-digit code sent to your email.');

  const canSubmit = useMemo(() => email.trim().length > 0 && code.trim().length === 6, [email, code]);

  async function submit(e: React.FormEvent) {
    e.preventDefault();
    setSubmitting(true);
    setError(null);
    try {
      await auth.verifyEmail(email.trim(), code.trim());
      nav('/login', { replace: true, state: { email } });
    } catch {
      setError('Invalid or expired verification code');
    } finally {
      setSubmitting(false);
    }
  }

  async function resendCode() {
    setResending(true);
    setError(null);
    try {
      const result = await auth.resendVerificationCode(email.trim());
      setNotice(result.detail);
    } catch {
      setNotice('If an account needs verification, a code has been sent.');
    } finally {
      setResending(false);
    }
  }

  return (
    <div className="relative flex min-h-screen items-center justify-center overflow-hidden px-4 py-10">
      <div className="pointer-events-none absolute inset-0">
        <div className="absolute -right-24 top-20 h-72 w-72 rounded-full bg-cyan-500/15 blur-3xl" />
        <div className="absolute -left-24 bottom-16 h-72 w-72 rounded-full bg-sky-400/10 blur-3xl" />
      </div>

      <div className="glass-card relative w-full max-w-md border border-cyan-400/20 p-8 shadow-panel">
        <p className="text-xs uppercase tracking-[0.32em] text-cyan-300/70">Silicon Patient</p>
        <h2 className="mt-3 text-3xl font-semibold text-white">Verify your email</h2>
        <p className="mt-2 text-sm text-slate-400">Enter the 6-digit code sent to your email.</p>

        <form onSubmit={submit} className="mt-8 space-y-4">
          <div className="space-y-2">
            <label htmlFor="email" className="text-xs font-semibold uppercase tracking-[0.2em] text-slate-400">
              Email
            </label>
            <input
              id="email"
              value={email}
              onChange={(e) => setEmail(e.target.value)}
              type="email"
              autoComplete="email"
              placeholder="name@company.com"
              className="w-full rounded-2xl border border-white/10 bg-slate-950/70 px-4 py-3 text-sm text-white outline-none transition focus:border-cyan-400/70 focus:ring-2 focus:ring-cyan-500/20"
            />
          </div>

          <div className="space-y-2">
            <label htmlFor="code" className="text-xs font-semibold uppercase tracking-[0.2em] text-slate-400">
              6-digit code
            </label>
            <input
              id="code"
              value={code}
              onChange={(e) => setCode(e.target.value.replace(/\D/g, '').slice(0, 6))}
              inputMode="numeric"
              maxLength={6}
              placeholder="000000"
              className="w-full rounded-2xl border border-white/10 bg-slate-950/70 px-4 py-3 text-sm text-white outline-none transition focus:border-cyan-400/70 focus:ring-2 focus:ring-cyan-500/20"
            />
          </div>

          <div className="grid gap-3 sm:grid-cols-2">
            <button
              type="submit"
              disabled={!canSubmit || submitting}
              className="rounded-2xl bg-cyan-500 px-5 py-3 text-sm font-semibold text-midnight-950 transition hover:scale-[1.01] disabled:cursor-not-allowed disabled:opacity-70"
            >
              {submitting ? 'Verifying...' : 'Verify'}
            </button>
            <button
              type="button"
              onClick={resendCode}
              disabled={!email.trim() || resending}
              className="rounded-2xl border border-cyan-400/30 bg-white/5 px-5 py-3 text-sm font-semibold text-cyan-100 transition hover:bg-white/10 disabled:cursor-not-allowed disabled:opacity-70"
            >
              {resending ? 'Resending...' : 'Resend code'}
            </button>
          </div>
        </form>

        {notice && <div className="mt-4 rounded-2xl border border-cyan-400/20 bg-cyan-500/10 px-4 py-3 text-sm text-cyan-100">{notice}</div>}
        {error && <div className="mt-4 rounded-2xl border border-rose-400/20 bg-rose-500/10 px-4 py-3 text-sm text-rose-200">{error}</div>}

        <p className="mt-6 text-sm text-slate-400">
          Back to{' '}
          <Link to="/login" className="font-semibold text-cyan-300 hover:text-cyan-200">
            login
          </Link>
        </p>
      </div>
    </div>
  );
}
