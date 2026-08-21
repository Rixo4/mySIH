import React, { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { useNavigate } from 'react-router-dom';
import axios from 'axios';
import { useAuth } from '../context/AuthContext';

export function LoginPage() {
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const auth = useAuth();
  const nav = useNavigate();

  useEffect(() => {
    if (email) return;
    const clearIfPhone = () => {
      const el = document.getElementById('email') as HTMLInputElement | null;
      if (!el) return;
      const val = (el.value || '').trim();
      // If the browser injected a phone-like value (digits, plus, parentheses) without an @, clear it
      if (/^[0-9()+\-\s]{6,}$/.test(val) && !/@/.test(val)) {
        setEmail('');
        try {
          el.value = '';
        } catch {
          // ignore
        }
      }
    };

    clearIfPhone();
    const t = window.setTimeout(clearIfPhone, 500);
    return () => window.clearTimeout(t);
  }, []);

  async function submit(e: React.FormEvent) {
    e.preventDefault();
    setSubmitting(true);
    setError(null);
    try {
      await auth.login(email, password);
      nav('/app');
    } catch (err) {
      const detail = axios.isAxiosError(err) ? err.response?.data?.detail : null;
      if (detail === 'Email verification required') {
        nav('/verify-email', { state: { email, message: detail } });
        return;
      }
      setError('Invalid credentials');
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <div className="relative flex min-h-screen items-center justify-center overflow-hidden px-4 py-10">
      <div className="pointer-events-none absolute inset-0">
        <div className="absolute -left-24 top-20 h-72 w-72 rounded-full bg-cyan-500/15 blur-3xl" />
        <div className="absolute -right-24 bottom-16 h-72 w-72 rounded-full bg-sky-400/10 blur-3xl" />
      </div>

      <div className="glass-card relative w-full max-w-md border border-white/15 p-8 shadow-2xl rounded-3xl bg-obsidian-900/90 backdrop-blur-2xl">
        <div className="flex items-center gap-2 mb-2">
          <span className="w-2 h-2 rounded-full bg-violet-400 alert-dot" />
          <p className="text-xs uppercase tracking-[0.25em] font-mono text-violet-300 font-bold">Silicon Patient / NeuroSIH</p>
        </div>
        <h2 className="text-3xl font-bold text-white font-sans">Welcome back</h2>
        <p className="mt-2 text-sm text-slate-400">Sign in to continue to your simulation dashboard.</p>

        <form onSubmit={submit} autoComplete="off" className="mt-8 space-y-4">
          <div className="space-y-2">
            <label htmlFor="email" className="text-xs font-semibold uppercase tracking-[0.15em] text-slate-400 font-mono">
              Email
            </label>
            <input
              id="email"
              name="login_email"
              value={email}
              onChange={(e) => setEmail(e.target.value)}
              placeholder="name@company.com"
              type="email"
              inputMode="email"
              autoComplete="off"
              autoCapitalize="none"
              autoCorrect="off"
              spellCheck={false}
              required
              className="w-full rounded-full border border-white/10 bg-white/[0.04] px-5 py-3 text-sm text-white outline-none transition focus:border-violet-400/70 focus:ring-2 focus:ring-violet-500/20"
            />
          </div>

          <div className="space-y-2">
            <label htmlFor="password" className="text-xs font-semibold uppercase tracking-[0.15em] text-slate-400 font-mono">
              Password
            </label>
            <input
              id="password"
              name="login_password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              placeholder="Enter your password"
              type="password"
              autoComplete="off"
              required
              className="w-full rounded-full border border-white/10 bg-white/[0.04] px-5 py-3 text-sm text-white outline-none transition focus:border-violet-400/70 focus:ring-2 focus:ring-violet-500/20"
            />
          </div>

          <button
            type="submit"
            disabled={submitting}
            className="w-full btn-primary justify-center text-sm py-3 px-5 rounded-full shadow-lg"
          >
            {submitting ? 'Signing in...' : 'Login'}
          </button>

          <button
            type="button"
            onClick={async () => {
              setSubmitting(true);
              setError(null);
              try {
                await auth.login('researcher@siliconpatient.com', 'ResearcherPass2026#');
                nav('/app');
              } catch (err) {
                setError('Failed to log in with guest researcher credentials');
              } finally {
                setSubmitting(false);
              }
            }}
            disabled={submitting}
            className="w-full rounded-full bg-white/[0.05] hover:bg-violet-600/20 border border-white/15 px-5 py-3 text-sm font-semibold text-violet-300 transition hover:scale-[1.01] disabled:opacity-70 flex items-center justify-center gap-2 cursor-pointer"
          >
            <span>⚡ Quick Guest Researcher Access</span>
          </button>
        </form>

        {error && <div className="mt-4 rounded-2xl border border-rose-400/20 bg-rose-500/10 px-4 py-3 text-sm text-rose-200">{error}</div>}

        <p className="mt-6 text-sm text-slate-400 text-center">
          New user?{' '}
          <Link to="/signup" className="font-semibold text-violet-300 hover:text-white underline">
            Create an account
          </Link>
        </p>
      </div>
    </div>
  );
}
