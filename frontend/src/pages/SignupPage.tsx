import React, { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '../context/AuthContext';

export function SignupPage() {
  const [fullName, setFullName] = useState('');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [company, setCompany] = useState('');
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
      const result = await auth.signup(fullName, email, password, company || undefined);
      nav('/verify-email', { state: { email, message: result.detail, otp: result.otp } });
    } catch (err) {
      setError('Unable to create account');
    } finally {
      setSubmitting(false);
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
        <h2 className="mt-3 text-3xl font-semibold text-white">Create your account</h2>
        <p className="mt-2 text-sm text-slate-400">Sign up to access neural simulation dashboards and run history.</p>

        <form onSubmit={submit} autoComplete="off" className="mt-8 space-y-4">
          <div className="space-y-2">
            <label htmlFor="fullName" className="text-xs font-semibold uppercase tracking-[0.2em] text-slate-400">
              Full Name
            </label>
            <input
              id="fullName"
              name="signup_full_name"
              value={fullName}
              onChange={(e) => setFullName(e.target.value)}
              placeholder="Your full name"
              autoComplete="off"
              required
              className="w-full rounded-2xl border border-white/10 bg-slate-950/70 px-4 py-3 text-sm text-white outline-none transition focus:border-cyan-400/70 focus:ring-2 focus:ring-cyan-500/20"
            />
          </div>

          <div className="space-y-2">
            <label htmlFor="email" className="text-xs font-semibold uppercase tracking-[0.2em] text-slate-400">
              Email
            </label>
            <input
              id="email"
              name="signup_email"
              value={email}
              onChange={(e) => setEmail(e.target.value)}
              placeholder="name@company.com"
              type="email"
              autoComplete="off"
              inputMode="email"
              autoCapitalize="none"
              autoCorrect="off"
              spellCheck={false}
              required
              className="w-full rounded-2xl border border-white/10 bg-slate-950/70 px-4 py-3 text-sm text-white outline-none transition focus:border-cyan-400/70 focus:ring-2 focus:ring-cyan-500/20"
            />
          </div>

          <div className="space-y-2">
            <label htmlFor="password" className="text-xs font-semibold uppercase tracking-[0.2em] text-slate-400">
              Password
            </label>
            <input
              id="password"
              name="signup_password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              placeholder="Create a strong password"
              type="password"
              autoComplete="off"
              required
              className="w-full rounded-2xl border border-white/10 bg-slate-950/70 px-4 py-3 text-sm text-white outline-none transition focus:border-cyan-400/70 focus:ring-2 focus:ring-cyan-500/20"
            />
          </div>

          <div className="space-y-2">
            <label htmlFor="company" className="text-xs font-semibold uppercase tracking-[0.2em] text-slate-400">
              Company (Optional)
            </label>
            <input
              id="company"
              name="signup_company"
              value={company}
              onChange={(e) => setCompany(e.target.value)}
              placeholder="Company name"
              autoComplete="off"
              className="w-full rounded-2xl border border-white/10 bg-slate-950/70 px-4 py-3 text-sm text-white outline-none transition focus:border-cyan-400/70 focus:ring-2 focus:ring-cyan-500/20"
            />
          </div>

          <button
            type="submit"
            disabled={submitting}
            className="w-full rounded-2xl bg-cyan-500 px-5 py-3 text-sm font-semibold text-midnight-950 transition hover:scale-[1.01] disabled:cursor-not-allowed disabled:opacity-70"
          >
            {submitting ? 'Creating account...' : 'Create account'}
          </button>
        </form>

        {error && <div className="mt-4 rounded-2xl border border-rose-400/20 bg-rose-500/10 px-4 py-3 text-sm text-rose-200">{error}</div>}

        <p className="mt-6 text-sm text-slate-400">
          Already have an account?{' '}
          <Link to="/login" className="font-semibold text-cyan-300 hover:text-cyan-200">
            Sign in
          </Link>
        </p>
      </div>
    </div>
  );
}
