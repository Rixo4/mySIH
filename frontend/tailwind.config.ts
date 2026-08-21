import type { Config } from 'tailwindcss';

export default {
  darkMode: ['class'],
  content: ['./index.html', './src/**/*.{js,ts,jsx,tsx}'],
  theme: {
    extend: {
      colors: {
        brand: {
          400: '#8b5cf6',
          500: '#6d28d9',
          600: '#5b21b6',
        },
        obsidian: {
          950: '#05030a',
          900: '#090714',
          850: '#0e0b1f',
          800: '#141029',
          700: '#1c1738',
        },
        surface: {
          900: '#06040d', // Main page deep backdrop
          800: '#0b0817', // Card background
          700: '#120e24', // Element/Badge background
          600: '#181430',
          500: '#201b3f',
        },
        violetGlow: {
          400: '#a78bfa',
          500: '#8b5cf6',
          600: '#7c3aed',
        },
        midnight: {
          950: '#030712',
          900: '#07111f',
          850: '#0b1628',
          800: '#0f172a',
        },
        cobalt: {
          400: '#38bdf8',
          500: '#0ea5e9',
          600: '#0284c7',
        },
        neon: {
          400: '#34d399',
          500: '#10b981',
          600: '#059669',
        },
      },
      fontFamily: {
        sans: ['Inter', 'system-ui', 'sans-serif'],
        mono: ['JetBrains Mono', 'monospace'],
      },
      backgroundImage: {
        'grid-pattern': "radial-gradient(circle at 1px 1px, rgba(139, 92, 246, 0.12) 1px, transparent 0)",
        'hero-glow': 'radial-gradient(ellipse 90% 60% at 50% -10%, rgba(124, 58, 237, 0.28) 0%, rgba(30, 27, 75, 0.15) 45%, transparent 80%)',
        'spotlight-top-left': 'radial-gradient(ellipse 70% 60% at 15% 10%, rgba(139, 92, 246, 0.35) 0%, rgba(99, 102, 241, 0.12) 50%, transparent 80%)',
        'spotlight-cta': 'conic-gradient(from 180deg at 50% 0%, rgba(139, 92, 246, 0.4) 0deg, rgba(124, 58, 237, 0.15) 45deg, transparent 90deg, transparent 270deg, rgba(124, 58, 237, 0.15) 315deg, rgba(139, 92, 246, 0.4) 360deg)',
        'radial-shell': 'radial-gradient(circle at top, rgba(168, 85, 247, 0.16), transparent 45%), radial-gradient(circle at 80% 0%, rgba(16,185,129,0.08), transparent 30%)',
      },
      animation: {
        'float': 'float 6s ease-in-out infinite',
        'glow-pulse': 'glowPulse 3s ease-in-out infinite alternate',
        'drift': 'drift 8s ease-in-out infinite',
        'beam-flare': 'beamFlare 4s ease-in-out infinite alternate',
        'shimmer': 'shimmer 2.5s linear infinite',
      },
      keyframes: {
        float: {
          '0%, 100%': { transform: 'translateY(0px)' },
          '50%': { transform: 'translateY(-12px)' },
        },
        glowPulse: {
          '0%': { boxShadow: '0 0 25px rgba(124, 58, 237, 0.25)' },
          '100%': { boxShadow: '0 0 50px rgba(139, 92, 246, 0.55)' },
        },
        drift: {
          '0%, 100%': { transform: 'translate3d(0,0,0)' },
          '50%': { transform: 'translate3d(0,-8px,0)' },
        },
        beamFlare: {
          '0%': { opacity: '0.6', transform: 'scale(0.98) translateY(0px)' },
          '100%': { opacity: '1', transform: 'scale(1.03) translateY(-4px)' },
        },
        shimmer: {
          '0%': { backgroundPosition: '-200% 0' },
          '100%': { backgroundPosition: '200% 0' },
        },
      },
      boxShadow: {
        glow: '0 0 0 1px rgba(139, 92, 246, 0.25), 0 20px 60px rgba(9, 7, 20, 0.65)',
        spotlight: '0 0 80px 20px rgba(124, 58, 237, 0.3)',
        panel: '0 25px 70px rgba(5, 3, 10, 0.75)',
        glass: '0 8px 32px 0 rgba(0, 0, 0, 0.45)',
      },
    },
  },
  plugins: [],
} satisfies Config;
