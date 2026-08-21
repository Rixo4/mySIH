import React from 'react';
import { Navbar } from '../components/landing/Navbar';
import { Hero } from '../components/landing/Hero';
import { FeatureCards } from '../components/landing/FeatureCards';
import { HowItWorks } from '../components/landing/HowItWorks';
import { ShowcaseGrid } from '../components/landing/ShowcaseGrid';
import { FinalCTA } from '../components/landing/FinalCTA';
import { Footer } from '../components/landing/Footer';

export function LandingPage() {
  return (
    <div className="min-h-screen bg-surface-900 text-slate-100 font-sans antialiased overflow-x-hidden relative selection:bg-brand-500/30 selection:text-white">
      {/* Glassmorphic Navbar */}
      <Navbar />

      <main className="relative z-10">
        {/* Futuristic Hero Section with Ambient Glow & Infinite Ticker */}
        <Hero />

        {/* 6-Card Responsive Feature Grid */}
        <FeatureCards />

        {/* 5-Step Process Section with Connecting Line */}
        <HowItWorks />

        {/* Showcase Grid with Signal Indicators & Reliability Badges */}
        <ShowcaseGrid />

        {/* High-Impact Final Call To Action Banner */}
        <FinalCTA />
      </main>

      {/* 5-Column Footer */}
      <Footer />
    </div>
  );
}