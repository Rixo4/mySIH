import React, { createContext, useContext, useState, type ReactNode } from 'react';
import { api, resendVerificationCode, setAccessToken as setApiAccessToken, verifyEmail } from '../api/client';

type AuthContextValue = {
  accessToken: string | null;
  setAccessToken: (token: string | null) => void;
  login: (email: string, password: string) => Promise<void>;
  signup: (full_name: string, email: string, password: string, company_name?: string) => Promise<{ detail: string; otp?: string; email?: string }>;
  verifyEmail: (email: string, code: string) => Promise<{ detail: string; otp?: string }>;
  resendVerificationCode: (email: string) => Promise<{ detail: string; otp?: string }>;
  logout: () => Promise<void>;
};

const AuthContext = createContext<AuthContextValue | undefined>(undefined);

const ACCESS_TOKEN_STORAGE_KEY = 'spp.accessToken';

function readStoredAccessToken() {
  try {
    return window.localStorage.getItem(ACCESS_TOKEN_STORAGE_KEY);
  } catch {
    return null;
  }
}

function updateAccessToken(token: string | null, setAccessTokenState: (value: string | null) => void) {
  setAccessTokenState(token);
  setApiAccessToken(token);
  try {
    if (token) {
      window.localStorage.setItem(ACCESS_TOKEN_STORAGE_KEY, token);
    } else {
      window.localStorage.removeItem(ACCESS_TOKEN_STORAGE_KEY);
    }
  } catch {
    // Ignore storage failures and keep the in-memory token in sync.
  }
}

export function AuthProvider({ children }: { children: ReactNode }) {
  const [accessToken, setAccessTokenState] = useState<string | null>(() => {
    const token = readStoredAccessToken();
    if (token) {
      setApiAccessToken(token);
    }
    return token;
  });

  async function login(email: string, password: string) {
    const res = await api.post('/auth/login', { email, password });
    const token = res.data?.access_token || res.data?.accessToken || null;
    updateAccessToken(token, setAccessTokenState);
  }

  async function signup(full_name: string, email: string, password: string, company_name?: string) {
    const res = await api.post('/auth/signup', { full_name, email, password, company_name });
    return res.data;
  }

  async function verifyEmailAddress(email: string, code: string) {
    return verifyEmail(email, code);
  }

  async function resendVerificationCodeForEmail(email: string) {
    return resendVerificationCode(email);
  }

  async function logout() {
    await api.post('/auth/logout');
    updateAccessToken(null, setAccessTokenState);
  }

  const value: AuthContextValue = {
    accessToken,
    setAccessToken: (token) => updateAccessToken(token, setAccessTokenState),
    login,
    signup,
    verifyEmail: verifyEmailAddress,
    resendVerificationCode: resendVerificationCodeForEmail,
    logout
  };

  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}

export function useAuth() {
  const context = useContext(AuthContext);
  if (!context) {
    throw new Error('useAuth must be used within AuthProvider');
  }
  return context;
}
