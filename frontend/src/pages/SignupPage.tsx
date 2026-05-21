import React, { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '../context/AuthContext';

export function SignupPage() {
  const [fullName, setFullName] = useState('');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [company, setCompany] = useState('');
  const [error, setError] = useState<string | null>(null);
  const auth = useAuth();
  const nav = useNavigate();

  async function submit(e: React.FormEvent) {
    e.preventDefault();
    try {
      await auth.signup(fullName, email, password, company || undefined);
      nav('/login');
    } catch (err) {
      setError('Unable to create account');
    }
  }

  return (
    <div className="p-4">
      <h2 className="text-xl mb-2">Sign up</h2>
      <form onSubmit={submit} className="space-y-2">
        <input value={fullName} onChange={(e) => setFullName(e.target.value)} placeholder="Full name" />
        <input value={email} onChange={(e) => setEmail(e.target.value)} placeholder="Email" />
        <input value={password} onChange={(e) => setPassword(e.target.value)} placeholder="Password" type="password" />
        <input value={company} onChange={(e) => setCompany(e.target.value)} placeholder="Company (optional)" />
        <button type="submit">Create account</button>
      </form>
      {error && <div className="text-red-500">{error}</div>}
    </div>
  );
}
