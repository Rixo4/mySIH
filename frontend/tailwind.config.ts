import type { Config } from 'tailwindcss';

export default {
  darkMode: ['class'],
  content: ['./index.html', './src/**/*.{ts,tsx}'],
  theme: {
    extend: {
      colors: {
        midnight: {
          950: '#030712',
          900: '#07111f',
          850: '#0b1628',
          800: '#0f172a'
        },
        cobalt: {
          400: '#38bdf8',
          500: '#0ea5e9',
          600: '#0284c7'
        },
        neon: {
          400: '#34d399',
          500: '#10b981',
          600: '#059669'
        },
        amberish: {
          400: '#fbbf24',
          500: '#f59e0b',
          600: '#d97706'
        },
        danger: {
          400: '#f87171',
          500: '#ef4444',
          600: '#dc2626'
        }
      },
      boxShadow: {
        glow: '0 0 0 1px rgba(56, 189, 248, 0.18), 0 20px 60px rgba(2, 6, 23, 0.45)',
        panel: '0 20px 60px rgba(2, 6, 23, 0.55)'
      },
      backgroundImage: {
        'radial-shell': 'radial-gradient(circle at top, rgba(56,189,248,0.14), transparent 36%), radial-gradient(circle at 80% 0%, rgba(16,185,129,0.08), transparent 25%)'
      },
      keyframes: {
        drift: {
          '0%, 100%': { transform: 'translate3d(0,0,0)' },
          '50%': { transform: 'translate3d(0,-6px,0)' }
        }
      },
      animation: {
        drift: 'drift 8s ease-in-out infinite'
      }
    }
  },
  plugins: []
} satisfies Config;
