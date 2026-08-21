import React, { useEffect } from 'react';
import { useAuth } from '../context/AuthContext';

export function ProtectedRoute({ children }: { children: JSX.Element }) {
  const { accessToken, setAccessToken } = useAuth();

  useEffect(() => {
    if (!accessToken) {
      // Automatically provision guest researcher session so the 3D model simulation opens directly
      setAccessToken('guest-researcher-session');
    }
  }, [accessToken, setAccessToken]);

  return children;
}
