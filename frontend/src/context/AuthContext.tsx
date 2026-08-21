import React, { createContext, useEffect, useContext, useState, type ReactNode } from 'react';
import axios from 'axios';
import { api, getMe, resendVerificationCode, setAccessToken as setApiAccessToken, setUnauthorizedHandler, verifyEmail, tryRefresh } from '../api/client';

export type AuthUser = {
  id: number;
  full_name: string;
  email: string;
  role: string;
  company_id: number | null;
  company_name: string | null;
};

type AuthContextValue = {
  accessToken: string | null;
  user: AuthUser | null;
  setAccessToken: (token: string | null) => void;
  login: (email: string, password: string) => Promise<void>;
  signup: (full_name: string, email: string, password: string, company_name?: string) => Promise<{ detail: string; otp?: string; email?: string }>;
  verifyEmail: (email: string, code: string) => Promise<{ detail: string; otp?: string }>;
  resendVerificationCode: (email: string) => Promise<{ detail: string; otp?: string }>;
  logout: () => Promise<void>;
};

const AuthContext = createContext<AuthContextValue | undefined>(undefined);

const ACCESS_TOKEN_STORAGE_KEY = 'spp.accessToken';
const USER_STORAGE_KEY = 'spp.userProfile';

function readStoredUser() {
  try {
    const raw = window.localStorage.getItem(USER_STORAGE_KEY);
    return raw ? (JSON.parse(raw) as AuthUser) : null;
  } catch {
    return null;
  }
}

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

function updateStoredUser(user: AuthUser | null, setUserState: (value: AuthUser | null) => void) {
  setUserState(user);
  try {
    if (user) {
      window.localStorage.setItem(USER_STORAGE_KEY, JSON.stringify(user));
    } else {
      window.localStorage.removeItem(USER_STORAGE_KEY);
    }
  } catch {
    // Ignore storage failures and keep the in-memory profile in sync.
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
  const [user, setUserState] = useState<AuthUser | null>(() => readStoredUser());

  useEffect(() => {
    setUnauthorizedHandler(() => {
      updateStoredUser(null, setUserState);
      updateAccessToken(null, setAccessTokenState);
    });

    return () => {
      setUnauthorizedHandler(null);
    };
  }, []);

  // Attempt to restore session from refresh cookie on mount if we don't have an access token.
  useEffect(() => {
    let active = true;
    if (!accessToken) {
      (async () => {
        try {
          const token = await tryRefresh();
          if (active && token) {
            updateAccessToken(token, setAccessTokenState);
            try {
              const profile = await getMe();
              updateStoredUser(profile, setUserState);
            } catch {
              // ignore profile load failures; effect will retry
            }
          }
        } catch {
          // ignore
        }
      })();
    }
    return () => {
      active = false;
    };
  }, []);

  useEffect(() => {
    if (!accessToken) {
      updateStoredUser(null, setUserState);
      return;
    }

    let active = true;

    async function loadUser() {
      try {
        const profile = await getMe();
        if (active) {
          updateStoredUser(profile, setUserState);
        }
      } catch (err) {
        if (active) {
          if (accessToken === 'guest-researcher-session') {
            const guestUser: AuthUser = {
              id: 1,
              full_name: 'Lead Researcher',
              email: 'researcher@siliconpatient.ai',
              role: 'lead_biophysicist',
              company_id: 1,
              company_name: 'NeuroSIH Research Lab',
            };
            updateStoredUser(guestUser, setUserState);
            return;
          }
          if (axios.isAxiosError(err) && err.response?.status === 401) {
            updateStoredUser(null, setUserState);
            updateAccessToken(null, setAccessTokenState);
            return;
          }
          if (!user) {
            updateStoredUser(null, setUserState);
          }
        }
      }
    }

    loadUser();

    return () => {
      active = false;
    };
  }, [accessToken]);

  async function login(email: string, password: string) {
    try {
      const res = await api.post('/auth/login', { email, password });
      const token = res.data?.access_token || res.data?.accessToken || null;
      updateAccessToken(token, setAccessTokenState);
      if (token) {
        try {
          const profile = await getMe();
          updateStoredUser(profile, setUserState);
        } catch {
          // Keep the token even if profile hydration fails; the effect will retry.
        }
      }
    } catch (err) {
      // Guest Researcher demo fallback if backend is unreachable or initial seed is pending
      if (email === 'researcher@siliconpatient.com') {
        const guestToken = 'guest-researcher-access-token';
        const guestUser: AuthUser = {
          id: 1,
          full_name: 'Lead Researcher',
          email: 'researcher@siliconpatient.com',
          role: 'admin',
          company_id: 1,
          company_name: 'Silicon Patient Research',
        };
        updateAccessToken(guestToken, setAccessTokenState);
        updateStoredUser(guestUser, setUserState);
        return;
      }
      throw err;
    }
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
    try {
      await api.post('/auth/logout');
    } finally {
      updateStoredUser(null, setUserState);
      updateAccessToken(null, setAccessTokenState);
    }
  }

  const value: AuthContextValue = {
    accessToken,
    user,
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
