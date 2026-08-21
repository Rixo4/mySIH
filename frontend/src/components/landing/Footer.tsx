import React from 'react';
import { Activity, Github, Linkedin, Shield, Twitter, Youtube } from 'lucide-react';
import { Link } from 'react-router-dom';

export function Footer() {
  return (
    <footer className="bg-obsidian-950 border-t border-white/10 text-slate-400 font-sans pt-16 pb-12">
      <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8">
        <div className="flex flex-col md:flex-row items-center justify-between gap-8 pb-10 border-b border-white/10">
          {/* Brand Logo */}
          <Link to="/" className="flex items-center gap-2.5">
            <div className="flex items-center justify-center w-8 h-8 rounded-full bg-violet-600/30 border border-violet-400/40 shadow-[0_0_15px_rgba(124,58,237,0.4)]">
              <Activity className="w-4 h-4 text-violet-300" />
            </div>
            <span className="text-base font-bold text-white tracking-tight font-sans">
              Silicon<span className="text-violet-400">Patient</span>
            </span>
          </Link>

          {/* Clean Horizontal Footer Links */}
          <nav className="flex flex-wrap items-center justify-center gap-8 text-xs font-medium text-slate-400">
            <Link to="/" className="hover:text-white transition-colors">Solutions</Link>
            <Link to="/features" className="hover:text-white transition-colors">Products</Link>
            <Link to="/showcase" className="hover:text-white transition-colors">Pricing</Link>
            <Link to="/how-it-works" className="hover:text-white transition-colors">Careers</Link>
            <Link to="/how-it-works" className="hover:text-white transition-colors">Resources</Link>
            <a href="#privacy" className="hover:text-white transition-colors">Privacy & Policy</a>
          </nav>
        </div>

        {/* Bottom Copyright */}
        <div className="pt-8 flex flex-col md:flex-row items-center justify-between gap-4 text-xs font-mono text-slate-500">
          <p>© {new Date().getFullYear()} Silicon Patient Inc. All rights reserved.</p>
          <p className="text-[11px] text-slate-600">In-silico biophysics & neural simulation platform.</p>
        </div>
      </div>
    </footer>
  );
}
