import React, { createContext, useContext, useState, useEffect } from 'react';
import { api, setAccessToken as setApiAccessToken } from '../api/client';
  useEffect(() => {
    // attempt silent refresh on load
    api.post('/auth/refresh')
      .then((res) => {
        const token = res.data?.access_token || res.data?.accessToken || null;
        setAccessToken(token);
        setApiAccessToken(token);
      })
      .catch(() => {
        setAccessToken(null);
        setApiAccessToken(null);
      });
  }, []);
      setAccessToken(token);
      setAccessToken(token);
      setAccessToken(token);
      setAccessToken(token);
      setAccessToken(token);
      setAccessToken(token);
      setAccessToken(token);
      setAccessToken(token);
      setAccessToken(token);
      setAccessToken(token);
      setAccessToken(token);
      setAccessToken(token);
      setAccessToken(token);
      setAccessToken(token);
      setAccessToken(token);
    }).catch(() => {
      setAccessToken(null);
    });
  }, []);

  async function login(email: string, password: string) {
    const res = await api.post('/auth/login', { email, password });
    const token = res.data?.access_token || res.data?.accessToken || null;
    setAccessToken(token);
    setApiAccessToken(token);
  }

  async function signup(full_name: string, email: string, password: string, company_name?: string) {
    await api.post('/auth/signup', { full_name, email, password, company_name });
  }

  async function logout() {
    await api.post('/auth/logout');
    setAccessToken(null);
  }

  const value: AuthContextValue = { accessToken, setAccessToken, login, signup, logout };

  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}
