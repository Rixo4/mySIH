import React, { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '../context/AuthContext';

export function LoginPage() {
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState<string | null>(null);
  const auth = useAuth();
  const nav = useNavigate();

  async function submit(e: React.FormEvent) {
    e.preventDefault();
    try {
      await auth.login(email, password);
      nav('/');
    } catch (err) {
      setError('Invalid credentials');
    }
  }

  return (
    <div className="p-4">
      <h2 className="text-xl mb-2">Login</h2>
      <form onSubmit={submit} className="space-y-2">
        <input value={email} onChange={(e) => setEmail(e.target.value)} placeholder="Email" />
        <input value={password} onChange={(e) => setPassword(e.target.value)} placeholder="Password" type="password" />
        <button type="submit">Login</button>
      </form>
      {error && <div className="text-red-500">{error}</div>}
    </div>
  );
}
