import React from 'react';
import { Navbar } from '../../components/landing/Navbar';
import { FeatureCards } from '../../components/landing/FeatureCards';
import { FinalCTA } from '../../components/landing/FinalCTA';
import { Footer } from '../../components/landing/Footer';

export function FeaturesPage() {
  return (
    <div className="min-h-screen bg-obsidian-950 text-slate-100 font-sans antialiased overflow-x-hidden relative">
      <Navbar />

      <main className="relative z-10 pt-16">
        {/* Dedicated Features Component */}
        <FeatureCards />

        {/* Call to Action */}
        <FinalCTA />
      </main>

      <Footer />
    </div>
  );
}
